/*
 * all the framebuffer allocated here store decoded data or raw data.
 * Maybe this should be accessed exlusive by mutex lock.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vpu.h"
#include "dbg.h"


#define FB_INDEX_MASK	 (FRAME_BUF_NUM - 1)

static  int               fb_index = 0;
static  struct frame_buf *fbarray[FRAME_BUF_NUM];
static  struct frame_buf  fbpool[FRAME_BUF_NUM];

static  pthread_mutex_t   g_fb_mtx = PTHREAD_MUTEX_INITIALIZER;

#define fbpool_lock()     pthread_mutex_lock(&g_fb_mtx)
#define fbpool_unlock()   pthread_mutex_unlock(&g_fb_mtx)

/* called by sdk init routine */
void
VPU_framebuf_init()
{
	int i;

	for (i = 0; i < FRAME_BUF_NUM; i++)
	{
		fbarray[i] = &fbpool[i];
	}
}

static struct frame_buf *
get_framebuf()
{
	struct frame_buf *fb;

    fbpool_lock();
	fb = fbarray[fb_index];
	fbarray[fb_index] = 0;

	++fb_index;
	fb_index &= FB_INDEX_MASK;
	fbpool_unlock();

	return fb;	
}

static void
put_framebuf(struct frame_buf *fb)
{
    fbpool_lock();
	--fb_index;
	fb_index &= FB_INDEX_MASK;
	fbarray[fb_index] = fb;
	fbpool_unlock();
}

struct frame_buf *
vpu_framebuf_alloc(int strideY, int height)
{
	struct frame_buf *fb;
	int size;

	fb = get_framebuf();
	if (fb == NULL)
		return NULL;

	size = strideY * height;
	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
	fb->desc.size = (size * 3 / 2);

	if (IOGetPhyMem(&fb->desc))
	{
		DBG("--- Frame buffer allocation failed ---\n");
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	fb->addrY = fb->desc.phy_addr;
	fb->addrCb = fb->addrY + size;
	fb->addrCr = fb->addrCb + (size>>2);

	fb->desc.virt_uaddr = IOGetVirtMem(&(fb->desc));
	if (fb->desc.virt_uaddr <= 0)
	{
		IOFreePhyMem(&fb->desc);
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	return fb;
}

void
vpu_framebuf_free(struct frame_buf *fb)
{
    if (fb->desc.virt_uaddr)
    {
        IOFreeVirtMem(&fb->desc);
    }

    if (fb->desc.phy_addr)
    {
        IOFreePhyMem(&fb->desc);
    }

    memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
    put_framebuf(fb);
}


