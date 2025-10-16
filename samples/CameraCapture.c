#include "Samples.h"
#include "CameraCapture.h"

// Avoid INT32 type conflicts between jpeglib.h and CommonDefs.h
#define XMD_H  // Prevent jpeglib.h from defining INT32
#include <jpeglib.h>
#include <setjmp.h>

// Define CLIP3 macro if not available
#ifndef CLIP3
#define CLIP3(min, max, val) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))
#endif

STATUS initializeCamera(CameraContext* ctx, const char* device, int width, int height) {
    STATUS retStatus = STATUS_SUCCESS;
    struct v4l2_format fmt = {0};
    struct v4l2_requestbuffers req = {0};
    struct v4l2_buffer buf = {0};
    int i;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Camera context is NULL");

    // Open camera device
    ctx->fd = open(device, O_RDWR);
    CHK_ERR(ctx->fd >= 0, STATUS_OPEN_FILE_FAILED, "Failed to open camera device: %s", strerror(errno));

    ctx->width = width;
    ctx->height = height;
    ctx->format = CAPTURE_FORMAT;

    // Set format
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = CAPTURE_FORMAT;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    CHK_ERR(ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to set format: %s", strerror(errno));

    // Store actual format set by driver
    ctx->format = fmt.fmt.pix.pixelformat;

    DLOGI("Camera format set: %dx%d, format: %c%c%c%c", 
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          (fmt.fmt.pix.pixelformat) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    // Request buffers
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    CHK_ERR(ioctl(ctx->fd, VIDIOC_REQBUFS, &req) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to request buffers: %s", strerror(errno));

    DLOGI("Requested %d buffers, driver allocated %d buffers", NUM_BUFFERS, req.count);
    CHK_ERR(req.count >= 2, STATUS_NOT_ENOUGH_MEMORY, "Insufficient buffer memory: got %d, need at least 2", req.count);

    // Use actual allocated buffer count
    int actual_buffers = req.count;

    // Map buffers
    for (i = 0; i < actual_buffers; i++) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        CHK_ERR(ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to query buffer: %s", strerror(errno));

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);

        CHK_ERR(ctx->buffers[i].start != MAP_FAILED, STATUS_NOT_ENOUGH_MEMORY, "Failed to map buffer: %s", strerror(errno));
    }

    ctx->buffer_count = actual_buffers;

    // Queue buffers
    for (i = 0; i < actual_buffers; i++) {
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        CHK_ERR(ioctl(ctx->fd, VIDIOC_QBUF, &buf) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to queue buffer: %s", strerror(errno));
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CHK_ERR(ioctl(ctx->fd, VIDIOC_STREAMON, &type) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to start streaming: %s", strerror(errno));

    DLOGI("Camera initialized successfully");

CleanUp:
    return retStatus;
}

STATUS captureFrame(CameraContext* ctx, PBYTE* frameData, PUINT32 frameSize) {
    STATUS retStatus = STATUS_SUCCESS;
    struct v4l2_buffer buf = {0};
    fd_set fds;
    struct timeval tv;
    int r;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Camera context is NULL");
    CHK_ERR(frameData != NULL, STATUS_NULL_ARG, "Frame data pointer is NULL");
    CHK_ERR(frameSize != NULL, STATUS_NULL_ARG, "Frame size pointer is NULL");

    // Wait for frame to be available
    FD_ZERO(&fds);
    FD_SET(ctx->fd, &fds);

    tv.tv_sec = 2; // 2 second timeout
    tv.tv_usec = 0;

    r = select(ctx->fd + 1, &fds, NULL, NULL, &tv);
    CHK_ERR(r > 0, STATUS_OPERATION_TIMED_OUT, "Timeout waiting for frame");
    CHK_ERR(r != -1, STATUS_OPERATION_TIMED_OUT, "Select error: %s", strerror(errno));

    // Dequeue buffer
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    CHK_ERR(ioctl(ctx->fd, VIDIOC_DQBUF, &buf) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to dequeue buffer: %s", strerror(errno));

    // Copy frame data
    *frameSize = buf.bytesused;
    *frameData = (PBYTE) ctx->buffers[buf.index].start;

    // Queue buffer back
    CHK_ERR(ioctl(ctx->fd, VIDIOC_QBUF, &buf) >= 0, STATUS_OPERATION_TIMED_OUT, "Failed to requeue buffer: %s", strerror(errno));

CleanUp:
    return retStatus;
}

STATUS cleanupCamera(CameraContext* ctx) {
    STATUS retStatus = STATUS_SUCCESS;
    int i;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Camera context is NULL");

    // Stop streaming
    if (ctx->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);

        // Unmap buffers
        for (i = 0; i < ctx->buffer_count; i++) {
            if (ctx->buffers[i].start != MAP_FAILED) {
                munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            }
        }

        close(ctx->fd);
        ctx->fd = -1;
    }

    DLOGI("Camera cleaned up successfully");

CleanUp:
    return retStatus;
}

STATUS convertYUYVToYUV420(PBYTE yuyv_data, PBYTE yuv420_data, int width, int height) {
    STATUS retStatus = STATUS_SUCCESS;
    int i, j;
    PBYTE y_plane = yuv420_data;
    PBYTE u_plane = yuv420_data + (width * height);
    PBYTE v_plane = u_plane + (width * height / 4);

    CHK_ERR(yuyv_data != NULL, STATUS_NULL_ARG, "YUYV data is NULL");
    CHK_ERR(yuv420_data != NULL, STATUS_NULL_ARG, "YUV420 data is NULL");

    // Convert YUYV to YUV420P
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j += 2) {
            int yuyv_idx = (i * width + j) * 2;
            int y_idx = i * width + j;
            int uv_idx = (i / 2) * (width / 2) + (j / 2);

            // Y values
            y_plane[y_idx] = yuyv_data[yuyv_idx];
            y_plane[y_idx + 1] = yuyv_data[yuyv_idx + 2];

            // U and V values (subsampled)
            if (i % 2 == 0) {
                u_plane[uv_idx] = yuyv_data[yuyv_idx + 1];
                v_plane[uv_idx] = yuyv_data[yuyv_idx + 3];
            }
        }
    }

CleanUp:
    return retStatus;
}

// Error handling structure for libjpeg
struct jpeg_error_mgr_ext {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    struct jpeg_error_mgr_ext* myerr = (struct jpeg_error_mgr_ext*) cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

STATUS decodeMJPEGToYUV420(PBYTE mjpeg_data, UINT32 mjpeg_size, PBYTE yuv420_data, int width, int height) {
    STATUS retStatus = STATUS_SUCCESS;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr_ext jerr;
    JSAMPARRAY buffer;
    int row_stride;
    PBYTE rgb_buffer = NULL;
    int rgb_size = width * height * 3;
    int y_idx = 0, uv_idx = 0;
    PBYTE y_plane = yuv420_data;
    PBYTE u_plane = yuv420_data + (width * height);
    PBYTE v_plane = u_plane + (width * height / 4);

    CHK_ERR(mjpeg_data != NULL, STATUS_NULL_ARG, "MJPEG data is NULL");
    CHK_ERR(yuv420_data != NULL, STATUS_NULL_ARG, "YUV420 data is NULL");

    // Allocate RGB buffer for temporary storage
    rgb_buffer = (PBYTE) MEMCALLOC(1, rgb_size);
    CHK_ERR(rgb_buffer != NULL, STATUS_NOT_ENOUGH_MEMORY, "Failed to allocate RGB buffer");

    // Initialize JPEG decompression
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        CHK_ERR(FALSE, STATUS_INVALID_OPERATION, "JPEG decompression failed");
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, mjpeg_data, mjpeg_size);

    CHK_ERR(jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK, STATUS_INVALID_OPERATION, "Invalid JPEG header");

    // Set decompression parameters
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    CHK_ERR(cinfo.output_width == width && cinfo.output_height == height, 
            STATUS_INVALID_OPERATION, "JPEG dimensions mismatch: expected %dx%d, got %dx%d",
            width, height, cinfo.output_width, cinfo.output_height);

    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    // Read scanlines and convert to YUV420
    y_idx = 0;
    uv_idx = 0;

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        PBYTE rgb_row = buffer[0];
        
        // Convert RGB to YUV for this row
        for (int x = 0; x < width; x++) {
            PBYTE rgb_pixel = rgb_row + (x * 3);
            int r = rgb_pixel[0];
            int g = rgb_pixel[1];
            int b = rgb_pixel[2];

            // Convert RGB to Y
            int y = (int)(0.299 * r + 0.587 * g + 0.114 * b);
            y_plane[y_idx++] = (BYTE) CLIP3(0, 255, y);

            // Convert RGB to U and V (subsampled for every 2x2 block)
            if ((cinfo.output_scanline - 1) % 2 == 0 && x % 2 == 0) {
                int u = (int)(-0.169 * r - 0.331 * g + 0.5 * b + 128);
                int v = (int)(0.5 * r - 0.419 * g - 0.081 * b + 128);
                u_plane[uv_idx] = (BYTE) CLIP3(0, 255, u);
                v_plane[uv_idx] = (BYTE) CLIP3(0, 255, v);
                uv_idx++;
            }
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    DLOGV("MJPEG decoded successfully to YUV420: %dx%d", width, height);

CleanUp:
    if (rgb_buffer != NULL) {
        MEMFREE(rgb_buffer);
    }
    return retStatus;
}