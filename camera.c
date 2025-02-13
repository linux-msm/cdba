/*
 * Copyright (c) 2024, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <jpeglib.h>

#include "camera.h"
#include "device.h"

static int camera_init_device(const char *dev_name, int *fd_ref, char **driver_ref)
{
	*fd_ref = open(dev_name, O_RDWR);
	if (*fd_ref < 0)
	{
		fprintf(stderr, "Couldn't open device: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	struct v4l2_capability cap;

	if (ioctl(*fd_ref, VIDIOC_QUERYCAP, &cap) < 0)
	{
		fprintf(stderr, "Couldn't query device capabilities: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	*driver_ref = strdup((char *)cap.driver);

	return 0;
}

static int camera_configure_format(const int fd, int *width_ref, int *height_ref, uint32_t *format_ref)
{
	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
	{
		fprintf(stderr, "Couldn't get format: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	struct v4l2_frmsizeenum frame_size = {0};
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	frame_size.pixel_format = fmt.fmt.pix.pixelformat;

	if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) < 0)
	{
		fprintf(stderr, "Couldn't get frame size: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	fmt.fmt.pix.width = frame_size.discrete.width;
	fmt.fmt.pix.height = frame_size.discrete.height;

	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
	{
		fprintf(stderr, "Couldn't set format: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	*width_ref = fmt.fmt.pix.width;
	*height_ref = fmt.fmt.pix.height;
	*format_ref = fmt.fmt.pix.pixelformat;

	return 0;
}

static int camera_request_buffer(const int fd)
{
	struct v4l2_requestbuffers reqbuf = {0};
	reqbuf.count = 1;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0)
	{
		fprintf(stderr, "Couldn't request buffer: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	return 0;
}

static int camera_query_buffer(const int fd, uint8_t **buf_ref, struct v4l2_buffer *buf_info_ref)
{
	buf_info_ref->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf_info_ref->memory = V4L2_MEMORY_MMAP;
	buf_info_ref->index = 0;

	if (ioctl(fd, VIDIOC_QUERYBUF, buf_info_ref) < 0)
	{
		fprintf(stderr, "Couldn't query buffer: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	*buf_ref = mmap(NULL, buf_info_ref->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf_info_ref->m.offset);
	if (buf_ref == MAP_FAILED)
	{
		fprintf(stderr, "Couldn't map buffer: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	return 0;
}

static int camera_capture_frame(const int fd, struct v4l2_buffer *buf_info)
{
	if (ioctl(fd, VIDIOC_QBUF, buf_info))
	{
		fprintf(stderr, "Couldn't queue buffer: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
	{
		fprintf(stderr, "Couldn't start streaming: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	if (ioctl(fd, VIDIOC_DQBUF, buf_info) < 0)
	{
		fprintf(stderr, "Couldn't dequeue buffer: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
	{
		fprintf(stderr, "Couldn't stop streaming: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	return 0;
}

static int camera_yuyv_to_jpeg(uint8_t **jpeg_ref, unsigned long *size_ref, const uint8_t *yuyv, const int width, const int height)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	JSAMPROW row_pointer[1];

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	jpeg_mem_dest(&cinfo, jpeg_ref, size_ref);

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_YCbCr;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 100, TRUE);

	jpeg_start_compress(&cinfo, TRUE);

	uint8_t* row_buffer = (uint8_t*)malloc(width * 3 * sizeof(uint8_t));
	if (!row_buffer)
	{
		fprintf(stderr, "Memory allocation failed\n");
		jpeg_destroy_compress(&cinfo);
		return -1;
	}

	while (cinfo.next_scanline < height)
	{
		const uint32_t offset = cinfo.next_scanline * width * 2;
		for (uint32_t i = 0, j = 0; i < width * 2; i += 4, j += 6)
		{
			row_buffer[j + 0] = yuyv[offset + i + 0];
			row_buffer[j + 1] = yuyv[offset + i + 1];
			row_buffer[j + 2] = yuyv[offset + i + 3];
			row_buffer[j + 3] = yuyv[offset + i + 2];
			row_buffer[j + 4] = yuyv[offset + i + 1];
			row_buffer[j + 5] = yuyv[offset + i + 3];
		}
		row_pointer[0] = row_buffer;
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	free(row_buffer);

	return 0;
}

int camera_capture_jpeg(uint8_t **buffer_ref, unsigned long *size_ref, const char *video_device)
{
	int fd = -1;
	uint8_t *raw_buffer = NULL;

	int ret = 0;
	char *driver_str = NULL;

	int width, height = 0;
	uint32_t format = 0;

	struct v4l2_buffer buf_info = {0};

	uint8_t *jpeg = NULL;

	ret = camera_init_device(video_device, &fd, &driver_str);
	if (ret < 0)
	{
		goto cleanup;
	}

	ret = camera_configure_format(fd, &width, &height, &format);
	if (ret < 0)
	{
		goto cleanup;
	}

	fprintf(stderr, "Driver: %s, Resolution: %dx%d, Format: %c%c%c%c\n", driver_str, width, height,
		(char)((format >> 0) & 0xFF), (char)((format >> 8) & 0xFF), (char)((format >> 16) & 0xFF), (char)((format >> 24) & 0xFF));
	free(driver_str);

	ret = camera_request_buffer(fd);
	if (ret < 0)
	{
		goto cleanup;
	}

	ret = camera_query_buffer(fd, &raw_buffer, &buf_info);
	if (ret < 0)
	{
		goto cleanup;
	}

	camera_capture_frame(fd, &buf_info);

	switch (format)
	{
		case V4L2_PIX_FMT_YUYV:
			camera_yuyv_to_jpeg(&jpeg, size_ref, raw_buffer, width, height);
			break;

		default:
			fprintf(stderr, "Unsupported format\n");
			ret = -1;
			goto cleanup;
	}

	*buffer_ref = jpeg;

cleanup:
	if (fd >= 0)
	{
		close(fd);
	}
	if (raw_buffer != NULL)
	{
		munmap(raw_buffer, buf_info.length);
	}

	return ret;
}
