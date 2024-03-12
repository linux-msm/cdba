/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/time.h>
#include <alloca.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdba.h"
#include "list.h"
#include "watch.h"

static bool quit_invoked;

struct watch {
	struct list_head node;

	int fd;
	int (*cb)(int, void*);
	void *data;
};

struct timer {
	struct list_head node;
	struct timeval tv;

	void (*cb)(void *);
	void *data;
};

static struct list_head read_watches = LIST_INIT(read_watches);
static struct list_head timer_watches = LIST_INIT(timer_watches);

void watch_add_readfd(int fd, int (*cb)(int, void*), void *data)
{
	struct watch *w;

	w = calloc(1, sizeof(*w));
	w->fd = fd;
	w->cb = cb;
	w->data = data;

	list_add(&read_watches, &w->node);
}

void watch_timer_add(int timeout_ms, void (*cb)(void *), void *data)
{
	struct timeval tv_timeout;
	struct timeval now;
	struct timer *t;

	t = calloc(1, sizeof(*t));

	gettimeofday(&now, NULL);

	tv_timeout.tv_sec = timeout_ms / 1000;
	tv_timeout.tv_usec = (timeout_ms % 1000) * 1000;

	t->cb = cb;
	t->data = data;
	timeradd(&now, &tv_timeout, &t->tv);

	list_add(&timer_watches, &t->node);
}

static struct timeval *watch_timer_next(void)
{
	static struct timeval timeout;
	struct timeval now;
	struct timer *next;
	struct timer *t;

	if (list_empty(&timer_watches))
		return NULL;

	next = list_entry_first(&timer_watches, struct timer, node);

	list_for_each_entry(t, &timer_watches, node) {
		if (timercmp(&t->tv, &next->tv, <))
			next = t;
	}

	gettimeofday(&now, NULL);
	timersub(&next->tv, &now, &timeout);
	if (timeout.tv_sec < 0) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
	}

	return &timeout;
}

static void watch_timer_invoke(void)
{
	struct timeval now;
	struct timer *tmp;
	struct timer *t;

	gettimeofday(&now, NULL);

	list_for_each_entry_safe(t, tmp, &timer_watches, node) {
		if (timercmp(&t->tv, &now, <)) {
			t->cb(t->data);

			list_del(&t->node);
			free(t);
		}
	}
}

void watch_quit(void)
{
	quit_invoked = true;
}

int watch_main_loop(bool (*quit_cb)(void))
{
	struct timeval *timeoutp;
	struct watch *w;
	fd_set rfds;
	int nfds;
	int ret;

	while (!quit_invoked) {
		if (quit_cb && quit_cb())
			break;

		nfds = 0;

		list_for_each_entry(w, &read_watches, node) {
			nfds = MAX(nfds, w->fd);
			FD_SET(w->fd, &rfds);
		}

		timeoutp = watch_timer_next();
		ret = select(nfds + 1, &rfds, NULL, NULL, timeoutp);
		if (ret < 0 && errno == EINTR)
			continue;
		else if (ret < 0) {
			int err = errno;
			fprintf(stderr, "select returned %s\n", strerror(err));
			return -err;
		}

		watch_timer_invoke();

		list_for_each_entry(w, &read_watches, node) {
			if (FD_ISSET(w->fd, &rfds)) {
				ret = w->cb(w->fd, w->data);
				if (ret < 0) {
					fprintf(stderr, "cb returned %d\n", ret);
					return ret;
				}
			}
		}
	}

	return 0;
}

int watch_run(void)
{
	struct watch *w;
	bool found = false;

	list_for_each_entry(w, &read_watches, node) {
		if (w->fd == STDIN_FILENO)
			found = true;
	}

	if (!found) {
		fprintf(stderr, "rfds is trash!\n");
		return -EINVAL;
	}

	return watch_main_loop(NULL);
}
