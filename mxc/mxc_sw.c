#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "mxc_sdk.h"

static int  rtmeo = 0;
static void (*sw_handler)(void) = NULL;

static void*
sw_loop(void *arg)
{
	int fd;
	struct ievent   ev;
	struct timeval *tmeo;
	struct timeval  tv;
	fd_set rset;
	int ret;

	pthread_detach(pthread_self());
	fd = open("/dev/mxcain2", O_RDONLY);
	if (fd < 0) pthread_exit(NULL);

	tv.tv_sec = rtmeo;
	tv.tv_usec = 0;
	tmeo = NULL;
	while (1)
	{
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		ret = select(fd+1, &rset, NULL, NULL, tmeo);
		if (ret > 0&&FD_ISSET(fd, &rset)) {
			if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.value == EVT_DN) {
					if (rtmeo == 0) {
						if (sw_handler)
							sw_handler();
						tmeo = NULL;
					} else {
						tv.tv_sec = rtmeo;
						tv.tv_usec = 0;
						tmeo = &tv;
					}
				} else {
					tmeo = NULL;
				}
			}
		} else if (ret == 0) {
			if (sw_handler)
				sw_handler();
			tmeo = NULL;
		}
	}
	close(fd);
	pthread_exit(NULL);
}

int
mxc_switch_init(int time, void *cb)
{
	pthread_t tid;
	rtmeo = time;
	sw_handler = cb;
	return pthread_create(&tid, NULL, sw_loop, NULL);
}

