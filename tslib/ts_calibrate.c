/*
 *  tslib/tests/ts_calibrate.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the GPL.  Please see the file
 * COPYING for more details.
 *
 * $Id: ts_calibrate.c,v 1.8 2004/10/19 22:01:27 dlowder Exp $
 *
 * Basic test program for touchscreen library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include "dbg.h"
#include "tslib.h"

#define SCR_W    800
#define SCR_H    480

#define MAX_SAMPLES   128

typedef struct {
	int x[5], xfb[5];
	int y[5], yfb[5];
	int a[7];
} calibration;

static calibration g_tscal;
static int  ref_x[5] = {50, SCR_W-50, SCR_W-50, 50,       SCR_W/2};
static int  ref_y[5] = {50, 50,       SCR_H-50, SCR_H-50, SCR_H/2};

static int
sort_by_x(const void* a, const void *b)
{
	return (((struct ts_sample *)a)->x - ((struct ts_sample *)b)->x);
}

static int
sort_by_y(const void* a, const void *b)
{
	return (((struct ts_sample *)a)->y - ((struct ts_sample *)b)->y);
}

static int
getxy(struct tsdev *ts, int *x, int *y)
{
	struct ts_sample samp[MAX_SAMPLES];
	int index, middle;

	do {
		if (ts_read_raw(ts, &samp[0], 1) < 0) {
			perror("ts_read");
			return(-1);
		}
		
	} while (samp[0].pressure == 0);

	/* Now collect up to MAX_SAMPLES touches into the samp array. */
	index = 0;
	do {
		if (index < MAX_SAMPLES-1)
			index++;
		if (ts_read_raw(ts, &samp[index], 1) < 0) {
			perror("ts_read");
			return(-1);
		}
	} while (samp[index].pressure > 0);
//	printf("Took %d samples...\n",index);

	/*
	 * At this point, we have samples in indices zero to (index-1)
	 * which means that we have (index) number of samples.  We want
	 * to calculate the median of the samples so that wild outliers
	 * don't skew the result.  First off, let's assume that arrays
	 * are one-based instead of zero-based.  If this were the case
	 * and index was odd, we would need sample number ((index+1)/2)
	 * of a sorted array; if index was even, we would need the
	 * average of sample number (index/2) and sample number
	 * ((index/2)+1).  To turn this into something useful for the
	 * real world, we just need to subtract one off of the sample
	 * numbers.  So for when index is odd, we need sample number
	 * (((index+1)/2)-1).  Due to integer division truncation, we
	 * can simplify this to just (index/2).  When index is even, we
	 * need the average of sample number ((index/2)-1) and sample
	 * number (index/2).  Calculate (index/2) now and we'll handle
	 * the even odd stuff after we sort.
	 */
	middle = index/2;
	if (x) {
		qsort(samp, index, sizeof(struct ts_sample), sort_by_x);
		if (index & 1)
			*x = samp[middle].x;
		else
			*x = (samp[middle-1].x + samp[middle].x) / 2;
	}
	if (y) {
		qsort(samp, index, sizeof(struct ts_sample), sort_by_y);
		if (index & 1)
			*y = samp[middle].y;
		else
			*y = (samp[middle-1].y + samp[middle].y) / 2;
	}
	return 0;
}

////////////////////////////////////////////////////
int
ts_do_calibration(void)
{
	calibration *cal;
	int j;
	float n, x, y, x2, y2, xy, z, zx, zy;
	float det, a, b, c, e, f, i;
	float scaling = 65536.0;

	cal = &g_tscal;
	// Get sums for matrix
	n = x = y = x2 = y2 = xy = 0;
	for(j=0;j<5;j++) {
		n += 1.0;
		x += (float)cal->x[j];
		y += (float)cal->y[j];
		x2 += (float)(cal->x[j]*cal->x[j]);
		y2 += (float)(cal->y[j]*cal->y[j]);
		xy += (float)(cal->x[j]*cal->y[j]);
	}

	// Get determinant of matrix -- check if determinant is too small
	det = n*(x2*y2 - xy*xy) + x*(xy*y - x*y2) + y*(x*xy - y*x2);
	if(det < 0.1 && det > -0.1) {
		DBG("ts_calibrate: determinant is too small -- %f\n",det);
		return -1;
	}

	// Get elements of inverse matrix
	a = (x2*y2 - xy*xy)/det;
	b = (xy*y - x*y2)/det;
	c = (x*xy - y*x2)/det;
	e = (n*y2 - y*y)/det;
	f = (x*y - n*xy)/det;
	i = (n*x2 - x*x)/det;

	// Get sums for x calibration
	z = zx = zy = 0;
	for(j=0;j<5;j++) {
		z += (float)cal->xfb[j];
		zx += (float)(cal->xfb[j]*cal->x[j]);
		zy += (float)(cal->xfb[j]*cal->y[j]);
	}

	// Now multiply out to get the calibration for framebuffer x coord
	cal->a[0] = (int)((a*z + b*zx + c*zy)*(scaling));
	cal->a[1] = (int)((b*z + e*zx + f*zy)*(scaling));
	cal->a[2] = (int)((c*z + f*zx + i*zy)*(scaling));
/*
	printf("%f %f %f\n",(a*z + b*zx + c*zy),
				(b*z + e*zx + f*zy),
				(c*z + f*zx + i*zy));
*/
	// Get sums for y calibration
	z = zx = zy = 0;
	for(j=0;j<5;j++) {
		z += (float)cal->yfb[j];
		zx += (float)(cal->yfb[j]*cal->x[j]);
		zy += (float)(cal->yfb[j]*cal->y[j]);
	}

	// Now multiply out to get the calibration for framebuffer y coord
	cal->a[3] = (int)((a*z + b*zx + c*zy)*(scaling));
	cal->a[4] = (int)((b*z + e*zx + f*zy)*(scaling));
	cal->a[5] = (int)((c*z + f*zx + i*zy)*(scaling));
/*
	printf("%f %f %f\n",(a*z + b*zx + c*zy),
				(b*z + e*zx + f*zy),
				(c*z + f*zx + i*zy));
*/
	// If we got here, we're OK, so assign scaling to a[6] and return
	cal->a[6] = (int)scaling;
	return 0;
}

int
ts_get_sample (struct tsdev *ts, int index)
{
	int  ret = -1;
	if (getxy(ts, &g_tscal.x[index], &g_tscal.y[index]) == 0)
	{
		g_tscal.xfb [index] = ref_x[index];
		g_tscal.yfb [index] = ref_y[index];
		ret = 0;
//		DBG("%s : X = %4d Y = %4d\n", tsPos[index], g_tscal.x[index], g_tscal.y[index]);
	}
	return ret;
}

int
ts_save_calibration(void)
{
	char cal_buffer[256];
	int cal_fd = -1;
	if ((cal_fd = open ("/etc/pointercal", O_CREAT | O_WRONLY)) < 0)
		return -1;
	sprintf (cal_buffer,"%d %d %d %d %d %d %d",
			 g_tscal.a[1], g_tscal.a[2], g_tscal.a[0],
			 g_tscal.a[4], g_tscal.a[5], g_tscal.a[3], g_tscal.a[6]);
	write (cal_fd, cal_buffer, strlen(cal_buffer) + 1);
	close (cal_fd);
	return 0;
}

void
ts_ref_xy(int *x, int *y)
{
	int i;
	for (i = 0; i < 5; i++)
	{
		x[i] = ref_x[i];
		y[i] = ref_y[i];
	}
}



