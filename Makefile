TOOL_PREFIX = arm-926ejs-linux-
CC      = $(TOOL_PREFIX)gcc
AR 		= $(TOOL_PREFIX)ar
RANLIB 	= $(TOOL_PREFIX)ranlib
STRIP   = $(TOOL_PREFIX)strip

LINUXPATH = /home/mx27/linux-2.6.19.2_mx27
SRC_PATH  = /home/mx27/app/sdk
LIB_PATH  = /home/mx27/app/lib
INC_PATH  = $(LIB_PATH)/include
ULIB_PATH = /home/mx27/app/ulib

INCDIR    = $(LINUXPATH)/include -I. -I$(INC_PATH) -I$(SRC_PATH)/inc -I$(ULIB_PATH)/inc
OBJDIR    = /home/mx27/app/sdk/obj
OBJS      = $(OBJDIR)/*.o

dir_all = ./util ./tslib ./vpu ./video ./audio ./libmp3 ./voip ./mxc

ifeq ($(findstring vod,$(MAKECMDGOALS)), vod)
dir_y = voip
else
dir_y = $(dir_all)
endif

ifeq ($(findstring debug,$(MAKECMDGOALS)), debug)
CFLAGS = -I$(INCDIR) -O2 -DDEBUG -Wall
else
CFLAGS = -I$(INCDIR) -O2 -Wall
endif

export CFLAGS
export OBJDIR
export CC

all:
	[ -d $(OBJDIR) ] || mkdir $(OBJDIR)
	for i in $(dir_y) ; do \
		[ ! -d $$i ] || make -C $$i MAKECMDGOALS=$(MAKECMDGOALS)|| exit $$? ; \
	done
	$(AR) crsv libsdk.a $(OBJS)
	chmod 777 libsdk.a
	mv libsdk.a $(LIB_PATH)/lib

debug:
	make MAKECMDGOALS=debug

vod:
	make MAKECMDGOALS=vod

clean:
	for i in $(dir_all) ; do \
		make -C $$i clean ; \
	done
	rm -rf $(OBJDIR)/*.o
	rm -rf libsdk.* *.o *.bak
	rm -f ./inc/*.bak
