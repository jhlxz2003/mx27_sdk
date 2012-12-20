#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>  
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define  RS485     "/dev/ttymxc2"

#define  enable_WR()    mxc_set_gpio(2, 0)
#define  enable_RD()    mxc_set_gpio(2, 1)

static int speed_arr[] = {B115200, B38400, B19200, B9600, B4800, B2400, B1200, B300, 0};
static int name_arr[]  = {115200, 38400, 19200, 9600, 4800, 2400, 1200, 300, 0};

static int   rs485_fd = -1;
struct termios   SaveTermios;

extern int mxc_set_gpio(int gpioIdx, int val);

static int
writen(int fd, const void *vptr, size_t n)
{
	size_t nleft;
	ssize_t nwritten;
	const char *ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
			if (nwritten < 0 && errno == EINTR)
				nwritten = 0;
			else
				return (-1);
		}

		nleft -= nwritten;
		ptr += nwritten;
	}
	return (n);
}

static void
mxc_flush_rs485()
{
	while ((tcflush(rs485_fd, TCIOFLUSH) == -1)&&(errno == EINTR));
}

static void
mxc_drain_rs485()
{
	while ((tcdrain(rs485_fd) == -1)&&(errno == EINTR));
}

int
mxc_open_rs485()
{
	if (rs485_fd == -1) {
		if((rs485_fd = open(RS485, O_RDWR)) < 0) {
			return -1;
		}
		tcgetattr(rs485_fd, &SaveTermios);
		enable_RD();
	}
	return rs485_fd;
}

void
mxc_close_rs485()
{
	if (rs485_fd != -1)
	{
		mxc_flush_rs485();
		tcsetattr(rs485_fd, TCSADRAIN, &SaveTermios);
		close(rs485_fd);
		rs485_fd = -1;
	}
}

int
mxc_set_rs485(int speed, int databits, int stopbits, int parity, int vtime, int vmin)
{
	int    i;
	struct termios  options;

	options = SaveTermios;
	mxc_flush_rs485();
	for (i = 0; name_arr[i]; ++i)
	{
		if (speed == name_arr[i])
		{
			cfsetispeed(&options, speed_arr[i]);
			cfsetospeed(&options, speed_arr[i]);
			if (tcsetattr(rs485_fd, TCSANOW, &options) != 0) return -1;
			break;
		}
	}

	if (tcgetattr(rs485_fd, &options) != 0) return -1;

	options.c_cflag &= ~CSIZE;
	switch (databits) /*设置数据位数*/
	{
	case 7:
		options.c_cflag |= CS7;
		break;
	case 8:
		options.c_cflag |= CS8;
		break;
	default:
		printf("Unsupported data size\n");
		return -1;
	}

	/* 设置停止位*/  
	switch (stopbits)
	{
	case 1:
		options.c_cflag &= ~CSTOPB;
		break;
	case 2:
		options.c_cflag |= CSTOPB;
	   	break;
	default:
		printf("Unsupported stop bits\n");
		return -1;
	}

	switch (parity)
	{
	case 0:
		options.c_cflag &= ~PARENB;  /* Clear parity enable */
		options.c_iflag &= ~INPCK;   /* disable input parity checking */
		break;
	case 1:
		options.c_cflag |= (PARODD | PARENB); /* 设置为奇效验*/
		options.c_iflag |= INPCK;    /* enable input parity checking */
		break;
	case 2:
		options.c_cflag |= PARENB;    /* Enable parity */
		options.c_cflag &= ~PARODD;   /* 转换为偶效验*/
		options.c_iflag |= INPCK;     /* enable input parity checking */
		break;
	default:
		printf("Unsupported parity\n");
		return -1;
	}

	options.c_iflag &= ~(BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXOFF);
	options.c_iflag |= IGNBRK;
	options.c_oflag &= ~OPOST;                  
	options.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	options.c_cflag |= CLOCAL|CREAD;

	options.c_cc[VTIME] = vtime;
	options.c_cc[VMIN] = vmin;

	if (tcsetattr(rs485_fd, TCSANOW, &options) != 0)
		return -1;
	return 0;
}

int
mxc_read_rs485(void *pBuf, size_t nCount)
{
	return read(rs485_fd, pBuf, nCount);
}

int
mxc_write_rs485(const void *buf, size_t count)
{
	int  ret = 0;

	mxc_flush_rs485();
	enable_WR();
	ret = writen(rs485_fd, buf, count);
	mxc_drain_rs485();
	enable_RD();

	return ret;
}


