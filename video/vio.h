#ifndef _VIO_H_
#define _VIO_H_

extern int   mxc_vpu_encoder_init(void);
extern int   start_video_encoder(void);
extern void  stop_video_encoder(void);

extern int   mxc_vpu_decoder_init(void);
extern void  mxc_set_disp_area(int x, int y, int w, int h);
extern int   mxc_video_decode_open(void);
extern int   mxc_video_decode_buffer_fill(char *buf, int len);
extern int   mxc_video_decode_start(void);
extern void  mxc_video_decode_stop(void);
extern int   mxc_video_decode_frame(char *buf, int len);

#endif

