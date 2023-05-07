CLIENT := cdba
SERVER := cdba-server

.PHONY: all

all: $(CLIENT) $(SERVER)

CFLAGS := -Wall -g -O2
# Wextra without few warnings
CFLAGS := $(CFLAGS) -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-sign-compare

# Few clang version still have warnings so fail only on GCC
GCC_CFLAGS := -Werror
GCC_CFLAGS += -Wformat-signedness -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wvla-larger-than=1
GCC_CFLAGS += -Walloc-zero -Wstringop-truncation -Wdouble-promotion -Wshadow -Wunsafe-loop-optimizations
GCC_CFLAGS += -Wpointer-arith -Wcast-align -Wwrite-strings -Wlogical-op -Wstrict-overflow=4 -Wundef -Wjump-misses-init
CLANG_CFLAGS := -Wnull-dereference -Wdouble-promotion -Wshadow -Wpointer-arith -Wwrite-strings -Wstrict-overflow=4 -Wundef
# TODO:
# GCC_CFLAGS += -Wcast-qual
# CLANG_CFLAGS += -Wcast-qual -Wcast-align
ifeq ($(CC),cc)
  CFLAGS += $(GCC_CFLAGS)
else ifeq ($(CC),clang)
  CFLAGS += $(CLANG_CFLAGS)
else
  $(info No compiler flags for: $(CC))
endif

LDFLAGS := -ludev -lyaml -lftdi -lusb

CLIENT_SRCS := cdba.c circ_buf.c
CLIENT_OBJS := $(CLIENT_SRCS:.c=.o)

SERVER_SRCS := cdba-server.c cdb_assist.c circ_buf.c conmux.c device.c device_parser.c fastboot.c alpaca.c ftdi-gpio.c console.c qcomlt_dbg.c
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
