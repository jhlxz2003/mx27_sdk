#ifndef _MXC_ALSA_H_
#define _MXC_ALSA_H_

#include <alsa/asoundlib.h>
#include "dbg.h"

typedef struct {
    int  type;

    int  channels;
    int  rate;

    int  latency_ms;
    int  can_pause;

    snd_pcm_t *handle;
	snd_pcm_uframes_t chunk_size;
	unsigned int bytes_per_sample;
    unsigned int bits_per_sample;
    unsigned int bytes_per_frame;
    unsigned int blocksize;
    unsigned int buffersize;
    
    int  prepause_space;
    int  paused;
} alsa_t;

#define PTIME         20
#define LATENCY_MS    500

///////////////////////////////////////////////////////////
extern int   ai_open(alsa_t *a, int mode, int rate, int chls);
extern int   ai_close(alsa_t *a);
extern int   ai_start_capture(alsa_t *a);
extern int   ai_read(alsa_t *a, unsigned char *buffer);

extern void  init_audio_grabber(void);
extern int   audio_grabber_start(int rate);
extern void  audio_grabber_stop(void);
extern void  audio_frame_drain(void);
extern int   audio_frame_ptr(char **ptr);

extern int   audio_out_open(int rate, int chls);
extern void  audio_out_close(int imed);
extern int   audio_out_play(unsigned char *buf, int len);
extern int   audio_out_pause(void);
extern void  audio_out_resume(void);
extern int   audio_out_chunk_bytes(void);

#endif
