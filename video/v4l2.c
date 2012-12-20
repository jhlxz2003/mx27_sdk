#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>	
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <asm/arch/mxcfb.h>
#include <asm/arch/mx2fb.h>
#include "vpu.h"
#include "dbg.h"

#define AUTO_STD      0
#define PAL_STD       1
#define NTSC_STD      2
#define SECAM_STD     3

#define V4L2_CID_MXC_PWRDN	    (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_DEV         "/dev/v4l/video0"

////////////////////////////////////////////////
/*
static int  g_v4lFd = -1;
static int  g_v4l_ref = 0;

static pthread_mutex_t   g_v4l_mtx = PTHREAD_MUTEX_INITIALIZER;

#define  v4l_lock()        pthread_mutex_lock(&g_v4l_mtx)
#define  v4l_unlock()      pthread_mutex_unlock(&g_v4l_mtx)
*/

int g_camera_dev = 0;

void
WS_camera_select(int i)
{
	g_camera_dev = ((i>0)?1:0);
}

#if 0
{
#endif

int
xioctl (int fd, int request, void *arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while ((-1 == r)&&(EINTR == errno));
	return r;
}

static int
v4l_get_defctl(int fd, int t)
{
	struct v4l2_queryctrl qc;
	CLEAR(qc);
	switch (t)
	{
	case 0:
		qc.id = V4L2_CID_BRIGHTNESS;
		break;
	case 1:
		qc.id = V4L2_CID_CONTRAST;
		break;
	case 2:
		qc.id = V4L2_CID_SATURATION;
		break;
	case 3:
		qc.id = V4L2_CID_HUE;
		break;
	case 4:
		qc.id = V4L2_CID_RED_BALANCE;
		break;
	case 5:
		qc.id = V4L2_CID_BLUE_BALANCE;
		break;
	/*
	case 6:  //deprecated
		qc.id = V4L2_CID_BLACK_LEVEL;
		break;
	*/
	default:
		return -1;
	}

	if (0 == xioctl(fd, VIDIOC_QUERYCTRL, &qc))
		return qc.default_value;		
	return -1;
}

static int
v4l_set_ctl(int fd, int t, int value)
{
	struct v4l2_control ctl;
	CLEAR(ctl);
	switch (t)
	{
	case 0:
		ctl.id = V4L2_CID_BRIGHTNESS;
		break;
	case 1:
		ctl.id = V4L2_CID_CONTRAST;
		break;
	case 2:
		ctl.id = V4L2_CID_SATURATION;
		break;
	case 3:
		ctl.id = V4L2_CID_HUE;
		break;
	case 4:
		ctl.id = V4L2_CID_RED_BALANCE;
		break;
	case 5:
		ctl.id = V4L2_CID_BLUE_BALANCE;
		break;
	case 6:
		ctl.id = V4L2_CID_MXC_PWRDN;
		break;
	default:
		return -1;
	}
	ctl.value = value;
	return xioctl (fd, VIDIOC_S_CTRL, &ctl);
}

static int
v4l_get_ctl(int fd, int t)
{
	struct v4l2_control ctl;
	CLEAR(ctl);
	switch (t)
	{
	case 0:
		ctl.id = V4L2_CID_BRIGHTNESS;
		break;
	case 1:
		ctl.id = V4L2_CID_CONTRAST;
		break;
	case 2:
		ctl.id = V4L2_CID_SATURATION;
		break;
	case 3:
		ctl.id = V4L2_CID_HUE;
		break;
	case 4:
		ctl.id = V4L2_CID_RED_BALANCE;
		break;
	case 5:
		ctl.id = V4L2_CID_BLUE_BALANCE;
		break;
	default:
		return -1;
	}

	if (0 == xioctl (fd, VIDIOC_G_CTRL, &ctl))
		return ctl.value;
	return -1;
}

int
open_chrdev(char *name, int nb)
{
	int  flags = O_RDWR;
	struct stat st;
	if (-1 == stat(name, &st))
	{
		DBG ("--- Cannot identify %s: %d, %s ---\n", name, errno, strerror (errno));
		return -1;
	}

	if (!S_ISCHR (st.st_mode))
	{
		DBG ("--- %s is no device ---\n", name);
		return -1;
	}

	if (nb == 1)
		flags |= O_NONBLOCK;
	return open(name, flags, 0);
}

int
v4l2_set_input(int fd, int chl)
{
	if (chl >= 0)
	{
		DBG("change to channel %d\n", chl);
		return xioctl(fd, VIDIOC_S_INPUT, &chl);
	}
	return 0;
}

int
v4l2_set_output(int fd, int o)
{
	if (o >= 0)
		return xioctl(fd, VIDIOC_S_OUTPUT, &o);
	return 0;
}

int
v4l2_set_rotate(int fd, int r)
{
	struct v4l2_control ctl;
	if (r > 0)
	{
		CLEAR(ctl);
		ctl.id = V4L2_CID_PRIVATE_BASE;
		ctl.value = r;
		return xioctl(fd, VIDIOC_S_CTRL, &ctl);
	}
	return 0;
}

int
v4l_set_std(int fd, int r)
{
	v4l2_std_id  id;

    switch (r)
    {
    case AUTO_STD:
    	id = V4L2_STD_ALL;
    	break;
    case PAL_STD:
    	id = V4L2_STD_PAL;
    	break;
    case NTSC_STD:
    	id = V4L2_STD_NTSC;
    	break;
    case SECAM_STD:
    	id = V4L2_STD_SECAM;
    	break;
    default:
    	return -1;
    }
    return xioctl(fd, VIDIOC_S_STD, &id);
}

///////////////////////////////////////////////////////
/* for both capture and output devices. */
int
v4l2_set_fmt(int fd, int w, int h, int io)
{
	struct v4l2_format fmt;
	CLEAR(fmt);
	if (io == 0)
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else
		fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.bytesperline = w;
	return xioctl(fd, VIDIOC_S_FMT, &fmt);
}

int
v4l2_get_fmt(int fd, struct v4l2_format *fmt, int io)
{
    if (io == 0)
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else
		fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (-1 == xioctl(fd, VIDIOC_G_FMT, fmt))
	{
		DBG("--- VIDIOC_G_FMT error ---\n");
		return -1;
	}

	DBG("\t--- Width = %d ---\n", fmt->fmt.pix.width);
	DBG("\t--- Height = %d ---\n", fmt->fmt.pix.height);
	DBG("\t--- Bytesperline = %d ---\n", fmt->fmt.pix.bytesperline);
	DBG("\t--- Image size = %d ---\n", fmt->fmt.pix.sizeimage);
	return 0;
}

/* only for capture device */
int
v4l2_set_frame_rate(int fd, int fps)
{
	struct v4l2_streamparm parm;
	CLEAR(parm);
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = fps;
	parm.parm.capture.capturemode = 0;

	if (xioctl(fd, VIDIOC_S_PARM, &parm) != 0)
	{
	    DBG("--- VIDIOC_S_PARM error ---\n");
	    return -1;
	}

	parm.parm.capture.timeperframe.numerator = 0;
	parm.parm.capture.timeperframe.denominator = 0;
	if (ioctl(fd, VIDIOC_G_PARM, &parm) < 0) 
	{
	    printf("--- VIDIOC_G_PARM error ---\n");
	    return -1;
	}
	DBG("--- frame_rate is %d ---\n", parm.parm.capture.timeperframe.denominator);
	return 0;
}

/*
int
v4l2_set_defcrop(int fd)
{
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	
	CLEAR (cropcap);
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
		DBG("cropcap.bounds.width = %d\ncropcap.bound.height = %d\ncropcap.defrect.width = %d\ncropcap.defrect.height = %d\n",
			cropcap.bounds.width, cropcap.bounds.height,
			cropcap.defrect.width, cropcap.defrect.height);
		CLEAR (crop);
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;
		return xioctl(fd, VIDIOC_S_CROP, &crop);
	}
	return -1;
}
*/
	
//////////////////////////////////////////////////////////////
//                 set capture video params                 //
//////////////////////////////////////////////////////////////
int
v4l_set_bright(int fd, int bri)
{
	if (bri >= 0) {
		return v4l_set_ctl(fd, 0, bri);
	}
	return 0;
}

int
v4l_set_contrast(int fd, int con)
{
	if (con >= 0) {
		return v4l_set_ctl(fd, 1, con);
	}
	return 0;
}

int
v4l_set_saturation(int fd, int sat)
{
	if (sat >= 0) {
		return v4l_set_ctl(fd, 2, sat);
	}
	return 0;
}

int
v4l_set_hue(int fd, int hue)
{
	if (hue >= 0) {
		return v4l_set_ctl(fd, 3, hue);
	}
	return 0;
}

int
v4l_set_red_balance(int fd, int value)
{
	if (value >= 0)
	{
	    return v4l_set_ctl(fd, 4, value);
	}
	return 0;
}

int
v4l_set_blue_balance(int fd, int value)
{
	if (value >= 0)
	{
	    return v4l_set_ctl(fd, 5, value);
	}
	return 0;
}

int
v4l_pwr_on(int fd, int v)
{
	if (v >= 0)
	{
	    return v4l_set_ctl(fd, 6, v);
	}
	return 0;
}


int
v4l_get_bright(int fd)
{
	return v4l_get_ctl(fd, 0);
}

int
v4l_get_contrast(int fd)
{
	return v4l_get_ctl(fd, 1);
}

int
v4l_get_saturation(int fd)
{
	return v4l_get_ctl(fd, 2);
}

int
v4l_get_hue(int fd, int hue)
{
	return v4l_get_ctl(fd, 3);
}

int
v4l_get_red_balance(int fd)
{
	return v4l_get_ctl(fd, 4);
}

int
v4l_get_blue_balance(int fd)
{
	return v4l_get_ctl(fd, 5);
}

#if 0
}
#endif

#ifdef __cplusplus
}
#endif

