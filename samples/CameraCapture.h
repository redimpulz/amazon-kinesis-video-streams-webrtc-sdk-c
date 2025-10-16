#ifndef __CAMERA_CAPTURE_H__
#define __CAMERA_CAPTURE_H__

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define CAMERA_DEVICE "/dev/video0"
#define CAPTURE_WIDTH 640
#define CAPTURE_HEIGHT 480
// #define CAPTURE_FORMAT V4L2_PIX_FMT_YUYV  // YUYV format (commented out)
#define CAPTURE_FORMAT V4L2_PIX_FMT_MJPEG   // MJPEG format
#define NUM_BUFFERS 4

typedef struct {
    void *start;
    size_t length;
} Buffer;

typedef struct {
    int fd;
    Buffer buffers[NUM_BUFFERS];
    int width;
    int height;
    int format;
    int buffer_count;
} CameraContext;

// Function declarations
STATUS initializeCamera(CameraContext* ctx, const char* device, int width, int height);
STATUS captureFrame(CameraContext* ctx, PBYTE* frameData, PUINT32 frameSize);
STATUS cleanupCamera(CameraContext* ctx);
STATUS convertYUYVToYUV420(PBYTE yuyv_data, PBYTE yuv420_data, int width, int height);  // YUYV conversion (commented out)
STATUS decodeMJPEGToYUV420(PBYTE mjpeg_data, UINT32 mjpeg_size, PBYTE yuv420_data, int width, int height);

#endif // __CAMERA_CAPTURE_H__