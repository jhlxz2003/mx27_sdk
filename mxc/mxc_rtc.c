/*
* read rtc,write rtc and set rtc alarm
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define MAX_RTC_NODES      16

#define MAXI(x, y)  (((x)>(y))?(x):(y))

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD(name) struct list_head name = { &(name), &(name) }

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

typedef struct rtc_node {
	struct list_head list;
	time_t tm;
	void (*action)(unsigned long data);
	unsigned long data;
} rtc_node_t;

static pthread_mutex_t  rtc_mtx = PTHREAD_MUTEX_INITIALIZER;

#define rtc_lock()    pthread_mutex_lock(&rtc_mtx)
#define rtc_unlock()  pthread_mutex_unlock(&rtc_mtx)

static int rtc_ncnt = 0;
static int rtc_st = 0;

static LIST_HEAD(rtc_list);
static LIST_HEAD(rtc_free);

static int rtc_pfd[2] = {-1, -1};
static int rtcFd = -1;

static void 
__u_list_add(struct list_head *new,struct list_head *prev,struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static void
__u_list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/* make sure entry is in list. */
static void 
u_list_del(struct list_head *entry)
{
	 __u_list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

static void 
u_list_add(struct list_head *new, struct list_head *head)
{
	__u_list_add(new, head, head->next);
}

static void 
u_list_append(struct list_head *new, struct list_head *head)
{
	__u_list_add(new, head->prev, head);
}

static int
u_list_empty(const struct list_head *head)
{
	return head->next == head;
}

#if 0
{
#endif

/* DONE */
static rtc_node_t*
rtc_node_new()
{
	struct list_head *list;
	rtc_node_t *node = NULL;

	if (!u_list_empty(&rtc_free)) {
		list = rtc_free.next;
		u_list_del(list);
		node = (rtc_node_t*)list;
	} else {
		if (rtc_ncnt < MAX_RTC_NODES) {
			node = calloc(1, sizeof(rtc_node_t));
			if (node)
				++rtc_ncnt;
		}
	}

	if (node) {
		INIT_LIST_HEAD(&node->list);
	}

	return node;
}

/* DONE */
static void
rtc_node_free(rtc_node_t *node)
{
	if (!u_list_empty(&node->list))
		u_list_del(&node->list);
	memset(node, 0, sizeof(rtc_node_t));
	u_list_append(&node->list, &rtc_free);
}

/* DONE */
static int
set_rtc_alarm(time_t utc)
{
	int fd;
	struct rtc_time rtm;
	struct tm  gtm;

	memset(&gtm, 0, sizeof(struct tm));
	memset(&rtm, 0, sizeof(struct rtc_time));
	localtime_r(&utc, &gtm);
	rtm.tm_year = gtm.tm_year;
	rtm.tm_mon = gtm.tm_mon;
	rtm.tm_mday = gtm.tm_mday;
	rtm.tm_hour = gtm.tm_hour;
	rtm.tm_min = gtm.tm_min;
	rtm.tm_sec = gtm.tm_sec;

	fd = open ("/dev/misc/rtc", O_RDONLY);
	if (fd ==  -1) {
		perror("/dev/misc/rtc");
		return -1;
	}

	if (ioctl(fd, RTC_ALM_SET, &rtm) == -1) {
		perror("ioctl:RTC_ALM_SET");
		close(fd);
		return -1;
	}

#ifdef DEBUG
	if (ioctl(fd, RTC_ALM_READ, &rtm) == -1) {
		perror("ioctl:RTC_ALM_READ");
		close(fd);
		return -1;
	}

	fprintf(stderr, "\n\nAlarm time now set to %d-%d-%d, %02d:%02d:%02d.\n",
		rtm.tm_year + 1900, rtm.tm_mon + 1, rtm.tm_mday, rtm.tm_hour, rtm.tm_min, rtm.tm_sec);
#endif

	if (ioctl(fd, RTC_AIE_ON, 0) == -1) {
		perror("ioctl:RTC_AIE_ON");
		close(fd);
		return -1;
	}

	return fd;
}

/* DONE */
static void
stop_rtc_alarm(int fd)
{
	ioctl(fd, RTC_AIE_OFF, 0);
	close(fd);
}

/* DONE */
static rtc_node_t*
probe_node()
{
	rtc_node_t* node = NULL;
	rtc_lock();
	if (!u_list_empty(&rtc_list))
		node = (rtc_node_t*)rtc_list.next;
	rtc_unlock();
	return node;
}

/* DONE */
static void*
rtc_loop(void *arg)
{
	int ret;
	time_t tme;
	fd_set rset;
	int maxfd;
	rtc_node_t *node;

	pthread_detach(pthread_self());
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, rtc_pfd) < 0) pthread_exit(NULL);
	printf("--- in rtc_loop ---\n");
	rtcFd = -1;
	while (1)
	{
		tme = 0;
		FD_ZERO(&rset);
		FD_SET(rtc_pfd[0], &rset);
		maxfd = rtc_pfd[0];
		if (rtc_st > 0)
		{
			node = probe_node();
			if (node)
			{
				tme = node->tm;
				rtcFd = set_rtc_alarm(tme);
				if (rtcFd < 0)
				{
					rtc_st = 0;
					break;
				}
				FD_SET(rtcFd, &rset);	
				maxfd = MAXI(rtcFd, rtc_pfd[0]);
			}
			else
			{
				rtc_st = 0;
			}
		}
		ret = select(maxfd+1, &rset, NULL, NULL, NULL);
		if (ret > 0)
		{
			if (FD_ISSET(rtc_pfd[0], &rset))
			{
				char c;
				if (read(rtc_pfd[0], &c, 1) == 1)
				{
					if (c == 'a')
					{
						if (rtc_st == 0)
						{
							rtc_st = 1;
							continue;
						}
					}
					else if (c == 'd')
					{
						if (rtcFd >= 0)
						{
							stop_rtc_alarm(rtcFd);
							rtcFd = -1;
						}
						rtc_st = 0;
						write(rtc_pfd[0], "D", 1);
						continue;
					}
				}
			}

			if ((rtc_st == 1)&&(rtcFd >= 0)&&FD_ISSET(rtcFd, &rset))
			{
				unsigned long data;
				if (read(rtcFd, &data, sizeof(data)) > 0)
				{
					struct list_head *list;
					rtc_lock();
					list = rtc_list.next;
					while (list != &rtc_list)
					{
						node = (rtc_node_t*)list;
						if (node->tm != tme) break;
						list = list->next;
						node->action(node->data);
						rtc_node_free(node);
					}
					rtc_unlock();
				}
			}
		}

		if (rtcFd >= 0)
		{
			stop_rtc_alarm(rtcFd);
			rtcFd = -1;
		}
	}

	close(rtc_pfd[0]);
	close(rtc_pfd[1]);
	rtc_pfd[0] = -1;
	rtc_pfd[1] = -1;
	pthread_exit(NULL);
}

/* DONE */
static int
suspend_rtc_loop()
{
	char c = 0;
	int ret = 0;
	if (rtc_st != 0)
	{
		write(rtc_pfd[1], "d", 1);
		read(rtc_pfd[1], &c, 1);
		ret = 1;
	}
	return ret;
}

/* DONE */
static void
resume_rtc_loop()
{
	if (rtc_st == 0)
		write(rtc_pfd[1], "a", 1);
}

#if 0
}
#endif

/* DONE */
int
mxc_rtc_init()
{
	pthread_t tid;
	printf("--- rtc_loop created ---\n");
	return pthread_create(&tid, NULL, rtc_loop, NULL);
}

/* DONE */
unsigned long
mxc_add_rtc_alarm(time_t tm, void (*action)(unsigned long), unsigned long data)
{
	int flag = 0;
	rtc_node_t* node;

	flag = suspend_rtc_loop();

	rtc_lock();
	node = rtc_node_new();
	if (node == NULL) {
		rtc_unlock();
		if (flag)
			resume_rtc_loop();
		return 0;
	}
	node->tm = tm;
	node->action = action;
	node->data = data;

	if (u_list_empty(&rtc_list)) {
		u_list_append(&node->list, &rtc_list);
	} else {
		rtc_node_t* tmp;
		struct list_head *list;
		list = rtc_list.next;
		tmp = (rtc_node_t*)list;
		
		if (tm < tmp->tm)
		{		
			u_list_add(&node->list, &rtc_list);
		}
		else if (tm == tmp->tm)
		{
			u_list_add(&node->list, &tmp->list);
		}
		else
		{
			list = list->next;
			while (list != &rtc_list)
			{
				tmp = (rtc_node_t*)list;
				if (tm > tmp->tm)
					list = list->next;
				else if (tm == tmp->tm)
				{
					u_list_add(&node->list, &tmp->list);
					break;
				}
				else if (tm < tmp->tm)
				{
					u_list_append(&node->list, &tmp->list);
					break;
				}
			}

			if (list == &rtc_list) {
				u_list_append(&node->list, &rtc_list);
			}
		}
	}
	rtc_unlock();
	resume_rtc_loop();
	return (unsigned long)node;
}

/* DONE */
void
mxc_remove_rtc_alarm(unsigned long rtcid)
{
	int flag;
	rtc_node_t *node = (rtc_node_t*)rtcid;

	flag = suspend_rtc_loop();
	rtc_lock();
	rtc_node_free(node);
	rtc_unlock();
	if (flag)
		resume_rtc_loop();
}

/* DONE */
int
mxc_get_rtc(struct tm *tm)
{
	int fd;
	int flag = 0;
	struct rtc_time rtc_tm;

	flag = suspend_rtc_loop();

	rtc_lock();
	fd = open ("/dev/misc/rtc", O_RDONLY);
	if (fd ==  -1) {
		rtc_unlock();
		perror("/dev/misc/rtc");
		return -1;
	}

	if (ioctl(fd, RTC_RD_TIME, &rtc_tm) == -1) {
		close(fd);
		rtc_unlock();
		perror("ioctl");
		return -1;
	}

	close(fd);
	rtc_unlock();

	if (flag)
		resume_rtc_loop();

#if 0
	fprintf(stderr, "\n\nCurrent RTC date/time is %d-%d-%d, %02d:%02d:%02d.\n",
		rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday, 
		rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
#endif

	memset(tm, 0, sizeof(struct tm));
	tm->tm_year = rtc_tm.tm_year;
	tm->tm_mon = rtc_tm.tm_mon;
	tm->tm_mday = rtc_tm.tm_mday;
	tm->tm_hour = rtc_tm.tm_hour;
	tm->tm_min = rtc_tm.tm_min;
	tm->tm_sec = rtc_tm.tm_sec;
	return 0;
}

/* DONE */
int
mxc_set_rtc(struct tm *tm)
{
	int fd;
	int flag = 0;
	int ret = 0;
	time_t tme;
	struct rtc_time rtc_tm;
	struct list_head *list;

	tme = mktime(tm);
	rtc_tm.tm_year = tm->tm_year;
	rtc_tm.tm_mon = tm->tm_mon;
	rtc_tm.tm_mday = tm->tm_mday;
	rtc_tm.tm_hour = tm->tm_hour;
	rtc_tm.tm_min = tm->tm_min;
	rtc_tm.tm_sec = tm->tm_sec;

	flag = suspend_rtc_loop();
	if (flag == 1)
	{
		rtc_node_t *node;
		rtc_lock();
		list = rtc_list.next;
		while (list != &rtc_list)
		{
			node = (rtc_node_t*)list;
			if (node->tm > tme) break;
			list = list->next;
			rtc_node_free(node);
		}
		rtc_unlock();
	}

	rtc_lock();
	fd = open ("/dev/misc/rtc", O_RDONLY);
	if (fd ==  -1) {	
		rtc_unlock();
		perror("/dev/misc/rtc");
		return -1;
	}

	if (ioctl(fd, RTC_SET_TIME, &rtc_tm) == -1) {
		perror("ioctl");	
		ret = -1;
	}
	close(fd);
	rtc_unlock();

	if (flag != 0)
		resume_rtc_loop();
	return ret;
}

