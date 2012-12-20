#ifndef _ACCESS_H_
#define _ACCESS_H_

extern int   start_vod(char *ip, int type, int proto);
extern void  stop_vod(void);
extern void  mxc_media_init(void);

extern void  mxc_audio_on(void);

#endif

