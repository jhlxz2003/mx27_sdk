#ifndef __THREAD_H__
#define __THREAD_H__

#include <pthread.h>

typedef struct _thread {
	pthread_t     thr_id;
	volatile int  thr_st;
	volatile int  thr_q;
	pthread_mutex_t   thr_mtx;
	pthread_cond_t    thr_cnd;

	int  (*prepare) (void);
	void (*run) (void*);
	void *priv_data;
	char *name;
} thread_t;

#define thread_priv_data(thr)   ((thr)->priv_data)

#define THREAD_LOOP_START(thr)  while (!(thr)->thr_q) {
#define THREAD_LOOP_END         }

#define THREAD_LOOP(thr)    while (!(thr)->thr_q)

#define THREAD_NOT_RUNNING(thr)  Thread_Lock(thr); \
								if ((thr)->thr_st == 0) {

#define THREAD_IS_RUNNING(thr)  Thread_Lock(thr); \
								if ((thr)->thr_st == 1) {

#define THREAD_END(thr)         } \
                                Thread_UnLock(thr); 

int  new_thread(thread_t *thr, void (*func)(void*), void *arg, char *name, int (*pre)(void));
int  run_thread(thread_t *thr);
int  run_thread_p_locked(thread_t *thr);
void suspend_thread(thread_t *thr);

/* called by user run function */
void  thread_run_quit(thread_t *thr);
void  thread_run_prepared(thread_t *thr);

void  Thread_Lock(thread_t *thr);
void  Thread_UnLock(thread_t *thr);

int   fd_can_read(int fd, int ms);

#endif

