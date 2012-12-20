#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static int gpioFd = -1;
static int ledsFd = -1;

int
mxc_gpio_init(void)
{
	if (gpioFd < 0) {
		gpioFd = open("/dev/misc/gpio", O_RDWR);
	}
	return gpioFd;
}

int
mxc_leds_init(void)
{
	if (ledsFd < 0) {
		ledsFd = open("/dev/misc/leds", O_RDWR);
	}
	return ledsFd;
}

void
mxc_gpio_close(void)
{
	if (gpioFd != -1) {
		close(gpioFd);
		gpioFd = -1;
	}
}

void
mxc_leds_close(void)
{
	if (ledsFd != -1) {
		close(ledsFd);
		ledsFd = -1;
	}
}

int
mxc_set_gpio(int gpioIdx, int val)
{
	char buf[2];
	if (gpioFd < 0) return -1;
	buf[0] = (char)gpioIdx;
	buf[1] = (char)val;
	return write(gpioFd, buf, 2);
}

int
mxc_set_leds(int val)
{
	unsigned char c = (unsigned char)val;
	if (ledsFd < 0) return -1;
	return write(ledsFd, &c, 1);
}

int
mxc_get_gpio(int gpioIdx)
{
	char c;
	if (gpioFd < 0) return -1;
	c = (char)gpioIdx;
	if (read(gpioFd, &c, 1) == 1)
		return (int)c;
	return -1;
}

int
mxc_get_leds(void)
{
	unsigned char c;
	if (ledsFd < 0) return -1;
	if (read(ledsFd, &c, 1) == 1)
		return (int)c;
	return -1;
}

int
mxc_set_alarm(int idx, int val)
{
	return mxc_set_gpio(idx, val);
}
