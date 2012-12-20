#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mxc_sdk.h"

#define AI_IOC_MAGIC  0xfa
#define AI_IOCGLVL   _IOR(AI_IOC_MAGIC, 0, unsigned int)
#define AI_IOCSTMEO  _IOW(AI_IOC_MAGIC, 1, unsigned int)

/*
#define RBUFSZ    32

typedef struct {
	int     rb_cnt;
	char   *rb_bufptr;
	char    rb_buf[RBUFSZ];
} Rbuf;

static Rbuf rbuf[3];
static int  aiFd[3] = {-1, -1, -1};

static ssize_t
read_one(Rbuf *tsd, int fd, char *ptr)
{
	if (tsd->rb_cnt <= 0) {
	again:
		if ((tsd->rb_cnt = read(fd, tsd->rb_buf, RBUFSZ)) < 0) {
			if (errno == EINTR) goto again;
			return (-1);
		} else if (tsd->rb_cnt == 0)
			return (0);
		tsd->rb_bufptr = tsd->rb_buf;
	}

	tsd->rb_cnt--;
	*ptr = *tsd->rb_bufptr++;
	return (1);
}

int
mxc_alarmin_open(int i)
{
	char devname[10] = {0};

	if (i > 2) return -1;
	if (aiFd[i] == -1) {
		memset(&rbuf[i], 0, sizeof(Rbuf));
		sprintf(devname, "/dev/mxcain%d", i);
		aiFd[i] = open(devname, O_RDONLY);
	}
	return aiFd[i];
}

int
mxc_get_alarm(int i, struct ievent *ev)
{
	char c;
	int  rc;

	if (i > 2||aiFd[i] < 0) return -1;
	if ((rc = read_one(&rbuf[i], aiFd[i], &c)) == 1) {
		return (int)c;
	} else if (rc == 0) {
		return 0;
	} else {
		perror("read mxcain error");
		return (-1);
	}
}
*/

int
mxc_alarmin_open(int i)
{
	char devname[10] = {0};
	if (i > 2) return -1;
	sprintf(devname, "/dev/mxcain%d", i);
	return open(devname, O_RDONLY);
}

int
mxc_get_alarm(int fd, struct ievent *ev)
{
	return read(fd, ev, sizeof(struct ievent));
}

int
mxc_poll_alarm(int fd)
{
	unsigned int v;
	if (ioctl(fd, AI_IOCGLVL, &v) < 0) return -1;
	return (int)v;
}

int
mxc_alarmin_close(int fd)
{
	return close(fd);
}

