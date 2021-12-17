/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"

extern int h_errno;

struct conmux {
	int fd;
};

struct conmux_lookup {
	char *host;
	char *port;
};

struct conmux_response {
	char *title;
	char *status;
	char *result;
	char *state;
};

static void free_response(struct conmux_response *resp)
{
	free(resp->title);
	free(resp->status);
	free(resp->result);
}

static uint8_t nibble(const char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'z')
		return 10 + ch - 'a';
	if (ch >= 'A' && ch <= 'Z')
		return 10 + ch - 'A';
	return -1;
}

static int parse_response(const char *buf, struct conmux_response *resp)
{
	const char *p = buf;
	char value[64];
	char key[64];
	char *d;

	memset(resp, 0, sizeof(*resp));

	while (*p) {
		while (isspace(*p))
			p++;
		if (!*p)
			break;

		d = key;
		while (isalpha(*p))
			*d++ = *p++;
		*d = '\0';

		if (*p++ != '=') {
			warnx("parsing reqistry lookup response: expected '='");
			return -1;
		}

		d = value;
		while (isprint(*p) && !isspace(*p)) {
			if (*p == '%') {
				p++;

				if (!isxdigit(p[0]) || !isxdigit(p[1])) {
					warnx("parsing reqistry lookup response: truncated percent-encoding");
					return -1;
				}

				*d++ = (nibble(p[0]) << 4) | nibble(p[1]);
				p += 2;
			} else {
				*d++ = *p++;
			}
		}
		*d = '\0';

//		printf("%s %s\n", key, value);
		if (!strcmp(key, "result"))
			resp->result = strdup(value);
		else if (!strcmp(key, "status"))
			resp->status = strdup(value);
		else if (!strcmp(key, "title"))
			resp->title = strdup(value);
		else if (!strcmp(key, "state"))
			resp->state = strdup(value);
		else
			warnx("parsing conmux response: unknown key \"%s\"", key);

	}

	return 0;
}

static int registry_lookup(const char *service, struct conmux_lookup *result)
{
	struct conmux_response resp = {};
	struct sockaddr_in saddr;
	char buf[256];
	ssize_t n;
	char *p;
	int ret;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		err(1, "failed to create registry socket");

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(63000);

	ret = inet_aton("127.0.0.1", &saddr.sin_addr);
	if (ret <= 0)
		err(1, "failed inet_aton");

	ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret < 0)
		err(1, "failed to connect to registry");

	ret = snprintf(buf, sizeof(buf), "LOOKUP service=%s\n", service);
	if (ret >= (int)sizeof(buf))
		errx(1, "service name too long for registry lookup request");

	n = write(fd, buf, ret + 1);
	if (n < 0)
		err(1, "failed to send registry lookup request");

	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0)
		err(1, "failed to receive registry lookup response");

	buf[n] = '\0';
	buf[strcspn(buf, "\n")] = '\0';

	ret = parse_response(buf, &resp);
	if (ret)
		goto out;

	p = strchr(resp.result, ':');
	if (!p) {
		warnx("parsing reqistry lookup response: invalid formatting of result");
		ret = -1;
		goto out;
	}
	*p++ = '\0';

	result->host = strdup(resp.result);
	result->port = strdup(p);

	ret = strcmp(resp.status, "OK") ? -1 : 0;

out:
	close(fd);

	free_response(&resp);

	return ret;
}

static int conmux_data(int fd, void *data)
{
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		return n;

	if (!n) {
		fprintf(stderr, "Received EOF from conmux\n");
		watch_quit();
	} else {
		cdba_send_buf(MSG_CONSOLE, n, buf);
	}

	return 0;
}

void *conmux_open(struct device *dev)
{
	struct addrinfo hints = {0}, *addrs, *addr;
	struct conmux_response resp = {};
	struct conmux_lookup lookup;
	struct conmux *conmux;
	const char *service = dev->control_dev;
	const char *user;
	ssize_t n;
	char req[256];
	int ret;
	int fd = -1;

	user = getenv("USER");
	if (!user)
		user = "unknown";

	ret = registry_lookup(service, &lookup);
	if (ret)
		exit(1);

	fprintf(stderr, "conmux device at %s:%s\n", lookup.host, lookup.port);

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	ret = getaddrinfo(lookup.host, lookup.port, &hints, &addrs);
	if (ret != 0)
		errx(1, "failed resolve \"%s\": %s", lookup.host, gai_strerror(ret));

	for (addr = addrs; addr; addr = addr->ai_next) {
		fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (fd < 0)
			err(1, "failed to create registry socket");

		ret = connect(fd, addr->ai_addr, addr->ai_addrlen);
		if (ret < 0) {
			warn("failed to connect to conmux instance");
			close(fd);
			fd = -1;
		}
	}

	if (fd == -1)
		errx(1, "failed to connect to conmux instance");

	ret = snprintf(req, sizeof(req), "CONNECT id=cdba:%s to=console\n", user);
	if (ret >= (int)sizeof(req))
		errx(1, "unable to fit connect request in buffer");

	n = write(fd, req, ret + 1);
	if (n < 0)
		err(1, "failed to write conmux connect request");

	n = read(fd, req, sizeof(req) - 1);
	if (n < 0)
		err(1, "failed to read conmux response");
	req[n] = '\0';

	ret = parse_response(req, &resp);
	if (ret || strcmp(resp.status, "OK"))
		errx(1, "failed to connect to conmux instance");
	free_response(&resp);

	conmux = calloc(1, sizeof(*conmux));
	conmux->fd = fd;

	watch_add_readfd(conmux->fd, conmux_data, NULL);

	return conmux;
}

int conmux_power_on(struct device *dev)
{
	struct conmux *conmux = dev->cdb;
	char sz[] = "~$hardreset\n";

	fprintf(stderr, "power on\n");

	return write(conmux->fd, sz, sizeof(sz));
}

static int conmux_power_off(struct device *dev)
{
	struct conmux *conmux = dev->cdb;
	char sz[] = "~$off\n";

	fprintf(stderr, "power off\n");

	return write(conmux->fd, sz, sizeof(sz));
}

int conmux_power(struct device *dev, bool on)
{
	if (on)
		return conmux_power_on(dev);
	else
		return conmux_power_off(dev);
}

int conmux_write(struct device *dev, const void *buf, size_t len)
{
	struct conmux *conmux = dev->cdb;

	return write(conmux->fd, buf, len);
}

void *conmux_console_open(struct device *dev)
{
	return dev->cdb;
}

const struct control_ops conmux_ops = {
	.open = conmux_open,
	.power = conmux_power,
};

const struct console_ops conmux_console_ops = {
	.open = conmux_console_open,
	.write = conmux_write,
};
