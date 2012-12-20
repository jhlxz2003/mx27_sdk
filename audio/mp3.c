#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "af_dec.h"
#include "aout.h"
#include "mpg123.h"
#include "dbg.h"

static int             lib_init = 0;
static mpg123_handle  *g_handle = NULL;
static af_decoder_t    g_mp3_dec;
static off_t           curr_fr = 0;

static int  seek = 0;
static int  oldst = 0;

static af_info_t *g_inf = NULL;

static unsigned int  delay_frames = 0;

static int  nospace = 0;
static int  tmpsz = 0;
static unsigned char tmpbuf[16*1024];

#define PRELOAD_TM   500

#if 0
{
#endif

static int
mp3_lib_init(void)
{
	int err = MPG123_OK;
	if (lib_init == 0)
	{		
		err = mpg123_init();
		if (err == MPG123_OK) lib_init = 1;
	}
	return (err==MPG123_OK?0:-1);
}

/* DONE */
static int
mp3_getinfo(af_info_t* inf)
{
	int        result;
	double     tpf;
	double     tm;

	if (!g_handle || mpg123_scan(g_handle) != MPG123_OK)
	{
	    DBG("--- mpg123_scan() error ---\n");
	    return -1;
	}

	result = mpg123_getformat(g_handle, &inf->rate, &inf->channels, &inf->encoding);
	if(result != MPG123_OK)
	{
		DBG("--- mpg123_getformat() error ---\n");
		return -1;
	}
    mpg123_position(g_handle, 0, 0, NULL, &inf->frames, NULL, &tm);
    inf->duration = (int)tm;

    tpf = mpg123_tpf(g_handle);
    inf->fps = (double)1.0/tpf;
    DBG("--- frames = mpg123_position() = %ld, duration = %u secs, fps = %f ---\n",
        inf->frames, inf->duration, inf->fps);

	return 0;
}

/* DONE */
static int
_mp3_open(char *fname)
{
	if (g_handle == NULL)
	{
		if ((g_handle = mpg123_new(NULL, NULL)) == NULL)
		{
		    DBG("--- mpg123_new error ---\n");
			return -1;
		}

		if (mpg123_open(g_handle, fname) != MPG123_OK)
	    {
		    DBG("--- Cannot open %s: %s ---\n", fname, mpg123_strerror(g_handle));
		    mpg123_delete(g_handle);
		    g_handle = NULL;
		    return -1;
	    }
	    curr_fr = 0;
	}
	return 0;
}

/* DONE */
static void
_mp3_close(void)
{
	if (g_handle != NULL)
	{
	    mpg123_close (g_handle);
		mpg123_delete(g_handle);
		g_handle = NULL;
	}
}

/* DONE */
static off_t
mp3_seek_time(double sec)
{
	off_t  idx;
	if ((idx = mpg123_timeframe(g_handle, sec)) < 0) return idx;
	DBG("--- seek to %d frame ---\n", idx);
	return mpg123_seek_frame(g_handle, idx, SEEK_SET);
}

/* DONE */
static int
mp3_open(af_info_t *info)
{
    if (_mp3_open(info->path) < 0)
	{
		DBG("--- open mp3 file: %s error ---\n", info->path);
		return -1;
	}

	if (info->inited == 0)
	{
		if (mp3_getinfo(info) < 0)
		{
		    _mp3_close();
	        return -1;
		}
		info->inited = 1;
	}

	DBG("---- START PLAYING file: %s, duratio: %u, frames: %li, rate: %ld, chls: %d, fps: %f ----\n",
	    info->path, info->duration, info->frames, info->rate, info->channels, info->fps);

	g_inf = info;
    
	delay_frames = (unsigned int)(PRELOAD_TM*info->fps/1000);
	if (delay_frames < 8)
	    delay_frames = 8;
	DBG("--- delay_ms = %ums, delay_frames = %u ---\n", PRELOAD_TM, delay_frames);
	seek = 0;

	return 0;
}

static void
mp3_pause(void)
{
    aout_pause();
}

static void
mp3_resume(void)
{
    aout_resume();
}

static void
mp3_seek(double secs, int st)
{
    aout_flush();
	seek = 1;
	oldst = st;
	nospace = 0;
	mp3_seek_time(secs);
}

static int
mp3_seek_end(int resume_st)
{
    if (seek == 1)
    {
        DBG("--- SEEK END ---\n");
		seek = 0;
	    if (oldst == resume_st)
		{
		    aout_resume();
			DBG("--- CONTINUE PLAYING ---\n");
	    }
	    return oldst;
	}
	return -1;
}

static void
mp3_close(int imed)
{
    _mp3_close();
    aout_close(imed);
}

/* DONE */
/* return:-2-done,-1-error,0-continue*/
static int
mp3_play_frame(void)
{
	unsigned char *audio;
	size_t         bytes;
	int            mc;
	off_t          curr_fr;
	int            ret;

	/* The first call will not decode anything but return MPG123_NEW_FORMAT! */
	if (nospace == 1)
	{
//	    DBG("--- when paused, data not output yet ---\n");
	    if (tmpsz > 0)
	    {
	        aout_write((char*)tmpbuf, tmpsz);
	    }
	    tmpsz = 0;
	    nospace = 0;
	}

	mc = mpg123_decode_frame(g_handle, &curr_fr, &audio, &bytes);
	if(mc != MPG123_OK)
	{
		if (mc == MPG123_NEW_FORMAT)
		{
			long rate;
			int  channels, format;
			mpg123_getformat(g_handle, &rate, &channels, &format);
			DBG("+++ Note: New output format %liHz %ich, format %i, curr_fr:%d +++\n", rate, channels, format, curr_fr);
			return 0;
		}
		else if (mc == MPG123_DONE)
		{
			DBG("--- MPG123 play done ---\n");
			return -2;
		}
		else
		{
			DBG("--- MPG123 play error ---\n");
			return -1;
		}
	}

	if (bytes)
	{
        ret = aout_write((char*)audio, bytes);

		if (delay_frames > 0&&curr_fr == delay_frames+1)
		{
		    DBG("--- TIIME TO OPEN SOUND DEVICE ---\n");
            if (aout_open(g_inf->rate, g_inf->channels) < 0)
	        {
	        	DBG("--- OPEN SOUND DEVICE error ---\n");
	            _mp3_close();
	            return -1;
	        }
		}
		return ret;
	}
	DBG("--- MPG123 decode zero bytes ---\n");
	return 0;
}

/* DONE */
/* return:-2-done,-1-error,0-continue*/
static int
mp3_play_frame_nb(void)
{
	unsigned char *audio;
	size_t         bytes;
	int            mc;
	off_t          curr_fr;

    if (nospace == 1)
    {
        DBG("--- paused no space ---\n");
        return 0;
    }

	/* The first call will not decode anything but return MPG123_NEW_FORMAT! */
	mc = mpg123_decode_frame(g_handle, &curr_fr, &audio, &bytes);
	if(mc != MPG123_OK)
	{
		if (mc == MPG123_NEW_FORMAT)
		{
			long rate;
			int  channels, format;
			mpg123_getformat(g_handle, &rate, &channels, &format);
			DBG("+++ Note: New output format %liHz %ich, format %i, curr_fr:%d +++\n", rate, channels, format, curr_fr);
			return 0;
		}
		else if (mc == MPG123_DONE)
		{
			DBG("+++ MPG123 play done +++\n");
			return -2;
		}
		else
		{
			DBG("--- MPG123 play error ---\n");
			return -1;
		}
	}

	if (bytes)
	{
//		DBG("--- BYTES = %d, curr_fr = %d ---\n", bytes, curr_fr);
		if (aout_write_nonblock((char*)audio, bytes) == 0)
		{
		    DBG("+++ fifo buffer no space in paused situation. +++\n");
		    memcpy(tmpbuf, audio, bytes);
		    tmpsz = bytes;
		    nospace = 1;
		    return 0;
		}
		else
		{
		    return bytes;
		}
	}
	else
	{
		DBG("--- MPG123 decode zero bytes ---\n");
		return 0;
	}
}

af_decoder_t*
load_mp3_module(void)
{
	mp3_lib_init();
	memset(&g_mp3_dec, 0, sizeof(af_decoder_t));
	g_mp3_dec.open = mp3_open;
	g_mp3_dec.play[0] = mp3_play_frame;
	g_mp3_dec.play[1] = mp3_play_frame_nb;
	g_mp3_dec.pause = mp3_pause;
	g_mp3_dec.resume = mp3_resume;
	g_mp3_dec.seek = mp3_seek;
	g_mp3_dec.seek_end = mp3_seek_end;
	g_mp3_dec.close = mp3_close;
	register_af_decoder(&g_mp3_dec);
	return &g_mp3_dec;
}

#if 0
}
#endif

