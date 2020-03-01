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

/**
 * Detects if motion has occurred between two frames by differencing
 * @param src1 Frame 1
 * @param src2 Frame 2
 * @param dst Output frame
 * @return size of output image
 */
int detect_motion(unsigned char *src1, unsigned char *src2, unsigned char *dst) {
    int i = 0;
    int j = 0;

    for (i = 0; i < WIDTH*2; i++) {
        for (j = 0; j < HEIGHT; j++) {
            int src1_value = *(src1 + (i + j*WIDTH*2));
            int src2_value = *(src2 + (i + j*WIDTH*2));

            int value = src1_value - src2_value;

            if (value < 0) {
                value = 0;
            }

           *(dst + (i + j*WIDTH*2))  = value;
        }
    }

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
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *img = NULL;
    SDL_Thread *fetch_video_thread;
    SDL_Event e;

    unsigned char *old_buffer = malloc(WIDTH * HEIGHT * 2);
    unsigned char *output_buffer = malloc(WIDTH * HEIGHT * 2);

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
                    detect_motion(old_buffer, g_cam_info.buffers[e.user.code].start, output_buffer);

                    // Update SDL window with output frame
                    SDL_UpdateTexture(texture, NULL, output_buffer, WIDTH*2);
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);

                    // Copy current frame to old frame
                    memcpy(old_buffer, g_cam_info.buffers[e.user.code].start, WIDTH*HEIGHT*2);
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
    free(old_buffer);
    free(output_buffer);

    return 0;
}
