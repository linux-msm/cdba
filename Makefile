CLIENT := cdba
CDBA := bad

.PHONY: all

all: $(CLIENT) $(CDBA)

CFLAGS := -Wall -g -O2
LDFLAGS := -ludev -lyaml

CLIENT_SRCS := cdba.c circ_buf.c
CLIENT_OBJS := $(CLIENT_SRCS:.c=.o)

CDBA_SRCS := bad.c cdb_assist.c circ_buf.c conmux.c device.c device_parser.c fastboot.c
CDBA_OBJS := $(CDBA_SRCS:.c=.o)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(CDBA): $(CDBA_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(CLIENT) $(CLIENT_OBJS) $(CDBA_OBJS)
