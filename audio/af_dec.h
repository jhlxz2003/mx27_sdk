#ifndef DECODER_H_
#define DECODER_H_

typedef struct _audio_info
{
	char    path[128];
	char   *name;
	int     duration;
	long    frames;
    double  fps;
	long    rate;
	int     channels;
	int     encoding;
	/* for wav file */
	unsigned int dataoff;
	unsigned int datasz;
	int     byte_p_sec;

	int     inited;
} af_info_t;

typedef struct _af_decoder {
	struct _af_decoder *next;
	int   type;
	int   delay;
	int  (*open)       (af_info_t *);
	int  (*play[2])    (void);
	void (*seek)       (double sec, int st);
	int  (*seek_end)   (int);
	void (*pause)      (void);
	void (*resume)     (void);
	void (*close)      (int imed);
} af_decoder_t;

#define AO_NONBLOCK_MODE   1

af_decoder_t *load_mp3_module(void);
af_decoder_t *load_wav_module(void);
extern void   register_af_decoder(af_decoder_t *dec);
extern int    af_decoder_init(void);

#endif
