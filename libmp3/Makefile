OBJS = compat.o parse.o frame.o format.o dct64.o equalizer.o \
    id3.o optimize.o readers.o tabinit.o libmpg123.o index.o \
    stringbuf.o icy.o icy2utf8.o ntom.o synth.o synth_8bit.o \
    layer1.o layer2.o layer3.o synth_arm.o feature.o lfs_wrap.o lfs_alias.o

CFLAGS = -I. -O2 -DOPT_ARM -DREAL_IS_FIXED

all: $(OBJS)
%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
	@mv *.o $(OBJDIR)

clean:
	rm -f *.o *.bak *.lo *.la
