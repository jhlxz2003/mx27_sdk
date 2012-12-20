#ifndef _V4L_H_
#define _V4L_H_

#define V4L2_DEF_W    640
#define V4L2_DEF_H    480


#define V4L2_FB_DEV   "/dev/fb0"

struct v4l2_format;

extern int   open_chrdev(char *name, int nb);
extern int   xioctl (int fd, int request, void *arg);

extern int   v4l2_set_input(int fd, int chl);
extern int   v4l2_set_output(int fd, int o);

extern int   v4l2_set_rotate(int fd, int r);
extern int   v4l2_set_fmt(int fd, int w, int h, int io);
extern int   v4l2_get_fmt(int fd, struct v4l2_format *fmt, int io);
extern int   v4l2_set_frame_rate(int fd, int fps);
//extern int   v4l2_set_defcrop(int fd);

extern int   v4l_set_video_param(int fd, int bri, int con, int sat, int hue);
extern int   v4l_get_video_param(int fd, int *bri, int *con, int *sat, int *hue);
extern int   v4l_get_video_param_default(int fd, int *bri, int *con, int *sat, int *hue);
extern int   v4l_set_video_red_balance(int fd, int value);
extern int   v4l_set_video_blue_balance(int fd, int value);


#endif

