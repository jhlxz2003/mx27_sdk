#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <alloca.h>
#include <alsa/asoundlib.h>

#include "mxc_alsa.h"
#include "dbg.h"

static snd_pcm_stream_t  g_stream[] = {SND_PCM_STREAM_PLAYBACK, SND_PCM_STREAM_CAPTURE};
static unsigned int sleep_min = 0;  // min ticks to sleep\n"
static int avail_min = -1;          // min available space for wakeup is # microseconds
static int start_delay = 0;         // delay for automatic PCM start is # microseconds
static int stop_delay = 0;          // delay for automatic PCM stop is # microseconds from xrun

/* DONE */
static int
alsa_setup(alsa_t *a)
{
    snd_pcm_hw_params_t *params;
    snd_pcm_sw_params_t *swparams;
    snd_pcm_uframes_t    buffer_size, period_size;
    snd_pcm_uframes_t    tmp_buf_size;
    snd_pcm_uframes_t    xfer_align;
    snd_pcm_uframes_t    start_threshold, stop_threshold;
    unsigned int rate;
    unsigned nframes;
    int      err;
    int      latency;
    size_t   n;

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_sw_params_alloca(&swparams);

    err = snd_pcm_hw_params_any(a->handle, params);
    if (err < 0) {
		DBG("Broken configuration for this PCM: no configurations available\n");
		return -1;
    }

    err = snd_pcm_hw_params_set_access(a->handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
		DBG("Access type not available\n");
		return -1;
	}

    err = snd_pcm_hw_params_set_format(a->handle, params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
		DBG("Sample format not available\n");
		return -1;
    }

    err = snd_pcm_hw_params_set_channels(a->handle, params, a->channels);
    if (err < 0) {
		snd_pcm_hw_params_get_channels(params, &a->channels);
		DBG("set Channels failed and forced to set %d channels", a->channels);
    }

    rate = a->rate;
    err = snd_pcm_hw_params_set_rate_near(a->handle, params, &rate, NULL);
    if (err < 0) {
		DBG("rate not supported, and being set %d\n", rate);
    }
    a->rate = rate;

	//////////////////////////////////////////////////////////////////////////////
    nframes = (rate*PTIME/1000)/a->channels;

	snd_pcm_hw_params_set_period_size_near(a->handle, params, &nframes, NULL);
    DBG("--- period size set to: %d frames", nframes);

    tmp_buf_size = (rate / 1000) * a->latency_ms;
    snd_pcm_hw_params_set_buffer_size_near(a->handle, params, &tmp_buf_size);

#ifdef DEBUG
    latency = tmp_buf_size / (rate / 1000);
    DBG("--- buffer size set to: %d and latency = %d ---\n", (int)tmp_buf_size, latency);
#endif

    err = snd_pcm_hw_params(a->handle, params);
    if (err < 0) {
		DBG("--- Unable to install hw params ---\n");
		return -1;
    }
    
    //////////////////////////////////////////////////////////////

    snd_pcm_hw_params_get_period_size(params, &period_size, NULL);
    snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
    a->chunk_size = period_size;
    DBG("----------------- chunk_size = %u, buffer_size = %u ----------------\n", (unsigned int)period_size, (unsigned int)buffer_size);
    if (period_size == buffer_size) {
		DBG("--- chunk_size and buffer_size cannot be equal ---\n");
		return -1;
    }

	if (a->type == 0) {
		a->can_pause = snd_pcm_hw_params_can_pause(params);
	    DBG("--- alsa_can_pause = %d ---\n", a->can_pause);

    	snd_pcm_sw_params_current(a->handle, swparams);
    	err = snd_pcm_sw_params_get_xfer_align(swparams, &xfer_align);
		if (err < 0) {
			DBG("--- Unable to obtain xfer align ---\n");
		}

		if (sleep_min)
			xfer_align = 1;
		err = snd_pcm_sw_params_set_sleep_min(a->handle, swparams, sleep_min);
	
		if (avail_min < 0)
			n = period_size;
		else
			n = (double) rate * avail_min / 1000000;
		err = snd_pcm_sw_params_set_avail_min(a->handle, swparams, n);

		n = (buffer_size / xfer_align) * xfer_align;
		if (start_delay <= 0) {
			start_threshold = n + (double) rate * start_delay / 1000000;
		} else {
			start_threshold = (double) rate * start_delay / 1000000;
		}
		if (start_threshold < 1)
			start_threshold = 1;
		if (start_threshold > n)
			start_threshold = n;
		err = snd_pcm_sw_params_set_start_threshold(a->handle, swparams, start_threshold);

		if (stop_delay <= 0) 
			stop_threshold = buffer_size + (double) rate * stop_delay / 1000000;
		else
			stop_threshold = (double) rate * stop_delay / 1000000;
		err = snd_pcm_sw_params_set_stop_threshold(a->handle, swparams, stop_threshold);

		err = snd_pcm_sw_params_set_xfer_align(a->handle, swparams, xfer_align);

    	if (snd_pcm_sw_params(a->handle, swparams) < 0) {
			DBG("--- snd_pcm_sw_params failed ---\n");
			return -1;
    	}
    }

    ///////////////////////////////////////////////////////////////

    a->bits_per_sample = snd_pcm_format_physical_width(SND_PCM_FORMAT_S16_LE);
    a->bytes_per_sample = a->bits_per_sample >> 3;
    a->bytes_per_frame = a->bytes_per_sample * a->channels;
    a->blocksize = a->chunk_size * a->bytes_per_frame;
    a->buffersize = buffer_size * a->bytes_per_frame;
    DBG("--- audio blocksize = %d ---\n", a->blocksize);

    return 0;
}

/* DONE */
/* mode: nonblock mode, generally it should be set to 0. */
static int
alsa_open(alsa_t *a, int mode)
{
    int  err;
	int  open_mode = 0;

	if (mode) {
        open_mode |= SND_PCM_NONBLOCK;
    }

    err = snd_pcm_open(&a->handle, "default", g_stream[a->type], open_mode);
    if (err < 0) {
		DBG("--- audio open error: %s:%d ---\n", snd_strerror(err), open_mode);
		a->handle = NULL;
		return -1;
    }

	if (mode) {
		err = snd_pcm_nonblock(a->handle, 1);
		if (err < 0) {
			DBG("--- nonblock setting error: %s ---\n", snd_strerror(err));
			snd_pcm_close(a->handle);
			a->handle = NULL;
			return -1;
		}
	}
    return alsa_setup(a);
}

#ifndef timersub
#define	timersub(a, b, result) \
do { \
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
	if ((result)->tv_usec < 0) { \
		--(result)->tv_sec; \
		(result)->tv_usec += 1000000; \
	} \
} while (0)
#endif

/* I/O suspend handler */
static int
alsa_suspend(snd_pcm_t *handle)
{
	int res;

	while ((res = snd_pcm_resume(handle)) == -EAGAIN) {
		DBG("--- snd_pcm_resume error: %s ---", snd_strerror(res));
		sleep(1);	/* wait until suspend flag is released */
	}

	if (res < 0)
	{
		if ((res = snd_pcm_prepare(handle)) < 0)
		{
			DBG("--- suspend: prepare error: %s ---", snd_strerror(res));
			return -1;
		}
	}
	return 0;
}

/* stream: 0-playback, 1-capture */
static int
alsa_xrun(snd_pcm_t *handle, int stream)
{
    snd_pcm_status_t *status;
    int res;

    snd_pcm_status_alloca(&status);
    if ((res = snd_pcm_status(handle, status)) < 0) {
		DBG("--- status error: %s ---\n", snd_strerror(res));
		return -1;
    }

    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
    #if 1
		struct timeval now, diff, tstamp;
		gettimeofday(&now, 0);
		snd_pcm_status_get_trigger_tstamp(status, &tstamp);
		timersub(&now, &tstamp, &diff);
		printf("--- %s!!! (at least %.3f ms long) ---\n", (stream == 0)?"underrun":"overrun",
		       diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
	#endif
		if ((res = snd_pcm_prepare(handle)) < 0) {
			DBG("--- xrun: snd_pcm_prepare() error ---\n");
			return -1;
		}
		return 0;		/* ok, data should be accepted again */
    }
    
    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_DRAINING) {
    	if (stream == 1) {
			if ((res = snd_pcm_prepare(handle)) < 0) {
				DBG("--- xrun: snd_pcm_prepare() error ---\n");
				return -1;
			}
			return 0;
		}
	}

    return -1;
}

/////////////////////////////////////////////////////////////
#if 0
{
#endif

/* DONE */
int
ai_open(alsa_t *a, int mode, int rate, int chls)
{
	if (a->handle) return -1;
	
	if (chls)
		a->channels = chls;
	else
		a->channels = 1;

	if (rate)
		a->rate = rate;
	else
		a->rate = 16000;

	a->type = 1;
	a->latency_ms = 100;

    return alsa_open(a, mode);
}

/* DONE */
int
ai_close(alsa_t *a)
{
	if (a->handle) {
//	    snd_pcm_drain(a->handle);
		snd_pcm_close(a->handle);
		a->handle = NULL;
		return 0;
	}
    return -1;
}


/* DONE */
int
ai_start_capture(alsa_t *a)
{
	if (a->handle == NULL) return -1;
	return snd_pcm_start(a->handle);
}

/* DONE */
int
ai_read(alsa_t *a, unsigned char *buffer)
{
    int  r;
    unsigned int  result = 0;
    snd_pcm_uframes_t  count;
    static int lost = 0;

	count = a->chunk_size;
	while (count > 0)
	{
		r = snd_pcm_readi(a->handle, buffer, count);
		if ((r == -EAGAIN)||(r >= 0 && (size_t)r < count)) {
//			DBG("--- snd_pcm_readi: -EAGAIN-%s:r=%d ---\n", snd_strerror(r), r);
			snd_pcm_wait(a->handle, 1000);
			if (++lost >= 50) {
				lost = 0;
			    return -1;
			}
		} else if (r == -EPIPE) {
		    DBG("--- snd_pcm_readi: -EPIPE ---\n");
			if (alsa_xrun(a->handle, 1) < 0) return -1;
		} else if (r == -ESTRPIPE) {
			DBG("--- snd_pcm_readi: -ESTRPIPE ---\n");
			if (alsa_suspend(a->handle) < 0) return -1;
		} else if (r < 0) {
			DBG("--- read error: %s ---\n", snd_strerror(r));
			return -1;
		}

		if (r > 0)
		{
		    lost = 0;
			result += r;
			count  -= r;
			buffer += r * a->bytes_per_frame;
		}
	}
	return result;
}

#if 0
}
#endif


/////////////////////////////////////////////////////////////
#if 0
{
#endif

#if 0
static int
get_space(alsa_t *a)
{
    snd_pcm_status_t *status;
    int ret;

    snd_pcm_status_alloca(&status);
    if ((ret = snd_pcm_status(a->handle, status)) < 0) {
		DBG("--- snd_pcm_status() error: %s ---\n", snd_strerror(ret));
		return 0;
    }

    ret = snd_pcm_status_get_avail(status) * a->bytes_per_frame; /* ????? */
    if (ret > a->buffersize)
		ret = a->buffersize;
    return ret;
}

/* DONE: delay in seconds between first and last sample in buffer */
static float
get_delay(alsa_t *a)
{
	if (a->handle) {
		snd_pcm_sframes_t delay;

		if (snd_pcm_delay(a->handle, &delay) < 0)
			return 0;

    	if (delay < 0) {
		/* underrun - move the application pointer forward to catch up */
		/* snd_pcm_forward() exists since 0.9.0rc8 */
			snd_pcm_forward(a->handle, -delay);
			delay = 0;
		}
		return (float)delay / (float)a->rate;
	} else {
		return 0;
	}
}
#endif

static inline void
ao_close(alsa_t *a, int immed)
{
    if (a->handle)
    {
        if (!immed)
            snd_pcm_drain(a->handle);
        snd_pcm_close(a->handle);
        a->handle = NULL;
    }
}

/* DONE */
static inline int
ao_open(alsa_t *a, int rate, int channels, int nonblock)
{
    if (a->handle) return -1;
	
	a->channels = channels;
	a->rate = rate;
	a->type = 0;
	a->latency_ms = 140; //old value: 140
	a->paused = 0;

    return alsa_open(a, nonblock);
}

static inline int
ao_pause(alsa_t *a)
{
    int err;

    if (a->handle == NULL||a->paused == 1) return -1;
    if (a->can_pause)
    {
        if ((err = snd_pcm_pause(a->handle, 1)) < 0)
        {
            DBG("--- alsa-pause: pcm pause error: %s ---\n", snd_strerror(err));
            a->can_pause = 0;
            return ao_pause(a);
        }
        DBG("--- alsa-pause: pause supported by hardware ---\n");
    }
    else
    {
//    	a->prepause_space = get_space(a);
        if ((err = snd_pcm_drop(a->handle)) < 0)
        {
            DBG("--- alsa-pause: pcm drop error: %s ---\n", snd_strerror(err));
            return -1;
        }
        DBG("--- alsa-pause: pcm drop ---\n");
    }
    a->paused = 1;
    return 0;
}

/* DONE */
/* stop playing and empty buffers (for seeking/pause) */
static inline void
ao_reset(alsa_t *a)
{
    int err;

    if (a->handle == NULL) return;
    if ((err = snd_pcm_drop(a->handle)) < 0)
    {
	    DBG("--- alsa-reset: pcm drop error: %s ---\n", snd_strerror(err));
	    return;
    }

    if ((err = snd_pcm_prepare(a->handle)) < 0)
    {
	    DBG("--- alsa-reset: pcm prepare error: %s ---\n", snd_strerror(err));
    }
}

/*
static void
ao_resume_refill(int prepause_sp)
{
	int fillcnt = get_space() - prepause_sp;
	if (fillcnt > 0 ) {
		void *silence = calloc(fillcnt, 1);
		play(silence, fillcnt, 0);
		free(silence);
	}
}
*/
 
/* DONE */
static inline void
ao_resume(alsa_t *a)
{
    int err;

    if (a->handle == NULL||a->paused == 0) return;
/*    
    if (snd_pcm_state(alsa_handler) == SND_PCM_STATE_SUSPENDED) {
        while ((err = snd_pcm_resume(alsa_handler)) == -EAGAIN) sleep(1);
    }
*/
    if (a->can_pause)
    {
        if ((err = snd_pcm_pause(a->handle, 0)) < 0)
        {
            DBG("--- alsa-resume: pcm resume error: %s ---\n", snd_strerror(err));
            return;
        }
        DBG("--- alsa-resume: resume supported by hardware ---\n");
    }
    else
    {
        if ((err = snd_pcm_prepare(a->handle)) < 0)
        {
            DBG("--- alsa-resume: pcm prepare error: %s ---\n", snd_strerror(err));
            return;
        }
//        ao_resume_refill(prepause_space);
    }
}

/* len: bytes */
static inline int
ao_play(alsa_t *a, void *data, int len)
{
	ssize_t r;
	ssize_t result = 0;
	snd_pcm_uframes_t count;

    count = len / (a->bytes_per_frame);
	if (count < a->chunk_size)
    {
		snd_pcm_format_set_silence(SND_PCM_FORMAT_S16_LE, data + count*a->bytes_per_frame, (a->chunk_size - count)*a->channels);
		count = a->chunk_size;
	}
//	DBG("--- snd_pcm_writei: count = %d, chunk_size = %d ---\n", count, a->chunk_size);
	while (count > 0)
	{
		r = snd_pcm_writei(a->handle, data, count);
		if (r == -EAGAIN) {
			snd_pcm_wait(a->handle, 1000);
//			DBG("--- snd_pcm_writei: r = %d:%s ---\n", r, snd_strerror(r));
		} else if (r == -EPIPE) {
			DBG("--- snd_pcm_writei: -EPIPE ---\n");
			if (alsa_xrun(a->handle, 0) < 0) return -1;
		} else if (r == -ESTRPIPE) {
			DBG("--- snd_pcm_writei: -ESTRPIPE ---\n");
			if (alsa_suspend(a->handle) < 0) return -1;
		} else if (r == -EINTR) {
			continue;
		} else if (r < 0) {
			DBG("--- write error: %s ---\n", snd_strerror(r));
			return -1;
		}

		if (r > 0)
		{
			result += r;
			count -= r;
			data += r * a->bytes_per_frame;
		}
	}
	DBG("--- snd_pcm_writei: %d frames ---\n", result);
	return (result * a->bytes_per_frame);
}

#if 0
}
#endif

///////////////////////////////////////////
static alsa_t  g_ao;

int
audio_out_open(int rate, int chls)
{
	return ao_open(&g_ao, rate, chls, 1);
}

void
audio_out_close(int imed)
{
	ao_close(&g_ao, imed);
}

int
audio_out_play(unsigned char *buf, int len)
{
	return ao_play(&g_ao, buf, len);
}

int
audio_out_pause(void)
{
	return ao_pause(&g_ao);
}

void
audio_out_resume(void)
{
	ao_resume(&g_ao);
}

int
audio_out_chunk_bytes(void)
{
	return g_ao.blocksize;
}

