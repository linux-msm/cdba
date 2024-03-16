/*
 * Copyright (c) 2023, Qualcomm Innovaction Center, Inc
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
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
