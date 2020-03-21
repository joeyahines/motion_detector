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
    while(!g_thread_exit) {
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

#define BG_MODEL_SIZE 10
/**
 * Detects if motion has occurred between two frames by differencing
 * @param new_frame New frame from video source
 * @param background_model Background model of the scene
 * @param output difference output between input frame and background model
 * @param bg_model_ndx ndx of the background model buffer
 * @return size of output image
 */
int detect_motion(const uchar *new_frame, uchar **background_model, uchar *output, int *bg_model_ndx) {
    int i = 0;
    int j = 0;

    for (i = 0; i < WIDTH*2; i++) {
        for (j = 0; j < HEIGHT; j++) {
            int frame_value = *(new_frame + (i + j*WIDTH*2));

            int bg_value = 0;
            for (int ndx = 0; ndx < BG_MODEL_SIZE; ndx++) {
                int bg_ndx = (ndx + *bg_model_ndx) % BG_MODEL_SIZE;
                bg_value += (*(background_model[bg_ndx] + (i + j*WIDTH*2)))/(BG_MODEL_SIZE);
            }

            if (bg_value < 0) {
                bg_value = 0;
            }
            else if (bg_value > 255) {
                bg_value = 255;
            }

            int out_value = frame_value - bg_value;

            if (out_value < 0) {
                out_value = 0;
            }

            *(output + (i + j*WIDTH*2))  = out_value;
            *(background_model[(*bg_model_ndx)] + (i + j*WIDTH*2))  = frame_value;
        }
    }

    *bg_model_ndx = (*bg_model_ndx + 1) % BG_MODEL_SIZE;
    return i*j;
}

/**
 * Opens webcam interface and SDL to display motion output to the user
 * @param argc number of args
 * @param argv arg values
 * @return exit code
 */
int main(int argc, char * argv[]) {
    g_cam_info.fd = -1;
    g_cam_info.dev_name = "/dev/video0";
    uchar * background_model[BG_MODEL_SIZE];
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *img = NULL;
    SDL_Thread *fetch_video_thread;
    SDL_Event e;
    int bg_model_ndx = 0;

    unsigned char *output_buffer = malloc(WIDTH * HEIGHT * 2);

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
            background_model[i] = malloc(WIDTH * HEIGHT * 2);
    }

    // Setup webcam for video vapture
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
    win = SDL_CreateWindow("motion_detector", 100, 100, WIDTH*2, HEIGHT*2, 0);
    renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    // Start updating video
    fetch_video_thread = SDL_CreateThread(process_webcam_video,NULL,NULL);

    // Main loop
    while (1) {
        // If there is a new SDL Event
        if ( SDL_PollEvent(&e) ) {
            // Process event
            switch (e.type) {
                // If user wants to quit, shutdown video thread
                case SDL_QUIT:
                    g_thread_exit = 1;
                    break;
                // After video thread has shutdown, head to cleanup
                case EXIT_EVENT:
                   goto cleanup;
               // On new frame
                case NEW_FRAME_EVENT:
                    // Find difference between current thread and previous frame
                    detect_motion(g_cam_info.buffers[e.user.code].start, background_model, output_buffer, &bg_model_ndx);

                    // Update SDL window with output frame
                    SDL_UpdateTexture(texture, NULL, output_buffer, WIDTH*2);
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);

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
    free(output_buffer);

    for (int i = 0; i < BG_MODEL_SIZE; i++) {
        free(background_model[i]);
    }

    return 0;
}
