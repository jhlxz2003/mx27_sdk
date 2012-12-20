#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "mxc_sdk.h"
#include "ulib.h"
#include "call.h"
#include "proto.h"
#include "access.h"
#include "../video/vio.h"

#define  CALLER_CONNECTED     1
#define  CALLER_REQ_SND       2
#define  CALLER_REQ_ACK       3
#define  CALLER_RING          4
#define  CALLER_LW_QRY        5
#define  CALLER_LW_CFM        6
#define  CALLER_ACCEPT        7
#define  CALLER_MUTE          8

#define  CALLEE_SND_ACK      10
#define  CALLEE_RING         11
#define  CALLEE_ASK_LW       12
#define  CALLEE_START_LW     13
#define  CALLEE_ACCSND       14
#define  CALLEE_ACCEPT       15
#define  CALLEE_MUTE         16
#define  CALLEE_RELAY        17

#define  lcd_x       22
#define  lcd_y       133
#define  lcd_w       320
#define  lcd_h       240


call_t call;

//extern int  has_slaves;

int  callee_type = 0;

//static char  msessid[40];
static int   thread_st = 0;
static int   sockfd = -1;
static int   call_pfd[2] = {-1, -1};
static char *vip = NULL;

static pthread_mutex_t   call_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    call_cond = PTHREAD_COND_INITIALIZER;

#define  call_P()    pthread_cond_wait(&call_cond, &call_mutex)
#define  call_V(st)    \
do{ \
	thread_st = (st); \
	pthread_cond_signal(&call_cond); \
} while (0)

static int    handle_caller(int fd, char* buf, int n);
static int    caller_pipe_fn(char c);
static int    caller_tmeo_fn();

static int    handle_callee(int fd, char* buf, int n);
static int    callee_pipe_fn(char c);
static int    callee_tmeo_fn();

//static int    get_peer_info(char *ip, unsigned short *prt);

static call_fn_t   caller_fn = {
	handle_caller,
	caller_pipe_fn,
	caller_tmeo_fn
	};

static call_fn_t   callee_fn = {
	handle_callee,
	callee_pipe_fn,
	callee_tmeo_fn
	};

static tmeo_t   call_tmeo = {&call.timerid, MAX_TIMEOUT};

#define clear_session_state()  memset(&call, 0, sizeof(call_t))
#define call_lock()  pthread_mutex_lock(&call_mutex)

PVOID   fn_hup[2] = {NULL};
PVOID   fn_acc[2] = {NULL};
PVOID   fn_unlock[2] = {NULL};
PVOID   fn_sft[2] = {NULL};

PVOIDINT    fn_mute[2] = {NULL};
PVOIDINT    fn_cam[2] = {NULL};
PVOIDINT    fn_record[2] = {NULL};

static PINT fn_valid = NULL;

typedef int (*GETIP)(unsigned int, int, char*);

static GETIP  get_ip = NULL;

typedef int (*PINTVOID)(void);
static PINTVOID  win_showed_cb = NULL;

PVOIDINT  fn_set_nr = NULL;
PVOIDINT  ring_cb = NULL;
PVOID     show_from_outside = NULL;
PVOID     show_from_inside = NULL;
PVOID     last_ui = NULL;

PVOID     fn_prepare = NULL;
PVOID     fn_finish = NULL;

////////////////////////////////////////
//      notify callee UI thread       //
////////////////////////////////////////
PVOID  notify_nv = NULL;
PVOID  notify_nv_rec = NULL;
PVOID  notify_acc_callee = NULL;

////////////////////////////////////////
//      notify caller UI thread       //
////////////////////////////////////////
static PVOID  notify_sft = NULL;
static PVOID  notify_deny = NULL;
static PVOID  notify_none = NULL;
static PVOID  notify_nvo = NULL;
static PVOID  notify_busy = NULL;
static PVOID  notify_acc_caller = NULL;
static PVOID  notify_reset_caller_ui = NULL;

static PVOIDINT   notify_lw = NULL;
static PVOIDINT   notify_lw_start = NULL;

unsigned int  sdk_nr;
int    sdk_hostid;

char *sdk_sgip = NULL;
char *sdk_prip = NULL;

static int dr = 0;

int
call_trylock()
{
	return pthread_mutex_trylock(&call_mutex);
}

void
call_unlock()
{
	pthread_mutex_unlock(&call_mutex);
}

/*
格式:512|[1]\r\n
说明:[1](≤16bytes)：会话ID
*/
/* DONE */
static int
snd_call_tmeout_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);

	len = sprintf(buf, "%d|%d\r\n", SsTmeoMsg, call.sess_id);
	u_writen(fd, buf, len);
	printf("--- send timedout message ---\n");
	return 0;
}

/* DONE */
static int
snd_call_hup_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", HupMsg, call.sess_id);
	u_writen(fd, buf, len);
	printf("--- send hangup message ---\n");
	return 0;
}

/* DONE */
static int
is_hup()
{
	char c;
	if (read(call_pfd[0], &c, 1) == 1&&c == 'h') return 1;
	return 0;
}

static int
recv_cmd(int fd, NET_Fn fn)
{
	static int  nSize = 0;
	int  nLen;
	char pBuff[1024];
	char szResult[1024];
	char *pHead;
	char *pTail;
	int   ret = 0;
	

	memset(pBuff, 0, 1024);
	nLen = u_read(fd, pBuff, 1024);
	if (nLen > 0)
	{
		printf("+++ pbuff:%s +++\n", pBuff);
		memcpy(&szResult[nSize], pBuff, nLen);
		nSize += nLen;
		pHead = szResult;
		pTail = strstr(pHead, "\r\n");
		while (pTail != NULL && nSize > 0) 
		{
			char szMsg[1024] = {0};
			int len = pTail - pHead + 2;
			memcpy(szMsg, pHead, len);
			nSize -= len;

			printf("+++ get message:nSize %d pHead %s  %s +++\n",nSize, pHead, szMsg);
			ret = (*fn)(fd, szMsg, len);
			if (ret < 0)
			{
				nSize = 0;
				return ret;
			}

			pHead = pTail + 2;
			pTail = strstr(pHead, "\r\n");
		}

		if (nSize > 0)
		{
			memmove(szResult, pHead, nSize);
			memset(&szResult[nSize], 0, 1024 - nSize);
		}
	}
	else
	{
		printf("+++ read error, errno = %d +++\n", errno);
		nSize = 0;
		return -1;
	}

	return ret;
}

/* called by both caller and callee, DONE */
static int
call_run(int sock_fd, int  p_rfd, call_fn_t *op, tmeo_t *tmeo)
{
	char   buf[64];
	int    ret = 0;
	char   c;
	int    maxfd;
	fd_set rset;
	struct timeval  tv;

	while (1)
	{
		memset(buf, '\0', 64);
		FD_ZERO(&rset);
		FD_SET(sock_fd, &rset);
		FD_SET(p_rfd, &rset);
		maxfd = U_MAX(p_rfd, sock_fd) + 1;
		printf("--- timeout = %d ---\n", *(tmeo->tmeo));
		if (*(tmeo->tmeo) > 0)
		{
			tv.tv_sec = *(tmeo->tmeo);
			tv.tv_usec = 0;
			ret = select(maxfd, &rset, NULL, NULL, &tv);
		}
		else if (*(tmeo->tmeo) == 0)
		{
			tv.tv_sec = tmeo->max_tmeo;
			tv.tv_usec = 0;
			ret = select(maxfd, &rset, NULL, NULL, &tv);
		}
		else if (*(tmeo->tmeo) < 0)
		{
			ret = select(maxfd, &rset, NULL, NULL, NULL);
		}

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				printf("--- select interrupted ---\n");
				continue;
			}
			else
			{
				printf("--- select error ---\n");
				return -1;
			}
		}
		else if (ret > 0)
		{
			*(tmeo->tmeo) = 0;
			if (FD_ISSET(sock_fd, &rset))
			{
			    printf("--- received net cmd ---\n");
				if ((ret = recv_cmd(sock_fd, op->nfn)) < 0) break;
			}

			if (FD_ISSET(p_rfd, &rset))
			{
			    printf("--- detect pipe cmd ---\n");
				if (u_read(p_rfd, &c, 1)==1)
				{
				    printf("--- received pipe cmd: %c ---\n", c);
					if ((ret = op->pfn(c)) < 0) break;
				}
			}
		}
		else if (ret == 0)
		{
			printf("--- timedout ---\n");
			if ((ret = op->tfn()) < 0) break;
		}
	}

	return ret;
}

#if 0
{
#endif

/*
static int
handle_mute_start()
{
	if (call.stat == CALLER_ACCEPT||call.stat == CALLEE_ACCEPT)
	{
		call.stat++;

		if (call.ivt == VIDEO_SPK)
		{
		}
		else
			stop_audio_vod();
	}
	else if (call.stat == CALLEE_RELAY)
		mute_slave(1);
	return 0;
}

static int
handle_mute_end(char *ip)
{
	if (call.stat == CALLER_MUTE||call.stat == CALLEE_MUTE)
	{
		call.stat--;
		if (call.ivt == VIDEO_SPK)
		{
		}
		else
			start_audio_vod(ip);
	}
	else if (call.stat == CALLEE_RELAY)
		mute_slave(0);
	return 0;
}

static int
snd_mute_start_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", MuteStartMsg, call.sess_id);
	return u_writen(fd, buf, len);
}

static int
snd_mute_end_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", MuteEndMsg, call.sess_id);
	return u_writen(fd, buf, len);
}
*/

//////////////////////////////////////////
/* DONE */
static void
caller_vod_cleanup()
{
	if (call.stat >= CALLER_ACCEPT)
	{
		stop_vod();
	}
}

static int
caller_snd_request(int fd)
{
	int     n;
	char  buff[16];

	memset(buff, 0, 16);
	printf("--- send request message ---\n");
	n = sprintf(buff, "%d|1|%07u|%d\r\n", ((call.ovt == 0)?CallReqMsg : CallReqNV), sdk_nr, sdk_hostid);
	if  (u_writen(fd, buff, n) < 0) return -1;
	call.stat = CALLER_REQ_SND;
	call.timerid = 5;
	return 0;
}

/* Caller, DONE */
static int
caller_handle_ack(char *buf)
{
	if (call.stat != CALLER_REQ_SND)
	{
		printf("state should be CALLER_REQ_SND, but not\n");
		return -1;
	}
	call.sess_id = strtol(buf, NULL, 10);
	call.stat = CALLER_REQ_ACK;
	call.timerid = 5;
	return 0;
}

/* Caller, DONE */
static void
caller_ring()
{
	if (ring_cb)
		(*ring_cb)(1);
	call.stat = CALLER_RING;
	call.timerid = RING_TMEO;
	printf("+++ ringing...,timeout = %d +++\n", call.timerid);
}

/* Caller, DONE */
static int
caller_handle_ring(char *buf)
{
	unsigned long sid;
	if (call.stat >= CALLER_RING)
	{
		printf("state should be CALLER_CONNECTED or CALLER_REQ_SND.\n");
		return -1;
	}

	sid = strtol(buf, NULL, 10);
	if (sid != call.sess_id)
	{
		printf("session_id not same\n");
		return -1;
	}

	caller_ring();
	return 0;
}

static int
snd_acc_ack()
{
	char  buf[32];
	int  n;

	memset(buf, 0, 32);
	n = sprintf(buf, "%d|%u\r\n", AcceptAck, call.sess_id);
	return u_writen(sockfd, buf, n);
}

static int
snd_accslv_ack()
{
	char  buf[32];
	int  n;

	memset(buf, 0, 32);
	n = sprintf(buf, "%d|%u\r\n", AccSlvAck, call.sess_id);
	return u_writen(sockfd, buf, n);
}

/* Caller, DONE */
static int
caller_handle_accept(char *buf)
{
	unsigned long sid;
	printf("--- received accept msg ---\n");
	if (call.stat < CALLER_RING)
	{
		printf("state should be greater than CALLER_RING\n");
		return -1;
	}

	sid = strtol(buf, NULL, 10);
	if (sid != call.sess_id)
	{
		printf("session_id error\n");
		return -1;
	}

	if (call.ovt == 1)
	{
		printf("+++ send_acc_ack +++\n");
		if (snd_acc_ack() <= 0)
			return -1;
	}

	if (notify_acc_caller)
		(*notify_acc_caller)();

	vip = call.peer_ip;
	if (call.ivt == VIDEO_SPK)
	{
		mxc_set_disp_area(lcd_x, lcd_y, lcd_w, lcd_h);
		if (start_vod(vip, 3, 0) < 0)
		{
			snd_call_hup_msg(sockfd);
			return -1;
		}
	}
	else
	{
		if (notify_nvo)
			(*notify_nvo)();
//		start_audio_vod(vip);
		start_vod(vip, 1, 0);
	}

	call.timerid = TALK_TMEO_I;
	call.stat = CALLER_ACCEPT;
	return 0;
}

/* stop door ring and display query UI and set lw_query state,startup 10s timer. */
/* Caller */
static int
caller_handle_asklw(char *buf)
{
	char *ptr;
	int   tmeo;
	unsigned long sid;

	if (call.stat > CALLER_RING) return -1;

	sid = strtol(buf, &ptr, 10);
	if (sid != call.sess_id)
	{
		printf("session_id error\n");
		return -1;
	}

	++ptr;
	tmeo = strtol(ptr, (char**)NULL, 10);

	if (ring_cb)
		(*ring_cb)(0);

	if (notify_lw)
		(*notify_lw)(tmeo);

	call.timerid = 60;
	call.stat = CALLER_LW_QRY;
	return 0;
}

/* Caller, DONE */
static int
caller_handle_lwstart(char *buf)
{
	char *ptr;
	int     tmeo;
	unsigned long sid;

	if (call.stat != CALLER_LW_CFM)
	{
		printf("state should be CALLER_LW_CFM\n");
		return -1;
	}

	sid = strtol(buf, &ptr, 10);
	if (sid != call.sess_id)
	{
		printf("session_id error\n");
		return -1;
	}

	++ptr;
	tmeo = strtol(ptr, (char**)NULL, 10);
	if (notify_lw_start)
		(*notify_lw_start)(tmeo);
	call.timerid = 60;
	return 0;
}

/* call function, DONE */
static int
snd_caller_lw_cfm(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", LvWrdCfmMsg, call.sess_id);
	if (u_writen(fd, buf, len) < 0)
	{
		return -1;
	}
	call.stat = CALLER_LW_CFM;
	call.timerid = 5;
	return 0;
}

static void
caller_quit()
{
	u_tcp_quit(sockfd);
	sockfd = -1;

	if (call.stat == CALLER_RING)
	{
	    printf("--- will stop ringing ---\n");
		if (ring_cb)
			(*ring_cb)(0);
	}

	caller_vod_cleanup();
}

/* Caller, DONE */
static int
handle_slave_acc(char *buf)
{
	unsigned long sid;
	char *ptr;
	char *p;
	if (call.stat < CALLER_RING)
	{
		printf("state should be greater than CALLER_RING\n");
		return -1;
	}

	sid = strtol(buf, &ptr, 10);
	if (sid != call.sess_id)
	{
		printf("session_id error\n");
		return -1;
	}

	ptr++;
	p = strchr(ptr, '\r');
	*p = '\0';
	memset(call.vod_ip, 0, 16);
	strcpy(call.vod_ip, ptr);
	printf("*** accepted slave ip = %s ***\n", call.vod_ip);
	if (ring_cb)
		(*ring_cb)(0);
	if (call.ovt == 1)
	{
		printf("*** send accepted slave ack ***\n");
		if (snd_accslv_ack() <= 0)
			return -1;
	}

	if (notify_acc_caller)
		(*notify_acc_caller)();

	vip = call.vod_ip;
	if (call.ivt == VIDEO_SPK)
	{
		mxc_set_disp_area(lcd_x, lcd_y, lcd_w, lcd_h);
		if (start_vod(vip, 3, 0) < 0)
		{
			snd_call_hup_msg(sockfd);
			return -1;
		}
	}
	else
	{
		if (notify_nvo)
			(*notify_nvo)();
//		start_audio_vod(vip);
		start_vod(vip, 1, 0);
	}

	call.timerid = TALK_TMEO_I;
	call.stat = CALLER_ACCEPT;
	return 0;
}

/* Caller, DONE */
/*
static int
handle_shift(char *buf)
{
	printf("--- received shift msg ---\n");
	if ((call.stat != CALLER_ACCEPT)&&(call.stat != CALLER_MUTE)) return -1;

//	if (call.ovt == 1)
//		tcp_audio_server_stop();
	if (call.ivt == VIDEO_SPK)
		stop_vod();
	else
		stop_audio_vod();
	call.stat = CALLER_RING;
	if (notify_sft)
		(*notify_sft)();
	if (ring_cb)
		(*ring_cb)(1);
	call.timerid = RING_TMEO;
	printf("+++ ringing..., timeout = %d +++\n", call.timerid);
	return 0;
}
*/

static int
handle_camera(int i)
{
	if (call.ivt ^ i)
	{
		call.ivt = i;
		if (call.stat >= CALLER_ACCEPT)
		{
			if (i == 0)
			{
				printf("+++ CALLEE CAMERA ON +++\n");
		//		stop_audio_vod();
				stop_vod();
				if (notify_acc_caller)
					(*notify_acc_caller)();
				if (vip)
				{
					mxc_set_disp_area(lcd_x, lcd_y, lcd_w, lcd_h);
					if (start_vod(vip, 3, 0) < 0)
					{
						snd_call_hup_msg(sockfd);
						return -1;
					}
				}
			}
			else if (i == 1)
			{
				printf("+++ CALLEE CAMERA OFF +++\n");
				stop_vod();
			}
		}
	}
	return 0;
}

/* DONE */
static int
handle_caller(int fd, char* buf, int n)
{
	int     ret = 0;
	int     cmd;
	char *ptr = NULL;

	cmd = get_cmd_type(buf, &ptr);
	ptr++;
	switch (cmd)
	{
	case BusyMsg: //has no sessionid
		if (notify_busy)
			(*notify_busy)();
		return  -2;
	case CallReqAck:
		ret = caller_handle_ack(ptr);
		break;
	case DenyMsg:
		if (notify_deny)
			(*notify_deny)();
		return -2;
		break;
	case RingMsg:
		ret = caller_handle_ring(ptr);
		break;
	case AcceptMsg:
		ret = caller_handle_accept(ptr);
		break;
	case AcceptNV:
		call.ivt = NVID_SPK;
		ret = caller_handle_accept(ptr);
		break;
	case AskLvWrdMsg:
	case DenyLmMsg:
		ret = caller_handle_asklw(ptr);
		break;
	case LvWrdStartMsg:
		ret = caller_handle_lwstart(ptr);
		break;
	case HupMsg:
	case SsTmeoMsg:
		ret = -1;
		break;
	case AccSlvMsg:
		ret = handle_slave_acc(ptr);
		break;
	case AccSlvNV:
		call.ivt = NVID_SPK;
		ret = handle_slave_acc(ptr);
		break;
	case AccSftMsg:
//		ret = handle_shift(ptr);
		break;
/*
	case MuteStartMsg:
		ret = handle_mute_start();
		break;
	case MuteEndMsg:
		ret = handle_mute_end(vip);
		break;
*/
	case CameraOn:
		ret = handle_camera(0);
		break;
	case CameraOff:
		ret = handle_camera(1);
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

static int
snd_avo_msg(int fd, int av)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", ((av == 0) ? CameraOn : CameraOff), call.sess_id);
	return u_writen(fd, buf, len);
}

/* DONE */
static int
caller_pipe_fn(char c)
{
	switch (c)
	{
	case 't':
		snd_call_tmeout_msg(sockfd);
		return -1;
	case 'h':
		printf("--- caller: hup button pressed ---\n");
		snd_call_hup_msg(sockfd);
		return -2;
	case 'c':
		if (call.stat == CALLER_ACCEPT&&call.ivt == VIDEO_SPK)
			return -1;
		else if (call.ivt == NVID_SPK)
		{
			if (notify_nvo)
				(*notify_nvo)();
			if (call.stat == CALLER_ACCEPT) {
		//		start_audio_vod(vip);
				start_vod(vip, 1, 0);
			}
		}
	case 'l':
		printf("--- caller: leaveword confirmed ---\n");
		if (call.stat == CALLER_LW_QRY)
		{
			if (ring_cb)
				(*ring_cb)(0);
			if (snd_caller_lw_cfm(sockfd) < 0)
				return -1;
		}
		break;
	/*
	case 'm':
		if (call.stat >= CALLER_ACCEPT)
		{
			if (snd_mute_start_msg(sockfd) <= 0)
				return -1;
		}
		break;
	case 'M':
		if (call.stat >= CALLER_ACCEPT)
		{
			if (snd_mute_end_msg(sockfd) <= 0)
				return -1;
		}
		break;
	*/
	case 'v': /* output video */
		if (call.ovt == 1&&call.stat > CALLER_REQ_ACK)
		{
			printf("+++ CALLER CAMERA ON +++\n");
			if (snd_avo_msg(sockfd, 0) <= 0)
				return -1;
			call.ovt = 0;
		}
		break;
	case 'V': /* output audio */
		if (call.ovt == 0&&call.stat > CALLER_REQ_ACK)
		{
			printf("+++ CALLER CAMERA OFF +++\n");
			if (snd_avo_msg(sockfd, 1) <= 0)
				return -1;
			call.ovt = 1;
		}
		break;
	default:
		break;
	}
	return 0;
}

/* DONE */
static int
caller_tmeo_fn()
{
	printf("--- call timedout ---\n");
	snd_call_tmeout_msg(sockfd);
	return -1;
}

/* DONE */
static int
caller_handle()
{
	int       n;
	int       err;
	int       fd;
	unsigned short  prt;

	/* following done */
	u_flush_fd(call_pfd[0]);

	n = (*get_ip)(call.nr, call.minor, call.peer_ip);
	if (n <= 0)
	{
		printf("the number not exists\n");
		return  EFNONR;
	}/*
	else if (n < 0)
	{
		if (get_peer_info(call.peer_ip, &prt) == 0)
		{
			printf("the number not exists\n");
			return  EFNONR;
		}
	}*/
	else if (n > 0)
	{
		prt = TLK_LISTEN_PORT;
	}

	printf("+++ target:nr-%u-%d, ip-%s, prt-%d +++\n", call.nr, call.minor, call.peer_ip, prt);	
	err = 0;
	n = 0;
	fd = u_connect_nonb(call.peer_ip, prt, 1);
	while (fd < 0&&n < 5)
	{
		if (is_hup()) return  EFOK;
		n++;
		fd = u_connect_nonb(call.peer_ip, prt, 1);
	}

	if (fd < 0||n == 5)
	{
		return  EFNONR;
	}

	sockfd = fd;
	u_tcp_set_nodelay(fd);
	u_tcp_keepalive(fd, 5, 1, 1);
	call.stat = CALLER_CONNECTED;
	if (caller_snd_request(fd) < 0)
	{
		printf("send request failed\n");
		u_tcp_quit(fd);
		sockfd = -1;
		return  EFERR;
	}

//	send_log_msg(CALLER_LOG, call.nr);

	vip = call.peer_ip;
	err = call_run(sockfd, call_pfd[0], &caller_fn, &call_tmeo);

	printf("--- caller quit ---\n");
	caller_quit();
	if (err != -2)
	{
		if (notify_reset_caller_ui)
			(*notify_reset_caller_ui)();
	}

	return 0;
}

#if 0
}
#endif

////////////////////////////////////
//       callee functions         //
////////////////////////////////////
static void
callee_set_acc()
{
	call.stat = CALLEE_ACCEPT;
	if (call.type <= 1)
	{
		call.timerid = TALK_TMEO_I;
	}
	else if (call.type > 1)
	{
		call.timerid = TALK_TMEO_O;
		DBG("--- accept timeout = %d ---\n", TALK_TMEO_O);
	}

	if (notify_acc_callee)
		(*notify_acc_callee)();
}

/* DONE */
static int
snd_accept_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	if (call.type == 1&&call.ovt == 1)
		len = sprintf(buf, "%d|%d\r\n", AcceptNV, call.sess_id);
	else
		len = sprintf(buf, "%d|%d\r\n", AcceptMsg, call.sess_id);
	return u_writen(fd, buf, len);
}

/* DONE */
static int
snd_open_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", OpenDoorMsg, call.sess_id);
	u_writen(fd, buf, len);
	printf("send open message\n");
	return 0;
}

/*
syntax:       502|[1]\r\n
description:[1](≤16bytes):session id
*/
/* call, DONE */
static int
snd_ring_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", RingMsg, call.sess_id);
	if  (u_writen(fd, buf, len) < 0)
	{
		printf("send ring msg error\n");
		return -1;
	}

	if (ring_cb&&(dr == 0)) {
		(*ring_cb)(1);
		dr = 1;
	}
/*
	if ((sdk_hostid == 0)&&(call.type != 1||call.nr != sdk_nr))
	{
		if (call.ivt == VIDEO_SPK)
			ring_slave();
		else
			ring_slave_nv();
	}
*/
	call.stat = CALLEE_RING;
	call.timerid = RING_TMEO;
	printf("--- send ring message success, timeout = %d ---\n", call.timerid);
	return 0;
}

/* CALLEE, DONE */
static int
snd_ask_leaveword(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d|%d\r\n", AskLvWrdMsg, call.sess_id, LWCFM_TMEO);
	if  (u_writen(fd, buf, len) < 0)
	{
		printf("send ask leaveword msg error\n");
		return -1;
	}
	call.stat = CALLEE_ASK_LW;
	call.timerid = LWCFM_TMEO;
	printf("send ask leaveword message\n");
	return 0;
}

/* CALLEE, DONE */
static void
snd_dn_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	len = sprintf(buf, "%d|%d\r\n", DenyMsg, call.sess_id);
	u_writen(fd, buf, len);
}

/* CALLEE, DONE */
static int
callee_handle_mode(int m)
{
	switch (m)
	{
	case NormalMode:
	case InLmMode:
		if (snd_ring_msg(sockfd) < 0) return (-1);
		break;
	case OutLmMode:
	case InDenyLmMode:
		if (snd_ask_leaveword(sockfd) < 0)
			return (-1);
		break;
	case InDenyMode:
	case OutDenyMode:
		snd_dn_msg(sockfd);
		return (-1);
	default:
		return (-1);
	}

	call.mode = m;
	return m;
}

/* CALLEE, DONE */
static int
win_showed(void)
{
	int   m = NormalMode;
/*
	int   lm;
	int   dn;

	lm = if_cur_lm();
	dn = if_cur_dn();

	if ((cur_mode == OUT_MODE)&&dn)
	{
		printf("+++ 外出免打扰模式 +++\n");
		m = OutDenyMode;
	}
	else if ((cur_mode == OUT_MODE)&&lm)
	{
		printf("+++ 外出留言模式 +++\n");
		m = OutLmMode;
	}
	else if ((cur_mode >= 0)&&(cur_mode != OUT_MODE)&&lm&&dn)
	{
		printf("+++ 免打扰留言模式 +++\n");
		m = InDenyLmMode;
	}
	else if ((cur_mode >= 0)&&(cur_mode != OUT_MODE)&&!lm&&dn)
	{
		printf("+++ 免打扰模式 +++\n");
		m = InDenyMode;
	}
	else if ((cur_mode >= 0)&&(cur_mode != OUT_MODE)&&lm&&!dn)
	{
		printf("+++ 留言模式 +++\n");
		m = InLmMode;
	}
	else
	{
		if (ef_is_deny_calling())
		{
			if (g_rec_denied)
			{
				printf("+++ 免打扰留言模式 +++\n");
				m = InDenyLmMode;
		  }
			else
			{
				printf("+++ 免打扰模式 +++\n");
				m = InDenyMode;
			}
		}
		else
		{
			printf("+++ 正常模式 +++\n");
			m = NormalMode;
		}
	}
*/
	if (win_showed_cb)
		m = (*win_showed_cb)();
	return callee_handle_mode(m);

}

/* CALLEE, DONE */
static int
display_gate_in()
{
	if (show_from_outside)
		(*show_from_outside)();
	
	mxc_set_disp_area(0, 0, 640, 480);
	if (start_vod(call.peer_ip, 2, 0) < 0)
	{
		printf("+++ VOD FAILED +++\n");
		snd_call_hup_msg(sockfd);
		return -1;
	}

	return 0;
}

/* CALLEE, DONE */
static int
display_indoor_in()
{
	if (fn_set_nr)
	{
		if (call.type == 1)
			(*fn_set_nr)(call.nr);
		else if (call.type == 0)
		{
			if (((sdk_sgip == NULL)&&(sdk_prip == NULL))
			||(!strcmp(sdk_sgip, sdk_prip)&&!strcmp(sdk_sgip, call.peer_ip)))
				(*fn_set_nr)(0);
			else if ((sdk_sgip != NULL)&&!strcmp(sdk_sgip, call.peer_ip))
				(*fn_set_nr)(-1);
			else if ((sdk_prip != NULL)&&!strcmp(sdk_prip, call.peer_ip))
				(*fn_set_nr)(-2);
		}
	}

	if (show_from_inside)
		(*show_from_inside)();

	if (call.ivt == VIDEO_SPK)
	{
		mxc_set_disp_area(lcd_x, lcd_y, lcd_w, lcd_h);
		if (start_vod(call.peer_ip, 2, 0) < 0)
		{
			printf("+++ VOD FAILED +++\n");
			snd_call_hup_msg(sockfd);
			return -1;
		}
	}
	else
	{
		if (notify_nv)
			(*notify_nv)();
	}

	return 0;
}

/* call, DONE */
static int
handle_conn()
{
	callee_type = 0;
	if (call.type >= 2)
	{
		if (display_gate_in() == 0)
			return win_showed();
	}
	else if (call.type <= 1)
	{
		if (display_indoor_in() == 0)
			return win_showed();
	}
	return -1;
}

/* DONE */
static int
snd_start_lw_msg(int fd)
{
	int    len;
	char buf[32];

	memset(buf, 0, 32);
	if ((call.stat != CALLEE_ASK_LW)||(call.sess_id == 0))
	{
		printf("status error,it should be CALLEE_ASK_LW\n");
		return -1;
	}

	len = sprintf(buf, "%d|%d|%d\r\n", LvWrdStartMsg, call.sess_id, LM_TMEO);
	if  (u_writen(fd, buf, len) < 0)
	{
		printf("snd_start_lw_msg error\n");
		return -1;
	}	
	printf("send lw_start message\n");

	call.timerid = LM_TMEO;
	call.stat = CALLEE_START_LW;

	return 0;
}

/* call, DONE */
static int
callee_handle_lwcfm(int fd)
{
	printf("--- received leaveword confirm message ---\n");
	if (call.stat != CALLEE_ASK_LW)
	{
		printf("call state should be CALLEE_ASK_LW,but not.\n");
		return -1;
	}

	if (snd_start_lw_msg(fd) < 0)
	{
		printf("snd_start_lw_msg error\n");
		return -1;
	}

//	EF_net_record_start();
	return 0;
}

/* DONE */
static void
leaveword_end()
{
	if (call.stat == CALLEE_START_LW&&call.lm_canceled == 0)
	{
//		EF_net_record_stop();
	}
}

/* DONE */
static void
leaveword_cancel()
{
	if (call.stat == CALLEE_START_LW)
	{
//		EF_net_record_cancel();
		call.lm_canceled = 1;
	}
}

static void
handle_accept_ack()
{
	if (call.stat == CALLEE_ACCSND)
	{
//		if (call.ivt == NVID_SPK) {
//			start_vod(call.peer_ip, 1, 0);
//		}
		mxc_audio_on();
		callee_set_acc();
	}
}

/*
static void
handle_accslv_ack()
{
	if (call.type == 1&&call.stat == CALLEE_RELAY)
		acc_slave();
}
*/

static int
handle_camera_on()
{
	if (call.type == 1&&call.ivt == NVID_SPK)
	{
		call.ivt = VIDEO_SPK;
//		if (sdk_hostid == 0&&call.stat != CALLEE_ACCEPT&&call.stat != CALLEE_ACCSND)
//			camera_on_slave();

		if (call.stat == CALLEE_ACCEPT)
			stop_vod();
//			stop_audio_vod();

		if (call.stat != CALLEE_RELAY)
		{
			stop_vod();
			mxc_set_disp_area(lcd_x, lcd_y, lcd_w, lcd_h);
			if (start_vod(call.peer_ip, 3, 0) < 0)
			{
				snd_call_hup_msg(sockfd);
				return -1;
			}
			if (call.stat == CALLEE_ACCSND)
				callee_set_acc();
//			if (call.stat == CALLEE_ACCEPT)
//				dec_audio_on();
		}
	}
	return 0;
}

static void
handle_camera_off()
{
	if (call.type == 1&&call.ivt == VIDEO_SPK)
	{
		call.ivt = NVID_SPK;
//		if (sdk_hostid == 0)
//		{
//			camera_off_slave();
//		}
		stop_vod();
	}
}

/* DONE */
static int
handle_callee(int fd, char *buf, int n)
{
	int   ret = 0;
	int   cmd;
	char *ptr;
	unsigned long  sid;

	cmd = get_cmd_type(buf, &ptr);
	ptr++;
	sid = strtol(ptr, NULL, 10);
	if (call.sess_id != 0&&sid != call.sess_id)
	{
		printf("session id error\n");
		return -1;
	}

	switch (cmd)
	{
	case LvWrdCfmMsg:
		ret = callee_handle_lwcfm(fd);
		break;
	case HupMsg:
	case SsTmeoMsg:
		printf("+++ hangup or timedout +++\n");
		call.disable_acc = 1;
//		if (sdk_hostid == 0)
//			hup_slave();
		ret = -1;
		break;
/*
	case MuteStartMsg:
		ret = handle_mute_start();
		break;
	case MuteEndMsg:
		ret = handle_mute_end(call.peer_ip);
		break;
*/
	case CameraOn:
		ret = handle_camera_on();
		break;
	case CameraOff:
		handle_camera_off();
		break;

	case AcceptAck:
		printf("+++ local accept ack +++\n");
		handle_accept_ack();
		break;
	case AccSlvAck:
		printf("+++ slave accept ack +++\n");
//		if (sdk_hostid == 0)
//			handle_accslv_ack();
		break;
	default:
		break;
	}
	return ret;
}

/* DONE */
static void
hang_up(int fd)
{
	leaveword_cancel();
	snd_call_hup_msg(fd);
}

/* DONE */
static void
unlock_door(int fd)
{
	leaveword_cancel();
	snd_open_msg(fd);
	/*
	if (elv_start == 1)
	{
		add_visitor(1, r_nr%100);
	}*/
}

/* DONE */
static int
snd_shift_msg(int fd)
{
	int  len;
	char buf[48];

	memset(buf, 0, 48);
	len = sprintf(buf, "%d|%d\r\n", AccSftMsg, call.sess_id);
	printf("+++ shift cmd: %s +++\n", buf);
	return u_writen(fd, buf, len);
}

/* need to be modified */
/*
static int
snd_slave_accepted(int fd, int v)
{
	int    len;
	char buf[48];

	memset(buf, 0, 48);
	len = sprintf(buf, "%d|%d|%s\r\n", ((v == 0) ? AccSlvMsg : AccSlvNV), call.sess_id, slv_evt->ip);
	return u_writen(fd, buf, len);
}
*/

/* call, DONE */
static int
handle_reconn()
{
	callee_type = 0;
	if (call.type > 1)
	{
		if (display_gate_in() < 0) return -1;
	}
	else if (call.type <= 1)
	{
		if (display_indoor_in() < 0) return -1;
	}

	return snd_shift_msg(sockfd);
}

///////////////////////////
//     pipe cmd handler   //
///////////////////////////
/*
static int
slave_sft_cmd()
{
	if (call.stat == CALLEE_RELAY)
	{
		printf("*** slave shift call ***\n");
		if (handle_reconn() <= 0)
		{
			printf("*** handle_reconn failed ***\n");
//			hup_slave();
			return -1;
		}

		if (ring_cb)
			(*ring_cb)(1);

		call.stat = CALLEE_RING;
		call.timerid = RING_TMEO;
		printf("+++ ringing..., timeout = %d +++\n", call.timerid);
	}
	return 0;
}

static int
local_sft_cmd()
{
	if ((sdk_hostid == 0)
	&&(call.stat == CALLEE_ACCEPT||call.stat == CALLEE_MUTE))
	&&has_slaves)
	{
		printf("*** master shift call ***\n");
		if (snd_shift_msg(sockfd) <= 0)
		{
//			hup_slave();
			return -1;
		}

		call.stat = CALLEE_RELAY;
		if (call.ivt == VIDEO_SPK)
		{
			stop_vod();
//			ring_slave();
		}
		else
		{
			stop_audio_vod();
//			ring_slave_nv();
		}

		if (last_ui)
			(*last_ui)();
	}
	return 0;
}

static int
slave_acc_cmd(char c)
{
	printf("*** slave accepted ***\n");
	if ((sdk_hostid == 0)&&(call.stat < CALLEE_ACCEPT||call.stat == CALLEE_RELAY))
	{
		if (call.stat == CALLEE_RING)
		{
		    printf("--- will stop ringing ---\n");
			if (ring_cb)
				(*ring_cb)(0);
		}
		leaveword_cancel();
		if (snd_slave_accepted(sockfd, c-'A') < 0)
		{
			printf("*** send slave accept msg error ***\n");
//			hup_slave();
			return -1;
		}

		if (call.stat != CALLEE_RELAY)
		{
			call.stat = CALLEE_RELAY;
		//	stop_vod();
			if (last_ui)
				(*last_ui)();
		}
	}
	return 0;
}
*/

static int
local_acc_cmd()
{
	if (call.stat < CALLEE_ACCSND)
	{
		DBG("--- local accept ---\n");
//		if (sdk_hostid == 0)
//			hup_slave();
		if (ring_cb&&(dr == 1)) {
			(*ring_cb)(0);
			dr = 0;
		}
		leaveword_cancel();
		if (snd_accept_msg(sockfd) < 0) return -1;
		/*
		if (call.type == 0||call.ivt == VIDEO_SPK)
		{
			mxc_audio_on();
			
			callee_set_acc();
		}
		else
		{*/
			DBG("+++ waiting for accept ack +++\n");
			call.timerid = 5;
			call.stat = CALLEE_ACCSND;
//		}
	}
	return 0;
}

static int
local_unlock_cmd()
{
	if (call.type >= 2)
	{
		printf("local unlock button pressed\n");
		unlock_door(sockfd);
//		if (sdk_hostid == 0)
//			hup_slave();
		return -1;
	}
	return 0;
}

static int
slave_unlock_cmd()
{
	if (call.type >= 2&&sdk_hostid == 0)
	{
		printf("slave unlock button pressed\n");
		unlock_door(sockfd);
		return -1;
	}
	return 0;
}

static int
local_hup_cmd()
{
	printf("---- callee: local hup button pressed ----\n");
	call.disable_acc = 1;
	hang_up(sockfd);
//	if (sdk_hostid == 0)
//		hup_slave();
	return -1;
}

static int
slave_hup_cmd()
{
	if (sdk_hostid == 0)
	{
		printf("---- callee: slave hup button pressed ----\n");
		call.disable_acc = 1;
		hang_up(sockfd);
		return -1;
	}
	return 0;
}

/*
static int
start_mute_cmd()
{
	if (call.stat >= CALLEE_ACCEPT)
	{
		printf("+++ start mute +++\n");
		if (snd_mute_start_msg(sockfd) <= 0)
		{
			if (sdk_hostid == 0)
				hup_slave();
			return -1;
		}
	}
	return 0;
}

static int
stop_mute_cmd()
{
	if (call.stat >= CALLEE_ACCEPT)
	{
		printf("+++ stop mute +++\n");
		if (snd_mute_end_msg(sockfd) <= 0)
		{
			if (sdk_hostid == 0)
				hup_slave();
			return -1;
		}
	}
	return 0;
}
*/

static int
local_camera_on_cmd()
{
	if (call.type == 1&&call.ovt == 1)
	{
		printf("+++ turn on camera +++\n");
		if (snd_avo_msg(sockfd, 0) <= 0)
		{
//			if (sdk_hostid == 0)
//				hup_slave();
			return -1;
		}
		call.ovt = 0;
	}
	return 0;
}

static int
local_camera_off_cmd()
{
	if (call.type == 1&&call.ovt == 0)
	{
		printf("+++ turn off camera +++\n");
		call.ovt = 1;
		if (snd_avo_msg(sockfd, 1) <= 0)
		{
//			if (sdk_hostid == 0)
//				hup_slave();
			return -1;
		}
	}
	return 0;
}

static int
slave_camera_on_cmd()
{
	if (sdk_hostid == 0&&call.type == 1&&call.stat == CALLEE_RELAY)
	{
		printf("+++ slave turn on camera +++\n");
		if (snd_avo_msg(sockfd, 0) <= 0)
		{
//			hup_slave();
			return -1;
		}
	}
	return 0;
}

static int
slave_camera_off_cmd()
{
	if (sdk_hostid == 0&&call.type == 1&&call.stat == CALLEE_RELAY)
	{
		printf("+++ slave turn off camera +++\n");
		if (snd_avo_msg(sockfd, 1) <= 0)
		{
//			hup_slave();
			return -1;
		}
	}
	return 0;
}

static int
media_off_cmd()
{
	if (call.stat != CALLEE_RELAY&&call.ivt == VIDEO_SPK)
	{
		leaveword_cancel();
//		if (sdk_hostid == 0)
//			hup_slave();
		return -1;
	}
	else if (call.ivt == NVID_SPK)
	{
		if (notify_nv)
			(*notify_nv)();
		if (call.stat == CALLEE_ACCEPT) {
			stop_vod();
			start_vod(call.peer_ip, 1, 0);
		}
	}
	return 0;
}

static void
record_cmd(int m)
{
	if (call.type == 1&&call.ivt == NVID_SPK)
	{
		if (notify_nv_rec)
			(*notify_nv_rec)();
	}
	else
	{
		if (m == 0)
		{
		//	EF_net_record_stop();
		}
		else
		{
		//	EF_net_record_start();
		}
	}
}

/* master callee pipe fn */
static int
callee_pipe_fn(char c)
{
	int  ret = 0;
	switch (c)
	{
	case 'S':
//		ret = slave_sft_cmd();
		break;
	case 's':
//		ret = local_sft_cmd();
		break;
	case 'A':
	case 'B':
//		ret = slave_acc_cmd(c);
		break;
	case 'a':
		ret = local_acc_cmd();
		break;
	case 'o': 
		ret = local_unlock_cmd();
		break;
	case 'O':
		ret = slave_unlock_cmd();
		break;
	case 'h': 
		ret = local_hup_cmd();
		break;
	case 'H':
		ret = slave_hup_cmd();
		break;
	case 'c':
		ret = media_off_cmd();
		break;
/*
	case 'm':
		ret = start_mute_cmd();
		break;
	case 'M':
		ret = stop_mute_cmd();
		break;
*/
	case 'v':
		ret = local_camera_on_cmd();
		break;
	case 'V':
		ret = local_camera_off_cmd();
		break;
	case 'u':
		ret = slave_camera_on_cmd();
		break;
	case 'U':
		ret = slave_camera_off_cmd();
		break;
	case 'r':
		record_cmd(1);
		break;
	case 'R':
		record_cmd(0);
		break;
	default:
		break;
	}
	return ret;
}

/* DONE */
static int
callee_tmeo_fn()
{
	if ((call.stat == CALLEE_RING)&&(call.mode == InLmMode))
	{
	    printf("--- timeout and will stop ringing ---\n");
		if (ring_cb&&(dr == 1)) {
			(*ring_cb)(0);
			dr = 0;
		}
		if (snd_ask_leaveword(sockfd) == 0)
			return 0;
	}
	call.disable_acc = 1;
	snd_call_tmeout_msg(sockfd);
//	if (sdk_hostid == 0)
//		hup_slave();
	return -1;
}

/* DONE */
static void
callee_quit()
{
	printf("--- callee_quit start ---\n");
	if (CALLEE_RING == call.stat) {
		if (ring_cb&&(dr == 1)) {
			(*ring_cb)(0);
			dr = 0;
		}
	}
	stop_vod();

	leaveword_end();

//	if (sdk_hostid == 0)
//		hup_slave();
//	printf("--- hangup slave ---\n");
	if (last_ui)
		(*last_ui)();
}

/* DONE */
static void
callee_handle()
{
	u_flush_fd(call_pfd[0]);

	printf("handle_connect:ip=%s\n", call.peer_ip);
	if (handle_conn() >= 0)
		call_run(sockfd, call_pfd[0], &callee_fn, &call_tmeo);

	printf("callee quit\n");
	callee_quit();
}

#define  pipe_write(c)   u_writen(call_pfd[1], c, 1)

/* caller or callee, DONE */
/* localhost hangup */
void
call_hup()
{
	char c = 'h';
	printf("--- SEND HUP PIPE CMD: %c ---\n", c);
	pipe_write(&c);
}

/* called by ui thread */
static void
call_camera_onoff(int m)
{
	char c;

	c = (m==0)?'v':'V';
	pipe_write(&c);
}

/* called by master thread */
void
slave_camera_onoff(int m)
{
	char c;

	c = (m==0)?'u':'U';
	pipe_write(&c);
}

void
video_off()
{
	char c = 'c';
	pipe_write(&c);
}

void
callee_record(int m)
{
	char c;
	c = ((m == 0)?'R':'r');
	pipe_write(&c);
}

/* callback for button clicked events */
/* called in UI thread */
/* DONE */
static void
callee_accept()
{
	char c = 'a';
	if (call.disable_acc == 0)
	{
		pipe_write(&c);
	}
}

/* DONE */
static void
callee_open()
{
	char c = 'o';
	pipe_write(&c);
}

static void
callee_shift()
{
	char c = 's';
	pipe_write(&c);
}

void
call_mute(int m)
{
	char c;

	c = (m==0)?'m':'M';
	pipe_write(&c);
}

///////////////////////////////////////////
//       called in master thread         //
///////////////////////////////////////////
void
slave_accept(int v)
{
	char c;

	c = 'A' + v;
	if (call.disable_acc == 0)
	{
		pipe_write(&c);
	}
}

/* DONE */
void
slave_open()
{
	char c = 'O';
	pipe_write(&c);
}

/* slave shift */
void
slave_shift()
{
	char c = 'S';
	pipe_write(&c);
}

/* slave hangup */
void
slave_hup()
{
	char c = 'H';
	pipe_write(&c);
}

////////////////////////////////////////////////////////
/********** CALLER PROCEDURE AND FUNCTIONS ***********/
////////////////////////////////////////////////////////
void
mxc_caller_timeout(void)
{
	char c = 't';
	pipe_write(&c);
}

void
mxc_caller_leaveword(void)
{
	char c = 'l';
	pipe_write(&c);
}

////////////////////////////////////////////////////////
/********** CALLEE PROCEDURE AND FUNCTIONS ***********/
////////////////////////////////////////////////////////
/* DONE */
static int
send_busy_msg(int fd)
{
	int   len;
	char buf[16];

	memset(buf, 0, 16);
	len = sprintf(buf, "%d\r\n", BusyMsg);
	if (u_writen(fd, buf, len) < 0) {
		return -1;
	}
	printf("send busy msg\n");
	return 0;
}

static int
callee_handle_req(int fd, void *buf, int n)
{
	char   tmp[32];
	char  *p[4] = {NULL};
	char  *ptr = NULL;
	char  *pt;
	int    k;
	int    vt, cmd;

	n = (n > 32)?32:n;
	memset(tmp, 0, 32);
	memcpy(tmp, buf, n);

	k = 0;
	pt = tmp;
	while ((p[k++] = strtok_r(pt, "|", &ptr))!=NULL)
	{
		pt = NULL;
		if (k >= 4) break;
	}

	cmd = atoi(p[0]);
	if (cmd == CallReqMsg)
	{
		printf("+++ CALLER VIDEO ON +++\n");
		vt = 0;
	}
	else if (cmd == CallReqNV)
	{
		printf("+++ CALLER VIDEO OFF +++\n");
		vt = 1;
	}
	else
		return -1;
	
	call.type = atoi(p[1]);
	call.nr = strtol(p[2], (char**)NULL, 10);
	if (p[3])
		call.minor = strtol(p[3], (char**)NULL, 10);
	else
		call.minor = 0;

	if (call.type == 1)
	{
		if ((call.nr != sdk_nr)&&fn_valid&&((*fn_valid)(call.nr) == 0)) return -1;
		call.ivt = vt;
	}
	else
		call.ivt = VIDEO_SPK;

	printf("*** CALLER-NR:%u-%d, type:%d ***\n", call.nr, call.minor, call.type);
	call.sess_id = u_random_int_range(100001, 99999999);
	memset(tmp, 0, 32);
	n = sprintf(tmp, "%d|%d\r\n", CallReqAck, call.sess_id);
	if (u_writen(fd, tmp, n) < 0)
	{
		clear_session_state();
		return -1;
	}
	printf("------ send ack: %s --------\n", tmp);
	call.stat = CALLEE_SND_ACK;
	return 0;
}

/* DONE */
static int
poll_request(int fd)
{
	char  buf[32];
	int    n;
	struct timeval  tv;

	tv.tv_sec = REQ_TMEO;
	tv.tv_usec = 0;
	if (u_readable_otimeval(fd, &tv) <= 0)
	{
		printf("*** CALLER ERROR ***\n");
		return -1;
	}

	memset(buf, 0, 32);
	n = u_rreadline(fd, buf, 32);
	if (n > 0)
	{
		return callee_handle_req(fd, buf, n);
	}
	return -1;
}

/* DONE */
static void*
callee_loop(void* arg)
{
	char  rip[16];
	int   fd;
	int   listenfd;

	pthread_detach(pthread_self());
again:
	listenfd = u_tcp_serv(NULL, TLK_LISTEN_PORT);
	while (1)
	{
		memset(rip, '\0', 16);
		if ((fd = u_accept(listenfd, rip)) < 0)
		{
			close(listenfd);
			goto again;
		}

		if (sdk_nr == 0)
		{
			close(fd);
			continue;
		}

		printf("connectfd = %d, ip = %s\n", fd, rip);
		if (call_trylock() == 0)
		{
			if (u_ud_lock() == 1)
			{
				clear_session_state();
				u_tcp_keepalive(fd, KEEP_ALIVE_IDLE, PROBE_ITVL, PROBE_CNT);
				if (poll_request(fd) == 0)
				{
			//		send_log_msg(CALLEE_LOG, call.nr);
					memset(call.peer_ip, 0, 16);
					memcpy(call.peer_ip, rip, 16);
					sockfd = fd;
					call_V(1);
				}
				else
				{
					close(fd);
					u_ud_unlock();
				}
			}
			else
			{
				printf("--- resource busy: system updating ---\n");
				send_busy_msg(fd);
				u_msleep(500);
				close(fd);
			}
			call_unlock();
		}
		else
		{
			printf("call busy\n");
			send_busy_msg(fd);
			u_msleep(500);
			close(fd);
		}
	}
	close(listenfd);
	pthread_exit(NULL);
}

/* DONE */
static void*
call_loop(void* arg)
{
	pthread_detach(pthread_self());
	printf("*******  call main loop, pid = %d ********\n", getpid());
	while (1)
	{
		call_lock();
		while (thread_st == 0) call_P();
		if (fn_prepare)
			(*fn_prepare)();

		if (thread_st == 2)
		{
			printf("in caller procedure\n");
			if (caller_handle() == EFNONR)
			{
				printf("+++++ NO THIS NUMBER +++++\n");
				if (notify_none)
					(*notify_none)();
			}
			printf("quit caller procedure\n");
		} 
		else if (thread_st == 1)
		{
			printf("in callee procedure\n");
			dr = 0;
			callee_handle();
			u_tcp_quit(sockfd);
			sockfd = -1;
			printf("quit callee procedure\n");
		}
		thread_st = 0;

		if (fn_finish)
			(*fn_finish)();
		u_ud_unlock();
		call_unlock();
	}

	pthread_exit(NULL);
}

/* DONE */
static void
call_new(void)
{
	pthread_t tid;

	memset(&call, 0, sizeof(call_t));
	pipe(call_pfd);
	u_set_nonblock(call_pfd[0]);
	u_set_nonblock(call_pfd[1]);
	fn_hup[0] = call_hup;
	fn_acc[0] = callee_accept;
	fn_unlock[0] = callee_open;
	fn_sft[0] = callee_shift;
	fn_mute[0] = call_mute;
	fn_cam[0] = call_camera_onoff;
	fn_record[0] = callee_record;
	pthread_create(&tid, NULL, call_loop, NULL);
	pthread_create(&tid, NULL, callee_loop, NULL);
}

/* DONE */
int
mxc_caller_callout(unsigned int nr, int minor, int vt)
{
	if (get_ip == NULL) return -1;
	if (call_trylock() == 0)
	{
		if (u_ud_lock() != 1)
		{
			printf("--- resource busy: system updating ---\n");
			call_unlock();
			return -1;
		}
		clear_session_state();
		call.nr = nr;
		call.ovt= vt;
		call.minor = minor;
		call_V(2);
		call_unlock();
		return 0;
	}
	printf("--- resource busy: caller thread not quit ---\n");
	return -1;
}

/////////////////////////////////////
void
mxc_start_call_proc(void)
{
	call_new();
	/*
	if (sdk_hostid == 0)
		start_master_thread();
	else
		start_slave_thread();
	*/
}

//////////////////////////////////////
//////////////////////////////////////
#if 0
{
#endif

int
is_talking(void)
{
	if (call_trylock() == 0)
	{
		call_unlock();
		return 0;
	}
	return 1;
}

/////////////////////////////////////////////
void
mxc_set_calling_cb(void *p, void *f)
{
	fn_prepare = p;
	fn_finish  = f;
}

void
mxc_set_valid_func(void *f)
{
	fn_valid = f;
}

void
mxc_set_id(unsigned int nr, int id)
{
	sdk_nr = nr;
	sdk_hostid = id;
}

unsigned int
mxc_get_peer_nr(void)
{
	return call.nr;
}

//////////////////////////////////////
//      callee action callback      //
//////////////////////////////////////
void
mxc_session_hangup(void)
{
	fn_hup[callee_type]();
}

void
mxc_session_shift(void)
{
	fn_sft[callee_type]();
}

void
mxc_session_accept(void)
{
	fn_acc[callee_type]();
}

void
mxc_session_unlock_door(void)
{
	fn_unlock[callee_type]();
}

void
mxc_camera_control(int m)
{
	fn_cam[callee_type](m);
}

void
mxc_record_control(int m)
{
	fn_record[callee_type](m);
}

//////////////////////////////////////
//    notify callee UI thread       //
//////////////////////////////////////
void
mxc_set_nv_notify_callee(void *f, void *r)
{
	notify_nv = f;
	notify_nv_rec = r;
}

void
mxc_set_acc_notify_callee(void *f)
{
	notify_acc_callee = f;
}

//////////////////////////////////////
//    notify caller UI thread       //
//////////////////////////////////////

void
mxc_set_shift_notify_caller(void *f)
{
	notify_sft = f;
}

void
mxc_set_deny_notify_caller(void *f)
{
	notify_deny= f;
}

void
mxc_set_none_notify_caller(void *f)
{
	notify_none = f;
}

void
mxc_set_nv_notify_caller(void *f)
{
	notify_nvo = f;
}

void
mxc_set_busy_notify_caller(void *f)
{
	notify_busy = f;
}

void
mxc_set_acc_notify_caller(void *f)
{
	notify_acc_caller = f;
}

void
mxc_set_leaveword_notify_caller(void *f, void *g)
{
	notify_lw = f;
	notify_lw_start = g;
}

void
mxc_set_initial_notify_caller(void *f)
{
	notify_reset_caller_ui = f;
}

//////////////////////////////////////////////
void
mxc_caller_camera_control(int m)
{
	call_camera_onoff(m);
}

void
mxc_caller_hup(void)
{
	call_hup();
}

///////////////////////////////////////////////
void
mxc_set_ring_cb(void *r)
{
	ring_cb = r;
}

void
mxc_set_getip_method(void *f)
{
	get_ip = f;
}

void
mxc_set_before_ring(void *f)
{
	win_showed_cb = f;
}

void
mxc_set_callee_win_from_outside(void *o)
{
	show_from_outside = o;
}

void
mxc_set_callee_win_from_inside(void *i)
{
	show_from_inside = i;
}

void
mxc_set_caller_nr(void *f)
{
	fn_set_nr = f;
}

void
mxc_set_sgip(char *ip)
{
	sdk_sgip = ip;
}

void
mxc_set_prip(char *ip)
{
	sdk_prip = ip;
}

void
mxc_set_last_ui(void *f)
{
	last_ui = f;
}

#if 0
}
#endif

