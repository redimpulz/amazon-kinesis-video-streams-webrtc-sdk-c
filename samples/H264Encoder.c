#include "Samples.h"
#include "H264Encoder.h"

STATUS initializeH264Encoder(H264EncoderContext* ctx, int width, int height, int fps, int bitrate) {
    STATUS retStatus = STATUS_SUCCESS;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Encoder context is NULL");

#ifdef ENABLE_OPENH264
    // Initialize OpenH264 encoder
    int rv = WelsCreateSVCEncoder(&ctx->encoder);
    CHK_ERR(rv == 0, STATUS_INVALID_OPERATION, "Failed to create OpenH264 encoder");

    // Set encoding parameters
    memset(&ctx->encoding_params, 0, sizeof(SEncParamExt));
    (*ctx->encoder)->GetDefaultParams(ctx->encoder, &ctx->encoding_params);

    ctx->encoding_params.iUsageType = CAMERA_VIDEO_REAL_TIME;
    ctx->encoding_params.fMaxFrameRate = fps;
    ctx->encoding_params.iPicWidth = width;
    ctx->encoding_params.iPicHeight = height;
    ctx->encoding_params.iTargetBitrate = bitrate;
    ctx->encoding_params.iMaxBitrate = bitrate * 1.2;
    ctx->encoding_params.iRCMode = RC_BITRATE_MODE;
    ctx->encoding_params.iTemporalLayerNum = 1;
    ctx->encoding_params.iSpatialLayerNum = 1;
    ctx->encoding_params.bEnableDenoise = false;
    ctx->encoding_params.bEnableBackgroundDetection = true;
    ctx->encoding_params.bEnableAdaptiveQuant = true;
    ctx->encoding_params.bEnableFrameSkip = false;
    ctx->encoding_params.bEnableLongTermReference = false;
    ctx->encoding_params.iLtrMarkPeriod = 30;
    ctx->encoding_params.uiIntraPeriod = fps * 2; // Key frame every 2 seconds
    ctx->encoding_params.eSpsPpsIdStrategy = CONSTANT_ID;
    ctx->encoding_params.bPrefixNalAddingCtrl = false;
    ctx->encoding_params.iComplexityMode = LOW_COMPLEXITY;
    ctx->encoding_params.bSimulcastAVC = false;

    // Spatial layer configuration
    ctx->encoding_params.sSpatialLayers[0].iVideoWidth = width;
    ctx->encoding_params.sSpatialLayers[0].iVideoHeight = height;
    ctx->encoding_params.sSpatialLayers[0].fFrameRate = fps;
    ctx->encoding_params.sSpatialLayers[0].iSpatialBitrate = bitrate;
    ctx->encoding_params.sSpatialLayers[0].iMaxSpatialBitrate = bitrate * 1.2;
    ctx->encoding_params.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    ctx->encoding_params.sSpatialLayers[0].uiLevelIdc = LEVEL_3_1;
    ctx->encoding_params.sSpatialLayers[0].iDLayerQp = 26;
    ctx->encoding_params.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

    // Initialize encoder
    rv = (*ctx->encoder)->InitializeExt(ctx->encoder, &ctx->encoding_params);
    CHK_ERR(rv == cmResultSuccess, STATUS_INVALID_OPERATION, "Failed to initialize OpenH264 encoder, error: %d", rv);

    // Set encoder options
    int videoFormat = videoFormatI420;
    (*ctx->encoder)->SetOption(ctx->encoder, ENCODER_OPTION_DATAFORMAT, &videoFormat);

    ctx->width = width;
    ctx->height = height;
    ctx->fps = fps;
    ctx->bitrate = bitrate;
    ctx->initialized = TRUE;

    DLOGI("H264 encoder initialized: %dx%d @ %d fps, %d bps", width, height, fps, bitrate);

#else
    CHK_ERR(FALSE, STATUS_NOT_IMPLEMENTED, "OpenH264 encoder is not available. Compile with ENABLE_OPENH264");
#endif

CleanUp:
    return retStatus;
}

STATUS encodeFrame(H264EncoderContext* ctx, PBYTE yuv_data, PBYTE* h264_data, PUINT32 h264_size, PBOOL isKeyFrame) {
    STATUS retStatus = STATUS_SUCCESS;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Encoder context is NULL");
    CHK_ERR(ctx->initialized, STATUS_INVALID_OPERATION, "Encoder not initialized");
    CHK_ERR(yuv_data != NULL, STATUS_NULL_ARG, "YUV data is NULL");
    CHK_ERR(h264_data != NULL, STATUS_NULL_ARG, "H264 data pointer is NULL");
    CHK_ERR(h264_size != NULL, STATUS_NULL_ARG, "H264 size pointer is NULL");

#ifdef ENABLE_OPENH264
    // Setup source picture
    memset(&ctx->pic, 0, sizeof(SSourcePicture));
    ctx->pic.iPicWidth = ctx->width;
    ctx->pic.iPicHeight = ctx->height;
    ctx->pic.iColorFormat = videoFormatI420;
    ctx->pic.iStride[0] = ctx->width;
    ctx->pic.iStride[1] = ctx->width / 2;
    ctx->pic.iStride[2] = ctx->width / 2;
    ctx->pic.pData[0] = yuv_data;
    ctx->pic.pData[1] = yuv_data + (ctx->width * ctx->height);
    ctx->pic.pData[2] = yuv_data + (ctx->width * ctx->height * 5 / 4);

    // Encode frame
    memset(&ctx->info, 0, sizeof(SFrameBSInfo));
    int rv = (*ctx->encoder)->EncodeFrame(ctx->encoder, &ctx->pic, &ctx->info);
    CHK_ERR(rv == cmResultSuccess, STATUS_INVALID_OPERATION, "Failed to encode frame, error: %d", rv);

    if (ctx->info.eFrameType != videoFrameTypeSkip) {
        // Calculate total encoded size
        UINT32 totalSize = 0;
        for (int layer = 0; layer < ctx->info.iLayerNum; layer++) {
            for (int nal = 0; nal < ctx->info.sLayerInfo[layer].iNalCount; nal++) {
                totalSize += ctx->info.sLayerInfo[layer].pNalLengthInByte[nal];
            }
        }

        *h264_size = totalSize;
        *h264_data = (PBYTE) ctx->info.sLayerInfo[0].pBsBuf;

        if (isKeyFrame != NULL) {
            *isKeyFrame = (ctx->info.eFrameType == videoFrameTypeIDR || ctx->info.eFrameType == videoFrameTypeI);
        }

        // Log frame information
        if (ctx->info.eFrameType == videoFrameTypeIDR || ctx->info.eFrameType == videoFrameTypeI) {
            DLOGV("Encoded I-frame: size=%u bytes", totalSize);
        } else {
            DLOGV("Encoded P-frame: size=%u bytes", totalSize);
        }
    } else {
        *h264_size = 0;
        *h264_data = NULL;
        if (isKeyFrame != NULL) {
            *isKeyFrame = FALSE;
        }
        DLOGV("Frame skipped by encoder");
    }

#else
    CHK_ERR(FALSE, STATUS_NOT_IMPLEMENTED, "OpenH264 encoder is not available. Compile with ENABLE_OPENH264");
#endif

CleanUp:
    return retStatus;
}

STATUS cleanupH264Encoder(H264EncoderContext* ctx) {
    STATUS retStatus = STATUS_SUCCESS;

    CHK_ERR(ctx != NULL, STATUS_NULL_ARG, "Encoder context is NULL");

#ifdef ENABLE_OPENH264
    if (ctx->initialized && ctx->encoder != NULL) {
        (*ctx->encoder)->Uninitialize(ctx->encoder);
        WelsDestroySVCEncoder(ctx->encoder);
        ctx->encoder = NULL;
        ctx->initialized = FALSE;
        DLOGI("H264 encoder cleaned up");
    }
#endif

CleanUp:
    return retStatus;
}