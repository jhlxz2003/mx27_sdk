#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <linux/types.h>

#include "mxc_alsa.h"
#include "dbg.h"
#include "../util/thread.h"

#define  RBUF_LEN     (640*1024)

#ifdef DEBUG
static volatile int   audio_drop;
#endif

static alsa_t         g_ai;
static thread_t       ai_thr;
static int            ai_rate = 0;
static unsigned char  ringbuffer[RBUF_LEN];
static volatile int   head;
static volatile int   tail;
static int            bufarr_sz;

///////////////////////////////////////////////
static void
ai_main(void *arg)
{
	thread_t *thr = arg;
	int       n;
#ifdef DEBUG
	int  total = 0;
#endif

	if (ai_open(&g_ai, 1, ai_rate, 1) < 0) {
		DBG("--- open audio capture device failed ---\n");
		thread_run_quit(thr);
		return;
	}
	bufarr_sz = RBUF_LEN/g_ai.blocksize;

	head = 0;
	tail = 0;
#ifdef DEBUG
	audio_drop = 0;
#endif
	thread_run_prepared(thr);

	ai_start_capture(&g_ai);
	DBG("--- Start Audio capture ---\n");
	THREAD_LOOP(thr)
	{
		if (ai_read(&g_ai, ringbuffer + tail * g_ai.blocksize) < 0) {
			u_msleep(20);
			continue;
		}
		
		n = (tail + 1) % bufarr_sz;
		if (n == head) {
        #ifdef DEBUG
			DBG("\n--- too bad - dropping audio frame, total = %d ! ---\n", ++audio_drop);
        #endif
		} else {
			tail = n;
		}
	}
	ai_close(&g_ai);
	DBG("--- Stop Audio capture ---\n");
}

/* DONE: called by audio encoder thread. */
int
audio_frame_ptr(char **ptr)
{
	if (head == tail) return 0;

	*ptr = (char *)ringbuffer + head * g_ai.blocksize;
	return g_ai.blocksize;
}

/* DONE: called by audio encoder thread. */
void
audio_frame_drain(void)
{
	head = (head + 1) % bufarr_sz;
}

/* DONE: called by audio encoder thread. */
void
audio_grabber_stop(void)
{
	DBG("--- TRY TO STOP AUDIO GRABBER ---\n");
	suspend_thread(&ai_thr);
	DBG("--- AUDIO GRABBER STOPPED ---\n");
}

int
audio_grabber_start(int rate)
{
	ai_rate = rate;
#ifndef DEBUG
	return run_thread(&ai_thr);
#else
	int  ret;
	ret = run_thread(&ai_thr);
	if (ret == 0) {
		DBG("--- AUDIO CAPTURE START FAILED ---\n");
	} else {
		DBG("--- AUDIO CAPTURE START SUCCESS ---\n");
	}
	return ret;
#endif
}

/* DONE: called when system started. */
void
init_audio_grabber(void)
{
	new_thread(&ai_thr, ai_main, NULL, NULL, NULL);
}

