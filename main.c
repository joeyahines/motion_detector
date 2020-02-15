/*
 * Motion Detector
 *
 * Joey Hines
 *
 * Video capture code based off an example from:
 * https://www.linuxtv.org/downloads/v4l-dvb-apis-new/uapi/v4l/capture.c.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <jpeglib.h>
#include <libv4l2.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
    void   *start;
    size_t  length;
};

struct webcam_info {
    char * dev_name;
    int fd;
    struct buffer *buffers;
    int num_of_buffers;
    int frame_count;
};

static int xioctl(int fd, int request, void* argp)
{
    int r;

    do r = ioctl(fd, request, argp);
    while (-1 == r && EINTR == errno);

    return r;
}


static void open_device(struct webcam_info *cam_info) {
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

static void init_mmap(struct webcam_info * cam_info)
{
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

static void init_device(struct webcam_info *cam_info) {
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
    fmt.fmt.pix.width       = 320;
    fmt.fmt.pix.height      = 240;
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

static void start_capturing(struct webcam_info *cam_info) {
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

static void stop_capturing(struct webcam_info *cam_info) {
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == ioctl(cam_info->fd, VIDIOC_STREAMOFF, &type))
        exit(-1);
}

static int read_frame(struct webcam_info *cam_info) {
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


    fflush(stderr);
    fprintf(stderr, ".");
    fflush(stdout);

    if (-1 == ioctl(cam_info->fd, VIDIOC_QBUF, &buf))
        exit(EXIT_FAILURE);

    return 1;
}

static void mainloop(struct webcam_info *cam_info) {
    unsigned int count;

    count = cam_info->frame_count;

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

void YUV420toYUV444(int width, int height, unsigned char* src, unsigned char* dst) {
    int line, column;
    unsigned char *py, *pu, *pv;
    unsigned char *tmp = dst;

    // In this format each four bytes is two pixels. Each four bytes is two Y's, a Cb and a Cr.
    // Each Y goes to one of the pixels, and the Cb and Cr belong to both pixels.
    unsigned char *base_py = src;
    //unsigned char *base_pu = src+(height*width);
    //unsigned char *base_pv = src+(height*width)+(height*width)/4;

    for (line = 0; line < height; ++line) {
        for (column = 0; column < width*2; column++) {
            py = base_py+(line*width)+column*2;
            //pu = base_pu+(line/2*width/2)+column/2;
            //pv = base_pv+(line/2*width/2)+column/2;

            *tmp++ = *py;
            //*tmp++ = *pu;
            //*tmp++ = *pv;
        }
    }
}

static void jpegWrite(unsigned char* src, char* jpegFilename)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    unsigned char* tmp_row_buf = malloc(320*3*sizeof(char));

    JSAMPROW row_pointer[1];
    FILE *outfile = fopen( jpegFilename, "wb" );

    // try to open file for saving
    if (!outfile) {
        exit(EXIT_FAILURE);
    }

    // create jpeg data
    cinfo.err = jpeg_std_error( &jerr );
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    // set image parameters
    cinfo.image_width = 320;
    cinfo.image_height = 240;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    // set jpeg compression parameters to default
    jpeg_set_defaults(&cinfo);
    // and then adjust quality setting
    jpeg_set_quality(&cinfo, 70, TRUE);

    // start compress
    jpeg_start_compress(&cinfo, TRUE);

    // feed data
    row_pointer[0] = &tmp_row_buf[0];
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
        for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //input strides by 4 bytes, output strides by 6 (2 pixels)
            tmp_row_buf[j + 0] = src[offset + i + 0]; // Y (unique to this pixel)
            tmp_row_buf[j + 1] = src[offset + i + 1]; // U (shared between pixels)
            tmp_row_buf[j + 2] = src[offset + i + 3]; // V (shared between pixels)
            tmp_row_buf[j + 3] = src[offset + i + 2]; // Y (unique to this pixel)
            tmp_row_buf[j + 4] = src[offset + i + 1]; // U (shared between pixels)
            tmp_row_buf[j + 5] = src[offset + i + 3]; // V (shared between pixels)
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    // finish compression
    jpeg_finish_compress(&cinfo);

    // destroy jpeg data
    jpeg_destroy_compress(&cinfo);

    // close output file
    fclose(outfile);
}

int main() {
    struct webcam_info cam_info;
    cam_info.fd = -1;
    cam_info.dev_name = "/dev/video0";
    cam_info.frame_count = 10;

    open_device(&cam_info);

    printf("Opened webcam!\n");

    init_device(&cam_info);

    start_capturing(&cam_info);

    mainloop(&cam_info);

    stop_capturing(&cam_info);


    for (int i = 0; i < cam_info.num_of_buffers; ++i) {
        char file_name[100];

        snprintf(file_name, 100, "out%d.jpg", i);

        jpegWrite(cam_info.buffers[i].start, file_name);
    }

    for (int i = 0; i < cam_info.num_of_buffers; ++i) {
        if (-1 == munmap(cam_info.buffers[i].start, cam_info.buffers[i].length)) {
            exit(EXIT_FAILURE);
        }
    }


    close(cam_info.fd);
    return 0;
}
