#include <stdio.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <asm/byteorder.h>
#include "dbg.h"
#include "wav.h"
#include "mxc_alsa.h"
#include "af_dec.h"
#include "amixer.h"

#define WAV_BUFSZ    (32*1024)
#define MONO_LEN     (16*1024)

extern unsigned char  ao_tmpbuf[32*1024];
extern unsigned char  ao_framebuf[64*1024];

extern int  mono2stero(unsigned char *dst, unsigned char *src, int bytes);

static int  wavFd = -1;
static af_decoder_t  g_wav_dec;
static int  drv_open = 0;
static int  g_channels = 2;
static int  seek = 0;
static int  oldst = 0;
static af_info_t  *g_info;

typedef int (*DO_AO)(unsigned char *buf, unsigned int len);

/* DONE */
ssize_t
safe_read(int fd, void *buf, size_t count)
{
	ssize_t result = 0, res;

	while (count > 0)
	{
		if ((res = read(fd, buf, count)) == 0)
			break;
		if (res < 0)
			return result > 0 ? result : res;
		count -= res;
		result += res;
		buf = (char *)buf + res;
	}
	return result;
}

/* DONE */
static int
wav_header_check(void)
{
	WaveHeader *h;
	unsigned char *buf = NULL;

	buf = alloca(sizeof(WaveHeader));
	lseek(wavFd, 0, SEEK_SET);
	if (safe_read(wavFd, buf, sizeof(WaveHeader)) != sizeof(WaveHeader))
    {
    	DBG("--- READ WAV HEAD ERROR ---\n");
    	close(wavFd);
    	wavFd = -1;
    	return -1;
    }

    h = (WaveHeader *)buf;
    if (h->magic != WAV_RIFF || h->type != WAV_WAVE)
    {
    	DBG("--- NOT WAV FILE ---\n");
    	close(wavFd);
    	wavFd = -1;
		return -1;
	}
	return 0;
}

/* DONE */
static unsigned int
wav_read_chunk(unsigned int *length)
{
	WaveChunkHeader *c;
	u_int  type, len;

	if (safe_read(wavFd, ao_tmpbuf, sizeof(WaveChunkHeader)) != sizeof(WaveChunkHeader))
    {
        DBG("--- READ WAV HEAD ERROR ---\n");
        *length = 0;
        return 0;
    }

    c = (WaveChunkHeader*)ao_tmpbuf;
    type = c->type;
    len = LE_INT(c->length);
    len += len % 2;
#ifdef DEBUG
    {
    	char *p1 = (char*)&type;
    	u_int x = WAV_FMT;
    	char *p2 = (char*)&x;
        DBG("--- type = %c%c%c%c, chunk len = %d, type_value = %u ---\n", p1[0], p1[1], p1[2], p1[3], len, type);
        DBG("--- fmt_type = %c%c%c%c, fmt_type_value = %u ---\n", p2[0], p2[1], p2[2], p2[3], x);
    }
#endif
    if (len > WAV_BUFSZ)
    {
        DBG("--- not enough space for the chunk ---\n");
        *length = 0;
        return 0;
    }

    if (type == WAV_FMT)
    {
    	DBG("--- found fmt chunk ---\n");
        if (safe_read(wavFd, ao_tmpbuf, len) != len)
        {
            DBG("--- READ WAV CHUNK ERROR ---\n");
            *length = 0;
            return 0;
        }
    }
    else
    {
    	if (lseek(wavFd, len, SEEK_CUR) == (off_t)-1)
    	{
    		DBG("--- lseek ERROR ---\n");
            *length = 0;
            return 0;
    	}
    }
    *length = len;
    return type;
}

/* DONE */
static int
wav_format_handle(af_info_t *info)
{
	WaveFmtBody *f;

    f = (WaveFmtBody*)ao_tmpbuf;
	if (LE_SHORT(f->format) != WAV_PCM_CODE)
    {
		DBG("--- can't play not PCM-coded WAVE-files ---\n");
    	return -1;
	}
	if (LE_SHORT(f->channels) < 1)
    {
		DBG("--- can't play WAVE-files with %d tracks ---\n", LE_SHORT(f->channels));
    	return -1;
	}

	info->channels = LE_SHORT(f->channels);
	switch (LE_SHORT(f->bit_p_spl))
	{
	case 8:
		info->encoding = 1;
		break;
	case 16:
		info->encoding = 2;
		break;
	default:
		DBG("--- can't play WAVE-files with sample %d bits wide ---\n", LE_SHORT(f->bit_p_spl));
    	return -1;
	}

	info->rate = LE_INT(f->sample_fq);
	info->byte_p_sec = f->byte_p_sec;
	return 0;
}

/* DONE */
static int
wav_getinfo(af_info_t *info)
{
	u_int len;
	u_int type;
	WaveChunkHeader *c;

	if (wav_header_check() < 0) return -1;
	lseek(wavFd, sizeof(WaveHeader), SEEK_SET);
	while (1)
    {
    	len = 0;
    	type = wav_read_chunk(&len);
    	if (len == 0)
    	{
    		close(wavFd);
    		wavFd = -1;
    		return -1;
    	}

        if (type == WAV_FMT)
		{
			DBG("--- FOUND fmt chunk ---\n");
			if (len < sizeof(WaveFmtBody))
			{
		        DBG("--- unknown length of 'fmt ' chunk (read %u, should be %u at least) ---\n",
		            len, (unsigned int)sizeof(WaveFmtBody));
		        close(wavFd);
    		    wavFd = -1;
		        return -1;
	        }
			break;
		}
    }

    if (wav_format_handle(info) < 0)
    {
    	close(wavFd);
    	wavFd = -1;
		return -1;
    }

    while (1)
    {
		if (safe_read(wavFd, ao_tmpbuf, sizeof(WaveChunkHeader)) != sizeof(WaveChunkHeader))
        {
            DBG("--- READ WAV CHUNK HEAD ERROR ---\n");
            break;
        }

        c = (WaveChunkHeader*)ao_tmpbuf;
        type = c->type;
        len = LE_INT(c->length);
		if (type == WAV_DATA)
		{
			off_t  l;
			l = lseek(wavFd, 0, SEEK_CUR);
			if (l == (off_t)-1)
				break;
			else
			{
				info->dataoff = l;
				info->datasz = len;
				info->duration = (int)(len/info->byte_p_sec);
			    return 0;
			}
		}
		len += len % 2;
		if (lseek(wavFd, len, SEEK_CUR) == (off_t)-1)
			break;
	}

	if (wavFd >= 0)
	{
		close(wavFd);
    	wavFd = -1;
	}
    return -1;
}

/* DONE */
static int
wav_open(af_info_t *info)
{
    if ((wavFd = open(info->path, O_RDONLY)) < 0)
    {
    	DBG("--- OPEN WAV FILE ERROR ---\n");
    	return -1;
    }

    if (info->inited == 0)
    {
    	DBG("-- NOT GET WAV INFO YET ---\n");
    	if (wav_getinfo(info) < 0) return -1;
    	info->inited = 1;
    }
    else
    {
    	DBG("-- HAS GET WAV INFO ---\n");
    	if (lseek(wavFd, info->dataoff, SEEK_SET) == (off_t)-1)
    	{
    		DBG("-- SEEK TO DATA ERROR ---\n");
    		close(wavFd);
    		wavFd = -1;
    		return -1;
    	}
    }
    DBG("---wav: rate=%ld, channels=%d, format=%d, data start from %u ---\n",
        info->rate, info->channels, info->encoding, info->dataoff);
    DBG("--- duration=%d, byte_p_sec=%d, datasz=%u ---\n", info->duration, info->byte_p_sec, info->datasz);

    if (drv_open == 0)
    {
        g_channels = info->channels;
        if (audio_out_open(info->rate, info->channels) < 0)
        {
        	DBG("-- audio_out_open ERROR ---\n");
    		close(wavFd);
    		wavFd = -1;
        	return -1;
        }
        if (mixer_open() == 0)
        {
            DBG("--- mixer_open() failed ---\n");
    	    audio_out_close(1);
    	    close(wavFd);
    		wavFd = -1;
    	    return -1;
        }

        drv_open = 1;
    }
    g_info = info;
    seek = 0;

    return 0;
}

/* DONE */
static void
wav_close(int imed)
{
	if (wavFd != -1)
	{
	    close(wavFd);
	    wavFd = -1;
	}
	
	if (drv_open)
    {
        audio_out_close(1);
        mixer_close();
        drv_open = 0;
    }
}

/* DONE */
static void
wav_pause(void)
{
	audio_out_pause();
}

/* DONE */
static void
wav_resume(void)
{
	audio_out_resume();
}

static void
wav_seek(double secs, int st)
{
	off_t  off;

    seek = 1;
	oldst = st;
	off = secs*g_info->byte_p_sec;
	lseek(wavFd, off + g_info->dataoff, SEEK_SET);
	audio_out_pause();
}

static int
wav_seek_end(int resume_st)
{
    if (seek == 1)
    {
        DBG("--- SEEK END ---\n");
		seek = 0;
		audio_out_resume();
	    return oldst;
	}
	return -1;
}

static int
do_mono(unsigned char *buf, unsigned int len)
{
	unsigned int bytes;
    unsigned int n;

    bytes = ((len >> 1) << 1);
    n = mono2stero(ao_framebuf, ao_tmpbuf, bytes);
    return audio_out_play(ao_framebuf, n);
}

static int
do_stero(unsigned char *buf, unsigned int len)
{
	unsigned int bytes;

    bytes = ((len >> 2) << 2);
    return audio_out_play(ao_tmpbuf, bytes);
}


static DO_AO do_aout[2] = {do_mono, do_stero};
static int  maxsz[2] = {MONO_LEN, WAV_BUFSZ};

static int
wav_play(void)
{
	unsigned int bytes;

	bytes = safe_read(wavFd, ao_tmpbuf, maxsz[g_channels-1]);
	if (bytes < 0) return -1;
	else if (bytes == 0) return -2;
	return do_aout[g_channels-1](ao_tmpbuf, bytes);
}

static int
wav_play_nb(void)
{
	return 0;
}

af_decoder_t*
load_wav_module(void)
{
	memset(&g_wav_dec, 0, sizeof(af_decoder_t));
	g_wav_dec.type = 1;
	g_wav_dec.open = wav_open;
	g_wav_dec.play[0] = wav_play;
	g_wav_dec.play[1] = wav_play_nb;
	g_wav_dec.pause = wav_pause;
	g_wav_dec.resume = wav_resume;
	g_wav_dec.seek = wav_seek;
	g_wav_dec.seek_end = wav_seek_end;
	g_wav_dec.close = wav_close;
	register_af_decoder(&g_wav_dec);
	return &g_wav_dec;
}

