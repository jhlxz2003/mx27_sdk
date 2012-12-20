#include <stdio.h>
#include <malloc.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>

#include <pjmedia.h>
#include <pjlib.h>
#include <pjmedia-codec.h>

#include "ulib.h"
#include "dbg.h"
#include "common.h"
#include "audio_codec.h"
#include "mxc_alsa.h"
#include "../voip/server.h"
#include "../util/thread.h"

typedef struct speech_ept
{
    pjmedia_endpt   *endpt;
    pjmedia_codec   *codec;
    pjmedia_codec_param codec_param;
    int  id;
    int  pkt_size;
} speech_ept_t;

typedef struct speech_factory {
	char *name;
	int   rate;
	int  (*codec_init)(pjmedia_endpt*);
	int  (*codec_deinit)(void);
} speech_factory_t;

static pj_caching_pool caching_pool;

static speech_factory_t  g_cf[] = {
	{"pcmu", 8000, &pjmedia_codec_g711_init, &pjmedia_codec_g711_deinit},
	{"pcma", 8000, &pjmedia_codec_g711_init, &pjmedia_codec_g711_deinit},
	{"g722", 16000, &pjmedia_codec_g722_init, &pjmedia_codec_g722_deinit},
	{"gsm", 8000, &pjmedia_codec_gsm_init, &pjmedia_codec_gsm_deinit},
	{"g7221/16000", 16000, &pjmedia_codec_g7221_init, &pjmedia_codec_g7221_deinit}
};

#define codec_id_valid(t)     ((t) >= 0&&(t) < sizeof(g_cf)/sizeof(g_cf[0]))

static pj_pool_factory *pjmem;
static pj_pool_t *cpool;
static speech_ept_t  g_ept;

///////////////////////////////////////////
static int  audio_codec_ref = 0;
static pthread_mutex_t  codec_mtx = PTHREAD_MUTEX_INITIALIZER;

#define audio_codec_lock()       pthread_mutex_lock(&codec_mtx)
#define audio_codec_unlock()     pthread_mutex_unlock(&codec_mtx)
///////////////////////////////////////////

static int   speech_codec_init(speech_ept_t *ept, int ct);
static void  speech_codec_destroy(speech_ept_t *ept);
static int   speech_encode(speech_ept_t *ept, void *in, int in_sz, void *out, int *out_sz);
static int   speech_decode(speech_ept_t *ept, void *in, int in_sz, void *out, int *out_sz);

static int   g_codec_id = 0;

/* DONE */
static int
get_codec_type(char *name)
{
	int i;
	for (i = 0; i < sizeof(g_cf)/sizeof(g_cf[0]); ++i)
	{
		if (strcasecmp(name, g_cf[i].name) == 0)
		{
			DBG("--- codec: %s ---\n", name);
			return i;
		}
	}
	return -1;
}

/////////////////////////////////////////////////////////
//               encode and decode function            //
/////////////////////////////////////////////////////////
#if 0
{
#endif

/* DONE */
static int
speech_codec_init(speech_ept_t *ept, int ct)
{
	pj_str_t codec_id;
	const pjmedia_codec_info *ci[1];
	unsigned count;
	pj_status_t  status;
	speech_factory_t *cf;
	unsigned short frame_sz;

	if (!codec_id_valid(ct)) return -1;
	ept->id = -1;
	cf = &g_cf[ct];
	codec_id = pj_str((char*)cf->name);
	status = pjmedia_endpt_create(pjmem, NULL, 0, &ept->endpt);
	if (status != PJ_SUCCESS)
		return -1;

	status = cf->codec_init(ept->endpt);
	if (status != PJ_SUCCESS)
	{
		pjmedia_endpt_destroy(ept->endpt);
		ept->endpt = NULL;
		return -1;
	}

	count = 1;
	status = pjmedia_codec_mgr_find_codecs_by_id(pjmedia_endpt_get_codec_mgr(ept->endpt),
						 &codec_id, &count, ci, NULL);
	if (status != PJ_SUCCESS)
		goto err_quit;

	status = pjmedia_codec_mgr_alloc_codec(pjmedia_endpt_get_codec_mgr(ept->endpt), ci[0], &ept->codec);
	if (status != PJ_SUCCESS)
		goto err_quit;

	status = pjmedia_codec_mgr_get_default_param(pjmedia_endpt_get_codec_mgr(ept->endpt), ci[0], &ept->codec_param);
	if (status != PJ_SUCCESS)
		goto err_quit1;

	frame_sz = (ept->codec_param.info.avg_bps*20/1000)>>3;
	status = (*ept->codec->op->init)(ept->codec, cpool);
	if (status != PJ_SUCCESS)
		goto err_quit1;

	ept->codec_param.setting.vad = 0;
	status = ept->codec->op->open(ept->codec, &ept->codec_param);
	if (status != PJ_SUCCESS)
		goto err_quit1;

	if (ct == 3)
		ept->pkt_size = 33;
	else if (ct == 4)
		ept->pkt_size = frame_sz;
	else
		ept->pkt_size = 80;

	ept->id = ct;

	return 0;

err_quit1:
	pjmedia_codec_mgr_dealloc_codec(pjmedia_endpt_get_codec_mgr(ept->endpt), ept->codec);
	ept->codec = NULL;
err_quit:
	cf->codec_deinit();
	pjmedia_endpt_destroy(ept->endpt);
	ept->endpt = NULL;

	return -1;
}

/* DONE */
static void
speech_codec_destroy(speech_ept_t *ept)
{
	speech_factory_t *cf;

	if (codec_id_valid(ept->id)&&ept->endpt)
	{
		cf = &g_cf[ept->id];
		ept->codec->op->close(ept->codec);
		pjmedia_codec_mgr_dealloc_codec(pjmedia_endpt_get_codec_mgr(ept->endpt), ept->codec);
		cf->codec_deinit();
		pjmedia_endpt_destroy(ept->endpt);
		ept->endpt = NULL;
		ept->id = -1;
	}
}

/* DONE */
static int
speech_encode(speech_ept_t *ept, void *ibuf, int isz, void *obuf, int *osz)
{
	pjmedia_codec *codec = ept->codec;
	struct pjmedia_frame in, out;

	in.type = PJMEDIA_FRAME_TYPE_AUDIO;
	in.buf  = ibuf;
	in.size = isz;
	out.buf = obuf;
	if (codec->op->encode(codec, &in, *osz, &out) != 0)
	{
	    DBG("--- encode error ---\n");
		return -1;
	}
	*osz = out.size;
//	DBG("--- raw data size = %d, encoded data size = %d ---\n", isz, out.size);

	return 0;
}

////////////////////////////////////////////////////////
static int
speech_decode(speech_ept_t *ept, void *ibuf, int isz, void *obuf, int *osz)
{
	struct pjmedia_frame in, out;
	pjmedia_codec *codec = ept->codec;
	int  space = *osz;

//	DBG("--- ept->pkt_size = %d ---\n", ept->pkt_size);
	if (isz < ept->pkt_size) {
		DBG("--- input size = %d, not enough ---\n", isz);
		return 0;
	}
	in.type = PJMEDIA_FRAME_TYPE_AUDIO;
	in.buf  = ibuf;
	in.size = ept->pkt_size;
	out.buf = obuf;
	if (codec->op->decode(codec, &in, space, &out) != 0)
	{
		DBG("--- decode error ---\n");
		return -1;
	}

	*osz = out.size;
	return ept->pkt_size;
}

#if 0
}
#endif

/////////////////////////////////////////////////////////
//     called before starting encoding or decoding     //
/////////////////////////////////////////////////////////
#if 0
{
#endif

/* DONE: called when system initialization. */
/* before calling any codec functions */
void
mxc_speech_codec_prepare(void)
{
	pj_init();
	pj_caching_pool_init(&caching_pool, &pj_pool_factory_default_policy, 0);
	pjmem = &caching_pool.factory;
	cpool = pj_pool_create(pjmem, "cpool", 1024, 1024, NULL);

	memset(&g_ept, 0, sizeof(g_ept));
	g_ept.id = -1;
}

/* DONE: called before system exit. */
void
mxc_speech_codec_finish(void)
{
	pj_pool_release(cpool);
	pj_caching_pool_destroy(&caching_pool);
	pj_shutdown();
}

static inline int
speech_codec_id_by_name(char *codec_name)
{
	int t = -1;

	if (codec_name != NULL) {
		t = get_codec_type(codec_name);
	}
	return t;
}

/* DONE: called when system started. */
void
mxc_speech_codec_set_codec_by_id(int id)
{
	if (!codec_id_valid(id)) return;
	g_codec_id = id;
}

/* DONE: called when system started. */
void
mxc_speech_codec_set_codec_by_name(char *name)
{
	g_codec_id = speech_codec_id_by_name(name);
}

int
mxc_speech_codec_init(void)
{
	int ret;
	int ref = 0;

	if (g_codec_id < 0) return -1;

	audio_codec_lock();
	if (audio_codec_ref == 0) {
		ret = speech_codec_init(&g_ept, g_codec_id);
		if (ret == 0) {
			++audio_codec_ref;
		}
	}
	ref = audio_codec_ref;
	audio_codec_unlock();

	return ref;
}

int
mxc_speech_codec_destroy(void)
{
	int ref;

	audio_codec_lock();
	if (audio_codec_ref > 0) {
		--audio_codec_ref;
		if (audio_codec_ref == 0) {
			speech_codec_destroy(&g_ept);
		}
	}
	ref = audio_codec_ref;
	audio_codec_unlock();

	return ref;
}

int
mxc_speech_decode(char *ibuf, int ilen, char *obuf, int *osz)
{
	return speech_decode(&g_ept, ibuf, ilen, obuf, osz);
}

int
mxc_speech_get_rate(void)
{
	if (!codec_id_valid(g_codec_id)) return 0; 
	return g_cf[g_codec_id].rate;
}

#if 0
}
#endif

///////////////////////////////////////////////////////////
//                  encoder thread                       //
///////////////////////////////////////////////////////////
#if 0
{
#endif

#define ENC_BUF_SZ   320*2
static char  encoded_buf[ENC_BUF_SZ];

static thread_t  ae_thr;

static void
ae_main(void *arg)
{
	int   len;
	int   osz;
	char *ptr;
	thread_t  *thr = arg;
	speech_ept_t  *ept;

	ept = thread_priv_data(thr);

/*
	if (mxc_speech_codec_init() <= 0) {
		thread_run_quit(thr);
		return;
	}
*/
	if (audio_grabber_start(g_cf[g_codec_id].rate) < 0) {
		DBG("--- open sound device failed ---\n");
//		mxc_speech_codec_destroy();
		thread_run_quit(thr);
		return;
	}

	DBG("--- audio encoding started ---\n");
	thread_run_prepared(thr);	
	THREAD_LOOP(thr)
	{
		if ((len = audio_frame_ptr(&ptr)) == 0) {
			continue;
		}

		osz = ENC_BUF_SZ;
		if (speech_encode(ept, ptr, len, encoded_buf, &osz) == 0) {
			audio_packet_put(encoded_buf, osz);
			audio_frame_drain();
		}
	}

	audio_grabber_stop();
	DBG("--- close sound device success ---\n");
//	mxc_speech_codec_destroy();

	DBG("--- audio encoding stopped ---\n");
}

int
start_audio_encoder(void)
{
#ifndef DEBUG
	run_thread(&ae_thr);
#else
	int  ret;
	ret = run_thread(&ae_thr);
	if (ret == 0) {
		DBG("--- AUDIO ENCODING HAS STARTED ---\n");
	} else {
		DBG("--- AUDIO ENCODING START SUCCESS ---\n");
	}
	return ret;

#endif
}

void
stop_audio_encoder(void)
{
	DBG("--- TRYING TO STOP AUDIO ENCODING ---\n");
	suspend_thread(&ae_thr);
	DBG("--- AUDIO ENCODING STOPPED ---\n");
}

void
mxc_audio_encoder_init(void)
{
	new_thread(&ae_thr, ae_main, &g_ept, "audio encoder", NULL);
}

#if 0
}
#endif

///////////////////////////////////////////////////////////
//                  decoder thread                       //
///////////////////////////////////////////////////////////
#if 0
{
#endif

#define PBUF_SZ    (1024)

static char* a_out_buffer;
static int   a_out_buffer_size = PBUF_SZ;
static int   a_out_buffer_len = 0;

#if SPEECH_AOUT_THREAD
static int           preload_sz = 0;
static unsigned long curr_sz = 0;
static int           delay_ms = 20;
static int           opened = 0;

static int
aout_open_ifndelay(int rate)
{
	curr_sz = 0;
	opened = 0;
	preload_sz = (rate*2)*delay_ms/1000;
	DBG("--- play format: %s, rate = %d, preload = %d ---\n", g_cf[g_codec_id].name, rate, preload_sz);
	if (preload_sz == 0) {
	    if (aout_open(rate, 1) < 0)
	    {
		    DBG("--- open sound playback device failed ---\n");
		    return 0;
	    }
	    opened = 1;
	}
	return 1;
}

#endif

#if AUDIO_DECODER_THREAD

typedef struct _pkt {
	unsigned short len;
	char data[320];
} pkt_t;

#define PKT_ARR_SZ    1024

static pkt_t     pkt_arr[PKT_ARR_SZ];
static int       ridx = 0;
static int       widx = 0;

#endif

static void
play_voice(unsigned char *buf, int size)
{
	int   left = size;
	char *ptr = buf;
	int   n;

//	DBG("--- PLAYING SIZE = %d ---\n", size);
	while (left > 0) {
		n = audio_out_play(ptr, left);
		if (n <= 0) break;
		ptr += n;
		left -= n;
	}
}

static inline int
play_audio(char *buf, int size)
{
	int   n;
	int   left = size;
	char *iptr = buf;
	char *optr;
	int   sz;

	if (a_out_buffer_size < (size<<1)) {
		a_out_buffer = realloc(a_out_buffer, (size<<1));
		a_out_buffer_size = (size<<1);
	}

	a_out_buffer_len = 0;
	optr = a_out_buffer;
	sz   = a_out_buffer_size;
	while (left > 0) {
		n = speech_decode(&g_ept, iptr, left, optr, &sz);
		if (n <= 0) {
			break;
		}
		left -= n;
		iptr += n;
		optr += sz;
		a_out_buffer_len += sz;
		sz = a_out_buffer_size - a_out_buffer_len;
	}
#if AUDIO_DECODER_THREAD
	ridx = (ridx + 1) &(PKT_ARR_SZ - 1);
#endif

	if (a_out_buffer_len > 0) {
#if SPEECH_AOUT_THREAD
		n = aout_write(a_out_buffer, a_out_buffer_len);
		if (n > 0) {
			curr_sz += n;
			if (curr_sz >= preload_sz&&opened == 0) {
				if (aout_open(g_cf[g_codec_id].rate, 1) < 0)	{
					DBG("--- open sound device failed ---\n");
					return -1;
				}
				opened = 1;
			}
		}
#else
		play_voice(a_out_buffer, a_out_buffer_len);
#endif
	}
	return a_out_buffer_len;
}


#if AUDIO_DECODER_THREAD

static thread_t  ad_thr;

static void
ad_main(void *arg)
{
	thread_t  *thr = arg;
	speech_ept_t  *ept;
	int rate;

	ept = thread_priv_data(thr);
/*
	if (mxc_speech_codec_init() <= 0) {
		thread_run_quit(thr);
		return;
	}
*/
	rate = g_cf[g_codec_id].rate;

#if SPEECH_AOUT_THREAD
	if (aout_open_ifndelay(rate) == 0) {
		thread_run_quit(thr);
		return;
	}
#else
	if (audio_out_open(rate, 1) < 0) {
		thread_run_quit(thr);
		return;
	}
#endif

	DBG("--- audio decoding started ---\n");
	ridx = 0;
	widx = 0;
	thread_run_prepared(thr);
	THREAD_LOOP(thr)
	{
		if (ridx == widx) {
			continue;
		}
		play_audio(pkt_arr[ridx].data, pkt_arr[ridx].len);
	}

#if SPEECH_AOUT_THREAD
	if (opened)
	    aout_close(0);
#else
	audio_out_close(1);
#endif
//	mxc_speech_codec_destroy();

	DBG("--- audio decoding stopped ---\n");
}

/* called by access_main() in access.c */
static int
audio_decoder_input(char *buf, int len)
{
	if (((widx + 1)&(PKT_ARR_SZ - 1)) == ridx) {
//		DBG("--- TOO BAD: decoding buffer is full: widx = %d, ridx = %d ---\n", widx, ridx);
		return 0;
	}

//	DBG("--- audio data size: %d ---\n", len);

	if (len > 320) len = 320;
	memcpy(pkt_arr[widx].data, buf, len);
	pkt_arr[widx].len = len;
	widx = (widx + 1)&(PKT_ARR_SZ - 1);
	return len;
}

/* called by access_main() in access.c */
static int
start_audio_decoder(void)
{
#ifndef DEBUG
	run_thread(&ad_thr);
#else
	int  ret;
	ret = run_thread(&ad_thr);
	if (ret == 0) {
		DBG("--- AUDIO DECODING START FAILED ---\n");
	} else {
		DBG("--- AUDIO DECODING START SUCCESS ---\n");
	}
	return ret;
#endif
}

/* called by access_main() in access.c */
static void
stop_audio_decoder(void)
{
	DBG("--- TRYING TO STOP AUDIO DECODING ---\n");
	suspend_thread(&ad_thr);
	DBG("--- AUDIO DECODING STOPPED ---\n");
}

#endif

#if 0
}
#endif

////////////////////////////////////////////
/* called in access.c */
int
audio_decoder_prepare(void)
{
#if AUDIO_DECODER_THREAD == 0
#if SPEECH_AOUT_THREAD == 0
	return ((audio_out_open(g_cf[g_codec_id].rate, 1) == 0) ? 1 : 0);
#else
	return aout_open_ifndelay(g_cf[g_codec_id].rate);
#endif
#else
	return start_audio_decoder();
#endif
}

/* called in access.c */
void
audio_decoder_finish(void)
{
#if AUDIO_DECODER_THREAD == 0
#if SPEECH_AOUT_THREAD == 0
	audio_out_close(1);
#else
	if (opened)
	    aout_close(0);
#endif
#else
	stop_audio_decoder();
#endif
}

/* DONE: called in access.c */
void
mxc_play_audio(char *buf, int size)
{
#if AUDIO_DECODER_THREAD == 0
	play_audio(buf, size);
#else
	int n = 0;
	while (audio_decoder_input(buf, size) == 0) {
		u_msleep(10);
		if (++n > 100) return;
	}
#endif
}

/* DONE */
void
mxc_audio_decoder_init(void)
{
	a_out_buffer = calloc(PBUF_SZ, 1);
#if AUDIO_DECODER_THREAD == 1
	new_thread(&ad_thr, ad_main, &g_ept, "audio decoder", NULL);
#endif
}
/////////////////////////////////////////////

