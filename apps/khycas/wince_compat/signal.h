#ifndef _WINCE_COMPAT_SIGNAL_H
#define _WINCE_COMPAT_SIGNAL_H

typedef int sig_atomic_t;
#define SIGINT 2
#define SIGILL 4
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15
#define SIGBREAK 21
#define SIGABRT 22

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#ifdef __cplusplus
extern "C" {
#endif
static inline void (*signal(int sig, void (*func)(int)))(int) {
    return SIG_DFL;
}
#ifdef __cplusplus
}
#endif

#endif
