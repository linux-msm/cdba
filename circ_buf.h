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
