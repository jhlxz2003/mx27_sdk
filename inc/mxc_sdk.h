#ifndef MXC_SDK_H
#define MXC_SDK_H

#include <time.h>
#include <sys/time.h>

#ifdef   DEBUG
#define  DBG(fmt, ...)   \
         do {               \
            printf("File:%s,Line:%d,Func:%s-", __FILE__, __LINE__, __FUNCTION__); \
            printf(fmt, ##__VA_ARGS__); \
         } while (0)
#else
#define  DBG(fmt, ...)
#endif

#define MXC_FB_W   800
#define MXC_FB_H   480
#define MXC_BPP    2

#define MXC_FB_NUM  1

struct ievent {
	struct timeval time;
	int  value;
};

typedef struct _rect {
	int left;
	int top;
	int w;
	int h;
} RECT;

typedef struct _disp_para {
    int                dispX;		 /* diplay top co-ordinate */
    int                dispY;		 /* diplay left co-ordinate */
    int                dispW;		 /* resize display width */
    int                dispH;	        /* resize display height */
    int                rotate;		 /* display rotate angle */
    int                overlay;      /* overlaid display */
} disp_para_t;

typedef enum {
	ENC_QCIF = 0,
	ENC_QVGA,
	ENC_CIF,
	ENC_VGA,
	ENC_4CIF,
} picfmt_t;

typedef void (*STREAM_CALLBACK)(void* buf, int size, int frame_type, void* context);

#define EVT_DN      0
#define EVT_UP      1

#ifndef _HAL_RGB_H_
#define _HAL_RGB_H_

typedef unsigned short  COLOR;

#define RGB888(r, g, b)   (unsigned int)(((unsigned int)(r)<<16)|((unsigned int)(g)<<8)|((unsigned int)(b)))
#define RGB565(r, g, b)   (unsigned short)(((unsigned short)(r&0xf8)<<8)|((unsigned short)(g&0xfc)<<3)|((unsigned short)(b>>3)))

#if MXC_BPP == 2
#define R(x)			((unsigned char)((((unsigned short)x)>>8)&0xf8))
#define G(x)			((unsigned char)((((unsigned short)x)>>3)&0xfc))
#define B(x)			((unsigned char)((((unsigned short)x)<<3)&0xf8))
#elif MXC_BPP == 3
#define R(x)			((unsigned char)((x>>16)&0xff))
#define G(x)			((unsigned char)((x>>8)&0xff))
#define B(x)			((unsigned char)((x&0xff)))
#endif

#if MXC_BPP == 2
#define RGB_WHITE			    RGB565(255,255,255)
#define RGB_BLACK			    RGB565(0,0,0)
#define RGB_GRAY			    RGB565(0xC0,0xC0,0xC0)
#define RGB_DKGRAY		        RGB565(0x80,0x80,0x80)
#define RGB_RED				    RGB565(255,0,0)
#define RGB_GREEN			    RGB565(0,255,0)
#define RGB_BLUE			    RGB565(0,0,255)

//color style 1
#define RGB_NAVYBLUEBRIGHT      RGB565(136,192,184)
#define RGB_NAVYBLUEDARK        RGB565(72,144,136)
#define RGB_NAVYLTGRAY		    RGB565(200,224,216)
#define RGB_NAVYDKGRAY		    RGB565(44,78,71)

//color style 2
#define RGB_SYSTEMBRIGHT		RGB_NAVYBLUEBRIGHT
#define RGB_SYSTEMDARK			RGB_NAVYBLUEDARK
#define RGB_SYSTEMLTGRAY		RGB_NAVYLTGRAY
#define RGB_SYSTEMDKGRAY		RGB_NAVYDKGRAY
#define RGB_SYSTEMBLACK		    RGB_BLACK
#define RGB_SYSTEMWHITE		    RGB_WHITE
#define RGB_SYSTEMTRANS		    RGB565(255,0,0)
#define RGB_SYSTEMHIBRIGHT	    RGB565(173,221,255)

#elif MXC_BPP == 3
#define RGB_WHITE			    RGB888(255,255,255)
#define RGB_BLACK			    RGB888(0,0,0)
#define RGB_GRAY			    RGB888(0xC0,0xC0,0xC0)
#define RGB_DKGRAY		        RGB888(0x80,0x80,0x80)
#define RGB_RED				    RGB888(255,0,0)
#define RGB_GREEN			    RGB888(0,255,0)
#define RGB_BLUE			    RGB888(0,0,255)

//color style 1
#define RGB_NAVYBLUEBRIGHT      RGB888(136,192,184)
#define RGB_NAVYBLUEDARK        RGB888(72,144,136)
#define RGB_NAVYLTGRAY		    RGB888(200,224,216)
#define RGB_NAVYDKGRAY		    RGB888(44,78,71)

//color style 2
#define RGB_SYSTEMBRIGHT		RGB_NAVYBLUEBRIGHT
#define RGB_SYSTEMDARK			RGB_NAVYBLUEDARK
#define RGB_SYSTEMLTGRAY		RGB_NAVYLTGRAY
#define RGB_SYSTEMDKGRAY		RGB_NAVYDKGRAY
#define RGB_SYSTEMBLACK		    RGB_BLACK
#define RGB_SYSTEMWHITE		    RGB_WHITE
#define RGB_SYSTEMTRANS		    RGB888(255,0,0)
#define RGB_SYSTEMHIBRIGHT	    RGB888(173,221,255)

#endif
#endif

extern int   InitSystem();
extern void  FiniSystem();

extern int   GetMenuAddr(unsigned char **pAddr);
extern int   GetVideoAddr(unsigned char **pAddr);
extern int   SetMenuBufIndex(int idx);
extern int   SetVideoBufIndex(int idx);
extern void  SetLcdScreenMode(int mode);

extern void  mxc_fb_set_transparent(int en, unsigned char alpha);
extern void  mxc_fb_set_mask(int en, unsigned int color);
extern void  mxc_fb_set_color(RECT *rc, unsigned short clr);
extern void  mxc_fb_clear_area(RECT *rc);
extern void  mxc_fb_prepare_disp(RECT *rc);
extern void  mxc_fb_finish_disp(void);
extern int   mxc_fb_set_brightness(unsigned char v);

extern unsigned long  mxc_add_rtc_alarm(time_t tm, void (*action)(unsigned long), unsigned long data);
extern void  mxc_remove_rtc_alarm(unsigned long rtcid);
extern int   mxc_get_rtc(struct tm *tm);
extern int   mxc_set_rtc(struct tm *tm);

/* 一旦开启看门狗便无法关闭 */
extern int   start_watchdog(unsigned int timeout);
//extern void  stop_watchdog();
/* mode: 0 or 1 */
extern void  feed_watchdog(int mode);

extern int   mxc_open_rs485();
extern void  mxc_close_rs485();
extern int   mxc_set_rs485(int speed, int databits, int stopbits, int parity, int vtime, int vmin);
extern int   mxc_read_rs485(void *pBuf, size_t nCount);
extern int   mxc_write_rs485(const void *buf, size_t count);

extern int   mxc_set_alarm(int idx, int val);

extern int   mxc_alarmin_open(int idx);
extern int   mxc_alarm_close(int fd);
extern int   mxc_get_alarm(int fd, struct ievent *ev);
extern int   mxc_poll_alarm(int fd);

extern int   mxc_switch_init(int time, void *cb);


//////////////////////////////////////////
//       video encoding functions       //
//////////////////////////////////////////
extern void   vpu_EncSetCallback(STREAM_CALLBACK func, void* context);
extern void   vpu_EncSetVideoPara(int bri, int con, int sat, int hue);
extern void   vpu_EncSetIFrameItvl(int itvl);
extern void   vpu_EncSetFrameRate(int fps);
extern void   vpu_EncSetBitrate(int  bitrate);
extern void   vpu_CaptureIFrame(void);
extern void   vpu_EncSetPicFmt(picfmt_t  fmt);

extern int    vpu_StartEnc(char *fname);
extern unsigned int vpu_StopEnc(int cancel);
extern void   vpu_ExitEnc(void);
extern int    vpu_EncGetFrameRate(void);
extern void   vpu_DecRegStopCallback(void *fn, void *data);

//////////////////////////////////////////
//       video decoding functions       //
//////////////////////////////////////////
/* Before calling this, we should set the 'para' appropriately at first. */
extern int   vpu_StartDecFile(char *name, disp_para_t *par);

/* 返回已经编码的帧数 */
extern void  vpu_StopDec(void);
extern void  vpu_ExitDec(void);
extern void  vpu_DecRestoreStopCb(void);
extern int   vpu_DecStat(void);
extern int   vpu_InputAvData(char *buf, int size);
extern void  vpu_DecSetFullScreenMode(int m);


//////////////////////////////////////////
//      video preview functions         //
//////////////////////////////////////////
#define V4L2_CID_PRIVATE_BASE		0x08000000
#define V4L2_CID_MXC_ROT		    (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_MXC_FLASH		    (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_MXC_FLICKER		(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_MXC_TEAR_PROTECT	(V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_MXC_PWRDN	        (V4L2_CID_PRIVATE_BASE + 4)

#define V4L2_MXC_ROTATE_NONE			0
#define V4L2_MXC_ROTATE_VERT_FLIP		1
#define V4L2_MXC_ROTATE_HORIZ_FLIP		2
#define V4L2_MXC_ROTATE_180			    3
#define V4L2_MXC_ROTATE_90_RIGHT		4
#define V4L2_MXC_ROTATE_90_RIGHT_VFLIP	5
#define V4L2_MXC_ROTATE_90_RIGHT_HFLIP	6
#define V4L2_MXC_ROTATE_90_LEFT			7

#define AUTO_STD      0
#define PAL_STD       1
#define NTSC_STD      2
#define SECAM_STD     3

extern void  WS_camera_select(int i);
extern void  WS_preview_set_input_size(int x, int y, int w, int h);
extern int   WS_preview_start(int left, int top, int w, int h);
extern void  WS_preview_stop(void);
extern int   WS_preview_fd(void);

extern int   v4l2_set_rotate(int fd, int r);
extern int   v4l_set_bright(int fd, int bri);
extern int   v4l_set_contrast(int fd, int con);
extern int   v4l_set_saturation(int fd, int sat);
extern int   v4l_set_hue(int fd, int hue);
extern int   v4l_set_red_balance(int fd, int red);
extern int   v4l_set_blue_balance(int fd, int blue);
extern int   v4l_pwr_on(int fd, int v);
extern int   v4l_set_std(int fd, int v);

extern int   v4l_get_bright(int fd);
extern int   v4l_get_contrast(int fd);
extern int   v4l_get_saturation(int fd);
extern int   v4l_get_hue(int fd);
extern int   v4l_get_red_balance(int fd);
extern int   v4l_get_blue_balance(int fd);

///////////////////////////////////////////
//              audio functions          //
///////////////////////////////////////////
#define MIXER_OUT_VOL    1
#define MIXER_IN_VOL     2
#define MIXER_SPK_SW     3
#define MIXER_LO_SW      4
#define MIXER_INROUTE    5
#define MIXER_INMUTE     6
#define MIXER_OUTMUTE    7

#define MXC_MICIN        0   //capture through microphone.
#define MXC_LIN_DIFF     1   //capture through differential line_in.
#define MXC_LIN_SE       2   //capture through single-ended line_in.

typedef struct _audio_info
{
	char    path[128];
	char   *name;
	int     duration;
	long    frames;
    double  fps;
	long    rate;
	int     channels;
	int     encoding;
	/* for wav file */
	unsigned int dataoff;
	unsigned int datasz;
	int     byte_p_sec;

	int     inited;
} af_info_t;

typedef void (*mixer_callback)(int, long);

extern void  mxc_speaker_on(int on);
extern void  mxc_lineout_on(int on);

extern void  mxc_capture_mode(int m);

extern void  mxc_playback_mute(int m);
extern void  mxc_capture_mute(int m);

extern void  mxc_playback_volume_set(int vol);
extern void  mxc_capture_volume_set(int vol);
extern int   mxc_playback_volume_get(void);
extern int   mxc_capture_volume_get(void);

extern void  mxc_mixer_set_callback(int numid, mixer_callback cb);

/* 0:default action-when plug,switch speaker off. */
extern void  mxc_jack_set_policy(int p);
extern int   mxc_jack_get_policy(void);
extern int   mxc_jack_is_plugged(void);

extern int   af_StartDecFile(af_info_t *info);
extern void  af_DecSeek(int tm);
extern void  af_DecPause(void);
extern void  af_DecResume(void);
extern void  af_StopDec(void);
extern void  af_DecRegStopCallback(void *fn, void *d);
extern void  af_DecRestoreStopCb(void);

///////////////////////////////////////////////////////////////
#define SPEECH_CODEC_ID_PCMU       0
#define SPEECH_CODEC_ID_PCMA       1
#define SPEECH_CODEC_ID_G722       2
#define SPEECH_CODEC_ID_GSM        3
#define SPEECH_CODEC_ID_G7221      4

#define SPEECH_CODEC_NAME_PCMU       "pcmu"
#define SPEECH_CODEC_NAME_PCMA       "pcma"
#define SPEECH_CODEC_NAME_G722       "g722"
#define SPEECH_CODEC_NAME_GSM        "gsm"
#define SPEECH_CODEC_NAME_G7221      "g7221/16000"


extern int   mxc_speech_encode_start (void);
extern void  mxc_speech_encode_stop  (void);
extern int   mxc_speech_encode_read  (char *buf, int len);

extern int   mxc_speech_decode_start (void);
extern void  mxc_speech_decode_stop  (int imed);
extern int   mxc_speech_decode_write (char *data, int len);


///////////////////////////////////////////////////////////////
#define  NormalMode           0
#define  OutLmMode            1
#define  InDenyLmMode         2
#define  InDenyMode           3
#define  InLmMode             4
#define  OutDenyMode          5

extern void  mxc_session_hangup(void);
extern void  mxc_session_shift(void);
extern void  mxc_session_accept(void);
extern void  mxc_session_unlock_door(void);
extern void  mxc_camera_control(int m);
extern void  mxc_record_control(int m);

extern void  mxc_set_nv_notify_callee(void *f, void *r);
extern void  mxc_set_acc_notify_callee(void *f);

extern void  mxc_set_shift_notify_caller(void *f);
extern void  mxc_set_deny_notify_caller(void *f);
extern void  mxc_set_none_notify_caller(void *f);
extern void  mxc_set_nv_notify_caller(void *f);
extern void  mxc_set_busy_notify_caller(void *f);
extern void  mxc_set_acc_notify_caller(void *f);
extern void  mxc_set_leaveword_notify_caller(void *f, void *g);
extern void  mxc_set_initial_notify_caller(void *f);
extern void  mxc_caller_camera_control(int m);
extern void  mxc_caller_hup(void);
extern void  mxc_set_ring_cb(void *f);
extern void  mxc_set_getip_method(void *f);
extern void  mxc_set_before_ring(void *f);
extern void  mxc_set_callee_win_from_outside(void *o);
extern void  mxc_set_callee_win_from_inside(void *i);
extern void  mxc_set_caller_nr(void *f);
extern void  mxc_set_sgip(char *ip);
extern void  mxc_set_prip(char *ip);
extern void  mxc_set_last_ui(void *f);

extern unsigned int  mxc_get_peer_nr(void);

extern int   mxc_caller_callout(unsigned int nr, int minor, int vt);
extern void  mxc_caller_timeout(void);
extern void  mxc_caller_leaveword(void);
extern void  mxc_start_call_proc(void);
extern void  mxc_set_id(unsigned int nr, int id);
extern void  mxc_set_valid_func(void *f);
extern void  mxc_set_calling_cb(void *p, void *f);



#endif

