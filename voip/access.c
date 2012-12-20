#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <pjlib.h>

#include "ulib.h"
#include "mxc_sdk.h"
#include "av_pkt.h"
#include "../audio/mxc_alsa.h"
#include "../audio/audio_codec.h"
#include "../audio/amixer.h"
#include "../video/vio.h"
#include "../util/thread.h"

#define TCP_SERVER_PORT    6600
#define RBUF_SZ           (640*1024)

typedef struct _acc {
	char serip[128];
	int  mt;
	int  proto;
	int  pfd[2];
} acc_t;

static char  buffer[RBUF_SZ];

static AVPacket pkt = {0, 0};
static int  pkt_mt = -1;
static int  pkt_len = 0;

static void (*over_cb)(void*);
static void *g_data;

static unsigned int  preload_v = 0;

static acc_t  g_acc;

static void
play_video(char *buf, int size)
{
	if (preload_v < 2) {
		mxc_video_decode_buffer_fill(buf, size);
	} else if (preload_v == 2) {
		mxc_video_decode_start();
		mxc_video_decode_buffer_fill(buf, size);
//	} else if (preload_v < 5) {
//		mxc_video_decode_buffer_fill(buf, size);
	} else {
		mxc_video_decode_frame(buf, size);
	}
	preload_v++;
}

/////////////////////////////////////////////////
void
mxc_vod_callback(void *f, void *d)
{
	over_cb = f;
	g_data = d;
}

/////////////////////////////////////////////////

static thread_t  acc_thr;

static int  tmp_pr;
static int  tmp_mt;
static char tmp_ip[128];

static void
access_main(void* arg)
{
	int   mt;
	int   access_flag;
	int   n;
	int   fd;
	int   af = 1;
	thread_t *thr = arg;
	acc_t *acc;
	
	acc = thread_priv_data(thr);
	if (acc->proto == 0) {
		if ((fd = u_connect_nonb(acc->serip, TCP_SERVER_PORT, 5)) < 0) {
			DBG("--- tcp connect %s failed ---\n", acc->serip);
			thread_run_quit(thr);
			return;
		}
		DBG("--- access network media: %s ---\n", acc->serip);
	} else {
		if ((fd = open(acc->serip, O_RDONLY)) < 0) {
			DBG("---- open %s failed ---\n", acc->serip);
			thread_run_quit(thr);
			return;
		}
		DBG("--- access local media: %s ---\n", acc->serip);
	}

	mt = acc->mt;
	access_flag = acc->proto;

	mxc_video_decode_open();
	if ((mt&0x01) == 1) {
		audio_decoder_prepare();
		DBG("--- audio_decoder_prepared ---\n");
	}

	if (access_flag == 0) {
		char buf[8];
		n = sprintf(buf, "#0:%d$", mt);
		DBG("--- send %s cmd ---\n", buf);
		write(fd, buf, n);
		u_sock_set_rcvbuf_sz(fd, 640*1024);
	}

	DBG("--- entering play loop ---\n");
	thread_run_prepared(thr);
	preload_v = 0;
	THREAD_LOOP(thr)
	{
		if (fd_can_read(acc->pfd[0], 0)) {
			char c;
			read(acc->pfd[0], &c, 1);
			if (c == 'a'&&(mt&0x01) == 0) {
				audio_decoder_prepare();
				mt |= 0x01;
				af = 0;
				write(fd, "#0:1$", 5);
				DBG("--- audio_decoder_prepared ---\n");
			}
		}

		pkt_len = 0;
		pkt_mt = -1;
		n = 0;
		if (fd_can_read(fd, 10)) {
			n = u_readn(fd, &pkt, HDR_SZ);
			if (n <= 0) break;
			pkt_mt = pkt.mt;
			pkt_len = pkt.len;
		}
		
		if (pkt_len > 0&&fd_can_read(fd, 10)) {
			n = u_readn(fd, buffer, pkt_len);
			if (n <= 0) break;
		}

		if ((pkt_mt == 1||pkt_mt == 0)&&(n > 0)) {
			if (pkt_mt == 0) { //play audio
				if (((mt&0x01) == 0)&&af == 1) {
					printf("---- audio_decoder_prepare() ----\n");
					audio_decoder_prepare();
					mt |= 0x01;
					af = 0;
				}
				mxc_play_audio(buffer, n);
//				DBG("--- audio data size = %d ---\n", n);
			} else { //play video
//				DBG("--- video data size = %d ---\n", n);
				play_video(buffer, n);
			}
		}
	}
	if (access_flag == 0) {
		write(fd, "#0:0$", 5);
		u_tcp_quit(fd);
	} else {
		close(fd);
	}

	if (mt&0x01) {
		audio_decoder_finish();
	}
	mxc_video_decode_stop();
	u_flush_fd(acc->pfd[0]);

	if (over_cb)
		(*over_cb)(g_data);
}

static int
prepare_vod(void)
{
	strcpy(g_acc.serip, tmp_ip);
	g_acc.mt = tmp_mt;
	g_acc.proto = tmp_pr;
	return 0;
}

/* type: 1: audio, 2: video, 3: audio+video */
/* proto: 0-access network, 1-access local file */
int
start_vod(char *ip, int mt, int proto)
{
#ifdef DEBUG
	int ret;
#endif
	memset(tmp_ip, 0, 128);
	strcpy(tmp_ip, ip);
	tmp_mt = mt;
	tmp_pr = proto;
#ifndef DEBUG
	return run_thread(&acc_thr);
#else
	ret = run_thread(&acc_thr);
	if (ret == 0) {
		DBG("--- ACCESS START FAILED ---\n");
	} else {
		DBG("--- ACCESS START SUCCESS ---\n");
	}
	return ret;
#endif
}

/* DONE */
void
stop_vod(void)
{
	suspend_thread(&acc_thr);
}

/* DONE */
void
mxc_media_init(void)
{
	memset(&g_acc, 0, sizeof(acc_t));
	pipe(g_acc.pfd);
	u_set_nonblock(g_acc.pfd[0]);
	u_set_nonblock(g_acc.pfd[1]);
	new_thread(&acc_thr, access_main, &g_acc, "access client", prepare_vod);
}

void
mxc_audio_on(void)
{
	THREAD_IS_RUNNING(&acc_thr)
	write(g_acc.pfd[1], "a", 1);
	THREAD_END(&acc_thr)
	
}


