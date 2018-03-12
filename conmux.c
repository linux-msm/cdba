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

#include "bad.h"
#include "conmux.h"

extern int h_errno;

struct conmux {
	int fd;
};

struct conmux_lookup {
	char *host;
	int port;
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
	char buf[128];
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
	if (ret >= sizeof(buf))
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
	result->port = strtol(p, NULL, 10);

	ret = strcmp(resp.status, "OK") ? -1 : 0;

out:
	close(fd);

	free_response(&resp);

	return ret;
}

static int conmux_data(int fd, void *data)
{
	struct msg hdr;
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		return n;

	hdr.type = MSG_CONSOLE;
	hdr.len = n;
	write(STDOUT_FILENO, &hdr, sizeof(hdr));
	write(STDOUT_FILENO, buf, n);

	return 0;
}

void *conmux_open(struct device *dev)
{
	struct conmux_response resp = {};
	struct conmux_lookup lookup;
	struct sockaddr_in saddr;
	struct conmux *conmux;
	struct hostent *hent;
	const char *service = dev->cdb_serial;
	ssize_t n;
	char req[128];
	int ret;
	int fd;

	ret = registry_lookup(service, &lookup);
	if (ret)
		exit(1);

	fprintf(stderr, "conmux device at %s:%d\n", lookup.host, lookup.port);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		err(1, "failed to create registry socket");

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(lookup.port);

	hent = gethostbyname(lookup.host);
	if (!hent) {
		errno = h_errno;
		err(1, "failed resolve \"%s\"", lookup.host);
	}

	saddr.sin_addr = *(struct in_addr *)hent->h_addr_list[0];

	ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret < 0)
		err(1, "failed to connect to conmux instance");

	ret = snprintf(req, sizeof(req), "CONNECT id=bjorn to=console\n");
	if (ret >= sizeof(req))
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

int conmux_power_off(struct device *dev)
{
	struct conmux *conmux = dev->cdb;
	char sz[] = "~$off\n";

	fprintf(stderr, "power off\n");

	return write(conmux->fd, sz, sizeof(sz));
}

int conmux_write(struct device *dev, const void *buf, size_t len)
{
	struct conmux *conmux = dev->cdb;

	return write(conmux->fd, buf, len);
}
