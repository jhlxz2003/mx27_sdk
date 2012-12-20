SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)

OBJS = $(patsubst %.c, %.o, $(SOURCES))

all: $(OBJS)
%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
	@mv *.o $(OBJDIR)

clean:
	rm -f *.o *.bak
