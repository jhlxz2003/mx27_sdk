#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pjlib.h>
#include "thread.h"

#define thread_lock(thr)     pthread_mutex_lock(&(thr)->thr_mtx)
#define thread_unlock(thr)   pthread_mutex_unlock(&(thr)->thr_mtx)

#define thread_p(thr)        pthread_cond_wait(&(thr)->thr_cnd, &(thr)->thr_mtx)
#define thread_v(thr)        pthread_cond_signal(&(thr)->thr_cnd)

#define thread_NP(thr, i)   \
	do {  \
		thread_lock(thr); \
		while ((thr)->thr_st == i)  \
			thread_p(thr);   \
		thread_unlock(thr);  \
	} while(0)

#define thread_V(thr, i)   \
	do {  \
		thread_lock(thr);  \
		(thr)->thr_st = i; \
		thread_v(thr);     \
		thread_unlock(thr);  \
	} while (0)

void
thread_run_quit(thread_t *thr)
{
	thread_V(thr, 0);
}

void
thread_run_prepared(thread_t *thr)
{
	thread_V(thr, 1);
}

int
run_thread(thread_t *thr)
{
	int  ret = 0;
	int  err = 0;

	thread_lock(thr);
	if (thr->thr_st == 0) {
		if (thr->prepare) {
			err = thr->prepare();
		}

		if (err < 0) goto err_q;
		thr->thr_q = 0;
		thr->thr_st = 1;
		thread_v(thr);
		thread_p(thr);
		ret = thr->thr_st;
	}
err_q:
	thread_unlock(thr);

	return ret;
}

int
run_thread_p_locked(thread_t *thr)
{
	thr->thr_q = 0;
	thr->thr_st = 1;
	thread_v(thr);
	thread_p(thr);
	return thr->thr_st;
}

void
suspend_thread(thread_t *thr)
{
	thread_lock(thr);
	if (thr->thr_st != 0) {
		thr->thr_q = 1;
		while (thr->thr_st != 0)
			thread_p(thr);
	}
	thread_unlock(thr);
}

static void*
thread_main(void *arg)
{
	pj_thread_desc  desc;
	pj_thread_t    *this_thread;
	thread_t *thr = arg;

	pthread_detach(pthread_self());
	if (thr->name) {
		pj_thread_register(thr->name, desc, &this_thread);
	}

	while (1)
	{
		thread_NP(thr, 0);
		(*thr->run)(arg);
		thread_V(thr, 0);
	}

	pthread_exit(NULL);
}

int
new_thread(thread_t *thr, void (*func)(void*), void *arg, char *name, int (*pre)(void))
{
	thr->run = func;
	pthread_mutex_init(&thr->thr_mtx, NULL);
	pthread_cond_init(&thr->thr_cnd, NULL);
	thr->thr_q = 0;
	thr->thr_st = 0;
	thr->priv_data = arg;
	thr->name = name;
	thr->prepare = pre;
	return pthread_create(&thr->thr_id, NULL, thread_main, thr);
}

void
Thread_Lock(thread_t *thr)
{
	thread_lock(thr);
}

void
Thread_UnLock(thread_t *thr)
{
	thread_unlock(thr);
}

