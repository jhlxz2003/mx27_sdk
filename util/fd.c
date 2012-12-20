#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int
fd_can_read(int fd, int ms)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd,&fds);
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;

	return select(fd+1, &fds, NULL, NULL, &tv) > 0;
}

