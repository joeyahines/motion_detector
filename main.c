/**
 * Motion Detector
 *
 * @author Joey Hines
 *
 * Detects motion from the video stream of a webcam or other V4L capture device
 */


#include <libv4l2.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include "cam_api.h"

struct webcam_info g_cam_info;


#define NEW_FRAME_EVENT (SDL_USEREVENT+1)
#define END_EVENT (SDL_USEREVENT+2)

int thread_exit = 0;
int process_webcam_video(void *ptr) {
    thread_exit=0;
    while(!thread_exit) {
        SDL_Event event;
        event.type = NEW_FRAME_EVENT;

        int ndx = get_next_frame(&g_cam_info);

        if (ndx > 0) {
            event.user.code = ndx;
            SDL_PushEvent(&event);
        }
    }

    thread_exit=0;
    SDL_Event event;
    event.type = END_EVENT;
    SDL_PushEvent(&event);
    return 0;
}


int detect_motion(unsigned char *src1, unsigned char *src2, unsigned char *dst) {
    int i;
    int j;

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
}

int main() {
    g_cam_info.fd = -1;
    g_cam_info.dev_name = "/dev/video0";
    SDL_Window *win = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *img = NULL;
    unsigned char * old_buffer = malloc(WIDTH*HEIGHT*2);
    unsigned char * output_buffer = malloc(WIDTH*HEIGHT*2);

    open_device(&g_cam_info);

    printf("Opened webcam!\n");

    init_device(&g_cam_info);

    start_capturing(&g_cam_info);

   // Start SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return 1;

    win = SDL_CreateWindow("motion_detector", 100, 100, WIDTH*2, HEIGHT*2, 0);
    renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    SDL_Thread *refresh_thread = SDL_CreateThread(process_webcam_video,NULL,NULL);

    while (1) {
        SDL_Event e;

        if ( SDL_PollEvent(&e) ) {
            switch (e.type) {
                case SDL_QUIT:
                    thread_exit = 1;
                    break;
                case END_EVENT:
                   goto cleanup;
                case NEW_FRAME_EVENT:
                    detect_motion(old_buffer, g_cam_info.buffers[e.user.code].start, output_buffer);
                    SDL_UpdateTexture(texture, NULL, output_buffer, WIDTH*2);
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);
                    memcpy(old_buffer, g_cam_info.buffers[e.user.code].start, WIDTH*HEIGHT*2);
                    break;
            }

      }

    }

    cleanup:
    SDL_FreeSurface(img);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    stop_capturing(&g_cam_info);
    deallocate_buffers(&g_cam_info);
    close_device(&g_cam_info);

    return 0;
}
