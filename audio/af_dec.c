#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "dbg.h"
#include "ulib.h"
#include "af_dec.h"
#include "../util/thread.h"

typedef struct _dec_ctl
{
	af_info_t      *info;
	int             pfd[2];
	int             stat;

    af_decoder_t   *af_head;
    af_decoder_t   *af_tail;
    af_decoder_t   *af_curr;

	void  (*notify_stop) (void *);
	void   *data;
	void   *thread;
} dec_ctl_t;

static dec_ctl_t  g_af_dc;

static int  seek_tm;

/* DONE */
static int
set_media_type(dec_ctl_t *dc, char *name)
{
    af_decoder_t *af;
    char *ptr;
    int   type;
    
    if ((ptr = strrchr(name, '.')) == NULL) return -1;

    ptr++;
    if (strcmp(ptr, "mp3") == 0)
        type = 0;
    else if (strcmp(ptr, "wav") == 0)
        type = 1;
    else
        return -1;

    if (dc->af_curr == NULL||dc->af_curr->type != type)
    {
        af = dc->af_head;
        while (af)
        {
            if (af->type == type)
                break;
            else
                af = af->next;
        }

        if (af)
        {
            dc->af_curr = af;
        }
        else
        {
            DBG("--- not support the format ---\n");
            return -1;
        }
    }
    return type;
}

//////////////////////////////////////////////////////////
#define PAUSE_PLAY      1
#define RESUME_PLAY     2
#define SEEK_PLAY       3

static thread_t  af_thr;

static int
af_get_cmd(dec_ctl_t *dc)
{
	char c;
	int  ret = 0;
	if (fd_can_read(dc->pfd[0], 1)) {
		if (read(dc->pfd[0], &c, 1) == 1) {
			switch (c)
			{
			case 'p':
				ret = PAUSE_PLAY;
				break;
			case 'r':
				ret = RESUME_PLAY;
				break;
			case 's':
				ret = SEEK_PLAY;
				break;
			default:
				break;
			}
		}
	}
	return ret;
}

static int
pause_loop(dec_ctl_t *dc)
{
	int cmd;
	int err;
	int ret = 0;
	thread_t  *thr = dc->thread;

	dc->af_curr->pause();
	THREAD_LOOP(thr)
	{
		err = dc->af_curr->play[1]();
		if (err == 0) {
			dc->af_curr->seek_end(0);
		} else if (err < 0) {
			ret = -1;
			break;
		}

		cmd = af_get_cmd(dc);
		if (cmd == RESUME_PLAY) {
			dc->af_curr->resume();
			break;
		} else if (cmd == SEEK_PLAY) {
			dc->af_curr->seek(seek_tm, 1);
		}
		u_msleep(10);
	}
	return ret;
}

static void
af_main(void *arg)
{
	dec_ctl_t *dc;
	int  cmd;
	int  err;
	int  st = 0;
	thread_t  *thr = arg;
	
	dc = thread_priv_data(thr);
	
	if (dc->af_curr->open(dc->info) != 0) {
		DBG("--- music decoder open failed ---\n");
		thread_run_quit(thr);
		return;
	}
	st = 0;
	thread_run_prepared(thr);
	DBG("--- entering decoding loop ---\n");
	u_flush_fd(dc->pfd[0]);
	THREAD_LOOP(thr)
	{
		err = dc->af_curr->play[st]();
		if (err < 0) {
			break;
		} else if ((st == 1)&&(err == 0)) {
			dc->af_curr->seek_end(1);
		    st = 0;
		}

		err = 0;
		cmd = af_get_cmd(dc);
		switch (cmd)
		{
		case PAUSE_PLAY:
//			DBG("--- received pause cmd ---\n");
			err = pause_loop(dc);
			break;
		case SEEK_PLAY:
//			DBG("--- received seek cmd ---\n");
			dc->af_curr->seek(seek_tm, 1);
			st = 1;
			break;
		default:
			break;
		}
		if (err < 0) break;
//		u_msleep(0);
	}
	DBG("--- leaving decoding loop ---\n");
	err = (err == -2)?0:1;
	dc->af_curr->close(err);
	thread_run_quit(thr);
	if (dc->notify_stop) {
		dc->notify_stop(dc->data);
		dc->notify_stop = NULL;
	}
	DBG("--- quit decoding loop ---\n");
}

void
register_af_decoder(af_decoder_t *dec)
{
	dec_ctl_t *dc = &g_af_dc;

	dec->next = NULL;
	if (dc->af_head == NULL)
	{
		dc->af_head = dec;
		dc->af_tail = dec;
	}
	else
	{
		dc->af_tail->next = dec;
		dc->af_tail = dec;
	}
}

/* DONE */
void
af_DecSeek(int tm)
{
    dec_ctl_t *dc = &g_af_dc;

    THREAD_IS_RUNNING(&af_thr)
    seek_tm = tm;
	write(dc->pfd[1], "s", 1);
	THREAD_END(&af_thr)
}

/* DONE */
void
af_DecPause(void)
{
	dec_ctl_t *dc = &g_af_dc;
	
	THREAD_IS_RUNNING(&af_thr)
	write(dc->pfd[1], "p", 1);
	THREAD_END(&af_thr)
}

/* DONE */
void
af_DecResume(void)
{
	dec_ctl_t *dc = &g_af_dc;
	
	THREAD_IS_RUNNING(&af_thr)
	write(dc->pfd[1], "r", 1);
	THREAD_END(&af_thr)
}

/* DONE */
void
af_StopDec(void)
{
	DBG("--- TRYING TO STOP PLAYING MUSIC ---\n");
	suspend_thread(&af_thr);
	DBG("--- MUSIC PLAYING STOPPED ---\n");
}

void
af_DecRegStopCallback(void *fn, void *d)
{
	dec_ctl_t *dec = &g_af_dc;
	dec->notify_stop = fn;
	dec->data = d;
}

static int
af_prepare(void)
{
	dec_ctl_t *dc = &g_af_dc;
	
	DBG("--- start playing %s ---\n", dc->info->path);
	if (strlen(dc->info->path) == 0||access(dc->info->path, F_OK) < 0)
	{
		DBG("--- %s not exists ---\n", dc->info->path);
		return -1;
	}
	    
	if (set_media_type(dc, dc->info->path) < 0)
	{
		DBG("--- decoder not exists ---\n");
		return -1;
	}
	return 0;
}

int
af_StartDecFile(af_info_t *info)
{
	int ret = 0;
	dec_ctl_t *dc = &g_af_dc;

	if (dc->af_head == NULL) {
		DBG("--- decoder not exists ---\n");
		return 0;
	}
	
	dc->info = info;

	ret = run_thread(&af_thr);
#ifdef DEBUG
	if (ret == 0) {
		DBG("--- MUSIC PLAYING START FAILED ---\n");
	} else {
		DBG("--- MUSIC PLAYING START SUCCESS ---\n");
	}
#endif
	return ret - 1;
}

int
af_decoder_init(void)
{
	dec_ctl_t *dc = &g_af_dc;

    memset(dc, 0, sizeof(dec_ctl_t));
	if (pipe(dc->pfd) < 0) return -1;

    dc->af_head = NULL;
    dc->af_tail = NULL;
    dc->af_curr = NULL;

    dc->af_curr = load_mp3_module();
    load_wav_module();
    
    dc->notify_stop = NULL;
	dc->data = NULL;
	dc->thread = &af_thr;

	return new_thread(&af_thr, af_main, dc, NULL, af_prepare);
}
