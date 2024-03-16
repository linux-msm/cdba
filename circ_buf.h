/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __CIRC_BUF_H__
#define __CIRC_BUF_H__

#include <stdlib.h>

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#define CIRC_BUF_SIZE 16384

struct circ_buf {
	char buf[CIRC_BUF_SIZE];
	size_t head;
	size_t tail;
};

#define CIRC_AVAIL(circ) (((circ)->head - (circ)->tail) & (CIRC_BUF_SIZE - 1))
#define CIRC_SPACE(circ) (((circ)->tail - (circ)->head - 1) & (CIRC_BUF_SIZE - 1))

#define CIRC_SPACE_TO_END(circ) MIN(CIRC_SPACE(circ), CIRC_BUF_SIZE - (circ)->head)

ssize_t circ_fill(int fd, struct circ_buf *circ);
size_t circ_peak(struct circ_buf *circ, void *buf, size_t len);
size_t circ_read(struct circ_buf *circ, void *buf, size_t len);

#endif
