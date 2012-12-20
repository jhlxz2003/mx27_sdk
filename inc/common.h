#ifndef _COMMON_H_
#define _COMMON_H_

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

static inline void mdelay(int ms)
{
	struct timeval tv;
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	select(0, NULL, NULL, NULL, &tv);
}

#endif

