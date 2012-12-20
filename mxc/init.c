#include <stdio.h>
#include "dbg.h"
#include "../audio/af_dec.h"
#include "../audio/mxc_alsa.h"
#include "../audio/audio_codec.h"
#include "../video/vpu.h"
#include "../video/vio.h"
#include "../voip/access.h"
#include "../voip/server.h"
#include "../audio/amixer.h"

extern int  mxc_fb_init(void);
extern void mxc_fb_finish(void);
extern int  mxc_rtc_init(void);
extern int  mxc_gpio_init(void);
extern void mxc_gpio_close(void);
extern void mxc_overlay_init(void);

extern void aout_Init(void);


/*
#define SPEECH_CODEC_ID_PCMU       0
#define SPEECH_CODEC_ID_PCMA       1
#define SPEECH_CODEC_ID_G722       2
#define SPEECH_CODEC_ID_GSM        3
#define SPEECH_CODEC_ID_G7221      4

#define SPEECH_CODEC_NAME_PCMU       "pcmu"
#define SPEECH_CODEC_NAME_PCMA       "pcma"
#define SPEECH_CODEC_NAME_G722       "g722"
#define SPEECH_CODEC_NAME_GSM        "gsm"
#define SPEECH_CODEC_NAME_G7221      "g7221/16000"
*/

int
InitSystem()
{
	if (mxc_fb_init() < 0)
	{
		DBG("--- mxc_fb_init error ---\n");
		return -1;
	}

	if (mxc_rtc_init() < 0)
	{
		DBG("--- mxc_rtc_init error ---\n");
		return -1;
	}

	if (mxc_gpio_init() < 0)
	{
		DBG("--- mxc_gpio_init error ---\n");
		return -1;
	}

	VPU_framebuf_init();
	mxc_overlay_init();

	if (mxc_vpu_encoder_init() < 0)
	{
		DBG("--- mxc_vpu_encoder_init error ---\n");
		return -1;
	}

	if (mxc_vpu_decoder_init() < 0)
	{
		DBG("--- mxc_vpu_decoder_init error ---\n");
		return -1;
	}

	aout_Init();
	af_decoder_init();

	////////////////////////////////
	init_audio_grabber();
	mxc_speech_codec_prepare();
	mxc_speech_codec_set_codec_by_id(SPEECH_CODEC_ID_G722);
	mxc_speech_codec_init();
	
	mxc_audio_encoder_init();
	mxc_audio_decoder_init();

	mxc_media_init();
	mxc_mixer_init();
	/////////////////////////////////

	start_media_server();

	return 0;
}

void
FiniSystem()
{
	mxc_speech_codec_destroy();
    mxc_speech_codec_finish();
    mxc_gpio_close();
	mxc_fb_finish();
}


