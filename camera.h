/*
 * Copyright (c) 2024, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <stdint.h>

int camera_capture_jpeg(uint8_t **buffer_ref, unsigned long *size_ref, const char *video_device);

#endif // __CAMERA_H__
