#ifndef PY_OBJSTR_H
#define PY_OBJSTR_H

#include <string.h>
#include <stddef.h>

#ifndef MICROPY_PY_BUILTINS_STR_OP_METHODS
typedef unsigned char byte;

static inline const byte *find_subbytes(const byte *haystack, size_t hlen, const byte *needle, size_t nlen, int dir) {
  (void)hlen;
  (void)dir;
  (void)nlen;
  return (const byte *)strstr((const char *)haystack, (const char *)needle);
}
#endif

#endif

