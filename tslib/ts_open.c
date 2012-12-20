/*
 *  tslib/src/ts_open.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 * $Id: ts_open.c,v 1.4 2004/07/21 19:12:59 dlowder Exp $
 *
 * Open a touchscreen device.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <linux/input.h>
#include "tslib-private.h"

typedef struct tslib_module_info* (*MOD_INIT_FUNC)(struct tsdev *dev, const char *params);

static const char *ts_par[] = {NULL, "pmin=1", "delta=30", "delta=100", NULL};

static MOD_INIT_FUNC mod_init[] = {&input_mod_init, 
	&pthres_mod_init, 
	&variance_mod_init, 
	&dejitter_mod_init,
	&linear_mod_init
	};

static int
ts_load_module(struct tsdev *ts, int i, int raw)
{
	struct tslib_module_info *info;
	int  ret = 0;
		
	info = (mod_init[i])(ts, ts_par[i]);
	if (!info) {
		return -1;
	}

	if (raw) {
		ret = __ts_attach_raw(ts, info);
	} else {
		ret = __ts_attach(ts, info);
	}

	if (ret) {
		info->ops->fini(info);
	}

	return ret;
}

struct tsdev *
ts_open(const char *name, int nonblock)
{
	struct tsdev *ts;
	int flags = O_RDONLY;

	if (nonblock)
		flags |= O_NONBLOCK;

	ts = malloc(sizeof(struct tsdev));
	if (ts) {
		memset(ts, 0, sizeof(struct tsdev));

		ts->fd = open(name, flags);
		if (ts->fd == -1)
			goto free;

		ts_load_module(ts, 0, 1);
		ts_load_module(ts, 1, 0);
		ts_load_module(ts, 2, 0);
		ts_load_module(ts, 3, 0);
		ts_load_module(ts, 4, 0);
	}

	return ts;

free:
	free(ts);
	return NULL;
}

