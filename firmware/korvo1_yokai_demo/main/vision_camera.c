/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "vision_camera.h"

#ifdef HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#define LOG_TAG "vision_camera"
#define ESP_LOGI(t, f, ...) printf("[%s] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) printf("[%s][WARN] " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) printf("[%s][ERR] " f "\n", t, ##__VA_ARGS__)
#else
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "bsp/esp32_s31_korvo_1.h"
#include "linux/videodev2.h"
#endif

static const char *TAG = "vision_camera";

size_t vision_memory_checkpoint(const char *stage)
{
#ifndef HOST_TEST
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t simd_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);
    size_t simd_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);
    ESP_LOGI(TAG, "[MEM] %s int_free=%u int_largest=%u psram_free=%u psram_largest=%u simd_free=%u simd_largest=%u",
             stage, (unsigned)internal_free, (unsigned)internal_largest,
             (unsigned)psram_free, (unsigned)psram_largest,
             (unsigned)simd_free, (unsigned)simd_largest);
    if (stage[0] == 'M' && (stage[1] == '1' || stage[1] == '4' || stage[1] == '7') && stage[2] == ' ') {
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_SIMD);
        ESP_LOGI(TAG, "[MEMINFO] %s total_free_bytes=%u largest_free_block=%u minimum_free_bytes=%u allocated_blocks=%u free_blocks=%u",
                 stage, (unsigned)info.total_free_bytes, (unsigned)info.largest_free_block,
                 (unsigned)info.minimum_free_bytes, (unsigned)info.allocated_blocks, (unsigned)info.free_blocks);
    }
    return psram_largest < simd_largest ? psram_largest : simd_largest;
#else
    (void)stage;
    return 0;
#endif
}

static bool s_inited = false;
static bool s_streaming = false;

#ifndef HOST_TEST
static int s_cam_fd = -1;
static uint8_t *s_buffers[VISION_CAM_BUFFER_COUNT] = {0};
static uint32_t s_buffer_len[VISION_CAM_BUFFER_COUNT] = {0};
static uint32_t s_frame_id = 0;
static uint32_t s_cam_width = VISION_CAM_WIDTH;
static uint32_t s_cam_height = VISION_CAM_HEIGHT;
static vision_pixel_format_t s_cam_pixfmt = VISION_PIXFMT_YUV422;
#else
static uint8_t s_host_buffer[VISION_CAM_WIDTH * VISION_CAM_HEIGHT * 2];
static uint32_t s_host_frame_id = 0;
#endif

static void camera_cleanup_partial_init(void)
{
#ifndef HOST_TEST
    for (int i = 0; i < VISION_CAM_BUFFER_COUNT; ++i) {
        if (s_buffers[i] && s_buffer_len[i] > 0) {
            munmap(s_buffers[i], s_buffer_len[i]);
            s_buffers[i] = NULL;
            s_buffer_len[i] = 0;
        }
    }

    if (s_cam_fd >= 0) {
        close(s_cam_fd);
        s_cam_fd = -1;
    }
#endif
    s_streaming = false;
    s_inited = false;
}

esp_err_t vision_camera_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

#ifndef HOST_TEST
    vision_memory_checkpoint("M3 before camera init");
    /* 1. Start BSP camera hardware (XCLK 20MHz on GPIO55, I2C SCCB, DVP bus) */
    static bool s_bsp_camera_started = false;
    if (!s_bsp_camera_started) {
        esp_err_t err = bsp_camera_start(NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bsp_camera_start failed: %s", esp_err_to_name(err));
            return err;
        }
        s_bsp_camera_started = true;
    }

    /* 2. Open V4L2 video device */
    s_cam_fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (s_cam_fd < 0) {
        ESP_LOGE(TAG, "Failed to open video dev %s", ESP_VIDEO_DVP_DEVICE_NAME);
        return ESP_FAIL;
    }

    /* 3. Query negotiated pixel format & resolution from V4L2 device */
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    if (ioctl(s_cam_fd, VIDIOC_G_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
        camera_cleanup_partial_init();
        return ESP_FAIL;
    }

    s_cam_width = fmt.fmt.pix.width;
    s_cam_height = fmt.fmt.pix.height;
    if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565 || fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565X) {
        s_cam_pixfmt = VISION_PIXFMT_RGB565;
    } else {
        s_cam_pixfmt = VISION_PIXFMT_YUV422;
    }
    ESP_LOGI(TAG, "Negotiated V4L2 format: %lux%lu, fourcc=0x%08lx (pixfmt=%d)",
             (unsigned long)s_cam_width, (unsigned long)s_cam_height,
             (unsigned long)fmt.fmt.pix.pixelformat, s_cam_pixfmt);

    /* 4. Request MMAP streaming buffers */
    struct v4l2_requestbuffers req = {
        .count = VISION_CAM_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_cam_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        camera_cleanup_partial_init();
        return ESP_FAIL;
    }
    if (req.count < VISION_CAM_BUFFER_COUNT) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS returned %u buffers, need %u",
                 req.count, VISION_CAM_BUFFER_COUNT);
        camera_cleanup_partial_init();
        return ESP_FAIL;
    }

    /* 5. Map driver buffers into userspace memory and queue them */
    for (int i = 0; i < VISION_CAM_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        if (ioctl(s_cam_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed for index %d", i);
            camera_cleanup_partial_init();
            return ESP_FAIL;
        }

        s_buffers[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                      MAP_SHARED, s_cam_fd, buf.m.offset);
        if (s_buffers[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed for index %d", i);
            s_buffers[i] = NULL;
            camera_cleanup_partial_init();
            return ESP_FAIL;
        }
        s_buffer_len[i] = buf.length;
        ESP_LOGI(TAG, "V4L2 buffer index=%d length=%lu address=%p",
                 i, (unsigned long)buf.length, s_buffers[i]);
    }

    /* Set 200ms DQBUF timeout so acquire doesn't hang if camera stops */
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 200000,
    };
    (void)ioctl(s_cam_fd, VIDIOC_S_DQBUF_TIMEOUT, &tv);
#endif

    s_inited = true;
    vision_memory_checkpoint("M4 after camera init");
    s_streaming = false;
#ifndef HOST_TEST
    ESP_LOGI(TAG, "Camera adapter initialized: %lux%lu pixfmt=%d buffers=%u bytes=%lu",
             (unsigned long)s_cam_width, (unsigned long)s_cam_height, s_cam_pixfmt,
             VISION_CAM_BUFFER_COUNT, (unsigned long)s_buffer_len[0]);
#else
    ESP_LOGI(TAG, "Camera adapter initialized: host simulation");
#endif
    return ESP_OK;
}

esp_err_t vision_camera_start(void)
{
    if (!s_inited) {
        esp_err_t err = vision_camera_init();
        if (err != ESP_OK) return err;
    }
    if (s_streaming) {
        return ESP_OK;
    }

#ifndef HOST_TEST
    /* Queue all DMA buffers into V4L2 driver prior to STREAMON */
    for (int i = 0; i < VISION_CAM_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_cam_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF start queue failed for index %d: errno=%d (%s)",
                     i, errno, strerror(errno));
            return ESP_FAIL;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_cam_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed: errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
#endif

    s_streaming = true;
    vision_memory_checkpoint("M5 after camera STREAMON");
    ESP_LOGI(TAG, "Camera streaming started");
    return ESP_OK;
}

esp_err_t vision_camera_acquire(vision_camera_frame_t *frame, TickType_t timeout)
{
    if (!frame || !s_inited || !s_streaming) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef HOST_TEST
    struct v4l2_buffer buf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_cam_fd, VIDIOC_DQBUF, &buf) != 0) {
        return ESP_ERR_TIMEOUT;
    }

    frame->data = s_buffers[buf.index];
    frame->width = (uint16_t)s_cam_width;
    frame->height = (uint16_t)s_cam_height;
    frame->frame_id = ++s_frame_id;
    frame->format = s_cam_pixfmt;
    frame->buffer_index = buf.index;
    frame->driver_private = (void *)(uintptr_t)buf.index;
#else
    (void)timeout;
    frame->data = s_host_buffer;
    frame->width = VISION_CAM_WIDTH;
    frame->height = VISION_CAM_HEIGHT;
    frame->frame_id = ++s_host_frame_id;
    frame->format = VISION_PIXFMT_RGB565;
    frame->buffer_index = 0;
    frame->driver_private = NULL;
#endif

    return ESP_OK;
}

void vision_camera_release(vision_camera_frame_t *frame)
{
    if (!frame || !frame->data) {
        return;
    }

#ifndef HOST_TEST
    if (s_cam_fd >= 0 && s_streaming) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = frame->buffer_index,
        };
        if (ioctl(s_cam_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF release failed for index %lu: errno=%d (%s)",
                     (unsigned long)frame->buffer_index, errno, strerror(errno));
        }
    }
#endif

    frame->data = NULL;
}

void vision_camera_stop(void)
{
    if (!s_inited || !s_streaming) {
        return;
    }

#ifndef HOST_TEST
    if (s_cam_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)ioctl(s_cam_fd, VIDIOC_STREAMOFF, &type);
    }
#endif

    s_streaming = false;
    vision_memory_checkpoint("M12 after camera STREAMOFF");
    ESP_LOGI(TAG, "Camera streaming stopped");
}

void vision_camera_deinit(void)
{
    if (!s_inited) {
        return;
    }

    vision_camera_stop();

#ifndef HOST_TEST
    for (int i = 0; i < VISION_CAM_BUFFER_COUNT; i++) {
        if (s_buffers[i] && s_buffer_len[i] > 0) {
            munmap(s_buffers[i], s_buffer_len[i]);
            s_buffers[i] = NULL;
            s_buffer_len[i] = 0;
        }
    }
    if (s_cam_fd >= 0) {
        close(s_cam_fd);
        s_cam_fd = -1;
    }
#endif

    s_inited = false;
    vision_memory_checkpoint("M13 after camera deinit/munmap");
    ESP_LOGI(TAG, "Camera adapter de-initialized");
}

bool vision_camera_is_streaming(void)
{
    return s_streaming;
}
