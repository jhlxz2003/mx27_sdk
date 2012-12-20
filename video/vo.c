#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vpu.h"
#include "dbg.h"
#include "common.h"

static  mxc_dec_t  g_vpu_dec;

#if 0
{
#endif

static int
vpu_decode_open(mxc_dec_t *dec)
{
	RetCode ret;
	DecHandle handle = {0};
	DecOpenParam oparam = {0};
	
	dec->mem_desc.size = STREAM_BUF_SIZE;
	if (IOGetPhyMem(&dec->mem_desc) < 0)
	{
		DBG("Unable to obtain physical mem\n");
		return -1;
	}

	if (IOGetVirtMem(&dec->mem_desc) <= 0)
	{
		DBG("Unable to obtain virtual mem\n");
		IOFreePhyMem(&dec->mem_desc);
		return -1;
	}

	dec->phy_bsbuf_addr = dec->mem_desc.phy_addr;
	dec->virt_bsbuf_addr = dec->mem_desc.virt_uaddr;
	
	oparam.bitstreamFormat = STD_AVC;
	oparam.bitstreamBuffer = dec->phy_bsbuf_addr;
	oparam.bitstreamBufferSize = STREAM_BUF_SIZE;
	oparam.mp4DeblkEnable = 0;
	oparam.reorderEnable = 0;

	ret = vpu_DecOpen(&handle, &oparam);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("vpu_DecOpen failed\n");
		IOFreeVirtMem(&dec->mem_desc);
		IOFreePhyMem(&dec->mem_desc);
		return -1;
	}

	dec->handle = handle;
	return 0;
}

static inline int
vpu_dec_fill_bsbuffer(mxc_dec_t *dec, char *buf, int size)
{
	DecHandle handle = dec->handle;
	u32 bs_va_startaddr = dec->virt_bsbuf_addr;
	u32 bs_va_endaddr = bs_va_startaddr + STREAM_BUF_SIZE;
	u32 bs_pa_startaddr = dec->phy_bsbuf_addr;
	PhysicalAddress pa_read_ptr, pa_write_ptr;
	u32 target_addr, space;
	RetCode ret;
	int to_end;

	if (size <= 0) return 0;
	ret = vpu_DecGetBitstreamBuffer(handle, &pa_read_ptr, &pa_write_ptr, &space);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("vpu_DecGetBitstreamBuffer failed\n");
		return -1;
	}

	if (space < size) return 0;

	target_addr = bs_va_startaddr + (pa_write_ptr - bs_pa_startaddr);
	if ((target_addr + size) > bs_va_endaddr)
	{
		to_end = bs_va_endaddr - target_addr;
		memcpy((u8 *)target_addr, buf, to_end);
		memcpy((u8 *)bs_va_startaddr, (buf + to_end), (size - to_end));
	}
	else
	{
		memcpy((u8 *)target_addr, buf, size);
	}

	ret = vpu_DecUpdateBitstreamBuffer(handle, size);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("vpu_DecUpdateBitstreamBuffer failed\n");
		return -1;
	}
	
	return size;
}

/* DONE, called after HeaderInfo served. */
static int
vpu_decode_parse(mxc_dec_t *dec)
{
	DecInitialInfo initinfo = {0};
	DecHandle handle = dec->handle;
	RetCode ret;

	vpu_DecSetEscSeqInit(handle, 1);
	ret = vpu_DecGetInitialInfo(handle, &initinfo);	
	vpu_DecSetEscSeqInit(handle, 0);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("vpu_DecGetInitialInfo failed %d\n", ret);
		return -1;
	}

	DBG("Decoder: Width = %d, Height = %d, Fps = %lu, BufferCount = %u\n",
			initinfo.picWidth, initinfo.picHeight,
			initinfo.frameRateInfo,
			initinfo.minFrameBufferCount);

	dec->fbcount = initinfo.minFrameBufferCount;
	dec->picwidth = initinfo.picWidth;
	dec->picheight = initinfo.picHeight;
	dec->picwidth = ((dec->picwidth + 15) & ~15);
	dec->picheight = ((dec->picheight + 15) & ~15);
	if ((dec->picwidth == 0) || (dec->picheight == 0))
		return -1;
	
	dec->img_size = dec->picwidth * dec->picheight;
	if (initinfo.frameRateInfo > 0)
	{
	    dec->fps = initinfo.frameRateInfo;
	}
	v4lsink_set_input_para(dec->sink, dec->picwidth, dec->picheight, dec->fps);
	return 0;
}

static int
vpu_decode_alloc_framebuffer(mxc_dec_t *dec)
{
	int i;
	DecHandle handle = dec->handle;
	int sz, sz1;
	vpu_v4lsink_t *snk = dec->sink;

    if (v4lsink_open(snk, dec->fbcount) < 0)
    {
        DBG("--- v4lsink_open error ---\n");
        return -1;
    }
    sz = dec->img_size;
    sz1 = sz >> 2;
	for (i = 0; i < dec->fbcount; ++i)
    {
		dec->fb[i].bufY = snk->buffers[i].offset;
		dec->fb[i].bufCb = dec->fb[i].bufY + sz;
		dec->fb[i].bufCr = dec->fb[i].bufCb + sz1;
	}

	if (vpu_DecRegisterFrameBuffer(handle, dec->fb, dec->fbcount, dec->picwidth) != RETCODE_SUCCESS)
	{
		DBG("Register frame buffer failed\n");
		v4lsink_close(dec->sink);
	    return -1;
	}
	return 0;
}

static void
vpu_decode_close(mxc_dec_t *dec)
{
	vpu_DecClose(dec->handle);
	IOFreeVirtMem(&dec->mem_desc);
	IOFreePhyMem(&dec->mem_desc);
}

static inline void
vpu_decode_stop(mxc_dec_t *dec)
{
    v4lsink_close(dec->sink);
    vpu_decode_close(dec);
    VPU_uninit();
}

static inline int
vpu_dec_prepare(mxc_dec_t *dec, char *buf, int size)
{
    if (VPU_init())
    {
        return -1;
    }

    if (vpu_decode_open(dec) < 0)
    {
        VPU_uninit();
        return -1;
    }

    if (vpu_dec_fill_bsbuffer(dec, buf, size) < 0)
    {
        goto err1;
    }
    
    if (vpu_decode_parse(dec) < 0)
    {
        goto err1;
    }

    if (vpu_decode_alloc_framebuffer(dec) < 0)
    {
        goto err1;
    }
    dec->frame_id = 0;
    DBG("--- vpu_dec_prepare success ---\n");
    return 0;

err1:   
    vpu_decode_close(dec);
    VPU_uninit();
    return -1;
}

static int 
vpu_decode_frame(mxc_dec_t *dec)
{
	DecOutputInfo outinfo = {0};
	vpu_v4lsink_t *snk = dec->sink;
	int err;
	RetCode ret;

	dec->decparam.prescanEnable = 1;
	ret = vpu_DecStartOneFrame(dec->handle, &dec->decparam);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("--- DecStartOneFrame failed ---\n");
		return -1;
	}

	while (vpu_IsBusy())
	{
		vpu_WaitForInt(200);
	}

    ret = vpu_DecGetOutputInfo(dec->handle, &outinfo);
    if (ret == RETCODE_FAILURE)
    {
        return 0;
    }
    else if (ret != RETCODE_SUCCESS)
    {
        DBG(" ---- vpu_DecGetOutputInfo failed ----\n");
        return -1;
    }

	if (outinfo.prescanresult == 0)
	{
        if (dec->eos)
        {
            DBG(" ---- finish decoding file ----\n");
            return 1;
        }
        else
        {
            return 0;
        }
    }

    if ((outinfo.indexFrameDisplay == -1)||(outinfo.indexFrameDisplay > dec->fbcount))
        return 1;
    else if ((outinfo.indexFrameDisplay == -3)||(outinfo.indexFrameDisplay == -2))
		return 0;
    err = v4lsink_put_data(snk);
    if (err)
    {
        DBG("---- v4lsink_put_data failed -----\n");
        return -1;
    }
    return 0;
}

#if 0
}
#endif

////////////////////////////////////////////////////////////////
static disp_para_t  g_par;

/* before calling this, we should set the para appropriately. */
void
mxc_set_disp_area(int x, int y, int w, int h)
{
	mxc_dec_t *dec = &g_vpu_dec;

	g_par.dispX = x;
	g_par.dispY = y;
	g_par.dispW = w;
	g_par.dispH = h;
	v4lsink_set_disp_para(dec->sink, &g_par);
}

int
mxc_video_decode_open(void)
{
	if (VPU_init())
    {
        return -1;
    }

    if (vpu_decode_open(&g_vpu_dec) < 0)
    {
        VPU_uninit();
        return -1;
    }

    DBG("--- mxc_video_decode_open success ---\n");
    return 0;
}

int
mxc_video_decode_buffer_fill(char *buf, int len)
{
	return vpu_dec_fill_bsbuffer(&g_vpu_dec, buf, len);
}

int
mxc_video_decode_start(void)
{	
    if (vpu_decode_parse(&g_vpu_dec) < 0)
    {
        goto err1;
    }

    if (vpu_decode_alloc_framebuffer(&g_vpu_dec) < 0)
    {
        goto err1;
    }
 
    DBG("--- mxc_video_decode_start success ---\n");
    return 0;

err1:   
    vpu_decode_close(&g_vpu_dec);
    VPU_uninit();
    return -1;
}

void
mxc_video_decode_stop(void)
{
	vpu_decode_stop(&g_vpu_dec);
}

int
mxc_video_decode_frame(char *buf, int len)
{
	if (vpu_dec_fill_bsbuffer(&g_vpu_dec, buf, len) < 0) return -1;
	return vpu_decode_frame(&g_vpu_dec);
}

/* called by sdk init routine. */
int
mxc_vpu_decoder_init(void)
{
	mxc_dec_t *dec = &g_vpu_dec;

	memset(dec, 0, sizeof(mxc_dec_t));
	dec->picwidth = 320;
	dec->picheight = 240;
	dec->fps = 30;

	dec->sink = v4lsink_get();
	memset(dec->sink, 0, sizeof(vpu_v4lsink_t));

    return 0;
}

