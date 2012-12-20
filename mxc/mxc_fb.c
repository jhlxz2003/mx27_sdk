#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>  
#include <sys/stat.h>   
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>    
#include <asm/arch/mxcfb.h>
#include <asm/arch/mx2fb.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <linux/types.h>

#include "mxc_sdk.h"
#include "dbg.h"

#define MXCFB_SCR_W      800
#define MXCFB_SCR_H      480

#define MXC_FB_NUM    1
#define MXC_FB_BPP    16

#define BYTES_PER_LINE   (MXCFB_SCR_W<<1)
#define BPP     2

typedef struct  fb_dev {
	int  inited;
	int  fd;
	int  idx;
	unsigned char *buffer;
	unsigned char *addr[2];
	unsigned int  size;
	struct fb_var_screeninfo  scr_info;
} fb_dev_t;

static char *fb_dev_name[] = {"/dev/fb0", "/dev/fb1"};
static fb_dev_t   fbDev[MXC_FB_NUM];

static void
dump_scr_info(int i)
{
	DBG("--- fb%d: xres = %d, yres = %d, xres_virtual = %d, yres_virtual = %d, bpp = %d\n", i,
			fbDev[i].scr_info.xres, fbDev[i].scr_info.yres,
			fbDev[i].scr_info.xres_virtual, fbDev[i].scr_info.yres_virtual,
			fbDev[i].scr_info.bits_per_pixel);
}

/* DONE */
static int
_fb_pan_display(int i, int yoffset)
{
	fbDev[i].scr_info.yoffset = yoffset;
	return ioctl(fbDev[i].fd, FBIOPAN_DISPLAY, &fbDev[i].scr_info);
}

/* DONE */
static int
_fb_init(int i)
{
	memset(&fbDev[i], 0, sizeof(fb_dev_t));
	if ((fbDev[i].fd = open(fb_dev_name[i], O_RDWR, 0)) < 0) {
		DBG("Unable to open %s\n", fb_dev_name[i]);
		return -1;
	}
	++fbDev[i].inited;

	if (ioctl(fbDev[i].fd, FBIOGET_VSCREENINFO, &fbDev[i].scr_info) < 0) {
		DBG("--- get vscreeninfo failed ---\n");
		close(fbDev[i].fd);
		return -1;
	}

	dump_scr_info(i);

	if (fbDev[i].scr_info.bits_per_pixel != MXC_FB_BPP
		||fbDev[i].scr_info.xres != MXCFB_SCR_W
		||fbDev[i].scr_info.yres != MXCFB_SCR_H) {
		fbDev[i].scr_info.activate = FB_ACTIVATE_NOW;
		fbDev[i].scr_info.bits_per_pixel = MXC_FB_BPP;
		fbDev[i].scr_info.xres = MXCFB_SCR_W;
		fbDev[i].scr_info.yres = MXCFB_SCR_H;
		if (ioctl(fbDev[i].fd, FBIOPUT_VSCREENINFO, &fbDev[i].scr_info) < 0) {
			DBG("--- set vscreeninfo failed ---\n");
			close(fbDev[i].fd);
			return -1;
		}
		u_sleep(100000);
		if (ioctl(fbDev[i].fd, FBIOGET_VSCREENINFO, &fbDev[i].scr_info) < 0) {
			DBG("--- get vscreeninfo failed ---\n");
			close(fbDev[i].fd);
			return -1;
		}

		dump_scr_info(i);

		if (fbDev[i].scr_info.bits_per_pixel != MXC_FB_BPP
			||fbDev[i].scr_info.xres != MXCFB_SCR_W
			||fbDev[i].scr_info.yres != MXCFB_SCR_H) {
			printf("--- /dev/fb%d set mode failed ---\n", i);
			close(fbDev[i].fd);
			return -1;
		}
	}

	fbDev[i].size = (fbDev[i].scr_info.xres * fbDev[i].scr_info.yres_virtual * MXC_FB_BPP) >> 3;
	fbDev[i].buffer = (unsigned char *)mmap(0, fbDev[i].size, PROT_READ | PROT_WRITE, MAP_SHARED, fbDev[i].fd, 0);
	if ((int)fbDev[i].buffer <= 0) {
		DBG("\nError: failed to map framebuffer device %d to memory.\n", i);
		close(fbDev[i].fd);
		return -1;
	}

	fbDev[i].addr[0] = fbDev[i].buffer;
	fbDev[i].addr[1] = fbDev[i].buffer + (fbDev[i].size>>1);
	fbDev[i].idx = 0;
	++fbDev[i].inited;
	ioctl(fbDev[i].fd, FBIOBLANK, FB_BLANK_UNBLANK);
	_fb_pan_display(i, 0);

	return 0;
}

/* DONE */
static void
_fb_finish(int i)
{
	if (fbDev[i].inited > 1)
		munmap(fbDev[i].buffer, fbDev[i].size);
	if (fbDev[i].inited > 0)
		close(fbDev[i].fd);
	memset(&fbDev[i], 0, sizeof(fb_dev_t));
}

/* DONE */
static int
_fb_disable(int i, int on)
{
	int blank;
	blank = (on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN);
	if (fbDev[i].inited != 0) {
		if (ioctl(fbDev[i].fd, FBIOBLANK, blank) < 0) {
			DBG("turning on/off lcd failed");
			return -1;
		}
		return 0;
	}
	return -1;
}

static void
_fb_set_char(int idx, RECT *rc, unsigned char c)
{
	int i;
	unsigned char *addr;
	fb_dev_t *fb = &fbDev[idx];

	addr = fb->addr[fb->idx] + rc->top*BYTES_PER_LINE + rc->left*BPP;
	for (i=0; i<rc->h; ++i) {		
		memset(addr, c, rc->w*BPP);
		addr += BYTES_PER_LINE;
	}
}

static void
_fb_clear_area(int idx, RECT *rc)
{
	_fb_set_char(idx, rc, 0);
}

static void
_fb_set_color(int idx, RECT *rc, unsigned short clr)
{
	int i;
	unsigned char  *addr;
	unsigned short *ptr;
	fb_dev_t *fb;

	if ((unsigned char)(clr >> 8) == (unsigned char)(clr&0xff)) {
		_fb_set_char(idx, rc, (unsigned char)(clr&0xff));
		return;
	}

	fb = &fbDev[idx];
	DBG("--- set to fb%d at address-%d ---\n", idx, fb->idx);
	addr = fb->addr[fb->idx] + rc->top*BYTES_PER_LINE + rc->left*BPP;
	ptr = (unsigned short*)addr;
	for (i=0; i<rc->w; ++i)
		*(ptr+i) = clr;

	addr += BYTES_PER_LINE;
	for (i=1; i<rc->h; ++i) {		
		memcpy((void*)addr, (void*)ptr, rc->w*BPP);
		addr += BYTES_PER_LINE;
	}
}

void
mxc_fb_set_transparent(int en, unsigned char alpha)
{
	struct mx2fb_gbl_alpha gbl_alpha;
	gbl_alpha.enable = en;
	if (en == 1)
		gbl_alpha.alpha = alpha;
	ioctl(fbDev[MXC_FB_NUM-1].fd, MX2FB_SET_GBL_ALPHA, &gbl_alpha);
}

void
mxc_fb_set_mask(int en, unsigned int color)
{
	struct mx2fb_color_key key;
	key.enable = en;
	if (en == 1)
		key.color_key = color;
	ioctl(fbDev[MXC_FB_NUM-1].fd, MX2FB_SET_CLR_KEY, &key);
}

/* DONE */
int 
mxc_fb_set_brightness(unsigned char v)
{
	if (fbDev[MXC_FB_NUM-1].inited == 0) return -1;
	if (ioctl(fbDev[MXC_FB_NUM-1].fd, MX2FB_SET_BRIGHTNESS, &v) < 0) {
		printf("Error setting brightness\n");
		return -1;
	}
	ioctl(fbDev[MXC_FB_NUM-1].fd, MXCFB_WAIT_FOR_VSYNC, 0);
	return 0;
}

/* DONE */
void
mxc_lcd_on(int on)
{
    int i;
	if (on == 0) {
	    for (i=0; i<MXC_FB_NUM; ++i)
	    {
	        _fb_disable(i, 0);
	    }
	} else {
		for (i=0; i<MXC_FB_NUM; ++i)
	    {
	        _fb_disable(i, 1);
	    }
	}
}

///////////////////////////////////////
#if 0
{
#endif

/* DONE */
int
mxc_fb_init()
{
    int i;
    for (i=0; i<MXC_FB_NUM; ++i)
    {
        if (_fb_init(i) < 0)
        {
            int j;
            for (j=i-1; j>=0; --j)
            {
                _fb_finish(j);
            }
            return -1;
        }
    }
	return 0;
}

/* DONE */
void
mxc_fb_finish()
{
    int i;
    for (i=0; i<MXC_FB_NUM; ++i)
    {
        _fb_finish(i);
    }
}

/* DONE */
int
GetMenuAddr(unsigned char **pAddr)
{
	if (fbDev[MXC_FB_NUM-1].inited == 2) {
		*pAddr = fbDev[MXC_FB_NUM-1].addr[0];
		*(pAddr+1) = fbDev[MXC_FB_NUM-1].addr[1];
		return 0;
	}
	return -1;
}

/*
int
GetVideoAddr(unsigned char **pAddr)
{
	if (fbDev[0].inited == 2) {
		*pAddr = fbDev[0].addr[0];
		*(pAddr+1) = fbDev[0].addr[1];
		return 0;
	}
	return -1;
}
*/

/* DONE */
int
SetMenuBufIndex(int idx)
{
	fbDev[MXC_FB_NUM-1].idx = idx;
	return _fb_pan_display(MXC_FB_NUM-1, idx*fbDev[MXC_FB_NUM-1].scr_info.yres);
}

/*
int
SetVideoBufIndex(int idx)
{
	fbDev[0].idx = idx;
	return _fb_pan_display(0, idx*fbDev[0].scr_info.yres);
}
*/

/* DONE */
void
SetLcdScreenMode(int mode)
{
	mxc_lcd_on(mode);
}

void
mxc_fb_clear_area(RECT *rc)
{
	_fb_clear_area(MXC_FB_NUM-1, rc);
}

#if MXC_FB_NUM == 2
void
mxc_fb_prepare_disp(RECT *rc)
{
	_fb_clear_area(1, rc);
	mxc_fb_set_mask(1, 0);
}

void
mxc_fb_finish_disp()
{
	mxc_fb_set_mask(0, 0);
}
#endif

void
mxc_fb_set_color(RECT *rc, unsigned short clr)
{
	_fb_set_color(MXC_FB_NUM-1, rc, clr);
}

#if 0
}
#endif

/*
#if 0
{
#endif

void
VL_fb_clear_area(RECT *rc)
{
	_fb_clear_area(0, rc);
}

void
VL_fb_set_color(RECT *rc, unsigned short clr)
{
	_fb_set_color(0, rc, clr);
}

#if 0
}
#endif
*/

