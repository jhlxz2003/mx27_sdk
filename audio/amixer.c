#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "dbg.h"

#define MIXER_OUT_VOL     1
#define MIXER_IN_VOL      2
#define MIXER_SPK_SW      3
#define MIXER_LO_SW       4
#define MIXER_INMUTE      5
#define MIXER_OUTMUTE     6
#define MIXER_MIC_BOOST   7
#define MIXER_MIC_AGC     8
#define MIXER_AEC_SW      9
#define MIXER_LEC_SW      10
#define MIXER_NR_SW       11
#define MIXER_MIC_BF      12
#define MIXER_DRC_SW      13

#define MIXER_ELEMENTS_NUM     13

typedef void (*mixer_callback)(int, long);

static mixer_callback  mixer_cb[MIXER_ELEMENTS_NUM] = {NULL};

#define CLIPVAL(val, min, max) \
	(((val) < (min)) ? (min) : (((val) > (max)) ? (max) : (val)))

static pthread_mutex_t  mixer_mtx = PTHREAD_MUTEX_INITIALIZER;
static snd_ctl_t *m_handle = NULL;
static int        mixer_ref = 0;
static pthread_t  mixer_tid = 0;
static int        mixer_thread_running = 0;

#define mixer_lock()     pthread_mutex_lock(&mixer_mtx)
#define mixer_unlock()   pthread_mutex_unlock(&mixer_mtx)

#ifdef DEBUG
static const char *
control_type(snd_ctl_elem_info_t *info)
{
	return snd_ctl_elem_type_name(snd_ctl_elem_info_get_type(info));
}

static const char *
control_access(snd_ctl_elem_info_t *info)
{
	static char result[10];
	char *res = result;

	*res++ = snd_ctl_elem_info_is_readable(info) ? 'r' : '-';
	*res++ = snd_ctl_elem_info_is_writable(info) ? 'w' : '-';
	*res++ = snd_ctl_elem_info_is_inactive(info) ? 'i' : '-';
	*res++ = snd_ctl_elem_info_is_volatile(info) ? 'v' : '-';
	*res++ = snd_ctl_elem_info_is_locked(info) ? 'l' : '-';
	*res++ = '\0';
	return result;
}

static void
show_control_id(snd_ctl_elem_id_t *id)
{
	unsigned int index, device, subdevice;
	printf("numid=%u,name='%s'", snd_ctl_elem_id_get_numid(id), snd_ctl_elem_id_get_name(id));
	index = snd_ctl_elem_id_get_index(id);
	device = snd_ctl_elem_id_get_device(id);
	subdevice = snd_ctl_elem_id_get_subdevice(id);
	if (index)
		printf(",index=%i", index);
	if (device)
		printf(",device=%i", device);
	if (subdevice)
		printf(",subdevice=%i", subdevice);
}
#endif

//////////////////////////////////////////////////////
//                handle mixer events               //
//////////////////////////////////////////////////////
#if 0
{
#endif

static void
mixer_events_value(snd_hctl_elem_t *helem)
{
    long value;
    int  numid;
    int  err;
    snd_ctl_elem_type_t   type;
	snd_ctl_elem_value_t *control;
	snd_ctl_elem_id_t    *id;
	snd_ctl_elem_info_t  *info;

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_value_alloca(&control);
	snd_ctl_elem_info_alloca(&info);

	snd_hctl_elem_get_id(helem, id);

#ifdef DEBUG
	printf("--- event value: ");
	show_control_id(id);
	printf(" ---\n");
#endif

    numid = snd_ctl_elem_id_get_numid(id);
    if ((err = snd_hctl_elem_info(helem, info)) < 0)
    {
        DBG("--- snd_hctl_elem_info() failed: %s ---\n", snd_strerror(err));
		return;
    }
    type = snd_ctl_elem_info_get_type(info);

    if ((err = snd_hctl_elem_read(helem, control)) < 0)
    {
        DBG("--- Control default element read error: %s ---\n", snd_strerror(err));
		return;
    }

    switch (type)
	{
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		value = snd_ctl_elem_value_get_boolean(control, 0);
		break;
	case SND_CTL_ELEM_TYPE_INTEGER:
		value = snd_ctl_elem_value_get_integer(control, 0);
		break;
	case SND_CTL_ELEM_TYPE_BYTES:
		value = snd_ctl_elem_value_get_byte(control, 0);
		break;
	default:
		return;
	}

    DBG("--- value = %ld ---\n", value);
	if (numid <= MIXER_ELEMENTS_NUM&&numid > 0)
	{
	    if (mixer_cb[numid-1] != NULL)
	    {
	        (*mixer_cb[numid-1])(numid, value);
	    }
	}
}

static int
element_callback(snd_hctl_elem_t *elem, unsigned int mask)
{
	if (mask & SND_CTL_EVENT_MASK_VALUE)
		mixer_events_value(elem);
	return 0;
}

static void
mixer_events_add(snd_hctl_elem_t *helem)
{
#ifdef DEBUG
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_id_alloca(&id);
	snd_hctl_elem_get_id(helem, id);

	printf("event add: ");
	show_control_id(id);
	printf("\n");
#endif
	snd_hctl_elem_set_callback(helem, element_callback);
}

static int
ctl_callback(snd_hctl_t *ctl, unsigned int mask, snd_hctl_elem_t *elem)
{
	if (mask & SND_CTL_EVENT_MASK_ADD)
		mixer_events_add(elem);
	return 0;
}

static void *
mixer_event_thread(void *data)
{
    snd_hctl_t *handle;
    snd_hctl_elem_t *helem;
	int err;

    pthread_detach(pthread_self());
    if ((err = snd_hctl_open_ctl(&handle, m_handle)) < 0)
    {
		DBG("--- Control default open error: %s\n", snd_strerror(err));
		pthread_exit(NULL);
	}

	snd_hctl_set_callback(handle, ctl_callback);
	if ((err = snd_hctl_load(handle)) < 0)
	{
		DBG("--- Control default hbuild error: %s ---\n", snd_strerror(err));
		snd_hctl_free(handle);
		free(handle);
		pthread_exit(NULL);
	}

	for (helem = snd_hctl_first_elem(handle); helem; helem = snd_hctl_elem_next(helem))
    {
		snd_hctl_elem_set_callback(helem, element_callback);
	}

	DBG("--- Ready to listen... ---\n");
	while (mixer_thread_running)
	{
		int res = snd_hctl_wait(handle, -1);
		if (res >= 0)
		{
			DBG("--- Poll ok: %i ---\n", res);
			mixer_lock();
			snd_hctl_handle_events(handle);
			mixer_unlock();
		}
	}

	snd_hctl_free(handle);
	free(handle);

	pthread_exit(NULL);
}

static void
mixer_thread_init(void)
{
    pthread_attr_t       pth_attrs;
    struct sched_param   pth_params;

    mixer_thread_running = 1;
    pthread_attr_init(&pth_attrs);
    pthread_attr_getschedparam(&pth_attrs, &pth_params);
    pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
    pthread_attr_setschedparam(&pth_attrs, &pth_params);
    pthread_create(&mixer_tid, &pth_attrs, mixer_event_thread, NULL);
    pthread_attr_destroy(&pth_attrs);
}

#if 0
}
#endif

int
mixer_open(void)
{
    int err = 0;
 
    mixer_lock();
    if (mixer_ref == 0)
    {
        if ((err = snd_ctl_open(&m_handle, "default", 0)) < 0)
        {
            DBG("--- snd_ctl_open() failed: %s ---\n", snd_strerror(err));
            goto _err;
        }
    }
    ++mixer_ref;
_err:
    mixer_unlock();

    return mixer_ref;
}

void
mixer_close(void)
{
	mixer_lock();
    if (mixer_ref > 0)
    {
        --mixer_ref;
        if (mixer_ref == 0)
        {
            snd_ctl_close(m_handle);
            m_handle = NULL;
        }
    }
    mixer_unlock();
}

/* roflag: 0-setting, 1-getting */
static int
mixer_control(int cmd, long *value, int roflag)
{
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *control;

    snd_ctl_elem_type_t type;
	unsigned int idx, count;
	long tmp, min, max;
	int  err;

    mixer_lock();
    if (m_handle == NULL)
    {
    	mixer_unlock();
    	return -1;
    }

	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_info_alloca(&info);
	snd_ctl_elem_value_alloca(&control);

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_numid(id, cmd);
    snd_ctl_elem_info_set_id(info, id);

	if ((err = snd_ctl_elem_info(m_handle, info)) < 0)
	{
		DBG("--- Cannot find the given element from control default ---\n");
		mixer_unlock();
		return err;
	}

    /* FIXME: Remove it when hctl find works ok !!! */
	snd_ctl_elem_info_get_id(info, id);
	type = snd_ctl_elem_info_get_type(info);
	count = snd_ctl_elem_info_get_count(info);

#ifdef DEBUG
    show_control_id(id);
    DBG("--- type = %s, access = %s, count = %i ---\n", control_type(info), control_access(info), count);
#endif

    if (roflag == 0)
    {
        snd_ctl_elem_value_set_id(control, id);
	    for (idx = 0; idx < count; idx++)
	    {
            switch (type)
            {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
        	    tmp = ((*value != 0) ? 1 : 0);
                snd_ctl_elem_value_set_boolean(control, idx, tmp);
			    break;
		    case SND_CTL_ELEM_TYPE_INTEGER:
			    min = snd_ctl_elem_info_get_min(info);
			    max = snd_ctl_elem_info_get_max(info);
			    tmp = CLIPVAL(*value, min, max);
                snd_ctl_elem_value_set_integer(control, idx, tmp);
                break;
		    case SND_CTL_ELEM_TYPE_BYTES:
			    tmp = CLIPVAL(*value, 0, 255);
			    snd_ctl_elem_value_set_byte(control, idx, tmp);
			    break;
		    default:
		        DBG("--- the type not supported ---\n");
		        mixer_unlock();
		        return -1;
		    }
	    }

	    if ((err = snd_ctl_elem_write(m_handle, control)) < 0)
	    {
		    DBG("Control default element write DBG: %s\n", snd_strerror(err));
	    }
	}
	else
	{
		snd_hctl_t *hctl;
		snd_hctl_elem_t *elem;

		if ((err = snd_hctl_open_ctl(&hctl, m_handle)) < 0)
	    {
			DBG("Control default open DBG: %s\n", snd_strerror(err));
			goto __quit;
		}

		if ((err = snd_hctl_load(hctl)) < 0)
	    {
			DBG("Control default load DBG: %s\n", snd_strerror(err));
			goto __hctl_quit;
		}
		elem = snd_hctl_find_elem(hctl, id);
		if (elem)
		{
			if ((err = snd_hctl_elem_read(elem, control)) < 0)
			{
			    DBG("Control default element read DBG: %s\n", snd_strerror(err));
			    goto __hctl_quit;
		    }
//		    for (idx = 0; idx < count; idx++)
//		    {
			    switch (type)
			    {
			    case SND_CTL_ELEM_TYPE_BOOLEAN:
				    *value = snd_ctl_elem_value_get_boolean(control, 0);
				    break;
			    case SND_CTL_ELEM_TYPE_INTEGER:
				    *value = snd_ctl_elem_value_get_integer(control, 0);
				    break;
			    case SND_CTL_ELEM_TYPE_BYTES:
				    *value = snd_ctl_elem_value_get_byte(control, 0);
				    break;
			    default:
				    break;
			    }
//		    }
		}
		else
		{
			printf("Could not find the specified element\n");
			err = -1;
		}
__hctl_quit:
		snd_hctl_free(hctl);
		free(hctl);
	}
__quit:
	mixer_unlock();

	return err;
}

//////////////////////////////////////////////////////
//                   external API                   //
//////////////////////////////////////////////////////
#if 0
{
#endif

static char g_mixer[MIXER_ELEMENTS_NUM] = {0xff};

static int
_mixer_set(int cmd, int value)
{
    int err;
    long v = (long)value;
    if (mixer_open() > 0)
    {
        err = mixer_control(cmd, &v, 0);
        mixer_close();
        return err;
    }
    return -1;
}

static int
mixer_get(int cmd)
{
    int  err;
    long v;
    if (mixer_open() > 0)
    {
        err = mixer_control(cmd, &v, 1);
        mixer_close();
        if (err < 0)
        {
            return -1;
        }
        else
        {
            return (int)v;
        }
    }
    return -1;
}

static int
mixer_set(int cmd, int value)
{
	if (cmd <= 0||cmd > MIXER_ELEMENTS_NUM) return -1;
	if (((char)value) != g_mixer[cmd - 1]) {
		if (_mixer_set(cmd, value) >= 0) {
			g_mixer[cmd - 1] = (char)value;
			return 1;
		}
		return -1;
	}
	return 0;
}

void
mxc_speaker_on(int on)
{
    mixer_set(MIXER_SPK_SW, on);
}

void
mxc_lineout_on(int on)
{
    mixer_set(MIXER_LO_SW, on);
}

/*
void
mxc_capture_mode(int m)
{
    mixer_set(MIXER_INROUTE, m);
}
*/

void
mxc_playback_mute(int m)
{
    mixer_set(MIXER_OUTMUTE, m);
}

void
mxc_capture_mute(int m)
{
    mixer_set(MIXER_INMUTE, m);
}

void
mxc_playback_volume_set(int vol)
{
    if (vol < 0||vol > 79) return;
    mixer_set(MIXER_OUT_VOL, vol);
}

void
mxc_capture_volume_set(int vol)
{
    if (vol < 0||vol > 79) return;
    mixer_set(MIXER_IN_VOL, vol);
}

int
mxc_playback_volume_get(void)
{
    return g_mixer[MIXER_OUT_VOL-1];
}

int
mxc_capture_volume_get(void)
{
    return g_mixer[MIXER_IN_VOL-1];
}

void
mxc_mixer_set_callback(int numid, mixer_callback cb)
{
    if (numid > 0&&numid <= MIXER_ELEMENTS_NUM)
    {
        mixer_cb[numid-1] = cb;
    }
}

void
mxc_mixer_set_aec(int on)
{
	mixer_set(MIXER_AEC_SW, on);
}

void
mxc_mixer_set_lec(int on)
{
	mixer_set(MIXER_LEC_SW, on);
}

void
mxc_mixer_set_drc(int on)
{
	mixer_set(MIXER_DRC_SW, on);
}

void
mxc_mixer_set_beam_forming(int on)
{
	mixer_set(MIXER_MIC_BF, on);
}

void
mxc_mixer_set_noise_reduction(int on)
{
	mixer_set(MIXER_NR_SW, on);
}

/* called when system start[init.c] */
void
mxc_mixer_init(void)
{
	int  i;
    long v;
    if (mixer_open() > 0)
    {
    	for (i = 0; i < MIXER_ELEMENTS_NUM; ++i) {
        	if (mixer_control(i+1, &v, 1) < 0) continue;
        	g_mixer[i] = (char)v;
        }
        mixer_close();
    }
}

#if 0
}
#endif
