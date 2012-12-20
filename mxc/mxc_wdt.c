#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef void (*WDTFUNC)(void);

static int wdtFd = -1;

int
start_watchdog(unsigned int timeout)
{
	if (wdtFd < 0) {
		wdtFd = open("/dev/misc/watchdog", O_WRONLY);
		if (wdtFd < 0) return -1;
	}
	if (ioctl(wdtFd, WDIOC_SETTIMEOUT, &timeout) < 0) {
		close(wdtFd);
		wdtFd = -1;
		return -1;
	}
	return 0;
}

#if 0
void
stop_watchdog()
{
	if (wdtFd != -1) {
		close(wdtFd);
		wdtFd = -1;
	}
}
#endif

static void
wdt_ioctl(void)
{
	ioctl(wdtFd, WDIOC_KEEPALIVE, 0);
}

static void
wdt_write(void)
{
	write(wdtFd, "\0", 1);
}

static WDTFUNC serve_fn[2] = {wdt_ioctl, wdt_write};

void
feed_watchdog(int mode)
{
	if (wdtFd != -1) {
		(*serve_fn[mode])();
	}
}
