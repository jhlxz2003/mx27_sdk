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
#include <linux/delay.h>
#include <sys/mman.h>
#include <asm/arch/mxcfb.h>
#include <asm/arch/mx2fb.h>
#include <pthread.h>
#include "v4l.h"
#include "vpu.h"
#include "dbg.h"

extern int g_camera_dev;

////////////////////////////////////////////////
//          called by encode routine          //      
////////////////////////////////////////////////

#if 0
{
#endif

/*
static void
v4l_unreq(mxc_enc_t *enc)
{
	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	xioctl(enc->fd, VIDIOC_REQBUFS, &req);
}
*/

static int
v4l_mmap(mxc_enc_t *enc)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;

	enc->bufCnt = 0;
	CLEAR(req);
	req.count = CAP_BUFF_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(enc->fd, VIDIOC_REQBUFS, &req) < 0)
	{
		DBG("--- v4l_capture_setup: VIDIOC_REQBUFS failed ---\n");
		return -1;
	}

	if (req.count < 3)
	{
		DBG("--- Insufficient buffer memory on %s: %d ---\n", V4L2_DEV, req.count);
		return -1;
	}

	for (; enc->bufCnt < req.count; enc->bufCnt++)
	{
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = enc->bufCnt;
		if (xioctl(enc->fd, VIDIOC_QUERYBUF, &buf) < 0)
		{
			DBG("--- VIDIOC_QUERYBUF error ---\n");
			return -1;
		}

		enc->capBuf[enc->bufCnt].length = buf.length;
		enc->capBuf[enc->bufCnt].offset = buf.m.offset;
	}
	DBG("--- capture buffer count = %d ---\n", enc->bufCnt);
	return 0;
}

static void
v4l_close(mxc_enc_t *enc)
{
//	v4l_unreq(enc);
	close(enc->fd);
	enc->fd = -1;
}

////////////////////////////////////////////////////////////
int 
v4l_capture_setup(mxc_enc_t *enc)
{
	char dev_name[32];

	if (enc->fd >= 0)
	{
		DBG("--- capture device already opened ---\n");
		v4l_close(enc);
//		return 0;
	}

    memset(dev_name, 0, 32);
    sprintf(dev_name, "/dev/v4l/video%d", g_camera_dev);
	if ((enc->fd = open_chrdev(dev_name, 0)) < 0)
	{
		DBG("--- Unable to open camera, FUCK!!!!!!!!!!!!!!!!!!!! ---\n");
		return -1;
	}

	if (v4l2_set_fmt(enc->fd, enc->picwidth, enc->picheight, 0) < 0)
	{
		DBG("--- set format failed ---\n");
		v4l_close(enc);
		return -1;
	}

	if (enc->vpara.cfg == 1)
	{/*
		if (v4l_set_video_param(enc->fd, enc->vpara.bri, enc->vpara.con, enc->vpara.sat, enc->vpara.hue) < 0)
		{
			DBG("--- set video param  failed ---\n");
			v4l_close(enc);
		    return -1;
		}
	*/
		v4l_set_bright(enc->fd, enc->vpara.bri);
        v4l_set_contrast(enc->fd, enc->vpara.con);
        v4l_set_saturation(enc->fd, enc->vpara.sat);
        v4l_set_hue(enc->fd, enc->vpara.hue);
	}

	if (enc->fps <= 0||enc->fps > 30)
		enc->fps = DEF_FPS;
	if (v4l2_set_frame_rate(enc->fd, enc->fps) < 0)
	{
		DBG("--- set frame rate failed ---\n");
		v4l_close(enc);
		return -1;
	}

	if (v4l_mmap(enc) < 0)
	{
		v4l_close(enc);
		return -1;
	}
	return 0;
}

int 
v4l_start_capturing(mxc_enc_t *enc)
{
	unsigned int i;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;

	for (i = 0; i < enc->bufCnt; i++)
       {
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
//		buf.m.offset = enc->capBuf[i].offset;
		if (xioctl(enc->fd, VIDIOC_QBUF, &buf) < 0)
		{
			DBG("VIDIOC_QBUF error\n");
			v4l_close(enc);
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(enc->fd, VIDIOC_STREAMON, &type) < 0)
	{
		DBG("VIDIOC_STREAMON error\n");
		v4l_close(enc);
		return -1;
	}

	return 0;
}

int
v4l_get_capture_data(mxc_enc_t *enc, struct v4l2_buffer *buf)
{
	memset(buf, 0, sizeof(struct v4l2_buffer));
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	if (xioctl(enc->fd, VIDIOC_DQBUF, buf) < 0)
	{
		DBG("--- VIDIOC_DQBUF failed ---\n");
		xioctl(enc->fd, VIDIOC_STREAMOFF, &buf->type);
		v4l_close(enc);
		return -1;
	}

	return 0;
}

int
v4l_put_capture_data(mxc_enc_t *enc, struct v4l2_buffer *buf)
{
	if (xioctl(enc->fd, VIDIOC_QBUF, buf) < 0)
	{
		DBG("--- VIDIOC_QBUF failed ---\n");
		xioctl(enc->fd, VIDIOC_STREAMOFF, &buf->type);
		v4l_close(enc);
		return -1;
	}
	return 0;
}

void 
v4l_stop_capturing(mxc_enc_t *enc)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(enc->fd, VIDIOC_STREAMOFF, &type);
	v4l_close(enc);
}

#if 0
}
#endif

#ifdef __cplusplus
}
#endif

