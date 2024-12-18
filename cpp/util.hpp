#ifndef __UTIL_H__
#define __UTIL_H__
#include <sys/time.h>
#include <unistd.h>

#ifdef __x86_64__
#include "string.h"
void *fast_memcpy(volatile void *dst, volatile void *src, int sz)
{
    return memcpy((void *)dst, (void *)src, sz);
}
#elif __aarch64__
void fast_memcpy(volatile void *dst, volatile void *src, int sz)
{
    if (sz & 63)
    {
        sz = (sz & -64) + 64;
    }
    asm volatile("NEONCopyPLD: \n"
                 "sub %[dst], %[dst], #64 \n"
                 "1: \n"
                 "ldnp q0, q1, [%[src]] \n"
                 "ldnp q2, q3, [%[src], #32] \n"
                 "add %[dst], %[dst], #64 \n"
                 "subs %[sz], %[sz], #64 \n"
                 "add %[src], %[src], #64 \n"
                 "stnp q0, q1, [%[dst]] \n"
                 "stnp q2, q3, [%[dst], #32] \n"
                 "b.gt 1b \n"
                 : [dst] "+r"(dst), [src] "+r"(src), [sz] "+r"(sz)
                 :
                 : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
}
#elif __arm__
void fast_memcpy(volatile void *dst, volatile void *src, int sz)
{
    if (sz & 63)
    {
        sz = (sz & -64) + 64;
    }
    asm volatile("NEONCopyPLD:                          \n"
                 "    VLDM %[src]!,{d0-d7}                 \n"
                 "    VSTM %[dst]!,{d0-d7}                 \n"
                 "    SUBS %[sz],%[sz],#0x40                 \n"
                 "    BGT NEONCopyPLD                  \n"
                 : [dst] "+r"(dst), [src] "+r"(src), [sz] "+r"(sz)
                 :
                 : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
}

#endif


long long millis()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;
}

#endif