// stubs.cpp - Stub implementations for missing third-party library functions
// These stubs allow materialsystem to link without nvtc.lib

#include "platform.h"

// nvtc.lib stub - S3TC (DXT) texture compression
// nvtc.h declares S3TCencode with C++ linkage, so we must match that
#if defined(_WIN32) && !defined(_X360)
#include "nvtc.h"

// Stub for S3TCencode - DXT compression (encoding) will silently produce empty output
void S3TCencode( DDSURFACEDESC *lpSrc, PALETTEENTRY *lpPal,
                 DDSURFACEDESC *lpDest, void *lpDestBuf,
                 unsigned int dwEncodeType, float *weight )
{
    // Stub: DXT encoding not available without nvtc.lib
    // Output will be zeroed/empty. Decompression (decoding) still works via s3tc_decode.cpp.
}

unsigned int S3TCgetEncodeSize( DDSURFACEDESC *lpDesc, unsigned int dwEncodeType )
{
    return 0;
}

int S3TCencodeEx( DDSURFACEDESC *lpSrc, PALETTEENTRY *lpPal,
                  DDSURFACEDESC *lpDest, void *lpDestBuf,
                  unsigned int dwEncodeType, float *weight,
                  LP_S3TC_PROGRESS_CALLBACK lpS3TCProgressProc,
                  LPVOID lpArg1, LPVOID lpArg2 )
{
    return 0;
}

#endif // _WIN32
