#ifndef _VPU_H
#define _VPU_H

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/videodev.h>
#include <pthread.h>
#include "../vpu/vpu_lib.h"
#include "../vpu/vpu_io.h"

typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;

#define VPU_MAX_INSTANCE     4

#define STREAM_BUF_SIZE	 0x40000
#define STREAM_FILL_SIZE	 0x8000
#define STREAM_READ_SIZE	(512 * 4)
#define STREAM_END_SIZE		 0

#define FRAME_BUF_NUM	 32
#define V4L2_BUFF_NUM    4
#define V4L2_DEV         "/dev/v4l/video0"

#define V4LSINK_BUF_MAX  16

#define CAP_BUFF_NUM   3

#define ENC_DEF_W      640
#define ENC_DEF_H      480

#define DEF_FPS        25

#define MXC_FB_W       800
#define MXC_FB_H       480

typedef struct _ftail {
    unsigned char magic[4];
    unsigned int  fps;
    unsigned int  frames;
} ftail_t;

typedef enum {
	ENC_QCIF = 0,
	ENC_QVGA,
	ENC_CIF,
	ENC_VGA,
	ENC_4CIF,
} picfmt_t;

struct frame_buf {
	int addrY;
	int addrCb;
	int addrCr;
	vpu_mem_desc desc;
};

struct v4l_buf {
//	void *start;
	off_t  offset;
	size_t length;
};

typedef struct _disp_para {
    int                dispX;		 /* diplay top co-ordinate */
    int                dispY;		 /* diplay left co-ordinate */
    int                dispW;		 /* resize display width */
    int                dispH;	     /* resize display height */
    int                rotate;		 /* display rotate angle */
//    int                overlay;
} disp_para_t;

typedef struct _v4lsink {
    int                fd;
    int                ncount;
    struct timeval     tv;
    struct v4l2_buffer buf;
    struct v4l_buf     buffers[V4LSINK_BUF_MAX];
    int                bufCnt;   

    unsigned long      usec;
    int                inWidth;
    int                inHeight;     /* the size of the incoming YUV stream */
    int                fullscreen;

    disp_para_t       *dispPara;

    pthread_t          tid;
} vpu_v4lsink_t;

typedef struct decode {
	DecHandle         handle;
	DecParam          decparam;

	vpu_mem_desc      mem_desc;
	PhysicalAddress   phy_bsbuf_addr;
	u32               virt_bsbuf_addr;
	int               img_size;
	int               picwidth;
	int               picheight;
	int               fbcount;
	FrameBuffer       fb[FRAME_BUF_NUM];
	struct frame_buf *pfbpool[FRAME_BUF_NUM];
	vpu_v4lsink_t    *sink;

	int               eos;
	int               fill_end_bs;
	unsigned int      frame_id;

	int               rot_angle;
	int               fps;
	void             (*stop_cb) (void*);
	void              *data;

	void              *thread;
} mxc_dec_t;

typedef void (*STREAM_CALLBACK)(void* buf, int size, int frame_type, void* context);

typedef struct video_param {
	int     bri;
	int     con;
	int     sat;
	int     hue;
	int     cfg;
} video_param_t;

typedef struct encode {
	EncHandle         handle;
	EncParam          encParam;
	EncOpenParam      encop;

	vpu_mem_desc      mem_desc;
	PhysicalAddress   phy_bsbuf_addr;
	u32               virt_bsbuf_addr;

	int               img_size;
	int               picwidth;
	int               picheight;

	int               fbcount;
	int               src_fbid;
	FrameBuffer       fb[FRAME_BUF_NUM];
	struct frame_buf *pfbpool[FRAME_BUF_NUM];

	unsigned char     sps_hdr[32];
	unsigned char     pps_hdr[32];
	int               sps_sz;
	int               pps_sz;

	STREAM_CALLBACK   callback;
	void             *data;
	unsigned int      frames;
	
	struct v4l_buf    capBuf[CAP_BUFF_NUM];
	int               bufCnt;
	/* v4l capture fd */
	int               fd;
	MirrorDirection   mirror;
	int               rot_angle;
	int               fps;
	int               bitrate;
	int               gop;

	pthread_t         tid;
	video_param_t     vpara;
	void             *thread;
} mxc_enc_t;

#define CLEAR(x)    memset(&(x), 0, sizeof(x))
#define CLR_PTR(x)  memset(x, 0, sizeof(*x))

/* called by sdk init routine */
extern void   VPU_framebuf_init(void);

/* called before opening encoder or decoder */
extern int    VPU_init(void);
/* called after closing encoder or decoder */
extern void   VPU_uninit(void);

extern void   vpu_EncSetCallback(STREAM_CALLBACK func, void* context);

extern vpu_v4lsink_t* v4lsink_get();
extern void   v4lsink_set_disp_para(vpu_v4lsink_t* sink, disp_para_t *par);
extern void   v4lsink_set_input_para(vpu_v4lsink_t* snk, int w, int h, int fps);
extern int    v4lsink_open(vpu_v4lsink_t* snk, int nframes);
extern void   v4lsink_close(vpu_v4lsink_t* snk);
extern int    v4lsink_put_data(vpu_v4lsink_t* snk);
extern int    v4lsink_reset(vpu_v4lsink_t* snk);

extern struct frame_buf *vpu_framebuf_alloc(int strideY, int height);
extern void   vpu_framebuf_free(struct frame_buf *fb);

extern int    v4l_start_capturing(mxc_enc_t *enc);
extern void   v4l_stop_capturing(mxc_enc_t *enc);
extern int    v4l_capture_setup(mxc_enc_t *enc);
extern int    v4l_get_capture_data(mxc_enc_t *enc, struct v4l2_buffer *buf);
extern int    v4l_put_capture_data(mxc_enc_t *enc, struct v4l2_buffer *buf);

extern void   mxc_overlay_init();
extern void   mxc_preview_set_input_size(int x, int y, int w, int h);
extern int    mxc_preview_start(int left, int top, int w, int h);
extern void   mxc_preview_stop();

extern int    vpu_GetEncData(char *buf, int len);
extern int    vpu_StartDec(disp_para_t *para);

#endif

