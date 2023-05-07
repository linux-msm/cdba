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
	size_t count = 0;
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

		count += n;

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

	return (void*)p - buf;
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

	return (void*)p - buf;
}
