#ifndef __H264_ENCODER_H__
#define __H264_ENCODER_H__

#ifdef ENABLE_OPENH264
#include <wels/codec_api.h>
#endif

#include "Samples.h"

typedef struct {
#ifdef ENABLE_OPENH264
    ISVCEncoder* encoder;
    SEncParamExt encoding_params;
    SFrameBSInfo info;
    SSourcePicture pic;
#endif
    int width;
    int height;
    int fps;
    int bitrate;
    BOOL initialized;
} H264EncoderContext;

// Function declarations
STATUS initializeH264Encoder(H264EncoderContext* ctx, int width, int height, int fps, int bitrate);
STATUS encodeFrame(H264EncoderContext* ctx, PBYTE yuv_data, PBYTE* h264_data, PUINT32 h264_size, PBOOL isKeyFrame);
STATUS cleanupH264Encoder(H264EncoderContext* ctx);

#endif // __H264_ENCODER_H__