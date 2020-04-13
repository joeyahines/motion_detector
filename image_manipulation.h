//
// Created by joey on 4/13/20.
//

#ifndef MOTION_DETECTOR_IMAGE_MANIPULATION_H
#define MOTION_DETECTOR_IMAGE_MANIPULATION_H
#define uchar unsigned char

void yuyv_get_pixel_value(const uchar *image, int i, int j, int width, uchar *pixel);
void yuyv_set_pixel_value(uchar *image, int i, int j, int width, const uchar *pixel);
void yuv_get_pixel_value(const uchar *image, int i, int j, int width, uchar *pixel);
void yuv_set_pixel_value(uchar *image, int i, int j, int width, const uchar *pixel);
void bg_model_get_pixel_value(const float *image, int i, int j, int width, float *pixel);
void bg_model_set_pixel_value(float *image, int i, int j, int width, const float *pixel);
void yuyv_to_yuv(const uchar *src, uchar *dest, int width, int height);
void yuv_to_yuyv(const uchar *src, uchar *dest, int src_width, int src_height);
void bg_model_to_yuyv(const float *src, uchar *dest, int src_width, int src_height);
#endif //MOTION_DETECTOR_IMAGE_MANIPULATION_H
