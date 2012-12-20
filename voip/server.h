#ifndef SERVER_H_
#define SERVER_H_

extern void  start_media_server(void);
extern char *mxc_get_leaveword_fname(void);
extern void  mxc_leaveword_stop(void);
extern int   mxc_leaveword_start(int cam, int type);
extern int   audio_packet_put(char *buffer, int size);
extern void  ev_packet_new(char *buffer, int size);

#endif

