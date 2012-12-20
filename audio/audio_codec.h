#ifndef AUDIO_CODEC_H_
#define AUDIO_CODEC_H_


#define SPEECH_CODEC_ID_PCMU       0
#define SPEECH_CODEC_ID_PCMA       1
#define SPEECH_CODEC_ID_G722       2
#define SPEECH_CODEC_ID_GSM        3
#define SPEECH_CODEC_ID_G7221      4

#define SPEECH_CODEC_NAME_PCMU     "pcmu"
#define SPEECH_CODEC_NAME_PCMA     "pcma"
#define SPEECH_CODEC_NAME_G722     "g722"
#define SPEECH_CODEC_NAME_GSM      "gsm"
#define SPEECH_CODEC_NAME_G7221    "g7221/16000"

/* audio decode in standalone thead. */
#define AUDIO_DECODER_THREAD       0

/* audio output in standalone thread. */
#define SPEECH_AOUT_THREAD         0

extern int  mxc_speech_codec_destroy(void);
extern int  mxc_speech_codec_init(void);
extern void mxc_speech_codec_set_codec_by_name(char *name);
extern void mxc_speech_codec_set_codec_by_id(int id);
extern void mxc_speech_codec_prepare(void);
extern void mxc_speech_codec_finish(void);

//////////////////////////////////////////////////
extern int  start_audio_encoder(void);
extern void stop_audio_encoder(void);
extern void mxc_audio_encoder_init(void);

extern int  audio_decoder_prepare(void);
extern void audio_decoder_finish(void);
extern void mxc_play_audio(char *buf, int size);
extern void mxc_audio_decoder_init(void);

#endif

