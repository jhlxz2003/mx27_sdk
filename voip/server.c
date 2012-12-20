#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "ulib.h"
#include "mxc_sdk.h"
#include "av_pkt.h"
#include "../audio/amixer.h"
#include "../audio/audio_codec.h"
#include "../video/vio.h"

#define LW_PATH         "/mnt/yaffs/leaveword"
#define AUD_SND_BLKSZ   (18*80)
#define TCP_SERVER_PORT  6600
#define TCP_SND_BUFSZ   (64*1024)

#define AUD_PKT_SZ      2048

#define CMD_LEN         5


typedef struct _aud_apkt {
	unsigned short len;
	char buff[320 + HDR_SZ];
} aud_pkt_t;

typedef struct _session {
	struct list_hdr list;
	struct event  ev;
	int    type;   //0:remote client,1:localhost client(recording).
	int    fd[2];
	int    req_av; //media type.
	int    st;

	struct timeval  tv;
	char   buffer[CMD_LEN<<1];
	int    head;
	int    tail;
} session_t;

typedef struct _ev_packet {
	struct list_hdr list;
	int   size;
	int   len;
	char *buffer;
} ev_packet_t;

typedef struct _av_evt {
	struct event  ev;
	int   st;
} av_evt_t;

typedef int (*FDFUNC)(int, char*, int);

static char   lw_fname[128];
static int    av_pfd[2];
static struct event_base *sever_ev_base = NULL;
static av_evt_t  av_evt;

////////////////////////////////////
static struct list_hdr free_sess_list = {&free_sess_list, &free_sess_list};
static struct list_hdr sess_list = {&sess_list, &sess_list};

/* added by sender thread and deleted by video decoder thread */
static struct list_hdr free_pkt_list = {&free_pkt_list, &free_pkt_list};
static pthread_mutex_t  free_plist_mtx = PTHREAD_MUTEX_INITIALIZER;

#define ev_free_plist_lock()      pthread_mutex_lock(&free_plist_mtx)
#define ev_free_plist_unlock()    pthread_mutex_unlock(&free_plist_mtx)

/* deleted by sender thread and added by video decoder thread */
static struct list_hdr  ev_pkt_list = {&ev_pkt_list, &ev_pkt_list};
static pthread_mutex_t  plist_mtx = PTHREAD_MUTEX_INITIALIZER;

#define ev_plist_lock()      pthread_mutex_lock(&plist_mtx)
#define ev_plist_unlock()    pthread_mutex_unlock(&plist_mtx)

static aud_pkt_t  aud_pkt[AUD_PKT_SZ];
static int  ridx = 0;
static int  widx = 0;

#define av_data_inform()  write(av_pfd[1], "a", 1)

static void  ev_packet_flush(void);
static void  add_av_event(av_evt_t *evt);
static void  del_av_event(av_evt_t *evt);
static int   pkt_num = 0;

static inline void
init_session(session_t *s)
{
	s->head = s->tail = 0;
	s->tv.tv_sec = s->tv.tv_usec = 0;
	s->st = 0;
	s->type = -1;
	s->req_av = 0;
}

/* DONE */
static inline session_t*
new_session(void)
{
	struct list_hdr *list;
	session_t *s = NULL;

	if (!u_list_empty(&free_sess_list)) {
		list = free_sess_list.next;
		u_list_del(list);
		s = (session_t *)list;
	} else {
		s = calloc(1, sizeof(session_t));
		if (s) {
			U_INIT_LIST_HEAD(&s->list);
		}
	}

	if (s) {
		init_session(s);
	}

	return s;
}

/* DONE */
static inline void
free_session(session_t *s)
{
	memset(s, 0, sizeof(session_t));
	U_INIT_LIST_HEAD(&s->list);
	u_list_append(&s->list, &free_sess_list);
}

/* DONE */
static void
destroy_session(session_t *s)
{
//	write(s->fd[0], "#0:0$", 5);
	u_tcp_quit(s->fd[0]);
	if (s->type == 1) {
		fsync(s->fd[1]);
		close(s->fd[1]);
	}

	if (!u_list_empty(&s->list)) {
		u_list_del(&s->list);
	}

	if (u_list_empty(&sess_list)) {
		del_av_event(&av_evt);
		stop_audio_encoder();
		if (s->type == 0) {
//			mxc_mixer_set_aec(0);
//			mxc_mixer_set_lec(0);
		}
		DBG("--- reset audio buffer ---\n");
		ridx = widx = 0;

		stop_video_encoder();
		DBG("--- reset video buffer ---\n");
		ev_packet_flush();
	}
	free_session(s);
	DBG("---- destroy_session, video packet num = %d ---\n", pkt_num);
}

////////////////////////////////////

/* DONE */
static void
mk_filename(char *buf)
{
	struct tm _tm;
	time_t  nowtm;
	char    url[64];

	memset(buf, '\0', 128);
	memset(url, '\0', 64);

	time(&nowtm);
	localtime_r(&nowtm, &_tm);
	sprintf(url, "%4d%02d%02d%02d%02d%02d.mp4", _tm.tm_year+1900, _tm.tm_mon+1, _tm.tm_mday, _tm.tm_hour, _tm.tm_min, _tm.tm_sec);
	sprintf(buf, "%s/%s", LW_PATH, url);
}

/* DONE */
static int
verify_path(char *path)
{
	if (access(path, F_OK) < 0)
	{
		if (mkdir(path, 0777) < 0)
		{
			return -1;
		}
	}
	return 0;
}

/* DONE */
static int
prepare_record(void)
{
	if (verify_path(LW_PATH) < 0) return -1;
	mk_filename(lw_fname);
	return open(lw_fname, O_WRONLY|O_CREAT);
}
////////////////////////////////////

static ev_packet_t *
_ev_packet_new(int size)
{
	ev_packet_t *pkt = NULL;
	struct list_hdr *list;
	int found = 0;

	ev_free_plist_lock();
	list = free_pkt_list.next;
	while (list != &free_pkt_list) {
		pkt = (ev_packet_t*)list;
		if (pkt->size >= size + HDR_SZ) {
			u_list_del(list);
			found = 1;
			break;
		}
		list = list->next;
	}
	ev_free_plist_unlock();
	if (found == 1) return pkt;

	pkt = malloc(sizeof(ev_packet_t));
	if (pkt) {
		memset(pkt, 0, sizeof(ev_packet_t));
		U_INIT_LIST_HEAD(&pkt->list);
		pkt->buffer = malloc(size + HDR_SZ);
		if (pkt->buffer) {
			memset(pkt->buffer, 0, size + HDR_SZ);
			pkt->size = size + HDR_SZ;
		} else {
			free(pkt);
			pkt = NULL;
		}
		pkt_num++;
	}
	return pkt;
}

/* called by sender thread */
static inline void
ev_packet_free(ev_packet_t *pkt)
{
	pkt->len = 0;
	
	ev_free_plist_lock();
	u_list_append(&pkt->list, &free_pkt_list);
	ev_free_plist_unlock();
}


static inline ev_packet_t *
ev_packet_get(void)
{
	struct list_hdr *list;
	ev_packet_t *pkt = NULL;
	
	ev_plist_lock();
	if (!u_list_empty(&ev_pkt_list)) {
		list = ev_pkt_list.next;
		u_list_del(list);
		pkt = (ev_packet_t *)list;
	}
	ev_plist_unlock();
	return pkt;
}

static void
ev_packet_flush(void)
{
	struct list_hdr *list;
	ev_packet_t *pkt;

	list = ev_pkt_list.next;
	while (list != &ev_pkt_list) {
		pkt = (ev_packet_t *)list;
		list = list->next;
		
		pkt->len = 0;
		u_list_del(&pkt->list);
		u_list_append(&pkt->list, &free_pkt_list);
	}
}

//////////////////////////////////////
static int
tcp_send(int fd, char *buf, int n, int blksz)
{
	int  sendLen;
	int  totalLen = n;
	char *ptr = buf;

	while (totalLen > 0)
	{
		while (totalLen > blksz)
		{
			sendLen = u_writen(fd, ptr, blksz);
			if (sendLen <= 0) return -1;
			totalLen -= sendLen;
			ptr += sendLen;
		}

		sendLen = u_writen(fd, ptr, totalLen);
		if (sendLen <= 0) return -1;
		totalLen -= sendLen;
		ptr += sendLen;
	}
	return n;
}

static int
net_send(int fd, char *buf, int n)
{
	return tcp_send(fd, buf, n, AUD_SND_BLKSZ);
}

static int
fwriten(int fd, char *vptr, int n)
{
	int nleft;
	int nwrite;
	char  *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0)
	{
		if ((nwrite = write(fd, ptr, nleft)) <= 0)
		{
			return (-1);
		}

		nleft -= nwrite;
		ptr   += nwrite;
	}

	return (n);
}

static FDFUNC   handle_data[] = {net_send, fwriten};

/* DONE */
static void
send_packet(ev_packet_t *pkt)
{
	session_t *s;
	struct list_hdr *list;

	list = sess_list.next;
	while (list != &sess_list) {
		s = (session_t*)list;
		if (s->req_av & 0x02) {
			if (handle_data[s->type](s->fd[1], pkt->buffer, pkt->len) < 0) {
				DBG("--- SENDER video data failed ---\n");
			} else {
				DBG("--- 1. SENDER: video data size = %d ---\n", pkt->len);
			}
		}
		list = list->next;
	}
}

/* DONE */
static void
add_conn_ev(session_t *s, void *cb, struct timeval *tv)
{
	event_set(&s->ev, s->fd[0], EV_READ, cb, s);
	event_base_set(sever_ev_base, &s->ev);
	event_add(&s->ev, tv);
}

/* DONE */
static void
handle_cmd(int fd, short event, void *arg)
{
	session_t *s = arg;
	char *p = NULL;
	int   n;
	char *ptr;
	int   type;

	if (event == EV_TIMEOUT) {
		DBG("--- timeout: setup cmd not received ---\n");
		destroy_session(s);
		return;
	}

	/////////////////////////////////////////////
	//         whatever session state          //
	/////////////////////////////////////////////
	ptr = &s->buffer[s->tail];
	n = read(fd, ptr, CMD_LEN - s->tail);
	if (n <= 0) { //network error
		DBG("--- read error: peer shutdown ---\n");
		destroy_session(s);
		return;
	}

	if (n < CMD_LEN - s->tail) { //not finish receiving cmd
		s->tail += n;
		s->tv.tv_sec = 10;
		s->tv.tv_usec = 0;
		add_conn_ev(s, (void*)handle_cmd, &s->tv);
		return;
	}

	n += s->tail;

	/* finish receiving cmd */
	s->head = s->tail = 0;

	DBG("--- received cmd: %s ---\n", s->buffer);

	/* should consider the situation: chaos/disturbed cmd. */
	p = memchr(s->buffer, '#', n);
	if (p == NULL) { /* chaos/disturbed cmd. */
		goto err_cmd;
	}
	++p;
	if (!memrchr(p, '$', n - 1)) { /* chaos/disturbed cmd. */
		goto err_cmd;
	}

	type = strtol(p, &ptr, 10);
	if (type == 0&&ptr == p) { /* chaos/disturbed cmd. */
		goto err_cmd;
	}

	ptr++;

	n = strtol(ptr, &p, 10);
	if (ptr == p) { /* chaos/disturbed cmd. */
		goto err_cmd;
	}

	if (n == 0) { /* finish vod */
		DBG("--- finish vod ---\n");
		destroy_session(s);
		return;
	}

/*	
	if (type == 2) {
		printf("--- capture audio cmd from remote ---\n");
		start_audio_encoder();
		destroy_session(s);
		return;
	}
*/
	if (s->st == 0) {
		if (type == 1) {
			int rfd;
			rfd = prepare_record();
			if (rfd < 0) {
				DBG("--- prepare_record() error ---\n");
				destroy_session(s);
				return;
			}
			s->fd[1] = rfd;
		} else {
			s->fd[1] = fd;
		}

		s->type = type;
		s->req_av = n;

		if (u_list_empty(&sess_list)) {
			add_av_event(&av_evt);
		}

		if (n&0x01) {
			if (s->type == 0) {
//				mxc_mixer_set_aec(1);
//				mxc_mixer_set_lec(1);
			}
			if (start_audio_encoder() == 0) {
				DBG("--- start_audio_encoder failed ---\n");
				s->req_av &= ~0x01;
			} else {		
				DBG("--- start_audio_encoder ---\n");
				
			}
		}

		if (n&0x02) {
			if (start_video_encoder() == 0) {
				DBG("--- start_video_encoder failed ---\n");
				s->req_av &= ~0x02;
			} else {
				DBG("--- start_video_encoder ---\n");
			}
		}

		if (s->req_av == 0) {
			destroy_session(s);
			DBG("---  no audio and video encoder, so quit ---\n");
			return;
		}
		u_list_append(&s->list, &sess_list);
		s->st = 1;
	}else if (type == 0) {
		if ((n&0x01)&&((s->req_av&0x01) == 0)) {
			start_audio_encoder();
			s->req_av |= 0x01;
			DBG("----------------------- received new cmd: start_audio_encoder -----------------------\n");
		}

		if ((n&0x02)&&((s->req_av&0x02) == 0)) {
			start_video_encoder();
			s->req_av |= 0x02;
		}
	}
	add_conn_ev(s, (void*)handle_cmd, NULL);
	return;

err_cmd:
	if (s->st == 0) {
		DBG("---- cmd error, kick the client! ---\n");
		destroy_session(s);
	} else { // if link has setup, not be disturbed by error cmd.
		DBG("---- cmd error, ignore it! ---\n");
		add_conn_ev(s, (void*)handle_cmd, NULL);
	}
}

/* DONE */
static void
handle_conn(int fd, short event, void *arg)
{
	int  conFd = -1;
	session_t *s;
	struct sockaddr_in addr;
	int  addrsize;
#ifdef DEBUG
	char ip[16];
#endif

	addrsize = sizeof(addr);
	if ((conFd = accept(fd, (struct sockaddr *)&addr, &addrsize)) < 0) {
		DBG("--- accept error ---\n");
		return;
	}

#ifdef DEBUG
	memset(ip, '\0', 16);
	inet_ntop(AF_INET, &addr.sin_addr, ip, 16);
	printf("--- media server connected from: %s:%d ---\n", ip, ntohs(addr.sin_port));
#endif

	s = new_session();
	if (s)
	{
		u_set_nonblock(conFd);
		u_tcp_set_nodelay(conFd);
		u_sock_set_sndbuf_sz(conFd, 640*1024);
		s->fd[0] = conFd;
		s->tv.tv_sec = 5;
		s->tv.tv_usec = 0;
		s->st = 0;
		add_conn_ev(s, (void*)handle_cmd, &s->tv);
		DBG("--- new session connected ---\n");
	}
	else
	{
		DBG("*** new_session failed! ***\n");
		u_tcp_quit(conFd);
	}
}

/* get av data and send */
static void
handle_av_data(int fd, short event, void *arg)
{
	ev_packet_t *pkt;
	int  ret = 3;

	u_flush_fd(fd);
	while (ret) {
		pkt = ev_packet_get(); //get encoded video data.
		if (pkt) {
			send_packet(pkt);
			ev_packet_free(pkt);
//			DBG("--- video data coming ---\n");
		} else {
//			DBG("--- no video data ---\n");
			ret &= ~0x02;
		}

		if (ridx != widx) {  //get encoded audio data.
			session_t *s;
			struct list_hdr *list;
			aud_pkt_t  *apkt;

			apkt = &aud_pkt[ridx];
			list = sess_list.next;
			while (list != &sess_list)
			{
				s = (session_t*)list;
				if (s->req_av & 0x01) {
					handle_data[s->type](s->fd[1], apkt->buff, apkt->len);
				}
				list = list->next;
			}
			ridx = (ridx + 1)&(AUD_PKT_SZ - 1);
//			DBG("--- audio data coming ---\n");
		} else {
			ret &= ~0x01;
//			DBG("--- no audio data ---\n");
		}
	}
}

static void
add_av_event(av_evt_t *evt)
{
	if (evt->st == 0) {
		DBG("---- add av data event ---\n");
		event_set(&evt->ev, av_pfd[0], EV_READ|EV_PERSIST, handle_av_data, evt);
		event_base_set(sever_ev_base, &evt->ev);
		event_add(&evt->ev, NULL);
		evt->st = 1;
	}
}

static void
del_av_event(av_evt_t *evt)
{
	if (evt->st == 1) {
		int fd;
		fd = EVENT_FD(&evt->ev);
		u_flush_fd(fd);
		event_del(&evt->ev);
		evt->st = 0;
		DBG("---- delete av data event ---\n");
	}
}

/* DONE */
static void*
server_ev_loop(void *arg)
{
	int  listenFd = -1;
	struct event  listen_ev;

	pthread_detach(pthread_self());
	av_evt.st = 0;
	DBG("********* media server pid = %d *********\n", getpid());
	listenFd = u_tcp_serv(NULL, TCP_SERVER_PORT);
	if (listenFd < 0)
	{
		DBG("--- u_tcp_serv() ---\n");
		pthread_exit(NULL);
	}

	u_set_nonblock(listenFd);

	sever_ev_base = event_base_new();
	event_set(&listen_ev, listenFd, EV_READ|EV_PERSIST, handle_conn, &listen_ev);
	event_base_set(sever_ev_base, &listen_ev);
	event_add(&listen_ev, NULL);

	event_base_dispatch(sever_ev_base);

	event_base_free(sever_ev_base);
	close(listenFd);

	pthread_exit(NULL);
}

////////////////////////////////////////////

/* called by video encoder thread */
void
ev_packet_new(char *buffer, int size)
{
	ev_packet_t *pkt;
	AVPacket *av_pkt;

	if ((pkt = _ev_packet_new(size)) == NULL) return;
	av_pkt = (AVPacket *)pkt->buffer;
	av_pkt->len = size;
	av_pkt->mt = 1;
	memcpy(pkt->buffer + HDR_SZ, buffer, size);
	pkt->len = size + HDR_SZ;
	
	ev_plist_lock();
	u_list_append(&pkt->list, &ev_pkt_list);
	ev_plist_unlock();
	av_data_inform();
}

/* called by audio encoder thread */
int
audio_packet_put(char *buffer, int size)
{
	aud_pkt_t *pkt;
	AVPacket  *av_pkt;

	if (((widx + 1)&(AUD_PKT_SZ - 1)) == ridx) return -1;
//	DBG("--- audio packet size = %d ---\n", size);
	pkt = &aud_pkt[widx];
	pkt->len = size + HDR_SZ;
	av_pkt = (AVPacket *)pkt->buff;
	av_pkt->mt = 0;
	av_pkt->len = size;
	memcpy(pkt->buff + HDR_SZ, buffer, size);
	widx = (widx + 1)&(AUD_PKT_SZ - 1);
	av_data_inform();

	return 0;
}

/////////////////////////////////////////////
static int recFd = -1;

/* type: 1: audio, 2: video, 3: audio+video */
int
mxc_leaveword_start(int cam, int type)
{
	int n;
	char buf[8];

	WS_camera_select(cam);
	if ((recFd = u_connect_nonb("127.0.0.1", TCP_SERVER_PORT, 3)) < 0)
	{
		DBG("tcp connect failed\n");
		return -1;
	}
#ifdef DEBUG
	{
		char *str[] = {"ÁôÑÔ", "Â¼Ïñ", "ÁôÑÔÂ¼Ïñ"};
		printf("-- ¿ªÊ¼: %s ---\n", str[type-1]);
	}
#endif
	n = sprintf(buf, "#1:%d$", type);
	return write(recFd, buf, n);
}

void
mxc_leaveword_stop(void)
{
	char buf[16];

	if (recFd >= 0)
	{
		printf("-- Í£Ö¹ÁôÑÔÂ¼Ïñ ---\n");
		write(recFd, "#1:0$", 5);
		read(recFd, buf, 5);
		u_tcp_quit(recFd);
		recFd = -1;
		printf("-- leaveword stopped ---\n");
	}
}

char*
mxc_get_leaveword_fname(void)
{
	return lw_fname;
}

/* called when system started */
void
start_media_server(void)
{
	pthread_t  tid;

	pipe(av_pfd);
	pthread_create(&tid, NULL, server_ev_loop, (void*)NULL);
}


