CLIENT := cdba
SERVER := cdba-server

.PHONY: all

all: $(CLIENT) $(SERVER)

CFLAGS := -Wall -g -O2
LDFLAGS := -ludev -lyaml

CLIENT_SRCS := cdba.c circ_buf.c
CLIENT_OBJS := $(CLIENT_SRCS:.c=.o)

SERVER_SRCS := cdba-server.c cdb_assist.c circ_buf.c conmux.c device.c device_parser.c fastboot.c alpaca.c console.c qcomlt_dbg.c
SERVER_OBJS := $(SERVER_SRCS:.c=.o)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(SERVER): $(SERVER_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(CLIENT) $(CLIENT_OBJS) $(SERVER) $(SERVER_OBJS)

install: $(CLIENT) $(SERVER)
	install -D -m 755 $(CLIENT) $(DESTDIR)$(prefix)/bin/$(CLIENT)
	install -D -m 755 $(SERVER) $(DESTDIR)$(prefix)/bin/$(SERVER)
