

#ifndef MOTION_DETECTOR_CAM_API_H
#define MOTION_DETECTOR_CAM_API_H
#define WIDTH 320
#define HEIGHT 240

#include <sys/types.h>

/**
 * A struct for storing frame data
 */
struct buffer {
    void   *start;
    size_t  length;
};

/**
 * Struct for storing shared webcam data
 */
struct webcam_info {
    char * dev_name;
    int fd;
    struct buffer *buffers;
    int num_of_buffers;
};

void open_device(struct webcam_info *);
void init_mmap(struct webcam_info *);
void init_device(struct webcam_info *);
void start_capturing(struct webcam_info *);
void stop_capturing(struct webcam_info *);
void close_device(struct webcam_info *);
void deallocate_buffers(struct webcam_info *);
int read_frame(struct webcam_info *);
int get_next_frame(struct webcam_info *);
#endif //MOTION_DETECTOR_CAM_API_H
