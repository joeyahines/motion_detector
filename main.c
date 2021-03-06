/**
 * Motion Detector
 *
 * @author Joey Hines
 *
 * Detects motion from the video stream of a webcam or other V4L capture device
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include "cam_api.h"
#include "image_manipulation.h"
#include "lib/quick_select/quick_select.h"
#include "lib/libattopng/libattopng.h"
#ifdef TEST_MODE
#include <jpeglib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#endif

// Model Parameters
#define BG_MODEL_SIZE 10
#define THRESHOLD 225
#define FILTER_SIZE 3

// SDL Events
#define NEW_FRAME_EVENT (SDL_USEREVENT+1)
#define EXIT_EVENT (SDL_USEREVENT+2)

// Color constants
const uchar YUV_BLACK[] = {0, 127, 127};
const uchar YUV_WHITE[] = {255, 127, 127};

// Application views
enum view {
    WEBCAM, MOTION_OUTPUT, BG_MODEL, MOTION_MASK, COLOR_MAP
};

// Global webcam info struct
struct webcam_info g_cam_info;

// Flag to exit processing thread
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
 * Uses a convolution and median filter so smooth the Y channel of the src image
 *
 * @param src source src to smooth
 * @param width width of the src
 * @param height height of the src
 * @param filter_size convolution and median filter size to use
 * @param dest filtered src
 */
void smooth_image(const unsigned char *src, int width, int height, int filter_size, unsigned char *dest) {
    double *neighborhood_values;
    double *kernel;
    int half_w = filter_size / 2;
    double coefficent = 1.0 / (2 * M_PI * 1.5 * 1.5);
    int L = (filter_size - 1) / 2;

    // Allocate kernel and neighborhood values
    kernel = (double *) malloc(sizeof(double) * width * height);
    neighborhood_values = (double *) malloc(sizeof(double) * filter_size * filter_size);

    // Generate smoothing kernel
    for (int i = 0; i < filter_size; i++) {
        for (int j = 0; j < filter_size; j++) {
            *(kernel + i + j * filter_size) = coefficent * pow(M_E, -(pow(i - L, 2) + pow(j - L, 2)) / (2 * 1.5 * 1.5));
        }
    }

    // Filter image
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            uchar output_pixel[3];
            double median;
            double smoothed_value = 0.0;

            // Smooth pixel
            for (int k = -half_w; k <= half_w; k++) {
                for (int l = -half_w; l <= half_w; l++) {
                    int ii = i + k;
                    int jj = j + l;

                    // If the pixel requested is in bounds
                    if (((ii < width) && (ii >= 0)) && ((jj < height) && (jj >= 0))) {
                        // Get Y channel value
                        uchar pixel_value = *(src + ii * 3 + jj * width * 3);
                        // Get filter value
                        double filter_val = *(kernel + (filter_size * (k + half_w) + l + half_w));
                        // Smooth value
                        smoothed_value += filter_val * (double) pixel_value;
                        *(neighborhood_values + (l + half_w) + (k + half_w) * filter_size) = pixel_value;
                    }
                }
            }

            // Find median
            median = quick_select(neighborhood_values, filter_size * filter_size);

            // Threshold median
            if (median > 240.0) {
                output_pixel[0] = 255;

            } else {
                output_pixel[0] = 0;
            }

            output_pixel[1] = 127;
            output_pixel[2] = 127;

            // Set output pixel of smoothed image
            yuv_set_pixel_value(dest, i, j, width, output_pixel);
        }
    }

    // Free malloced arrays
    free(neighborhood_values);
    free(kernel);
}

/**
 * Find the box of motion in the image
 *
 * @param image black and white motion image
 * @param rect SDL rect to populate
 * @param width width of the motion image
 * @param height height of motion image
 */
void find_motion_box(const uchar *image, SDL_Rect *rect, int width, int height) {
    int min_x = width;
    int min_y = height;
    int max_x = 0;
    int max_y = 0;
    int rect_height;
    int rect_width;
    int pixel_count = 0;
    int area;
    uchar pixel_value[3];

    // Look for motion box
    for (int i = 0; i < width; i += 2) {
        for (int j = 0; j < height; j++) {
            // Get pixel
            yuv_get_pixel_value(image, i, j, width, pixel_value);

            // Threshold pixel
            if (pixel_value[0] > 200) {
                // Determine if this pixel is the max or min row pixel
                if (i < min_x) {
                    min_x = i;
                } else if (i > max_x) {
                    max_x = i;
                }
                // Determine if this pixel is the max or min column pixel
                if (j < min_y) {
                    min_y = j;
                } else if (j > max_y) {
                    max_y = j;
                }
                pixel_count++;
            }
        }
    }

    // Find width and height of the rectangle
    rect_width = (max_x - min_x) * 2;
    rect_height = (max_y - min_y) * 2;
    area = rect_height * rect_width;

    // If the rectangle is too small or contains too few motion pixels
    if (area < 10 || pixel_count < 200) {
        // Draw a 0 sized rectangle
        rect->x = 0;
        rect->y = 0;
        rect->w = 0;
        rect->h = 0;
    } else {
        // Draw rectangle around motion
        rect->x = min_x * 2;
        rect->y = min_y * 2;
        rect->w = rect_width;
        rect->h = rect_height;
    }
}

/**
 * Finds the magnitude of a 3 entry array
 *
 * @param array 3 element float array
 * @return magnitude of the array
 */
double magnitude(float *array) {
    double pixel_mag = 0.0;

    // Sum up each element squared
    for (int k = 0; k < 3; k++) {
        pixel_mag += pow(array[k], 2);
    }

    // Take square root
    return sqrt(pixel_mag);
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
int
detect_motion(const uchar *new_frame, uchar **background_buffer, float *background_model, float *mask, uchar *output,
              int *bg_model_ndx, int filter_size) {
    int i;
    int j;
    uchar *pre_smoothed_output_image = malloc(WIDTH * HEIGHT * 3);
    uchar new_value[3];
    float bg_value[3];
    float normalized_pixel[3];
    float new_bg_model[3];
    uchar oldest_bg_model[3];
    float new_out_value;
    double pixel_mag;
    float new_mask_value;

    // Find each motion pixel
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            pixel_mag = 0;

            // Get pixel of the new frame
#ifndef TEST_MODE
            yuyv_get_pixel_value(new_frame, i, j, WIDTH, new_value);
#else
            yuv_get_pixel_value(new_frame, i, j, WIDTH, new_value);
#endif
            // Get bg model pixel
            bg_model_get_pixel_value(background_model, i, j, WIDTH, bg_value);
            // Get the oldest pixel of the background buffer
            yuv_get_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, oldest_bg_model);

            // For each channel
            for (int k = 0; k < 3; k++) {
                new_out_value = (((float) new_value[k] - 127.0f) - (bg_value[k] - 127.0f)) + 127;
                new_out_value = new_out_value * *(mask + i + j);

                normalized_pixel[k] = new_out_value;
            }

            // Find pixel magnitude
            pixel_mag = magnitude(normalized_pixel);

            // Threshold magnitude
            if ((int) pixel_mag < THRESHOLD) {
                // If the pixel magnitude is below the threshold, its not a motion pixel. Set pixel to black
                yuv_set_pixel_value(pre_smoothed_output_image, i, j, WIDTH, YUV_BLACK);
                // Increase the motion mask to make this pixel more sensitive to motion
                new_mask_value = *(mask + i + j * WIDTH) + 0.05f;
            } else {
                // If the pixel magnitude is above the threshold, its a motion pixel. Set pixel to white
                yuv_set_pixel_value(pre_smoothed_output_image, i, j, WIDTH, YUV_WHITE);
                // Decrease the motion mask to make this pixel less sensitive to motion
                new_mask_value = *(mask + i + j * WIDTH) - 0.2f;
            }

            // Update background model by adding in new frame and removing oldest frame from the model
            for (int k = 0; k < 3; k++) {
                new_bg_model[k] = (bg_value[k] + ((new_value[k]) / (float) BG_MODEL_SIZE) -
                                   ((oldest_bg_model[k]) / (float) BG_MODEL_SIZE));
            }

            // Overwrite oldest frame in the buffer with new frame
            yuv_set_pixel_value(background_buffer[*bg_model_ndx], i, j, WIDTH, new_value);
            // Update pixel in background model
            bg_model_set_pixel_value(background_model, i, j, WIDTH, new_bg_model);

            // Saturate mask value
            if (new_mask_value < 0.0) {
                new_mask_value = 0.0f;
            } else if (new_mask_value > 1.0) {
                new_mask_value = 1.0f;
            }

            // Update mask
            *(mask + i + j * WIDTH) = new_mask_value;

        }
    }

    // Increment oldest background model value
    *bg_model_ndx = (*bg_model_ndx + 1) % BG_MODEL_SIZE;

    // Smooth motion image
    smooth_image(pre_smoothed_output_image, WIDTH, HEIGHT, filter_size, output);

    // Free allocated buffer
    free(pre_smoothed_output_image);
    return i * j;
}

#ifndef TEST_MODE
/**
 * Opens webcam interface and SDL to display motion output to the user
 * @param argc number of args
 * @param argv arg values
 * @return exit code
 */
int main(int argc, char *argv[]) {
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *img = NULL;
    SDL_Event e;
    SDL_Rect rect;
    uchar *background_buffer[BG_MODEL_SIZE];
    int bg_model_ndx = 0;
    int pitch = WIDTH * 2;
    int bg_setup = 0;
    uchar *current_raw_frame;
    uchar *current_frame = malloc(WIDTH * HEIGHT * 3);
    float *background_model = malloc(WIDTH * HEIGHT * 3 * sizeof(float));
    uchar *motion_image = malloc(WIDTH * HEIGHT * 3);
    uchar *display_buffer = malloc(WIDTH * HEIGHT * 2);
    int view = 0;
    float *mask = malloc(WIDTH * HEIGHT * sizeof(float));
    char window_name[50];
    int change_window = 0;

    // Setup g_cam_info struct
    g_cam_info.fd = -1;
    g_cam_info.dev_name = argv[1];

    // Initialize background model buffer
    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        background_buffer[i] = malloc(WIDTH * HEIGHT * 3);
    }

    // Fill motion mask with 1.0
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            *(mask + i + j * WIDTH) = 1.0f;
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
                case EXIT_EVENT:
                    // After camera thread has shutdown, goto cleanup
                    goto cleanup;
                case SDL_KEYDOWN:
                    // On keypress
                    switch (e.key.keysym.sym) {
                        case SDLK_v:
                            view = (view + 1) % 4;
                            change_window = 1;
                            break;
                        case SDLK_c:
                            view = COLOR_MAP;
                            change_window = 1;
                    }
                    break;
                case NEW_FRAME_EVENT:
                    // On new frame
                    current_raw_frame = g_cam_info.buffers[e.user.code].start;

                    // If the background bootstrapping has not been preformed
                    if (!bg_setup) {
                        // Update background model and background buffer
                        for (int i = 0; i < WIDTH; i++) {
                            for (int j = 0; j < HEIGHT; j++) {
                                float bg_model_pixel[3];
                                uchar current_frame_pixel[3];
                                bg_model_get_pixel_value(background_model, i, j, WIDTH, bg_model_pixel);
                                yuyv_get_pixel_value(current_raw_frame, i, j, WIDTH, current_frame_pixel);

                                bg_model_pixel[0] = (bg_model_pixel[0] +
                                                     (float) current_frame_pixel[0] / BG_MODEL_SIZE);
                                bg_model_pixel[1] = (bg_model_pixel[1] +
                                                     (float) (current_frame_pixel[1]) / BG_MODEL_SIZE);
                                bg_model_pixel[2] = (bg_model_pixel[2] +
                                                     (float) (current_frame_pixel[2]) / BG_MODEL_SIZE);
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
                        // Lock texture for access
                        SDL_LockTexture(texture, NULL, (void **) &display_buffer, &pitch);

                        // Preform motion detection operations
                        detect_motion(current_raw_frame, background_buffer, background_model, mask, motion_image,
                                      &bg_model_ndx, FILTER_SIZE);

                        // Find motion box from the motion image

                        find_motion_box(motion_image, &rect, WIDTH, HEIGHT);
                        // Display current view
                        switch (view) {
                            case MOTION_OUTPUT:
                                // Motion image output
                                snprintf(window_name, 40, "Motion Detector: Motion Image");
                                yuv_to_yuyv(motion_image, display_buffer, WIDTH, HEIGHT);
                                break;
                            case BG_MODEL:
                                // Background model view
                                snprintf(window_name, 40, "Motion Detector: Background Model");
                                bg_model_to_yuyv(background_model, display_buffer, WIDTH, HEIGHT);
                                break;
                            default:
                            case WEBCAM:
                                // Video from webcam
                                snprintf(window_name, 40, "Motion Detector");
                                memcpy(display_buffer, current_raw_frame, WIDTH * HEIGHT * 2);
                                break;
                            case MOTION_MASK:
                                // Motion mask view
                                snprintf(window_name, 40, "Motion Detector: Motion Mask");
                                for (int i = 0; i < WIDTH; i++) {
                                    for (int j = 0; j < HEIGHT; j++) {
                                        uchar pixel_val[3];
                                        uchar mask_value = *(mask + i + j * WIDTH) * 255;
                                        pixel_val[0] = mask_value;
                                        pixel_val[1] = 127;
                                        pixel_val[2] = 127;

                                        yuyv_set_pixel_value(display_buffer, i, j, WIDTH, pixel_val);
                                    }
                                }
                                break;
                            case COLOR_MAP:
                                //Debug color ma
                                snprintf(window_name, 40, "Motion Detector: YUYV Color Space");
                                for (int i = 0; i < WIDTH; i++) {
                                    for (int j = 0; j < HEIGHT; j++) {
                                        uchar pixel_val[3];
                                        pixel_val[0] = 0;
                                        pixel_val[1] = (i / (float) (WIDTH)) * 255;
                                        pixel_val[2] = 255 - (j / (float) HEIGHT) * 255;

                                        yuv_set_pixel_value(current_frame, i, j, WIDTH, pixel_val);
                                    }
                                }
                                yuv_to_yuyv(current_frame, display_buffer, WIDTH, HEIGHT);
                        }


                        // Update SDL window with output current_frame
                        SDL_UnlockTexture(texture);
                        // Copy texture to render
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        // Draw motion rectangle
                        SDL_SetRenderDrawColor(renderer, 0, 255, 0, SDL_ALPHA_OPAQUE);
                        SDL_RenderDrawRect(renderer, &rect);

                        // Update display
                        SDL_RenderPresent(renderer);

                        if (change_window) {
                            // update window title
                            SDL_SetWindowTitle(win, window_name);
                            change_window = 0;
                        }

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
    free(motion_image);

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        free(background_buffer[i]);
    }

    return 0;
}
#endif

#ifdef TEST_MODE
int bytes_per_pixel = 3;   /* or 1 for GRACYSCALE images */
int color_space = JCS_RGB;

/**
 * Reads a jpeg file from disk
 * @param filename file location of jpeg
 * @param raw_image image buffer to store image atain
 * @return
 */
int read_jpeg_file(char *filename, uchar *raw_image) {
    struct jpeg_decompress_struct c_info;
    struct jpeg_error_mgr j_err;
    JSAMPROW row_pointer[1];
    FILE *image_file = fopen(filename, "rb");
    unsigned long location = 0;
    int i;

    // Check if ile opened
    if (!image_file) {
        printf("Error opening jpeg file %s\n!", filename);
        return -1;
    }

    // Setup decompress
    c_info.err = jpeg_std_error(&j_err);

    jpeg_create_decompress(&c_info);

    jpeg_stdio_src(&c_info, image_file);

    jpeg_read_header(&c_info, TRUE);

    jpeg_start_decompress(&c_info);

    row_pointer[0] = (unsigned char *) malloc(c_info.output_width * c_info.num_components);

    // Read JPEG file in
    while (c_info.output_scanline < c_info.image_height) {
        jpeg_read_scanlines(&c_info, row_pointer, 1);
        for (i = 0; i < c_info.image_width * c_info.num_components; i++)
            raw_image[location++] = row_pointer[0][i];
    }

    // Cleanup
    jpeg_finish_decompress(&c_info);
    jpeg_destroy_decompress(&c_info);
    free(row_pointer[0]);
    fclose(image_file);

    return 1;
}

#define RGBA(r, g, b, a) ((r) | ((g) << 8) | ((b) << 16) | ((a) << 24))

/**
 * Writes raw image data to a PNG file
 *
 * Taken from: https://github.com/misc0110/libattopng
 * @param filename File location to save to
 * @param image raw imageuffer
 * @return
 */
int write_png_file(char *filename, uchar *image) {
    libattopng_t *png = libattopng_new(WIDTH, HEIGHT, PNG_RGBA);

    // Get the greyscale value of each pixel and save it as RG
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            uchar value = *(image + i * 3 + j * WIDTH * 3);

            libattopng_set_pixel(png, i, j, RGBA(value, value, value, 255));
        }
    }

    // Write image to disk
    if(libattopng_save(png, filename)) {
        fprintf(stderr, "Failed to save png\n");
        exit(-1);
    }

    // Cleanup
    libattopng_destroy(png);
}

/**
 * Test mode main
 * @param arc arg count
 * @param argv arg values: 1 - CDNET data path 2 - test length
 * @return
 */
int main(int arc, char *argv[]) {
    char in_filename[100];
    char out_filename[100];
    int bg_model_ndx = 0;
    float *background_model = malloc(WIDTH * HEIGHT * 3 * sizeof(float));
    uchar *background_buffer[BG_MODEL_SIZE];
    uchar *motion_image = malloc(WIDTH * HEIGHT * 3);
    float *mask = malloc(WIDTH * HEIGHT * sizeof(float));
    uchar *raw_image = (uchar *) malloc(WIDTH * HEIGHT * 3);
    int number_of_test_frames = atoi(argv[2]);
    clock_t t;
    double run_time = 0;

    // Change dir to test data
    if (chdir(argv[1])) {
        fprintf(stderr, "Failed to change dir\n");
        exit(-1);
    }


    // Makes results directory if it does not already exis
    if (mkdir("results", S_IRWXU)) {
        if (errno != EEXIST) {
            fprintf(stderr, "Failed to make results dir\n");
            exit(-1);
        }
    }

    // Initialize background model buffer
    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        background_buffer[i] = malloc(WIDTH * HEIGHT * 3);
    }

    // Fill motion mask with 1.0
    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            *(mask + i + j * WIDTH) = 1.0f;
        }
    }

    // Run motion detector on each frame
    for (int ndx = 1; ndx < number_of_test_frames; ndx++) {
        snprintf(in_filename, 70, "input/in%06d.jpg", ndx);
        snprintf(out_filename, 70, "results/bin%06d.png", ndx);
        // Read JPEG and convert to YUV
        if (read_jpeg_file(in_filename, raw_image) == 1) {
            for (int i = 0; i < WIDTH * 3; i += 3) {
                for (int j = 0; j < HEIGHT; j++) {
                    uchar b = *(raw_image + i + j * WIDTH * 3);
                    uchar g = *(raw_image + i + 1 + j * WIDTH * 3);
                    uchar r = *(raw_image + i + 2 + j * WIDTH * 3);

                    *(raw_image + i + j * WIDTH * 3) = (0.257 * r) + (0.504 * g) + (0.098 * b) + 16;
                    *(raw_image + i + 1 + j * WIDTH * 3) = (0.439 * r) - (0.368 * g) - (0.071 * b) + 128;
                    *(raw_image + i + 2 + j * WIDTH * 3) = -(0.148 * r) - (0.291 * g) + (0.439 * b) + 128;
                }
            }

            //Run motion detection and time
            t = clock();
            detect_motion(raw_image, background_buffer, background_model, mask, motion_image,
                          &bg_model_ndx, FILTER_SIZE);

            run_time += ((double)(clock() - t)) / CLOCKS_PER_SEC;

            // Write png
            write_png_file(out_filename, motion_image);
        } else {
            exit(-1);
        }

        printf("Finished image %d\n", ndx);

    }

    // Print stats
    printf("Finished in processing %d frames in %f seconds. FPS: %f\n", number_of_test_frames, run_time,
           number_of_test_frames / run_time);

    free(raw_image);
}

#endif
