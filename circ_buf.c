/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "circ_buf.h"

/**
 * circ_fill() - read data into circular buffer
 * @fd:		non-blocking file descriptor to read
 * @circ:	circ_buf object to write to
 *
 * Return: 0 if fifo is full or fd depleted, negative errno on failure
 */
ssize_t circ_fill(int fd, struct circ_buf *circ)
{
	ssize_t space;
	ssize_t n = 0;

	do {
		space = CIRC_SPACE_TO_END(circ);
		if (!space) {
			errno = EAGAIN;
			return -1;
		}

		n = read(fd, circ->buf + circ->head, space);
		if (n == 0) {
			errno = EPIPE;
			return -1;
		} else if (n < 0)
			return -1;

		circ->head = (circ->head + n) & (CIRC_BUF_SIZE - 1);
	} while (n != space);

	return 0;
}

size_t circ_peak(struct circ_buf *circ, void *buf, size_t len)
{
	size_t tail = circ->tail;
	char *p = buf;

	while (len--) {
		if (tail == circ->head)
			return 0;

		*p++ = circ->buf[tail];

		tail = (tail + 1) & (CIRC_BUF_SIZE - 1);
	}

	return p - (char *)buf;
}

size_t circ_read(struct circ_buf *circ, void *buf, size_t len)
{
	char *p = buf;

	while (len--) {
		if (circ->tail == circ->head)
			return 0;

		*p++ = circ->buf[circ->tail];

		circ->tail = (circ->tail + 1) & (CIRC_BUF_SIZE - 1);
	}

	return p - (char *)buf;
}
