#ifndef __AMIXER_H__
#define __AMIXER_H__

extern void  mxc_mixer_init(void);
extern int   mixer_open(void);
extern void  mixer_close(void);
extern void  mxc_mixer_set_aec(int on);
extern void  mxc_mixer_set_lec(int on);
extern void  mxc_mixer_set_drc(int on);
extern void  mxc_mixer_set_beam_forming(int on);
extern void  mxc_mixer_set_noise_reduction(int on);

#endif
