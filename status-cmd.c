/*
 * Copyright (c) 2023, Qualcomm Innovaction Center, Inc
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

#include <err.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"
#include "status.h"
#include "status-cmd.h"

static void launch_status_cmd(struct device *dev)
{
	char *tokens[100];
	char *p;
	int t = 0;

	p = strtok(dev->status_cmd, " ");
	while (p) {
		tokens[t++] = p;
		p = strtok(NULL, " ");
		if (t == 100)
			exit(1);
	}
	tokens[t] = NULL;

	execvp(tokens[0], tokens);
	exit(1);
}

static int status_data(int fd, void *data)
{
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0)
		return n;

	status_send_raw(buf, n);
	return 0;
}

int status_cmd_open(struct device *dev)
{
	pid_t status_pid;
	int fd;

	status_pid = forkpty(&fd, NULL, NULL, NULL);
	if (status_pid < 0)
		err(1, "failed to fork");

	if(status_pid == 0) {
		launch_status_cmd(dev);
		/* Notreached */
	}

	watch_add_readfd(fd, status_data, dev);

	return 0;
}
