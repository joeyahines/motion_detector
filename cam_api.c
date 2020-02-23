/**
 * Interfaces with V4L to capture video
 *
 * This code was adapted from https://linuxtv.org/downloads/v4l-dvb-apis/uapi/v4l/capture.c.html
 */
#include "cam_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

static int xioctl(int fd, int request, void* argp) {
    int r;

    do r = ioctl(fd, request, argp);
    while (-1 == r && EINTR == errno);

    return r;
}


void open_device(struct webcam_info *cam_info) {
    struct stat st;

    if (-1 == stat(cam_info->dev_name, &st)) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                cam_info->dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no devicen", cam_info->dev_name);
        exit(EXIT_FAILURE);
    }

    cam_info->fd = open(cam_info->dev_name, O_RDWR | O_NONBLOCK, 0);

    if (-1 == cam_info->fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                cam_info->dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void init_mmap(struct webcam_info * cam_info) {
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(cam_info->fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", cam_info->dev_name);
            exit(EXIT_FAILURE);
        } else {
            exit(EXIT_FAILURE);
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", cam_info->dev_name);
        exit(EXIT_FAILURE);
    }

    cam_info->buffers = calloc(req.count, sizeof(*cam_info->buffers));

    if (!cam_info->buffers) {
        fprintf(stderr, "Out of memory\\n");
        exit(EXIT_FAILURE);
    }

    for (cam_info->num_of_buffers = 0; cam_info->num_of_buffers < req.count; ++cam_info->num_of_buffers) {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = cam_info->num_of_buffers;

        if (-1 == ioctl(cam_info->fd, VIDIOC_QUERYBUF, &buf)) {
            exit(EXIT_FAILURE);
        }

        cam_info->buffers[cam_info->num_of_buffers].length = buf.length;
        cam_info->buffers[cam_info->num_of_buffers].start =
                mmap(NULL /* start anywhere */,
                     buf.length,
                     PROT_READ | PROT_WRITE /* required */,
                     MAP_SHARED /* recommended */,
                     cam_info->fd, buf.m.offset);

        if (MAP_FAILED == cam_info->buffers[cam_info->num_of_buffers].start)
            exit(EXIT_FAILURE);
    }
}

void init_device(struct webcam_info *cam_info) {
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;

    if (-1 == xioctl(cam_info->fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", cam_info->dev_name);
            exit(EXIT_FAILURE);
        } else {
            fprintf(stderr, "Failed to to query %s, %s\n", cam_info->dev_name, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", cam_info->dev_name);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", cam_info->dev_name);
        exit(EXIT_FAILURE);
    }

    /* Select video input, video standard and tune here. */

    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(cam_info->fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == ioctl(cam_info->fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }
        }
    } else {
        /* Errors ignored. */
    }


    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (-1 == ioctl(cam_info->fd, VIDIOC_S_FMT, &fmt)) {
        exit(-1);
    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;

    init_mmap(cam_info);
}

void start_capturing(struct webcam_info *cam_info) {
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < cam_info->num_of_buffers; ++i) {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == ioctl(cam_info->fd, VIDIOC_QBUF, &buf))
            exit(EXIT_FAILURE);
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == ioctl(cam_info->fd, VIDIOC_STREAMON, &type))
        exit(EXIT_FAILURE);
}

void stop_capturing(struct webcam_info *cam_info) {
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == ioctl(cam_info->fd, VIDIOC_STREAMOFF, &type))
        exit(-1);
}


int read_frame(struct webcam_info *cam_info) {
    struct v4l2_buffer buf;
    CLEAR(buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == ioctl(cam_info->fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */

            default:
                exit(EXIT_FAILURE);
        }
    }

    assert(buf.index < cam_info->num_of_buffers);

    if (-1 == ioctl(cam_info->fd, VIDIOC_QBUF, &buf))
        exit(EXIT_FAILURE);

    return buf.index;
}

void mainloop(struct webcam_info *cam_info) {
    unsigned int count;

    while (count-- > 0) {
        for (;;) {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(cam_info->fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(cam_info->fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r) {
                if (EINTR == errno)
                    continue;
                exit(EXIT_FAILURE);
            }

            if (0 == r) {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if (read_frame(cam_info))
                break;
            /* EAGAIN - continue select loop. */

        }
    }
}

int get_next_frame(struct webcam_info *cam_info) {
    fd_set fds;
    struct timeval tv;
    int r;

    FD_ZERO(&fds);
    FD_SET(cam_info->fd, &fds);

    /* Timeout. */
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    r = select(cam_info->fd + 1, &fds, NULL, NULL, &tv);

    if (-1 == r) {
        if (EINTR == errno)
            return -1;
        exit(EXIT_FAILURE);
    }

    if (0 == r) {
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
    }

    return read_frame(cam_info);
}
void deallocate_buffers(struct webcam_info *cam_info) {
    for (int i = 0; i < cam_info->num_of_buffers; ++i) {
        if (-1 == munmap(cam_info->buffers[i].start, cam_info->buffers[i].length)) {
            exit(EXIT_FAILURE);
        }
    }
}

void close_device(struct webcam_info *cam_info) {
    close(cam_info->fd);
}

