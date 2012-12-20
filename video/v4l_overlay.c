#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>	
#include <sys/socket.h>
#include <sys/stat.h>	
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev.h>
#include <sys/mman.h>
#include <asm/arch/mxcfb.h>
#include <asm/arch/mx2fb.h>
#include <pthread.h>
#include "v4l.h"
#include "dbg.h"

extern int  g_camera_dev;
////////////////////////////////////////////////////
//           overlay/preview  routines            //
////////////////////////////////////////////////////
#if 0
{
#endif

typedef struct _rect {
	int left;
	int top;
	int width;
	int height;
} rect_t;

typedef struct _ovl {
	int fd;
	int ovl;
	int fr;
	int chl;
	rect_t irect;
} ovl_t;

#define ipu_fourcc(a,b,c,d)\
        (((__u32)(a)<<0)|((__u32)(b)<<8)|((__u32)(c)<<16)|((__u32)(d)<<24))

#define IPU_PIX_FMT_RGB332  ipu_fourcc('R','G','B','1') /*!<  8  RGB-3-3-2     */
#define IPU_PIX_FMT_RGB555  ipu_fourcc('R','G','B','O') /*!< 16  RGB-5-5-5     */
#define IPU_PIX_FMT_RGB565  ipu_fourcc('R','G','B','P') /*!< 16  RGB-5-6-5     */
#define IPU_PIX_FMT_RGB666  ipu_fourcc('R','G','B','6') /*!< 18  RGB-6-6-6     */
#define IPU_PIX_FMT_BGR24   ipu_fourcc('B','G','R','3') /*!< 24  BGR-8-8-8     */
#define IPU_PIX_FMT_RGB24   ipu_fourcc('R','G','B','3') /*!< 24  RGB-8-8-8     */
#define IPU_PIX_FMT_BGR32   ipu_fourcc('B','G','R','4') /*!< 32  BGR-8-8-8-8   */
#define IPU_PIX_FMT_BGRA32  ipu_fourcc('B','G','R','A') /*!< 32  BGR-8-8-8-8   */
#define IPU_PIX_FMT_RGB32   ipu_fourcc('R','G','B','4') /*!< 32  RGB-8-8-8-8   */
#define IPU_PIX_FMT_RGBA32  ipu_fourcc('R','G','B','A') /*!< 32  RGB-8-8-8-8   */
#define IPU_PIX_FMT_ABGR32  ipu_fourcc('A','B','G','R') /*!< 32  ABGR-8-8-8-8  */

#ifndef CLEAR
#define CLEAR(x)    memset(&(x), 0, sizeof(x))
#endif

#ifndef CLR_PTR
#define CLR_PTR(x)  memset(x, 0, sizeof(*x))
#endif

#ifndef V4L2_DEV
#define V4L2_DEV    "/dev/v4l/video0"
#endif


static ovl_t  mxc_ovl;

static int
v4l2_overlay_set_display_size(ovl_t *ovl, int left, int top, int width, int height)
{
	struct v4l2_format fmt;
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	fmt.fmt.win.w.left =  left;
	fmt.fmt.win.w.top = top;
	fmt.fmt.win.w.width = width;
	fmt.fmt.win.w.height = height;
	if (xioctl(ovl->fd, VIDIOC_S_FMT, &fmt) < 0)
	{
	    DBG("--- VIDIOC_S_FMT error ---\n");
	    return -1;
	}

	if (xioctl(ovl->fd, VIDIOC_G_FMT, &fmt) < 0) 
	{
	    DBG("--- VIDIOC_G_FMT error ---\n");
	    return -1;
	}
	return 0;
}

/* DONE */
static int
v4l2_overlay_set_crop(ovl_t *ovl)
{
	struct v4l2_crop crop;
	crop.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	if (g_camera_dev == 0) {
	    if (ovl->irect.width == 0||ovl->irect.height == 0) {
		ovl->irect.left = 0;
		ovl->irect.top = 0;
		ovl->irect.width = V4L2_DEF_W;
		ovl->irect.height = V4L2_DEF_H;
	    }
	    crop.c.left = ovl->irect.left;
	    crop.c.top = ovl->irect.top;
	    crop.c.width = ovl->irect.width;
	    crop.c.height = ovl->irect.height;
	} else {
		crop.c.left = 0;
	    crop.c.top = 0;
	    crop.c.width = 720;
	    crop.c.height = 576;
	}
	return xioctl(ovl->fd, VIDIOC_S_CROP, &crop);
}

/* DONE */
static int
v4l2_overlay_set_fb(ovl_t *ovl)
{
	int fd_fb;
	struct v4l2_framebuffer fb_v4l2;

	CLEAR(fb_v4l2);
	if (!ovl->ovl) {
		struct fb_fix_screeninfo fix;
		struct fb_var_screeninfo var;
		if ((fd_fb = open(V4L2_FB_DEV, O_RDWR)) < 0)	{
			DBG("Unable to open frame buffer\n");
			return -1;
		}

		if (xioctl(fd_fb, FBIOGET_VSCREENINFO, &var) < 0) {
			close(fd_fb);
			return -1;
		}
		if (xioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) < 0) {
			close(fd_fb);
			return -1;
		}

		fb_v4l2.fmt.width = var.xres;
		fb_v4l2.fmt.height = var.yres;
		if (var.bits_per_pixel == 32) {
			fb_v4l2.fmt.pixelformat = IPU_PIX_FMT_BGR32;
			fb_v4l2.fmt.bytesperline = 4 * fb_v4l2.fmt.width;
		} else if (var.bits_per_pixel == 24) {
			fb_v4l2.fmt.pixelformat = IPU_PIX_FMT_BGR24;
			fb_v4l2.fmt.bytesperline = 3 * fb_v4l2.fmt.width; 
		} else if (var.bits_per_pixel == 16) {
			fb_v4l2.fmt.pixelformat = IPU_PIX_FMT_RGB565;
			fb_v4l2.fmt.bytesperline = 2 * fb_v4l2.fmt.width; 
		}

		fb_v4l2.flags = V4L2_FBUF_FLAG_PRIMARY;
		fb_v4l2.base = (void *)fix.smem_start;

		close(fd_fb);
	}
	else
	{
		if (xioctl(ovl->fd, VIDIOC_G_FBUF, &fb_v4l2) < 0) {
			DBG("Get framebuffer failed\n");
			return -1;
		}
		fb_v4l2.flags = V4L2_FBUF_FLAG_OVERLAY;	
	}

	if (xioctl(ovl->fd, VIDIOC_S_FBUF, &fb_v4l2) < 0)
	{
		DBG("set framebuffer failed\n");
		return -1;
	} 

#ifdef DEBUG
	if (xioctl(ovl->fd, VIDIOC_G_FBUF, &fb_v4l2) < 0) {
		DBG("set framebuffer failed\n");
		return -1;
	} 

	DBG("\n--- frame buffer width %d, height %d, bytesperline %d ---\n", 
        fb_v4l2.fmt.width, fb_v4l2.fmt.height, fb_v4l2.fmt.bytesperline);
#endif

	return 0;
}

/* DONE */
static int 
v4l_overlay_start(ovl_t *ovl, int o)
{
    return xioctl(ovl->fd, VIDIOC_OVERLAY, &o);
}

////////////////////////////////////////////////////
/* DONE, intermal function called by sdk init routine  */
void
mxc_overlay_init()
{
	CLEAR(mxc_ovl);
	mxc_ovl.fd = -1;
	mxc_ovl.irect.left = 0;
	mxc_ovl.irect.top = 0;
	mxc_ovl.irect.width = V4L2_DEF_W;
	mxc_ovl.irect.height = V4L2_DEF_H;
	mxc_ovl.ovl = 1;
	mxc_ovl.fr = 25;
	mxc_ovl.chl = -1;
}

/* DONE */
void
WS_preview_set_input_size(int x, int y, int w, int h)
{
	mxc_ovl.irect.left = x;
	mxc_ovl.irect.top = y;
	mxc_ovl.irect.width = w;
	mxc_ovl.irect.height = h;
}

static void
v4l_ovl_close(ovl_t *ovl)
{
    close(ovl->fd);
	ovl->fd = -1;
}

/* DONE */
int 
WS_preview_start(int left, int top, int w, int h)
{
	ovl_t *ovl = &mxc_ovl;
	char  dev_name[32];

    if (ovl->fd >= 0)
    {
    	v4l_overlay_start(ovl, 0);
        v4l_ovl_close(ovl);
    }

    memset(dev_name, 0, 32);
    sprintf(dev_name, "/dev/v4l/video%d", g_camera_dev);

    if ((ovl->fd = open_chrdev(dev_name, 0)) >= 0)
	{
		if (v4l2_set_output(ovl->fd, 0) < 0)
		{
			DBG("--- v4l2_set_output error ---\n");
			goto err_quit;
		}

		v4l2_set_rotate(ovl->fd, 0);

		if (v4l2_overlay_set_crop(ovl) < 0)
		{
			DBG("--- v4l2_overlay_set_crop error ---\n");
			goto err_quit;
		}

		if (v4l2_overlay_set_display_size(ovl, left, top, w, h))
		{
			DBG("--- v4l2_overlay_set_display_size error ---\n");
			goto err_quit;
		}

		if (v4l2_set_frame_rate(ovl->fd, ovl->fr) < 0)
		{
			DBG("--- v4l2_set_frame_rate error ---\n");
			goto err_quit;
		}

		if (v4l2_overlay_set_fb(ovl) < 0)
		{
			DBG("--- v4l2_overlay_set_fb error ---\n");
			goto err_quit;
		}

		v4l2_set_input(ovl->fd, ovl->chl);

		if (v4l_overlay_start(ovl, 1) < 0)
		{
				DBG("--- v4l_overlay_start error ---\n");
				goto err_quit;
		}
	}
	else
	{
		DBG("--- Unable to open %s for overlay ---\n", V4L2_DEV);
		return -1;
	}
	return 0;

err_quit:
	v4l_ovl_close(ovl);
    return -1;
}

/* DONE */
void
WS_preview_stop(void)
{
	ovl_t *ovl = &mxc_ovl;

	if (ovl->fd >= 0)
    {
        v4l_overlay_start(ovl, 0);
        v4l_ovl_close(ovl);
    }
}

int
WS_preview_fd(void)
{
	return mxc_ovl.fd;
}

#if 0
}
#endif


