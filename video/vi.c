#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ulib.h"
#include "vpu.h"
#include "v4l.h"
#include "dbg.h"
#include "common.h"
#include "../voip/server.h"
#include "../util/thread.h"

static mxc_enc_t  g_vpu_enc;

static int  g_sz_width[] = {176, 320, 352, 640, 704};
static int  g_sz_height[] = {144, 240, 288, 480, 576};

#if 0
{
#endif

#define vpu_enc_free(enc)   \
	do { \
	IOFreeVirtMem(&enc->mem_desc); \
	IOFreePhyMem(&enc->mem_desc); \
	} while (0)

static void
vpu_enc_free_framebuf(mxc_enc_t *enc)
{
	int i;

	for (i = 0; i < enc->fbcount; i++)
	{
		vpu_framebuf_free(enc->pfbpool[i]);
	}
	enc->fbcount = 0;
}

static void 
vpu_enc_close(mxc_enc_t *enc)
{
	EncOutputInfo outinfo = {0};
	if (vpu_EncClose(enc->handle) == RETCODE_FRAME_NOT_COMPLETE)
	{
		vpu_EncGetOutputInfo(enc->handle, &outinfo);
		vpu_EncClose(enc->handle);
	}
}

static int
vpu_enc_malloc(mxc_enc_t *enc)
{
    enc->mem_desc.size = STREAM_BUF_SIZE;
    if (IOGetPhyMem(&enc->mem_desc))
    {
        DBG("--- Unable to obtain physical memory ---\n");
        return -1;
    }

    enc->virt_bsbuf_addr = IOGetVirtMem(&enc->mem_desc);
    if (enc->virt_bsbuf_addr <= 0)
    {
        DBG("--- Unable to map physical memory ---\n");
        IOFreePhyMem(&enc->mem_desc);
        return -1;
    }

    enc->phy_bsbuf_addr = enc->mem_desc.phy_addr;
    return 0;
}

static int 
vpu_enc_open(mxc_enc_t *enc)
{
    EncHandle handle = {0};
    RetCode ret;

    memset(&enc->encop, 0, sizeof(EncOpenParam));
    enc->encop.bitstreamBufferSize = STREAM_BUF_SIZE;
    enc->encop.bitstreamBuffer = enc->phy_bsbuf_addr;
    enc->encop.bitstreamFormat = STD_AVC;

	if (enc->rot_angle == 90 || enc->rot_angle == 270)
	{
		enc->encop.picWidth  = enc->picheight;
		enc->encop.picHeight = enc->picwidth;
	}
	else
	{
		enc->encop.picWidth  = enc->picwidth;
		enc->encop.picHeight = enc->picheight;
	}

    enc->encop.frameRateInfo = enc->fps;
    enc->encop.bitRate = enc->bitrate;
    enc->encop.gopSize = enc->gop;
    enc->encop.slicemode.sliceMode = 1;
    enc->encop.slicemode.sliceSizeMode = 0;
    enc->encop.slicemode.sliceSize = 4000;
    enc->encop.rcIntraQp = -1;

    enc->encop.EncStdParam.avcParam.avc_fmoEnable = 0;
    enc->encop.EncStdParam.avcParam.avc_fmoType = 0;
    enc->encop.EncStdParam.avcParam.avc_fmoSliceNum = 0;

    ret = vpu_EncOpen(&handle, &enc->encop);
    if (ret != RETCODE_SUCCESS)
    {
        DBG("--- Encoder open failed %d ---\n", ret);
        vpu_enc_free(enc);
        return -1;
    }

    enc->handle = handle;
    return 0;
}

static int
vpu_encode_config(mxc_enc_t *enc)
{
    EncHandle handle = enc->handle;
    SearchRamParam search_pa = {0};
    EncInitialInfo initinfo = {0};
    RetCode ret;

    search_pa.searchRamAddr = 0xFFFF4C00;
    ret = vpu_EncGiveCommand(handle, ENC_SET_SEARCHRAM_PARAM, &search_pa);
    if (ret != RETCODE_SUCCESS)
	{
		DBG("--- Encoder SET_SEARCHRAM_PARAM failed ---\n");
        vpu_enc_close(enc);
        vpu_enc_free(enc);
		return -1;
	}

    if (enc->rot_angle != 0)
    {
        vpu_EncGiveCommand(handle, ENABLE_ROTATION, 0);
        vpu_EncGiveCommand(handle, ENABLE_MIRRORING, 0);
        vpu_EncGiveCommand(handle, SET_ROTATION_ANGLE, &enc->rot_angle);
        vpu_EncGiveCommand(handle, SET_MIRROR_DIRECTION, &enc->mirror);
    }

    ret = vpu_EncGetInitialInfo(handle, &initinfo);
    if (ret != RETCODE_SUCCESS)
    {
        DBG("--- Encoder GetInitialInfo failed ---\n");
        vpu_enc_close(enc);
		vpu_enc_free(enc);
        return -1;
    }

	enc->fbcount = enc->src_fbid = initinfo.minFrameBufferCount;
	return 0;
}

static void
vpu_enc_free_resource(mxc_enc_t *enc)
{
	vpu_enc_free_framebuf(enc);
	vpu_enc_close(enc);
	vpu_enc_free(enc);
}

static int
vpu_enc_alloc_framebuf(mxc_enc_t *enc)
{
	int i;
	int stride;
	RetCode ret;

	for (i = 0; i < enc->fbcount; i++)
	{
		enc->pfbpool[i] = vpu_framebuf_alloc(enc->picwidth, enc->picheight);
		if (enc->pfbpool[i] == NULL)
		{
			enc->fbcount = i;
			goto err1;
		}

		enc->fb[i].bufY  = enc->pfbpool[i]->addrY;
		enc->fb[i].bufCb = enc->pfbpool[i]->addrCb;
		enc->fb[i].bufCr = enc->pfbpool[i]->addrCr;
	}

	/* Must be a multiple of 16 */
	stride = (enc->picwidth + 15) & ~15;
	ret = vpu_EncRegisterFrameBuffer(enc->handle, enc->fb, enc->fbcount, stride);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("--- Register frame buffer failed ---\n");
		goto err1;
	}

	if (v4l_capture_setup(enc) < 0)
	{
	    DBG("--- stage:0 ---\n");
		goto err1;
	}
	return 0;

err1:
	vpu_enc_free_resource(enc);
	return -1;
}

static int
vpu_encode_fill_headers(mxc_enc_t *enc)
{
	EncHeaderParam enchdr_param = {0};
	EncHandle handle = enc->handle;
	u32 vbuf;
	RetCode ret;
	u32 phy_bsbuf  = enc->phy_bsbuf_addr;
	u32 virt_bsbuf = enc->virt_bsbuf_addr;

	/* Must put encode header before encoding */
	enchdr_param.headerType = SPS_RBSP;
	ret = vpu_EncGiveCommand(handle, ENC_PUT_AVC_HEADER, &enchdr_param);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("--- put SPS_RBSP header failed ---\n");
		vpu_enc_free_resource(enc);
		return -1;
	}
	DBG("--- SPS_RBSP SIZE: %d ---\n", enchdr_param.size);

	enc->sps_sz = enchdr_param.size;
	vbuf = (virt_bsbuf + enchdr_param.buf - phy_bsbuf);
	memset(enc->sps_hdr, 0, enchdr_param.size);
	memcpy(enc->sps_hdr, (void*)vbuf, enchdr_param.size);

	enchdr_param.headerType = PPS_RBSP;
	ret = vpu_EncGiveCommand(handle, ENC_PUT_AVC_HEADER, &enchdr_param);
	if (ret != RETCODE_SUCCESS)
	{
		DBG("--- put PPS_RBSP header failed ---\n");
		vpu_enc_free_resource(enc);
		return -1;
	}

	DBG("--- PPS_RBSP SIZE: %d ---\n", enchdr_param.size);

	enc->pps_sz = enchdr_param.size;
	vbuf = (virt_bsbuf + enchdr_param.buf - phy_bsbuf);
	memset(enc->pps_hdr, 0, enchdr_param.size);
	memcpy(enc->pps_hdr, (void*)vbuf, enchdr_param.size);

	return 0;
}

static int
vpu_encode_start(mxc_enc_t *enc)
{
	int src_fbid = enc->src_fbid;

	enc->encParam.sourceFrame = &enc->fb[src_fbid];
	enc->encParam.quantParam = 30;
	enc->encParam.forceIPicture = 0;
	enc->encParam.skipPicture = 0;

	if (v4l_start_capturing(enc) < 0)
	{
		vpu_enc_free_resource(enc);
		return -1;
	}

	enc->img_size = enc->picwidth * enc->picheight;
	return 0;
}

static int
vpu_enc_prepare(mxc_enc_t *enc)
{
    if (VPU_init())
    {
        DBG("--- VPU_init() failed ---\n");
        return -1;
    }

    DBG("--- VPU_init() success ---\n");
    if (vpu_enc_malloc(enc) < 0)
    {
    	DBG("--- vpu_enc_malloc() failed ---\n");
        goto err;
    }
    
    DBG("--- vpu_enc_malloc() success ---\n");
    if (vpu_enc_open(enc) < 0)
    {
    	DBG("--- vpu_enc_open() failed ---\n");
        goto err;
    }
    
    DBG("--- vpu_enc_open() success ---\n");

    if (vpu_encode_config(enc) < 0)
    {
    	DBG("--- vpu_encode_config() failed ---\n");
        goto err;
    }
    DBG("--- vpu_encode_config() success ---\n");

    if (vpu_enc_alloc_framebuf(enc) < 0)
    {
        DBG("--- vpu_enc_alloc_framebuf failed ---\n");
        goto err;
    }
    DBG("--- vpu_enc_alloc_framebuf() success ---\n");

    if (vpu_encode_fill_headers(enc) < 0)
    {
    	DBG("--- vpu_encode_fill_headers failed ---\n");
        goto err;
    }
    DBG("--- vpu_encode_fill_headers() success ---\n");

    if (vpu_encode_start(enc) < 0)
    {
    	DBG("--- vpu_encode_start failed ---\n");
        goto err;
    }
    DBG("--- vpu_encode_start() success ---\n");
    enc->frames = 0;
    return 0;
err:
    VPU_uninit();
    return -1;
}

static int
vpu_encoding(mxc_enc_t *enc, char **ptr, int *size, int *frame_type)
{
	struct v4l2_buffer v4l2_buf;
	EncOutputInfo outinfo = {0};
	RetCode ret;
	int src_fbid = enc->src_fbid;
	int img_size = enc->img_size;
	FrameBuffer *fb = enc->fb;
	thread_t  *thr = enc->thread;

	if (v4l_get_capture_data(enc, &v4l2_buf) < 0) {
		DBG("--- v4l_get_capture_data failed ---\n");
		return -1;
	}

	if (thr->thr_q == 1) {
		DBG("--- received quit cmd ---\n");
		return 1;
	}

	fb[src_fbid].bufY  = enc->capBuf[v4l2_buf.index].offset;
	fb[src_fbid].bufCb = fb[src_fbid].bufY + img_size;
	fb[src_fbid].bufCr = fb[src_fbid].bufCb + (img_size >> 2);

	ret = vpu_EncStartOneFrame(enc->handle, &enc->encParam);
	if (ret != RETCODE_SUCCESS) {
		DBG("--- EncStartOneFrame failed ---\n");
		return -1;
	}

	while (vpu_IsBusy()) {
		vpu_WaitForInt(200);
	}

	ret = vpu_EncGetOutputInfo(enc->handle, &outinfo);
	if (enc->encParam.forceIPicture == 1) {
		enc->encParam.forceIPicture = 0;
	}

	if (ret != RETCODE_SUCCESS) {
		DBG("--- EncGetOutputInfo failed ---\n");
		return -1;
	}

	v4l_put_capture_data(enc, &v4l2_buf);

	if (thr->thr_q == 1) {
		DBG("--- received quit cmd ---\n");
		return 1;
	}

	*ptr = (char*)(enc->virt_bsbuf_addr + outinfo.bitstreamBuffer - enc->phy_bsbuf_addr);
	*size = outinfo.bitstreamSize;
	*frame_type = 2 + outinfo.picType;
	enc->frames++;

	return 0;
}

#if 0
}
#endif

//////////////////////////////////////////////

static void
vpu_encode_stop(mxc_enc_t *enc)
{
	v4l_stop_capturing(enc);
	DBG("--- v4l_stop_capturing success ---\n");
	vpu_enc_free_framebuf(enc);
	DBG("--- vpu_enc_free_framebuf success ---\n");
	vpu_enc_close(enc);
	DBG("--- vpu_enc_close success ---\n");
	vpu_enc_free(enc);
	DBG("--- vpu_enc_free success ---\n");
	VPU_uninit();
}

///////////////////////////////////////////////
//              encoder thread               //
///////////////////////////////////////////////
#if 0
{
#endif

void
vpu_set_picfmt(picfmt_t fmt)
{
	mxc_enc_t  *enc = &g_vpu_enc;

	if (fmt <= ENC_4CIF&&fmt >= ENC_QCIF)
	{
	    enc->picwidth = g_sz_width[fmt];
	    enc->picheight = g_sz_height[fmt];
    }
}

static thread_t   ve_thr;

static void
ve_main(void *arg)
{
	thread_t  *thr = arg;
	char *ptr;
	int   size;
	int   frame_type;
	char  buf[128];
#ifdef DEBUG
	char *ft[] = {"I frame", "P frame", "B frame"};
#endif
	mxc_enc_t *ve;

	ve = thread_priv_data(thr);

	if (vpu_enc_prepare(ve) < 0) {
		DBG("--- vpu_enc_prepare() failed ---\n");
		thread_run_quit(thr);
		return;
	}

	DBG("--- video encoding started-0 ---\n");
	thread_run_prepared(thr);
		
	memset(buf, 0, 128);
	memcpy(buf, ve->sps_hdr, ve->sps_sz);
	memcpy(buf + ve->sps_sz, ve->pps_hdr, ve->pps_sz);
	ev_packet_new(buf, ve->sps_sz+ve->pps_sz);

	THREAD_LOOP(thr)
	{
		if (vpu_encoding(ve, &ptr, &size, &frame_type) != 0) break;
//		if (frame_type == 2) {
//			DBG("--- 0. VIDEO ENCODER: video frame_type = %s, size = %d ---\n", ft[frame_type-2], size);
//		}
		ev_packet_new(ptr, size);
	}

	vpu_encode_stop(ve);
	thread_run_quit(thr);
	DBG("--- video encoding stopped ---\n");
}

int
start_video_encoder(void)
{
#ifndef DEBUG
	return run_thread(&ve_thr);
#else
	int  ret;
	ret = run_thread(&ve_thr);
	if (ret == 0) {
		DBG("--- VIDEO ENCODING HAS STARTED ---\n");
	} else {
		DBG("--- VIDEO ENCODING START SUCCESS ---\n");
	}
	return ret;
#endif
}

void
stop_video_encoder(void)
{
	DBG("--- TRYING TO STOP VIDEO ENCODING ---\n");
	suspend_thread(&ve_thr);
	DBG("--- VIDEO ENCODING STOPPED ---\n");
}

/* Internal function: called by sdk init routine, DONE */
int
mxc_vpu_encoder_init(void)
{
	mxc_enc_t *ve = &g_vpu_enc;

	memset(ve, 0, sizeof(mxc_enc_t));
	ve->picwidth = 320;
	ve->picheight = 240;
	ve->fps = 30;
	ve->thread = &ve_thr;

	return new_thread(&ve_thr, ve_main, &g_vpu_enc, NULL, NULL);
}


