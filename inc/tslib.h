#ifndef _TSLIB_H_
#define _TSLIB_H_
/*
 *  tslib/src/tslib.h
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.
 *
 * $Id: tslib.h,v 1.4 2005/02/26 01:47:23 kergoth Exp $
 *
 * Touch screen library interface definitions.
 */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include <stdarg.h>
#include <sys/time.h>

struct tsdev;

struct ts_sample {
	int		x;
	int		y;
	unsigned int	pressure;
	struct timeval	tv;
};

/*
 * Close the touchscreen device, free all resources.
 */
extern int ts_close(struct tsdev *);

/*
 * Configure the touchscreen device.
 */
//TSAPI int ts_config(struct tsdev *);

/*
 * Change this hook to point to your custom error handling function.
 */
extern int (*ts_error_fn)(const char *fmt, va_list ap);

/*
 * Returns the file descriptor in use for the touchscreen device.
 */
extern int ts_fd(struct tsdev *);

/*
 * Load a filter/scaling module
 */
//TSAPI int ts_load_module(struct tsdev *, const char *mod, const char *params);

/*
 * Open the touchscreen device.
 */
extern struct tsdev *ts_open(const char *dev_name, int nonblock);

/*
 * Return a scaled touchscreen sample.
 */
extern int ts_read(struct tsdev *, struct ts_sample *, int);

/*
 * Returns a raw, unscaled sample from the touchscreen.
 */
extern int ts_read_raw(struct tsdev *, struct ts_sample *, int);

extern int    ts_cal_inited();
extern void  ts_load_calibration();

extern void  ts_ref_xy(int *x, int *y);
extern int    ts_do_calibration(void);
extern int    ts_get_sample (struct tsdev *ts, int index);
extern int    ts_save_calibration(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _TSLIB_H_ */

