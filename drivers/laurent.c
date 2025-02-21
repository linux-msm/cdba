/*
 * Copyright (c) 2024, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Driver for KernelChip Laurent family of Ethernet-controlled relay arrays.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <yaml.h>

#include "cdba-server.h"
#include "device.h"
#include "device_parser.h"

struct laurent_options {
	const char *server;
	const char *password;
	unsigned int relay;
	int usb_relay;
};

struct laurent {
	struct laurent_options *options;

	struct addrinfo addr;
};

#define DEFAULT_PASSWORD	"Laurent"
#define TOKEN_LENGTH	128

void *laurent_parse_options(struct device_parser *dp)
{
	struct laurent_options *options;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	options = calloc(1, sizeof(*options));
	options->password = DEFAULT_PASSWORD;
	options->usb_relay = -1;

	device_parser_accept(dp, YAML_MAPPING_START_EVENT, NULL, 0);
	while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		if (!device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH))
			errx(1, "%s: expected value for \"%s\"", __func__, key);

		if (!strcmp(key, "server"))
			options->server = strdup(value);
		else if (!strcmp(key, "password"))
			options->password = strdup(value);
		else if (!strcmp(key, "relay"))
			options->relay = strtoul(value, NULL, 0);
		else if (!strcmp(key, "usb_relay"))
			options->usb_relay = strtoul(value, NULL, 0);
		else
			errx(1, "%s: unknown option \"%s\"", __func__, key);
	}

	device_parser_expect(dp, YAML_MAPPING_END_EVENT, NULL, 0);

	if (!options->server)
		errx(1, "%s: server hostname not specified", __func__);

	return options;
}

static void laurent_resolve(struct laurent *laurent)
{
	struct addrinfo hints = {};
	struct addrinfo *result, *rp;

	int ret;

	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	ret = getaddrinfo(laurent->options->server, "80", &hints, &result);
	if (ret != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		int fd = socket(rp->ai_family, rp->ai_socktype,
			    rp->ai_protocol);
		if (fd == -1)
			continue;

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			close(fd);
			break;
		}

		close(fd);
	}

	if (rp == NULL)
		errx(1, "Could not resolve / connect to the controller\n");

	laurent->addr = *rp;
	laurent->addr.ai_addr = malloc(rp->ai_addrlen);
	memcpy(laurent->addr.ai_addr, rp->ai_addr,rp->ai_addrlen);

	freeaddrinfo(result);           /* No longer needed */
}

static void *laurent_open(struct device *dev)
{
	struct laurent *laurent;

	laurent = calloc(1, sizeof(*laurent));

	laurent->options = dev->control_options;

	laurent_resolve(laurent);

	return laurent;
}

static int laurent_control(struct device *dev, unsigned int relay, bool on)
{
	struct laurent *laurent = dev->cdb;
	char buf[BUFSIZ];
	int fd, ret, len, off;

	fd = socket(laurent->addr.ai_family, laurent->addr.ai_socktype,
		    laurent->addr.ai_protocol);
	if (fd == -1) {
		warn("failed to open socket\n");
		return -1;
	}

	ret = connect(fd, laurent->addr.ai_addr, laurent->addr.ai_addrlen);
	if (ret == -1) {
		warn("failed to connect\n");
		goto err;
	}

	len = snprintf(buf, sizeof(buf), "GET /cmd.cgi?psw=%s&cmd=REL,%u,%d HTTP/1.0\r\n\r\n",
		       laurent->options->password,
		       relay,
		       on);
	if (len < 0) {
		warn("asprintf failed\n");
		goto err;
	}

	for (off = 0; off != len; ) {
		ret = send(fd, buf + off, len - off, 0);
		if (ret == -1) {
			warn("failed to send\n");
			goto err;
		}

		off += ret;
	}

	/* Dump controller response to stderr */
	while (true) {
		ret = recv(fd, buf, sizeof(buf), 0);
		if (ret == -1) {
			warn("failed to recv\n");
			goto err;
		}

		if (!ret)
			break;

		write(STDERR_FILENO, buf, ret);
	}

	write(STDERR_FILENO, "\n", 1);

	shutdown(fd, SHUT_RDWR);
	close(fd);

	return 0;

err:
	shutdown(fd, SHUT_RDWR);
	close(fd);

	return -1;
}

static int laurent_power(struct device *dev, bool on)
{
	struct laurent *laurent = dev->cdb;

	return laurent_control(dev,
			       laurent->options->relay,
			       on);
}

static void laurent_usb(struct device *dev, bool on)
{
	struct laurent *laurent = dev->cdb;

	if (laurent->options->usb_relay < 0)
		return;

	laurent_control(dev,
			laurent->options->usb_relay,
			on);
}

const struct control_ops laurent_ops = {
	.parse_options = laurent_parse_options,
	.open = laurent_open,
	.power = laurent_power,
	.usb = laurent_usb,
};
