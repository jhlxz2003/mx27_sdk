LINUXPATH = /home/mx27/linux-2.6.19.2_mx27
PLATFORM = IMX27ADS

SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)

OBJS = $(patsubst %.c, %.o, $(SOURCES))

CFLAGS += -D$(PLATFORM)

all: $(OBJS)
%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
	@mv *.o $(OBJDIR)

clean:
	rm -f *.o *.bak