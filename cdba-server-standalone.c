/*
 * Copyright (c) 2021, Linaro Ltd.
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

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "cdba-client.h"
#include "cdba-server.h"
#include "cdba-server-standalone.h"

#define UNIX_SOCKET_QUEUE 8

static int cdba_server_standalone_data(int fd, void *data)
{
	struct device *device = data;
	struct msg msg;
	int dsocket;
	int res;

	dsocket = accept(device->standalone_channel, NULL, NULL);
	if (dsocket == -1)
		return dsocket;

	if (read(dsocket, &msg, sizeof(msg)) == -1) {
		close(dsocket);
		return -1;
	}

	switch (msg.type) {
	case MSG_POWER_ON:
		res = device_power(device, true);
		break;
	case MSG_POWER_OFF:
		res = device_power(device, false);
		break;
	}
	write(dsocket, &res, sizeof(res));

	close(dsocket);

	return 0;
}

static int cdba_server_standalone_create_channel(struct device *device)
{
	char socket_name[PATH_MAX];
	struct sockaddr_un name;
	int csocket;
	int n;

	n = snprintf(socket_name, sizeof(socket_name), "/tmp/cdba-%s.socket", device->board);
	if (n >= sizeof(socket_name))
		errx(1, "failed to build channel path");

	csocket = socket(AF_LOCAL, SOCK_SEQPACKET, 0);
	if (csocket == -1)
		errx(1, "failed to create connection socket");

	memset(&name, 0, sizeof(name));
	name.sun_family = AF_UNIX;
        strncpy(name.sun_path, socket_name, sizeof(name.sun_path) - 1);
	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	/**
	 * When device is locked means that cdba-standalone client was inkoved to 
	 * get console so the cdba-server expects commands (power on/off) from 
	 * another client.
	 */
	if (device->locked) {
		/* XXX: Remove socket, tried use of SO_REUSEADDR but socket needs
		 * to be closed not useful when kill the process */
		unlink(name.sun_path);

		if (bind(csocket, (const struct sockaddr *) &name,
		    sizeof(name)) == -1)
			errx(1, "failed to bind connection socket");

		if (listen(csocket, UNIX_SOCKET_QUEUE) == -1)
			errx(1, "failed to bind connection socket");

		watch_add_readfd(csocket, cdba_server_standalone_data, device);
	} else {
		if (connect(csocket, (const struct sockaddr *) &name,
                          sizeof(name)) == -1)
			errx(1, "failed to connect socket");
	}

	return csocket;
}

int cdba_server_standalone_create(struct device *device)
{
	device->standalone_channel = cdba_server_standalone_create_channel(device);
	return device->standalone_channel;
}


int cdba_server_standalone_end(struct device *device)
{
	return close(device->standalone_channel);
}

void cdba_server_standalone_device_power(struct device *device, bool on)
{
	struct msg msg;
	int n;
	int res;

	if (on)
		msg.type = MSG_POWER_ON;
	else
		msg.type = MSG_POWER_OFF;

	n = write(device->standalone_channel, &msg, sizeof(msg));
	if (n < 0)
		err(1, "failed to write on cdba-server-standalone channel");

	n = read(device->standalone_channel, &res, sizeof(res));
	if (n < 0)
		err(1, "failed to read on cdba-server-standalone channel");
}
