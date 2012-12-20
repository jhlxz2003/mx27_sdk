#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include "mxc_alsa.h"
#include "amixer.h"
#include "dbg.h"

#define  OUTBURST     (32*1024)
#define  FIFO_BUFSZ   (320*1024)

typedef struct _buffer {
	unsigned char *bufbase;
	int            sz;
	int            wptr;
	int            rptr;
	unsigned int   nwrites;
	unsigned int   nreads;
} buffer_t;

typedef struct _fifo {
    buffer_t  buf;
    int       st;
    int       pfd[2];
    int       wakeme[2];
    int       chunk_sz;
    int       end;
} fifo_t;

typedef void (*AO_FUNC)(fifo_t*);

unsigned char  ao_tmpbuf[32*1024];
unsigned char  ao_framebuf[64*1024];

static fifo_t  g_af;
static int     drv_open = 0;
static int     g_channels;

static void    do_aout1chs(fifo_t *fio);
static void    do_aout2chs(fifo_t *fio);

static AO_FUNC ao_func[2] = {do_aout2chs, do_aout1chs};

static void
u_flush_fd(int fd)
{
	int nread;
	int left;
	char buf[256];
	if (ioctl(fd, FIONREAD, &left) < 0) return;
	while (left > 256)
	{
		nread = read(fd, buf, 256);
		left -= nread;
	}
	while (left)
	{
		nread = read(fd, buf, left);
		left -= nread;
	}
}

///////////////////////////////////////
//           buffer routine          //
///////////////////////////////////////
#if 0
{
#endif

static int
buffer_init(buffer_t *buf, int sz)
{
    int ret = 0;
    
    memset(buf, 0, sizeof(buffer_t));
	buf->bufbase = calloc(sz, sizeof(char));
	if (buf->bufbase)
	{
		buf->sz = sz;
		buf->rptr = buf->wptr = 0;
	    buf->nreads = 0;
	    buf->nwrites = 0;
	    ret = 1;
	}
	return ret;
}

static void
buffer_free(buffer_t *buf)
{
	if (buf->bufbase != NULL)
		free(buf->bufbase);
}

static void
buffer_reset(buffer_t *buf)
{
	buf->rptr = buf->wptr = 0;
	buf->nreads = 0;
	buf->nwrites = 0;
}

#if 0
}
#endif

///////////////////////////////////////
//            fifo routine           //
///////////////////////////////////////
/* DONE */
static int
fifo_init(int sz)
{
	fifo_t *fio = &g_af;

	memset(fio, 0, sizeof(fifo_t));
	if (buffer_init(&fio->buf, sz) == 0) return -1;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fio->pfd) < 0)
	{
		buffer_free(&fio->buf);
		return -1;
	}

	return 0;
}

static void
fifo_wakeup_writer(fifo_t *fio)
{
    if (fio->wakeme[1] == 1)
    {
        write(fio->pfd[0], "D", 1);
    }
}

static void
fifo_wakeup_reader(fifo_t *fio)
{
    if (fio->wakeme[0] == 1)
    {
        write(fio->pfd[1], "d", 1);
    }
}

static int
fifo_writer_block(fifo_t *fio)
{
    char c = 0;

    fio->wakeme[1] = 1;
    read(fio->pfd[1], &c, 1);
    fio->wakeme[1] = 0;
    if (c == 'e')
        return -1;
    else
        return 0;
}

int
mono2stero(unsigned char *dst, unsigned char *src, int bytes)
{
    unsigned short *input_samples = (unsigned short *)src;
    unsigned short *output_samples = (unsigned short *)dst;
    unsigned int    frames;

    frames = bytes>>1;
    while (frames--)
    {
        unsigned short os;
        os = *input_samples++;
        *output_samples++ = os;
        *output_samples++ = os;
    }

    return (bytes<<1);
}

/* mono play */
static int
_do_aout1chs(buffer_t *buf, int bytes)
{
    int to_end;
    int len;
    int written = 0;

    bytes = ((bytes >> 1) << 1);
    if (bytes > OUTBURST)
    {
        bytes = OUTBURST;
    }

    to_end = buf->sz - buf->rptr;
    if (bytes > to_end)
    {
        memcpy(ao_tmpbuf, buf->bufbase + buf->rptr, to_end);
        memcpy(ao_tmpbuf + to_end, buf->bufbase, bytes - to_end);
        len = mono2stero(ao_framebuf, ao_tmpbuf, bytes);
        written = audio_out_play(ao_framebuf, len);
    }
    else
    {
        len = mono2stero(ao_framebuf, buf->bufbase + buf->rptr, bytes);
        written = audio_out_play(ao_framebuf, len);
    }
    
    if (written > 0)
    {
        written >>= 1;
        buf->rptr = (buf->rptr + written) % buf->sz;
        buf->nreads += written;
    }

    return written;
}

static void
do_aout1chs(fifo_t *fio)
{
    int bytes;
    int len;
    buffer_t *buf = &fio->buf;

    bytes = buf->nwrites - buf->nreads;
    len = bytes << 1;
	if (len >= fio->chunk_sz)
	{
		if (_do_aout1chs(buf, bytes) <= 0)
		{
		    buffer_reset(buf);
		    fio->wakeme[0] = 0;
		    write(fio->pfd[0], "e", 1);
		    fio->st = 0;
		    fio->end = 0;
		}
		else
	    {
	        fifo_wakeup_writer(fio);
	    }
	}
	else
	{
		if (fio->end == 1)
		{
		    _do_aout1chs(buf, bytes);
		    buffer_reset(buf);
		    fio->wakeme[0] = 0;
		    write(fio->pfd[0], "e", 1);
		    DBG("--- finished playing ---\n");
		    fio->st = 0;
		    fio->end = 0;
		}
		else
		{
		    fio->wakeme[0] = 1;
		    fifo_wakeup_writer(fio);
		    fio->st = 2;
		}
	}
}

/* stero play */
static int
_do_aout2chs(buffer_t *buf, int bytes)
{
    int to_end;
    int written = 0;

    bytes = ((bytes >> 2) << 2);
    if (bytes > OUTBURST)
    {
        bytes = OUTBURST;
    }

    to_end = buf->sz - buf->rptr;
    if (bytes > to_end)
    {
        memcpy(ao_tmpbuf, buf->bufbase + buf->rptr, to_end);
        memcpy(ao_tmpbuf + to_end, buf->bufbase, bytes - to_end);
        written = audio_out_play(ao_tmpbuf, bytes);
    }
    else
    {
        written = audio_out_play(buf->bufbase + buf->rptr, bytes);
    }

    if (written > 0)
    {
        buf->rptr = (buf->rptr + written) % buf->sz;
        buf->nreads += written;
    }

    return written;
}

static void
do_aout2chs(fifo_t *fio)
{
    int bytes;
    buffer_t *buf = &fio->buf;

    bytes = buf->nwrites - buf->nreads;
	if (bytes >= fio->chunk_sz)
	{
		if (_do_aout2chs(buf, bytes) <= 0)
	    {
		    buffer_reset(buf);
		    fio->wakeme[0] = 0;
		    write(fio->pfd[0], "e", 1);
		    u_flush_fd(fio->pfd[0]);
		    fio->st = 0;
		    fio->end = 0;
	    }
	    else
	    {
	        fifo_wakeup_writer(fio);
	    }
	}
	else
	{
	    if (fio->end == 1)
        {
		    _do_aout2chs(buf, bytes);
		    buffer_reset(buf);
		    fio->wakeme[0] = 0;
		    write(fio->pfd[0], "e", 1);
		    DBG("--- finished playing ---\n");
		    u_flush_fd(fio->pfd[0]);
		    fio->st = 0;
		    fio->end = 0;
		}
		else
		{
		    fio->wakeme[0] = 1;
		    fio->st = 2;
		    fifo_wakeup_writer(fio);
        }
	}
}

/*
 st: 0-sleep, 1-pause, 2-not enough data, 3-run.
 */
static void*
aout_loop(void* arg)
{
	fd_set rset;
	int  ret;
	int  fd;
	char c;
	fifo_t    *fio;
	buffer_t  *buf;
	AO_FUNC    output = ao_func[0];
	struct timeval  tv;

    pthread_detach(pthread_self());

    fio = arg;
    buf = &fio->buf;
    fd = fio->pfd[0];
	fio->st = 0;
	fio->end = 0;

	while (1)
	{
		FD_ZERO(&rset);
		FD_SET(fd, &rset);		
		tv.tv_sec = 0;
	    tv.tv_usec = 1000;

        if (fio->st == 3)
        {
		    ret = select(fd + 1, &rset, NULL, NULL, &tv);
		}
		else
		{
		    ret = select(fd + 1, &rset, NULL, NULL, NULL);
		}

		if ((ret > 0)&&FD_ISSET(fd, &rset))
		{
			if (read(fd, &c, 1) == 1)
			{
				if (c == 'b') //start cmd
				{
					if (fio->st == 0)
					{
						DBG("--- received start aout cmd ---\n");
						fio->end = 0;
						output = ao_func[g_channels&1];
						fio->chunk_sz = audio_out_chunk_bytes();
						fio->st = 3;
					}
				}
				else if (c == 'e')  //imediately-stop cmd
				{
				    if (fio->st != 0)
				    {
				    	DBG("--- received stop aout cmd ---\n");
                        buffer_reset(buf);
				        fio->st = 0;
				        if (fio->wakeme[1] == 1) {
				            write(fd, "e", 1);
				        }
				        u_flush_fd(fd);
				        continue;
				    }
				}
				else if (c == 'E')  //delayed-stop cmd
				{
					if (fio->st > 0)
					{
						DBG("--- received decoding finished cmd ---\n");
						if (fio->st != 3)
		                    fio->st = 3;

				        fio->end = 1;
				    }
				}
				else if (c == 'p')  //pause cmd
				{
				    if (fio->st == 3)
				    {
				    	DBG("--- received pause cmd ---\n");
				        audio_out_pause();
				        fio->st = 1;
				        continue;
				    }
				}
				else if (c == 'r')  //resume cmd
				{
				    if (fio->st == 1)
				    {
				    	DBG("--- received resume cmd ---\n");
				    	audio_out_resume();
				        fio->st = 3;
				    }
				}
				else if (c == 'd')  //data arrived.
				{
					if (fio->st == 2)
					{
					//	DBG("--- data arrived ---\n");
					    fio->wakeme[0] = 0;
						fio->st = 3;
					}
				}
				else if (c == 's')  //seek cmd.
				{
					if (fio->st > 0)
					{
						DBG("--- aout:received seek cmd ---\n");
						if (fio->st != 1)
					    {
						    audio_out_pause();
						    fio->st = 1;
						}
					    buffer_reset(buf);
					    write(fd, "s", 1);
					    continue;
					}
				}
			}
		}

		if (fio->st == 3)
		{
			(*output)(fio);
		}
	}

	pthread_exit(NULL);
}

////////////////////////////////////////////////////////
//  following functions being called by other thread  //
////////////////////////////////////////////////////////
#if 0
{
#endif
/* DONE */
int
aout_open(int rate, int channels)
{
    if (drv_open == 0)
    {
    	if (channels == 0) //speech codec
    	{
    	    g_channels = 2;
    	    channels = 1;
    	}
    	else
    	{
    	    g_channels = channels;
    	    channels = 2;
    	}
    	
    	if (audio_out_open(rate, channels) < 0) return -1;
        if (mixer_open() == 0)
        {
            DBG("--- mixer_open() failed ---\n");
    	    audio_out_close(1);
    	    return -1;
        }
        drv_open = 1;
        write(g_af.pfd[1], "b", 1);
    }
    return 0;
}

/* DONE */
void
aout_close(int imed)
{
	char *cmd[] = {"E", "e"};
	char  c = 0;

	if (drv_open)
    {
    	g_af.wakeme[1] = 1;
        write(g_af.pfd[1], cmd[imed], 1);

        while (c != 'e')
            read(g_af.pfd[1], &c, 1);
        DBG("--- quit aout loop ---\n");
        u_flush_fd(g_af.pfd[1]);
        g_af.wakeme[1] = 0;
        buffer_reset(&g_af.buf);
        
        audio_out_close(imed);
        mixer_close();

        DBG("--- aout thread finished ---\n");
    }
    drv_open = 0;
}

/* DONE */
void
aout_pause(void)
{
	if (drv_open)
    {
        write(g_af.pfd[1], "p", 1);
    }
}

/* DONE */
void
aout_resume(void)
{
    if (drv_open)
    {
        write(g_af.pfd[1], "r", 1);
    }
}

/* DONE */
void
aout_flush(void)
{
    char c = 0;
    if (drv_open)
    {
        write(g_af.pfd[1], "s", 1);
        while (c != 's')
            read(g_af.pfd[1], &c, 1);
        DBG("--- aout flushed ---\n");
    }
}

/* DONE */
int
aout_write(char *buffer, int count)
{
    fifo_t   *fio = &g_af;
	buffer_t *buff;
	int       to_end;

    buff = &fio->buf;
	while (buff->sz - (buff->nwrites - buff->nreads) < count)
	{
		if (fifo_writer_block(fio) < 0)
		{
			DBG("--- RECEIVED finished cmd ---\n");
		    return -1;
		}
	}

	to_end = buff->sz - buff->wptr;
	if (to_end >= count)
	{
		memcpy(buff->bufbase + buff->wptr, buffer, count);
		buff->wptr += count;
		if (buff->wptr == buff->sz)
			buff->wptr = 0;
	}
	else
	{	
		memcpy(buff->bufbase + buff->wptr, buffer, to_end);
		memcpy(buff->bufbase, buffer + to_end, count - to_end);
		buff->wptr = count - to_end;
	}
	buff->nwrites += count;

	fifo_wakeup_reader(fio);
	return count;
}

/* DONE */
int
aout_write_nonblock(char *buffer, int count)
{
    fifo_t *fio = &g_af;
	buffer_t *buff;
	int  to_end;

    buff = &fio->buf;
	if (buff->sz - (buff->nwrites - buff->nreads) < count)
		return 0;

	to_end = buff->sz - buff->wptr;
	if (to_end >= count)
	{
		memcpy(buff->bufbase + buff->wptr, buffer, count);
		buff->wptr += count;
		if (buff->wptr == buff->sz)
			buff->wptr = 0;
	}
	else
	{	
		memcpy(buff->bufbase + buff->wptr, buffer, to_end);
		memcpy(buff->bufbase, buffer + to_end, count - to_end);
		buff->wptr = count - to_end;
	}
	buff->nwrites += count;

	fifo_wakeup_reader(fio);
	return count;
}

#if 0
}
#endif

/* DONE */
void
aout_Init(void)
{
	pthread_t  tid;
	pthread_attr_t  pth_attrs;

    if (fifo_init(FIFO_BUFSZ) < 0) return;

    pthread_attr_init(&pth_attrs);
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
    pthread_create(&tid, &pth_attrs, aout_loop, &g_af);
    pthread_attr_destroy(&pth_attrs);
}

