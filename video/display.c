#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "v4l.h"
#include "dbg.h"
#include "vpu.h"

static vpu_v4lsink_t  g_v4lsink;

static int
_v4lsink_set_output_size(vpu_v4lsink_t* snk, disp_para_t *para)
{
    struct v4l2_crop crop = {0};

    if (para)
    {
	    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	    crop.c.left = para->dispX;
	    crop.c.top = para->dispY;
	    if (para->dispW > 0&&para->dispH > 0)
	    {
	        crop.c.width = para->dispW;
	        crop.c.height = para->dispH;
	    }
	    else
	    {
	        crop.c.width = snk->inWidth;
	        crop.c.height = snk->inHeight;
	    }
	}
	else /* default displaying position and size. */
	{
	    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	    crop.c.left = (800-snk->inWidth)/2;
	    crop.c.top = (480-snk->inHeight)/2;
	    crop.c.width = snk->inWidth;
	    crop.c.height = snk->inHeight;
	}

	if (xioctl(snk->fd, VIDIOC_S_CROP, &crop) < 0)
	{
		DBG("VIDIOC_S_CROP failed\n");
		return -1;
	}
	return 0;
}

static int
v4lsink_set_output_size(vpu_v4lsink_t* snk)
{
    disp_para_t *para;
    disp_para_t  dispPar;
    if (snk->fullscreen)
    {
        dispPar.dispX = 0;
        dispPar.dispY = 0;
        dispPar.dispW = MXC_FB_W;
        dispPar.dispH = MXC_FB_H;
        para = &dispPar;
        DBG("--- full screen mode ---\n");
    }
    else
    {
        para = snk->dispPara;
        DBG("--- not full screen mode ---\n");
    }
    return _v4lsink_set_output_size(snk, para);
}

static int
v4lsink_set_fb(vpu_v4lsink_t* snk)
{
	struct v4l2_framebuffer fb;

	CLEAR(fb);
	if (xioctl(snk->fd, VIDIOC_G_FBUF, &fb) < 0)
	{
		DBG("get framebuffer failed\n");
		return -1;
	} 

    fb.capability = V4L2_FBUF_CAP_EXTERNOVERLAY;
	fb.flags = V4L2_FBUF_FLAG_OVERLAY;

	if (xioctl(snk->fd, VIDIOC_S_FBUF, &fb) < 0)
	{
		DBG("set framebuffer failed\n");
		return -1;
	}

	return 0;
}

static int
v4lsink_set_input_size(vpu_v4lsink_t* snk)
{
    struct v4l2_format fmt;
    if (v4l2_set_fmt(snk->fd, snk->inWidth, snk->inHeight, 1) < 0)
    {
        DBG("--- VIDIOC_S_FMT failed ---\n");
        return -1;
    }
    return v4l2_get_fmt(snk->fd, &fmt, 1);
}

static int
v4lsink_req_buf(vpu_v4lsink_t *snk, int nframes)
{
    struct v4l2_requestbuffers reqbuf;
    int i;

    if (nframes > V4LSINK_BUF_MAX)
        nframes = V4LSINK_BUF_MAX;
    CLEAR(reqbuf);
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.count = nframes;
	if (xioctl(snk->fd, VIDIOC_REQBUFS, &reqbuf) < 0)
	{
		DBG("VIDIOC_REQBUFS failed\n");
		return -1;
	}

	if (reqbuf.count < nframes)
	{
		DBG("VIDIOC_REQBUFS: not enough buffers\n");
		return -1;
	}

	for (i = 0; i < nframes; i++)
	{
		struct v4l2_buffer buffer = {0};

		buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = i;
		if (xioctl(snk->fd, VIDIOC_QUERYBUF, &buffer) < 0)
		{
			DBG("VIDIOC_QUERYBUF: not enough buffers\n");
			return -1;
		}

		memset(&snk->buffers[i], 0, sizeof(struct v4l_buf));
		snk->buffers[i].offset = buffer.m.offset;
		snk->buffers[i].length = buffer.length;
		DBG("--- VIDIOC_QUERYBUF: buffer length = %d ---\n", buffer.length);
	}
	snk->bufCnt = nframes;
	return 0;
}

int
v4lsink_open(vpu_v4lsink_t* snk, int nframes)
{
	int fd;

	fd = open( "/dev/v4l/video16", O_RDWR, 0);
	if (fd < 0)
    {
		DBG("unable to open /dev/v4l/video16\n");
		return -1;
	}

    snk->fd = fd;
	if (v4lsink_set_output_size(snk) < 0)
	{
	    goto err_quit;
	}

    if (snk->dispPara)
    {
	    v4l2_set_rotate(snk->fd, snk->dispPara->rotate);
    }

	if (v4lsink_set_fb(snk) < 0)
	{
	    DBG("--- v4lsink_set_fb failed ---\n");
	    goto err_quit;
	}

    if (v4lsink_set_input_size(snk) < 0)
    {
        DBG("--- v4lsink_set_input_size failed ---\n");
        goto err_quit;
    }

	if (v4lsink_req_buf(snk, nframes) < 0)
	{
	    DBG("--- v4lsink_req_buf failed ---\n");
        goto err_quit;
    }
    snk->ncount = 0;
	return 0;

err_quit:
    close(snk->fd);
    snk->fd = -1;
    return -1;
}

void
v4lsink_close(vpu_v4lsink_t* snk)
{
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	xioctl(snk->fd, VIDIOC_STREAMOFF, &type);
	close(snk->fd);
	snk->fd = -1;
}

int
v4lsink_put_data(vpu_v4lsink_t* snk)
{
	int type;

	snk->buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	snk->buf.memory = V4L2_MEMORY_MMAP;
	if (snk->ncount < snk->bufCnt)
	{
		snk->buf.index = snk->ncount;
		if (xioctl(snk->fd, VIDIOC_QUERYBUF, &snk->buf) < 0)
		{
//			DBG("VIDIOC_QUERYBUF failed\n");
			return -1;
		}
	}
	else
	{
		if (xioctl(snk->fd, VIDIOC_DQBUF, &snk->buf) < 0)
		{
//			DBG("VIDIOC_DQBUF failed\n");
			return -1;
		}
	}

    if (snk->ncount == 0)
	{
	    struct timeval tv;
		gettimeofday(&tv, 0);
		snk->buf.timestamp = tv;
		snk->tv = tv;
	}
	else
	{
		snk->tv.tv_usec += snk->usec;
		if (snk->tv.tv_usec >= 1000000)
		{
			snk->tv.tv_sec += 1;
			snk->tv.tv_usec -= 1000000;
		}
		snk->buf.timestamp = snk->tv;
	}

	if (xioctl(snk->fd, VIDIOC_QBUF, &snk->buf) < 0)
	{
//		DBG("VIDIOC_QBUF failed\n");
		return -1;
	}

	if (snk->ncount == 1)
	{
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (xioctl(snk->fd, VIDIOC_STREAMON, &type) < 0)
		{
//			DBG("VIDIOC_STREAMON failed\n");
			return -1;
		}
	}

	snk->ncount++;
	return 0;
}

/* called by decoder init function */
vpu_v4lsink_t*
v4lsink_get()
{
    return &g_v4lsink;
}

/* called by decode_parse function */
void
v4lsink_set_input_para(vpu_v4lsink_t* snk, int w, int h, int fps)
{
    int  fr;
    snk->inWidth = w;
    snk->inHeight = h;
    if (fps > 30||fps <= 0)
    {
        fps = DEF_FPS;
    }
    DBG("--- frame rate = %d ---\n", fps);
    if (fps >= 25)
    {
        fr = fps - 2;
    }
    else if (fps >= 20)
    {
        fr = fps - 1;
    }
    else
    {
        fr = fps;
    }        
    snk->usec = (unsigned long)(1000000/fr);
}

/* called by start decode function */
void
v4lsink_set_disp_para(vpu_v4lsink_t *snk, disp_para_t *par)
{
    snk->dispPara = par;
}

int
v4lsink_reset(vpu_v4lsink_t *snk)
{
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (xioctl(snk->fd, VIDIOC_STREAMOFF, &type) < 0)
	{
	    DBG("--- VIDIOC_STREAMOFF error ---\n");
	    return -1;
	}

	if (v4lsink_set_output_size(snk) < 0)
	{
	    DBG("--- v4lsink_set_output_size error ---\n");
	    return -1;
	}
	snk->ncount = 0;
	return 0;
}

