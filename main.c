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
#include "image_manipulation.h"

#define BG_MODEL_SIZE 10
#define THRESHOLD 225
#define NEW_FRAME_EVENT (SDL_USEREVENT+1)
#define EXIT_EVENT (SDL_USEREVENT+2)

enum view {WEBCAM, MOTION_OUTPUT, BG_MODEL, MOTION_MASK, COLOR_MAP};

struct webcam_info g_cam_info;
int g_process_thread_exit = 0;

/**
 * Thread to process new video from the webcam
 * @param ptr Pointer to data used by this function
 * @return 0
 */
int process_webcam_video(void *ptr) {
    g_process_thread_exit = 0;

    // While the thread is not exiting
    while (!g_process_thread_exit) {
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

/**
 * Compare function for qsort
 *
 * @param a first value to compare
 * @param b second value to compare
 * @return difference between a and b
 */
int compare(const void * a, const void * b) {
    return ( *(int*)a - *(int*)b );
}

/**
 * Uses a convolution and median filter so smooth the src
 *
 * @param src source src to smooth
 * @param width width of the src
 * @param height height of the src
 * @param filter_size convolution and median filter size to use
 * @param dest filtered src
 */
void smooth_image(const unsigned char *src, int width, int height, int filter_size, unsigned char *dest) {
    float *h_smooth;
    float *values;
    h_smooth = (float *) malloc(sizeof(float) * filter_size * filter_size);
    values = (float *) malloc(sizeof(float) * filter_size * filter_size);
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
                        yuv_get_pixel_value(src, ii, jj, width, pixel_value);
                        float filter_val = *(h_smooth + (filter_size * (k + half_w) + l + half_w));
                        tmp += filter_val * pixel_value[0];
                        *(values + (l + half_w) + (k + half_w) * filter_size) = (float)pixel_value[0];
                    }
                }
            }
            float median;
            qsort(values, filter_size*filter_size, sizeof(float), compare);

            if((filter_size*filter_size)%2 == 0)
                median = (values[(filter_size*filter_size-1)/2] + values[filter_size*filter_size/2])/2.0;
            else
                median = *(values + (filter_size*filter_size/2));

            yuv_get_pixel_value(src, i, j, width, output_pixel);

            if (median > 150) {
                output_pixel[0] = tmp;


            }
            else {
                output_pixel[0] = 0;
            }

            yuv_set_pixel_value(dest, i, j, width, output_pixel);
        }
    }
    free(values);
    free(h_smooth);
}

/**
 * Find the box of motion in the image
 *
 * @param image black and white motion image
 * @param rect SDL rect to populate
 * @param width width of the motion image
 * @param height height of motion image
 */
void find_motion_box(const uchar *image, SDL_Rect * rect, int width, int height) {
    int min_x = width;
    int min_y = height;
    int max_x = 0;
    int max_y = 0;
    int pixel_count = 0;

    uchar pixel_value[3];
    for (int i = 0; i < width; i += 2) {
       for (int j = 0; j < height; j++) {
            yuv_get_pixel_value(image, i, j, width, pixel_value);

            if(pixel_value[0] > 200) {
                if (i < min_x) {
                   min_x = i;
                }
                else if (i > max_x) {
                    max_x = i;
                }
                if (j < min_y) {
                    min_y = j;
                }
                else if (j > max_y) {
                    max_y = j;
                }
                pixel_count++;
            }
       }
    }

    int rect_width = (max_x - min_x)*2;
    int rect_height = (max_y - min_y)*2;
    int area = rect_height*rect_width;

    if (area < 10 || pixel_count < 300) {
        rect->x = 0;
        rect->y = 0;
        rect->w = 0;
        rect->h = 0;
    }
    else {
        rect->x = min_x*2;
        rect->y = min_y*2;
        rect->w = rect_width;
        rect->h = rect_height;
    }

}

/**
 * Detects if motion has occurred between by differencing and filtering the new frame with a background model
 *
 * @param new_frame new frame from the video service
 * @param background_buffer background buffer containing
 * @param background_model background model of the scene
 * @param mask motion mask of the image
 * @param output motion image output
 * @param bg_model_ndx Index of the oldest frame in the background model
 * @return
 */
int detect_motion(const uchar *new_frame, uchar **background_buffer, float *background_model, float* mask, uchar *output,
                  int *bg_model_ndx) {
    int i = 0;
    int j = 0;
    uchar *pre_smoothed_output_image = malloc(WIDTH * HEIGHT * 3);

    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            uchar new_value[3];
            float bg_value[3];
            float normalized_pixel[3];
            float new_bg_model[3];
            uchar oldest_bg_model[3];

            yuyv_get_pixel_value(new_frame, i, j, WIDTH, new_value);
            bg_model_get_pixel_value(background_model, i, j, WIDTH, bg_value);
            yuv_get_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, oldest_bg_model);

            for (int k = 0; k < 3; k++) {
                float new_out_value = ((new_value[k] - 127.0f) - (bg_value[k] - 127.0f))+127;
                new_out_value = new_out_value * *(mask + i + j);

                normalized_pixel[k] = new_out_value;
            }

            double pixel_mag = 0;

            for (int k = 0; k < 3; k++) {
                pixel_mag += pow(normalized_pixel[k], 2);
            }

            pixel_mag = sqrt(pixel_mag);


            float new_mask_value;
            if ((int)pixel_mag < THRESHOLD) {
                uchar out_value[] = {0, 127, 127};
                new_mask_value = *(mask + i + j * WIDTH) + 0.01f;

                yuv_set_pixel_value(pre_smoothed_output_image, i, j, WIDTH, out_value);


                //*(mask + i + j * WIDTH) = *(mask + i + j * WIDTH) == 255 ? 255: *(mask + i + j * WIDTH) + 5;
            }
            else {
                uchar out_value[] = {255, 127, 127};
                new_mask_value = *(mask + i + j * WIDTH) - 0.1f;
                yuv_set_pixel_value(pre_smoothed_output_image, i, j, WIDTH, out_value);
            }

            for (int k = 0; k < 3; k++) {
                new_bg_model[k] = (bg_value[k] + ((new_value[k])/ (float)BG_MODEL_SIZE) - ((oldest_bg_model[k])/ (float)BG_MODEL_SIZE));
            }
            yuv_set_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, new_value);
            bg_model_set_pixel_value(background_model, i, j, WIDTH, new_bg_model);

            if (new_mask_value < 0.0) {
                new_mask_value = 0.0f;
            }
            else if (new_mask_value > 1.0) {
                new_mask_value = 1.0f;
            }
            *(mask + i + j * WIDTH) = new_mask_value;

        }
    }

    *bg_model_ndx = (*bg_model_ndx + 1) % BG_MODEL_SIZE;

    smooth_image(pre_smoothed_output_image, WIDTH, HEIGHT, 3, output);
    free(pre_smoothed_output_image);
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
    SDL_Event e;
    int bg_model_ndx = 0;
    int pitch = WIDTH * 2;
    int bg_setup = 0;
    uchar *current_raw_frame;
    uchar *current_frame = malloc(WIDTH * HEIGHT * 3);
    float *background_model = malloc(WIDTH * HEIGHT * 3 * sizeof(float));
    uchar *output_frame = malloc(WIDTH * HEIGHT * 3);
    uchar *output_buffer = malloc(WIDTH * HEIGHT * 2);
    float *mask = malloc(WIDTH * HEIGHT * sizeof(float));
    SDL_Rect rect;
    char window_name[50];

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        background_buffer[i] = malloc(WIDTH * HEIGHT * 3);
    }

    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            *(mask + i + j*WIDTH) = 1.0f;
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
    win = SDL_CreateWindow("Motion Detector", 0, 0, WIDTH * 2, HEIGHT * 2, 0);
    renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, WIDTH,
                                             HEIGHT);

    // Start updating video
    SDL_CreateThread(process_webcam_video, NULL, NULL);

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
                    g_process_thread_exit = 1;
                    break;
                    // After camera thread has shutdown, goto cleanup
                case EXIT_EVENT:
                    goto cleanup;
                    // On new current_frame
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_v:
                            view = (view + 1) % 4;
                            break;
                        case SDLK_c:
                            view = COLOR_MAP;
                    }
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
                            case MOTION_OUTPUT:
                                snprintf(window_name, 40, "Motion Detector: Motion Image");
                                yuv_to_yuyv(output_frame, output_buffer, WIDTH, HEIGHT);
                                break;
                            case BG_MODEL:
                                snprintf(window_name, 40, "Motion Detector: Background Model");
                                bg_model_to_yuyv(background_model, output_buffer, WIDTH, HEIGHT);
                                break;
                            default:
                            case WEBCAM:
                                snprintf(window_name, 40, "Motion Detector");
                                memcpy(output_buffer, current_raw_frame, WIDTH*HEIGHT*2);
                                break;
                            case MOTION_MASK:
                                snprintf(window_name, 40, "Motion Detector: Motion Mask");
                                for (int i = 0; i < WIDTH; i++) {
                                    for (int j = 0; j < HEIGHT; j++) {
                                        uchar pixel_val[3];
                                        uchar mask_value = *(mask + i + j *WIDTH) * 255;
                                        pixel_val[0] = mask_value;
                                        pixel_val[1] = 127;
                                        pixel_val[2] = 127;

                                        yuyv_set_pixel_value(output_buffer, i, j, WIDTH, pixel_val);
                                    }
                                }
                                break;
                            case COLOR_MAP:
                                snprintf(window_name, 40, "Motion Detector: YUYV Color Space");
                                for (int i = 0; i < WIDTH; i++) {
                                    for (int j = 0; j < HEIGHT; j++) {
                                        uchar pixel_val[3];
                                        pixel_val[0] = 0;
                                        pixel_val[1] = (i/(float)(WIDTH))*255;
                                        pixel_val[2] = 255-(j/(float)HEIGHT)*255;

                                        yuv_set_pixel_value(current_frame, i, j, WIDTH, pixel_val);
                                    }
                                }
                                yuv_to_yuyv(current_frame, output_buffer, WIDTH, HEIGHT);
                        }

                        find_motion_box(output_frame, &rect, WIDTH, HEIGHT);

                        // Update SDL window with output current_frame
                        SDL_UnlockTexture(texture);
                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        SDL_SetRenderDrawColor(renderer, 0, 255, 0, SDL_ALPHA_OPAQUE);
                        SDL_RenderDrawRect(renderer, &rect);
                        SDL_RenderPresent(renderer);
                        SDL_SetWindowTitle(win, window_name);
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
