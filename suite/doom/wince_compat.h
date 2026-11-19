#ifndef LILIUM_DOOM_WINCE_COMPAT_H
#define LILIUM_DOOM_WINCE_COMPAT_H

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE * lilium_fopen(const char * utf8Path, const char * mode);
int lilium_remove(const char * utf8Path);
int lilium_rename(const char * oldUtf8, const char * newUtf8);
int lilium_mkdir(const char * utf8Path);
int lilium_file_exists(const char * utf8Path);
char * lilium_getenv(const char * name);
int lilium_system(const char * cmd);

void LiliumDoom_OnQuit(void);
void LiliumDoom_OnError(const char * msg);

#ifdef __cplusplus
}
#endif

#ifndef LILIUM_WINCE_COMPAT_IMPL
#ifdef fopen
#undef fopen
#endif
#define fopen(p, m) lilium_fopen((p), (m))

#ifdef remove
#undef remove
#endif
#define remove(p) lilium_remove((p))

#ifdef rename
#undef rename
#endif
#define rename(o, n) lilium_rename((o), (n))

#ifdef getenv
#undef getenv
#endif
#define getenv(n) lilium_getenv((n))

#ifdef system
#undef system
#endif
#define system(c) lilium_system((c))
#endif

#endif // LILIUM_DOOM_WINCE_COMPAT_H
