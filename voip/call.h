#ifndef _CALL_H_
#define _CALL_H_

#include "ulib.h"

#define  MAX_TIMEOUT     (3*60)
#define  RING_TMEO       30
#define  TALK_TMEO_O     60
#define  TALK_TMEO_I     (3*60)

#define  LWCFM_TMEO      10
#define  LW_TMEO         30
#define  LM_TMEO         30

/* caller_handle() return this value. */
#define  EFNONR        1
#define  EFOK          0
#define  EFERR        -1

#define  TLK_LISTEN_PORT   2999
#define  HOST_PRT          5010

#define  VIDEO_SPK    0
#define  NVID_SPK     1

typedef struct ev
{
	UListHdr  list;
	struct event   ev;
	int    id;
	int    stat;
	int    ovt;
	char  ip[16];
} ev_t;

typedef int  (*PIPE_Fn)(char);
typedef int  (*NET_Fn)(int, char*, int);
typedef int  (*TMEO_Fn)(void);

typedef struct call_fn
{
	NET_Fn     nfn;
	PIPE_Fn    pfn;
	TMEO_Fn   tfn;
} call_fn_t;

typedef struct  tmeo
{
	int  *tmeo;
	int   max_tmeo;
} tmeo_t;

typedef struct _call {
	int    stat;
	int    ovt;
	int    ivt;
	int    sess_id;
	int    timerid;	
	char peer_ip[16];
	unsigned int  nr;
	unsigned int  minor;

	/* caller */
	char vod_ip[16];

	/* callee */
	int    disable_acc;
//	int    win_showed;
	int    lm_canceled;
	int    type;
	int    mode;
} call_t;

typedef void (*PVOID) (void);
typedef void (*PVOIDINT) (int);
typedef int  (*PINT) (int);

#define  get_cmd_type(buf, pp)       strtol(buf, (char**)pp, 10)

extern call_t call;
extern int  callee_type;
extern ev_t *slv_evt;

extern unsigned int  sdk_nr;
extern int    sdk_hostid;

extern char  *sdk_sgip;
extern char  *sdk_prip;

extern PVOID   fn_hup[2];
extern PVOID   fn_acc[2];
extern PVOID   fn_unlock[2];
extern PVOID   fn_sft[2];

extern PVOIDINT    fn_mute[2];
extern PVOIDINT    fn_cam[2];
extern PVOIDINT    fn_record[2];

extern PVOID     fn_prepare;
extern PVOID     fn_finish;
extern PVOID     notify_nv;
extern PVOID     notify_nv_rec;
extern PVOID     notify_acc_callee;

extern PVOIDINT  fn_set_nr;
extern PVOIDINT  ring_cb;

extern PVOID     show_from_outside;
extern PVOID     show_from_inside;
extern PVOID     last_ui;

#ifdef  __cplusplus
extern "C" {
#endif

int   mxc_caller_callout(unsigned int nr, int minor, int vt);

void  start_master_thread();
void  ring_slave();
void  ring_slave_nv();
void  hup_slave();

/* called by call_thread */
void  mxc_caller_leaveword();
void  mxc_caller_timeout();
void  slave_camera_onoff(int m);

void  video_off();
void  slave_callee_voff();
void  start_call_proc();

/* called by master thread */
void  slave_shift();
void  slave_accept(int v);
void  slave_hup();
void  slave_open();
void  camera_on_slave();
void  camera_off_slave();
void  mute_slave(int i);
void  acc_slave();

void  start_slave_thread();

int   call_trylock();
void  call_unlock();

void  voice_channel_on(char *ip);
void  voice_channel_off();


int   start_audio_vod(char *ip);
void  stop_audio_vod();

#ifdef __cplusplus
}
#endif
#endif


