CDBA := bad

.PHONY: all

all: $(CDBA)

CFLAGS := -Wall -g -O2
LDFLAGS := -ludev

CDBA_SRCS := bad.c cdb_assist.c circ_buf.c device.c fastboot.c
CDBA_OBJS := $(CDBA_SRCS:.c=.o)

$(CDBA): $(CDBA_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(CDBA_OBJS)
