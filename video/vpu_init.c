#include <sys/stat.h>
#include <sys/time.h>
//#include <sys/types.h>
//#include <sys/socket.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vpu.h"
#include "dbg.h"

static int  vpu_ref = 0;
static pthread_mutex_t   vpu_mtx = PTHREAD_MUTEX_INITIALIZER;

#define  VPU_LOCK()      pthread_mutex_lock(&vpu_mtx)
#define  VPU_UNLOCK()    pthread_mutex_unlock(&vpu_mtx)

int
VPU_init(void)
{
	vpu_versioninfo ver;
	int  err = 0;

	VPU_LOCK();
	if (vpu_ref == 0)
	{
		err = IOSystemInit(NULL);
		if (err)
		{
			DBG("IOSystemInit failure\n");
			goto quit;
		}

		err = vpu_GetVersionInfo(&ver);
		if (err)
		{
			DBG("Cannot get version info\n");
			IOSystemShutdown();
			goto quit;
		}

		DBG("VPU firmware version: %d.%d.%d\n", ver.fw_major, ver.fw_minor, ver.fw_release);
		DBG("VPU library version: %d.%d.%d\n", ver.lib_major, ver.lib_minor, ver.lib_release);
	}
	else if (vpu_ref >= VPU_MAX_INSTANCE)
	{
		err = -1;
		goto quit;
	}
	++vpu_ref;

quit:
	VPU_UNLOCK();

	return err;
}

void
VPU_uninit(void)
{
	VPU_LOCK();
	if (vpu_ref > 0)
	{
		--vpu_ref;
		if (vpu_ref == 0)
		{
			IOSystemShutdown();
		}
	}
	VPU_UNLOCK();
}



