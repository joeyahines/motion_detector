/**
 * Motion Detector
 *
 * @author Joey Hines
 *
 * Detects motion from the video stream of a webcam or other V4L capture device
 */

#include <libv4l2.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include "cam_api.h"

#define NEW_FRAME_EVENT (SDL_USEREVENT+1)
#define EXIT_EVENT (SDL_USEREVENT+2)
#define uchar unsigned char

struct webcam_info g_cam_info;
int g_thread_exit = 0;

/**
 * Thread to process new video from the webcam
 * @param ptr Pointer to data used by this function
 * @return 0
 */
int process_webcam_video(void *ptr) {
    g_thread_exit = 0;

    // While the thread is not exiting
    while (!g_thread_exit) {
        SDL_Event event;
        event.type = NEW_FRAME_EVENT;

        // Get the next frame from the camera
        int ndx = get_next_frame(&g_cam_info);

        // If that frame is valid, send it to the main thread
        if (ndx > 0) {
            event.user.code = ndx;
            SDL_PushEvent(&event);
        }
    }

    // On exit, push EXIT_EVENT thread
    SDL_Event event;
    event.type = EXIT_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

void yuyv_get_pixel_value(const uchar *image, int i, int j, int width, uchar *pixel) {
    int i_real = i * 2;
    uchar new_y_value = *(image + i_real + j * width * 2);
    uchar new_v_value;
    uchar new_u_value;

    if ((i_real % 4) == 0) {
        new_u_value = *(image + i_real + 1 + j * width * 2);
        new_v_value = *(image + i_real + 3 + j * width * 2);
    } else {
        new_u_value = *(image + i_real - 1 + j * width * 2);
        new_v_value = *(image + i_real + 1 + j * width * 2);
    }

    pixel[0] = new_y_value;
    pixel[1] = new_u_value;
    pixel[2] = new_v_value;

}

void yuyv_set_pixel_value(uchar *image, int i, int j, int width, const uchar *pixel) {
    int i_real = i * 2;

    *(image + i_real + j * width * 2) = pixel[0];

    if (i_real % 4 == 0) {
        *(image + i_real + 1 + j * width * 2) = pixel[1];
        *(image + i_real + 3 + j * width * 2) = pixel[2];
    } else {
        *(image + i_real - 1 + j * width * 2) = pixel[1];
        *(image + i_real + 1 + j * width * 2) = pixel[2];
    }
}

void yuv_get_pixel_value(const uchar *image, int i, int j, int width, uchar *pixel) {
    for (int k = 0; k < 3; k++) {
        pixel[k] = *(image + i * 3 + j * width * 3 + k);
    }
}

void yuv_set_pixel_value(uchar *image, int i, int j, int width, const uchar *pixel) {
    for (int k = 0; k < 3; k++) {
        *(image + i * 3 + j * width * 3 + k) = pixel[k];
    }
}

void bg_model_get_pixel_value(const float *image, int i, int j, int width, float *pixel) {
    for (int k = 0; k < 3; k++) {
        pixel[k] = *(image + i * 3 + j * width * 3 + k);
    }
}

void bg_model_set_pixel_value(float *image, int i, int j, int width, const float *pixel) {
    for (int k = 0; k < 3; k++) {
        *(image + i * 3 + j * width * 3 + k) = pixel[k];
    }
}

void smooth_image(const uchar *image, int width, int height, int filter_size, uchar *output) {
    float *h_smooth;
    h_smooth = (float *) malloc(sizeof(float) * filter_size * filter_size);
    int half_w = filter_size / 2;

    // Generate smoothing filter
    for (int i = 0; i < filter_size; i++) {
        for (int j = 0; j < filter_size; j++) {
            *(h_smooth + i * filter_size + j) = 1.0 / (filter_size * filter_size);
        }
    }

    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            uchar output_pixel[3];
            float tmp = 0.0f;
            for (int k = -half_w; k <= half_w; k++) {
                for (int l = -half_w; l <= half_w; l++) {
                    int ii = i + k;
                    int jj = j + l;

                    if (((ii < width) && (ii >= 0)) && ((jj < height) && (jj >= 0))) {
                        uchar pixel_value[3];
                        yuyv_get_pixel_value(image, ii, jj, width, pixel_value);
                        float filter_val = *(h_smooth + (filter_size * (k + half_w) + l + half_w));
                        tmp += filter_val * pixel_value[0];
                        /*
                        for (int m = 0; m < 3; m++) {
                            tmp[m] += filter_val * pixel_value[m];
                        }
                        */
                    }
                }
            }


            yuyv_get_pixel_value(image, i, j, width, output_pixel);
            output_pixel[0] = tmp;

            yuyv_set_pixel_value(output, i, j, width, output_pixel);
        }
    }

    free(h_smooth);
}

void yuyv_to_yuv(const uchar *src, uchar *dest, int src_width, int src_height) {
    for (int i = 0; i < src_width; i += 2) {
        int src_i = i * 2;
        int dest_i = i * 3;
        for (int j = 0; j < src_height; j++) {
            uchar y1 = *(src + src_i + j * src_width * 2);
            uchar y2 = *(src + src_i + 2 + j * src_width * 2);
            uchar u = *(src + src_i + 1 + j * src_width * 2);
            uchar v = *(src + src_i + 3 + j * src_width * 2);

            *(dest + dest_i + j * src_width * 3) = y1;
            *(dest + dest_i + 3 + j * src_width * 3) = y2;

            *(dest + dest_i + 1 + j * src_width * 3) = u;
            *(dest + dest_i + 4 + j * src_width * 3) = u;

            *(dest + dest_i + 2 + j * src_width * 3) = v;
            *(dest + dest_i + 5 + j * src_width * 3) = v;
        }
    }
}

void yuv_to_yuyv(const uchar *src, uchar *dest, int src_width, int src_height) {
    for (int i = 0; i < src_width; i += 2) {
        int src_i = i * 3;
        int dest_i = i * 2;
        for (int j = 0; j < src_height; j++) {
            uchar y1 = *(src + src_i + j * src_width * 3);
            uchar y2 = *(src + src_i + 3 + j * src_width * 3);

            uchar u1 = *(src + src_i + 1 + j * src_width * 3);
            uchar u2 = *(src + src_i + 4 + j * src_width * 3);

            uchar v1 = *(src + src_i + 2 + j * src_width * 3);
            uchar v2 = *(src + src_i + 5 + j * src_width * 3);

            *(dest + dest_i + j * src_width * 2) = y1;
            *(dest + dest_i + 2 + j * src_width * 2) = y2;
            *(dest + dest_i + 1 + j * src_width * 2) = (u1 + u2) / 2;
            *(dest + dest_i + 3 + j * src_width * 2) = (v1 + v2) / 2;
        }
    }
}

void bg_model_to_yuyv(const float *src, uchar *dest, int src_width, int src_height) {
    for (int i = 0; i < src_width; i += 2) {
        int src_i = i * 3;
        int dest_i = i * 2;
        for (int j = 0; j < src_height; j++) {
            uchar y1 = (uchar)*(src + src_i + j * src_width * 3);
            uchar y2 = (uchar)*(src + src_i + 3 + j * src_width * 3);

            uchar u1 = (uchar)*(src + src_i + 1 + j * src_width * 3);
            uchar u2 = (uchar)*(src + src_i + 4 + j * src_width * 3);

            uchar v1 = (uchar)*(src + src_i + 2 + j * src_width * 3);
            uchar v2 = (uchar)*(src + src_i + 5 + j * src_width * 3);

            *(dest + dest_i + j * src_width * 2) = y1;
            *(dest + dest_i + 2 + j * src_width * 2) = y2;
            *(dest + dest_i + 1 + j * src_width * 2) = (u1 + u2) / 2;
            *(dest + dest_i + 3 + j * src_width * 2) = (v1 + v2) / 2;
        }
    }
}

#define BG_MODEL_SIZE 10
#define THRESHOLD 250

/**
 * Detects if motion has occurred between two frames by differencing
 * @param new_frame New frame from video source
 * @param background_model Background model of the scene
 * @param output difference output between input frame and background model
 * @param bg_model_ndx ndx of the background model buffer
 * @return size of output image
 */
int detect_motion(const uchar *new_frame, uchar **background_buffer, float *background_model, uchar* mask, uchar *output,
                  int *bg_model_ndx) {
    int i = 0;
    int j = 0;
    uchar *smoothed_image = malloc(WIDTH * HEIGHT * 3);

    smooth_image(new_frame, WIDTH, HEIGHT, 3, smoothed_image);

    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            uchar new_value[3];
            float bg_value[3];
            float normalized_pixel[3];
            float new_bg_model[3];
            uchar oldest_bg_model[3];

            yuyv_get_pixel_value(smoothed_image, i, j, WIDTH, new_value);
            bg_model_get_pixel_value(background_model, i, j, WIDTH, bg_value);
            yuv_get_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, oldest_bg_model);

            for (int k = 0; k < 3; k++) {
                float new_out_value = ((new_value[k] - 127.0f) - (bg_value[k] - 127.0f))+127;
                //new_out_value = (new_out_value * (*(mask + i + j * WIDTH)/255))+127;

                normalized_pixel[k] = new_out_value;
            }

            double pixel_mag = 0;

            for (int k = 0; k < 3; k++) {
                pixel_mag += pow(normalized_pixel[k], 2);
            }

            pixel_mag = sqrt(pixel_mag);

            for (int k = 0; k < 3; k++) {
                new_bg_model[k] = (bg_value[k] + ((new_value[k])/ (float)BG_MODEL_SIZE) - ((oldest_bg_model[k])/ (float)BG_MODEL_SIZE));
            }

            bg_model_set_pixel_value(background_model, i, j, WIDTH, new_bg_model);

            if ((int)pixel_mag < THRESHOLD) {
                uchar out_value[] = {0, 127, 127};

                yuv_set_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, new_value);
                yuv_set_pixel_value(output, i, j, WIDTH, out_value);

                //*(mask + i + j * WIDTH) = *(mask + i + j * WIDTH) == 255 ? 255: *(mask + i + j * WIDTH) + 1;
            }
            else {
                uchar out_value[] = {255, 127, 127};
                yuv_set_pixel_value(output, i, j, WIDTH, out_value);

                //*(mask + i + j * WIDTH) = *(mask + i + j * WIDTH) == 0 ? 0: *(mask + i + j * WIDTH) - 1;
            }


        }
    }

    *bg_model_ndx = (*bg_model_ndx + 1) % BG_MODEL_SIZE;

    free(smoothed_image);
    return i * j;
}

/**
 * Opens webcam interface and SDL to display motion output to the user
 * @param argc number of args
 * @param argv arg values
 * @return exit code
 */
int main(int argc, char *argv[]) {
    g_cam_info.fd = -1;
    g_cam_info.dev_name = "/dev/video0";
    uchar *background_buffer[BG_MODEL_SIZE];
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *img = NULL;
    SDL_Thread *fetch_video_thread;
    SDL_Event e;
    int bg_model_ndx = 0;
    int pitch = WIDTH * 2;
    int bg_setup = 0;
    uchar *current_raw_frame;
    uchar *current_frame = malloc(WIDTH * HEIGHT * 3);
    float *background_model = malloc(WIDTH * HEIGHT * 3 * sizeof(float));
    uchar *output_frame = malloc(WIDTH * HEIGHT * 3);
    unsigned char *output_buffer = malloc(WIDTH * HEIGHT * 2);
    unsigned char *mask = malloc(WIDTH * HEIGHT);

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        background_buffer[i] = malloc(WIDTH * HEIGHT * 3);
    }

    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            *(mask + i + j*WIDTH) = 255;
        }
    }


    // Setup webcam for video capture
    open_device(&g_cam_info);

    init_device(&g_cam_info);

    start_capturing(&g_cam_info);

    printf("Opened webcam!\n");

    // Start SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Unable to initialize SDL: %s", strerror(errno));
        return 1;
    }

    // Create SDL Window and Render
    win = SDL_CreateWindow("motion_detector", 0, 0, WIDTH * 2, HEIGHT * 2, 0);
    renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, WIDTH,
                                             HEIGHT);

    // Start updating video
    fetch_video_thread = SDL_CreateThread(process_webcam_video, NULL, NULL);


    int blur = 0;
    int view = 0;
    // Main loop
    while (1) {
        // If there is a new SDL Event
        if (SDL_PollEvent(&e)) {
            // Process event
            switch (e.type) {
                // On exit, shutdown the camera thread
                case SDL_QUIT:
                    g_thread_exit = 1;
                    break;
                    // After camera thread has shutdown, goto cleanup
                case EXIT_EVENT:
                    goto cleanup;
                    // On new current_frame
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_8:
                            blur++;
                            break;
                        case SDLK_2:
                            blur--;
                            blur = blur < 0 ? 0 : blur;
                            break;
                        case SDLK_v:
                            view = (view + 1) % 4;
                    }
                    printf("Blur: %d\n", blur);
                    break;
                case NEW_FRAME_EVENT:
                    current_raw_frame = g_cam_info.buffers[e.user.code].start;
                    //yuyv_to_yuv(current_raw_frame, current_frame, WIDTH, HEIGHT);

                    if (!bg_setup) {
                        for (int i = 0; i < WIDTH; i++) {
                            for (int j = 0; j < HEIGHT; j++) {
                                float bg_model_pixel[3];
                                uchar current_frame_pixel[3];
                                bg_model_get_pixel_value(background_model, i, j, WIDTH, bg_model_pixel);
                                yuyv_get_pixel_value(current_raw_frame, i, j, WIDTH, current_frame_pixel);

                                bg_model_pixel[0] = (bg_model_pixel[0] + (float)current_frame_pixel[0] / BG_MODEL_SIZE);
                                bg_model_pixel[1] = (bg_model_pixel[1] + (float)(current_frame_pixel[1]) / BG_MODEL_SIZE);
                                bg_model_pixel[2] = (bg_model_pixel[2] + (float)(current_frame_pixel[2]) / BG_MODEL_SIZE);
                                yuv_set_pixel_value(background_buffer[bg_model_ndx], i, j, WIDTH, current_frame_pixel);
                                bg_model_set_pixel_value(background_model, i, j, WIDTH, bg_model_pixel);
                            }
                        }

                        bg_model_ndx++;

                        if (bg_model_ndx >= BG_MODEL_SIZE) {
                            bg_model_ndx = 0;
                            bg_setup = 1;
                        }
                    } else {
                        SDL_LockTexture(texture, NULL, (void **) &output_buffer, &pitch);

                        // Find difference between current thread and previous current_frame
                        detect_motion(current_raw_frame, background_buffer, background_model, mask, output_frame, &bg_model_ndx);

                        switch (view) {
                            case 0:
                                yuv_to_yuyv(output_frame, output_buffer, WIDTH, HEIGHT);
                                break;
                            case 1:
                                bg_model_to_yuyv(background_model, output_buffer, WIDTH, HEIGHT);
                                break;
                            case 2:
                                memcpy(output_buffer, current_raw_frame, WIDTH*HEIGHT*2);
                                break;
                            default:
                                for (int i = 0; i < WIDTH; i++) {
                                    for (int j = 0; j < HEIGHT; j++) {
                                        uchar pixel_val[3];
                                        pixel_val[0] = 0;
                                        pixel_val[1] = (i/(float)(WIDTH))*255;
                                        pixel_val[2] = 255-(j/(float)HEIGHT)*255;

                                        yuv_set_pixel_value(current_raw_frame, i, j, WIDTH, pixel_val);
                                    }
                                }
                                yuv_to_yuyv(current_raw_frame, output_buffer, WIDTH, HEIGHT);
                        }

                        // Update SDL window with output current_frame
                        SDL_UnlockTexture(texture);
                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        SDL_RenderPresent(renderer);
                    }
                    break;
            }

        }
    }

    // Cleanup SDL and camera interface
    cleanup:
    SDL_FreeSurface(img);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    stop_capturing(&g_cam_info);
    deallocate_buffers(&g_cam_info);
    close_device(&g_cam_info);
    free(output_frame);

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        free(background_buffer[i]);
    }

    return 0;
}
