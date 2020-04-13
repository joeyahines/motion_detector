//
// Created by joey on 4/13/20.
//

#include "image_manipulation.h"

/**
 * Gets a pixel Column i and Row j of a YUYV image
 * @param image the YUYV image to access
 * @param i row of pixel
 * @param j column of pixel
 * @param width pixel width of the pixel
 * @param pixel 3 element array of uchars to put the pixel values into
 */
void yuyv_get_pixel_value(const unsigned char *image, int i, int j, int width, unsigned char *pixel) {
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

/**
 * Sets a pixel at Column i and Row j of a YUYV image
 * @param image the YUYV image to update
 * @param i row of the pixel
 * @param j column of hhe pixel
 * @param width width of the image
 * @param pixel 3 element array of uchars
 */
void yuyv_set_pixel_value(unsigned char *image, int i, int j, int width, const unsigned char *pixel) {
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

/**
 * Gets a pixel Column i and Row j of a YUV image
 * @param image the YUV image to access
 * @param i row of pixel
 * @param j column of pixel
 * @param width pixel width of the pixel
 * @param pixel 3 element array of uchars to put the pixel values into
 */
void yuv_get_pixel_value(const unsigned char *image, int i, int j, int width, unsigned char *pixel) {
    for (int k = 0; k < 3; k++) {
        pixel[k] = *(image + i * 3 + j * width * 3 + k);
    }
}

/**
 * Sets a pixel at Column i and Row j of a YUV image
 * @param image the YUV image to update
 * @param i row of the pixel
 * @param j column of hhe pixel
 * @param width width of the image
 * @param pixel 3 element array of uchars
 */
void yuv_set_pixel_value(uchar *image, int i, int j, int width, const uchar *pixel) {
    for (int k = 0; k < 3; k++) {
        *(image + i * 3 + j * width * 3 + k) = pixel[k];
    }
}

/**
 * Gets a pixel Column i and Row j of the Background model
 * @param image the YUV image to access
 * @param i row of pixel
 * @param j column of pixel
 * @param width pixel width of the pixel
 * @param pixel 3 element array of floats to put the pixel values into
 */
void bg_model_get_pixel_value(const float *image, int i, int j, int width, float *pixel) {
    for (int k = 0; k < 3; k++) {
        pixel[k] = *(image + i * 3 + j * width * 3 + k);
    }
}

/**
 * Sets a pixel at Column i and Row j of the Background model
 * @param image the YUV image to update
 * @param i row of the pixel
 * @param j column of hhe pixel
 * @param width width of the image
 * @param pixel 3 element array of floats
 */
void bg_model_set_pixel_value(float *image, int i, int j, int width, const float *pixel) {
    for (int k = 0; k < 3; k++) {
        *(image + i * 3 + j * width * 3 + k) = pixel[k];
    }
}

/**
 * Converts a YUYV image to a YUV image
 * @param src YUYV image
 * @param dest YUV image
 * @param width width of the src image
 * @param height height of the src image
 */
void yuyv_to_yuv(const uchar *src, uchar *dest, int width, int height) {
    for (int i = 0; i < width; i += 2) {
        int src_i = i * 2;
        int dest_i = i * 3;
        for (int j = 0; j < height; j++) {
            uchar y1 = *(src + src_i + j * width * 2);
            uchar y2 = *(src + src_i + 2 + j * width * 2);
            uchar u = *(src + src_i + 1 + j * width * 2);
            uchar v = *(src + src_i + 3 + j * width * 2);

            *(dest + dest_i + j * width * 3) = y1;
            *(dest + dest_i + 3 + j * width * 3) = y2;

            *(dest + dest_i + 1 + j * width * 3) = u;
            *(dest + dest_i + 4 + j * width * 3) = u;

            *(dest + dest_i + 2 + j * width * 3) = v;
            *(dest + dest_i + 5 + j * width * 3) = v;
        }
    }
}

/**
 * Converts a YUV image to a sub-sampled YUYV image
 * @param src YUV image
 * @param dest output YUYV image
 * @param src_width width of the src image
 * @param src_height height of the src image
 */
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

/**
 * Converts the background model to a YUYV image
 * @param src background model
 * @param dest output YUYV image
 * @param src_width width of the background model
 * @param src_height height of the background model
 */
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