LINUXPATH = /home/mx27/linux-2.6.19.2_mx27
PLATFORM = IMX27MPK271

SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)

OBJS = $(patsubst %.c, %.o, $(SOURCES))

all: $(OBJS)
%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
	@mv *.o $(OBJDIR)

clean:
	rm -f *.o *.bak
