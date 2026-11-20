#ifndef LILIUM_INFLATE_H
#define LILIUM_INFLATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decompresses raw RFC 1951 DEFLATE stream `src[0..srcLen)` into `dst[0..dstLen)`.
// Returns uncompressed byte count (>= 0) on success, or < 0 on error.
int lilium_inflate_raw(const uint8_t * src, size_t srcLen, uint8_t * dst, size_t dstLen);

#ifdef __cplusplus
}
#endif

#endif // LILIUM_INFLATE_H
