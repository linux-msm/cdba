CLIENT := cdba
CLIENT_STANDALONE := cdba-standalone
SERVER := cdba-server

.PHONY: all

all: $(CLIENT) $(CLIENT_STANDALONE) $(SERVER)

CFLAGS := -Wall -g -O2
LDFLAGS := -ludev -lyaml

CLIENT_COMMON_SRCS := cdba-client.c circ_buf.c
CLIENT_COMMON_OBJS := $(CLIENT_COMMON_SRCS:.c=.o)

CLIENT_SRCS := cdba.c
CLIENT_OBJS := $(CLIENT_SRCS:.c=.o)

CLIENT_STANDALONE_SRCS := cdba-standalone.c
CLIENT_STANDALONE_OBJS := $(CLIENT_STANDALONE_SRCS:.c=.o)

SERVER_SRCS := cdba-server.c cdba-server-standalone.c cdb_assist.c circ_buf.c conmux.c device.c device_parser.c fastboot.c alpaca.c console.c qcomlt_dbg.c
SERVER_OBJS := $(SERVER_SRCS:.c=.o)

$(CLIENT): $(CLIENT_OBJS) $(CLIENT_COMMON_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(CLIENT_STANDALONE): $(CLIENT_STANDALONE_OBJS) $(CLIENT_COMMON_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(SERVER): $(SERVER_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(CLIENT) $(CLIENT_OBJS) $(CLIENT_STANDALONE) $(CLIENT_STANDALONE_OBJS) $(CLIENT_COMMON_OBJS) $(SERVER) $(SERVER_OBJS)

install: $(CLIENT) $(CLIENT_STANDALONE) $(SERVER)
	install -D -m 755 $(CLIENT) $(DESTDIR)$(prefix)/bin/$(CLIENT)
	install -D -m 755 $(CLIENT_STANDALONE) $(DESTDIR)$(prefix)/bin/$(CLIENT_STANDALONE)
	install -D -m 755 $(SERVER) $(DESTDIR)$(prefix)/bin/$(SERVER)
