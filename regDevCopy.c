#include "regDevSup.h"

/* driver helper function ***************************************************/

/* byte swapping facility */

#if defined (__PPC__) && defined (__GNUC__) && __GNUC__*100+__GNUC_MINOR__ >= 403
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#elif (!defined (vxWorks) && __GNUC__ >= 3)
#include <byteswap.h>
#elif defined(_WIN32)
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) \
     ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |  \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define bswap_64(x) \
     ((((x) & 0xff00000000000000ull) >> 56) |  \
      (((x) & 0x00ff000000000000ull) >> 40) |  \
      (((x) & 0x0000ff0000000000ull) >> 24) |  \
      (((x) & 0x000000ff00000000ull) >> 8)  |  \
      (((x) & 0x00000000ff000000ull) << 8)  |  \
      (((x) & 0x0000000000ff0000ull) << 24) |  \
      (((x) & 0x000000000000ff00ull) << 40) |  \
      (((x) & 0x00000000000000ffull) << 56))
#endif

/* silly but useful :-) */
#define bswap_8(x) (x)

#define COPY(N, nelem, src, dest) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    while (nelem--) \
    { \
        *d++ = *s++; \
    } \
}

#define COPY_D(N, dlen, nelem, src, dest) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    size_t i = nelem * dlen; \
    while (i--) \
    { \
        *d++ = *s++; \
    } \
}

#define COPY_SWAP(N, nelem, src, dest) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    epicsUInt##N x; \
    while (nelem--) \
    { \
        x = *s++; \
        *d++ = bswap_##N(x); \
    } \
}

#define COPY_SWAP_D(N, dlen, nelem, src, dest) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    epicsUInt##N x; \
    unsigned int i; \
    while (nelem--) \
    { \
        s+=dlen; \
        for (i=0; i<dlen; i++) \
        { \
            x = *--s; \
            *d++ = bswap_##N(x); \
        } \
        s+=dlen; \
    } \
}

#define COPY_MASKED(N, nelem, src, dest, pmask) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    epicsUInt##N m = *(const epicsUInt##N*)pmask;\
    epicsUInt##N x; \
    while (nelem--) \
    { \
        x = (*s++ & m) | (*d & ~m); \
        *d++ = x; \
    } \
}

#define COPY_MASKED_D(N, dlen, nelem, src, dest, pmask) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    const epicsUInt##N *m = pmask;\
    epicsUInt##N x; \
    unsigned int i; \
    while (nelem--) \
    { \
        m = pmask; \
        for (i=0; i<dlen; i++) \
        { \
            x = *d & ~*m; \
            x |= *s++ & *m++; \
            *d++ = x; \
        } \
    } \
}

#define COPY_MASKED_SWAP(N, nelem, src, dest, pmask) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    epicsUInt##N m = bswap_##N(*(const epicsUInt##N*)pmask);\
    epicsUInt##N x; \
    while (nelem--) \
    { \
        x = *s++; \
        x = (bswap_##N(x) & m) | (*d & ~m); \
        *d++ = x; \
    } \
}

#define COPY_MASKED_SWAP_D(N, dlen, nelem, src, dest, pmask) \
{ \
    const volatile epicsUInt##N* s = src;\
    volatile epicsUInt##N* d = dest;\
    const epicsUInt##N* m = pmask;\
    epicsUInt##N x; \
    unsigned int i; \
    while (nelem--) \
    { \
        m = pmask; \
        s+=dlen; \
        m+=dlen; \
        for (i=0; i<dlen; i++) \
        { \
            x = bswap_##N(*d) & ~*--m; \
            x |= *--s & *m; \
            *d++ = bswap_##N(x); \
        } \
        s+=dlen; \
    } \
}

#define SWAP (1<<4)
#define MASK (1<<5)

static union {epicsUInt8 b[0]; epicsUInt32 u;} endianess = {.u = 0x12345678};

void regDevCopy(unsigned int dlen, size_t nelem, const volatile void* src, volatile void* dest, const void* pmask, int swap)
{
    /* check alignment */
    size_t alignment = (1<<dlen)-1;

    /* handle conditional swapping */
    if (swap == REGDEV_BE_SWAP) swap = (endianess.b[0] == 0x12);
    else if (swap == REGDEV_LE_SWAP) swap = (endianess.b[0] == 0x78);

    regDevDebugLog(REGDEV_DBG_COPY, "dlen=%d, nelem=%" Z "d, src=%p, dest=%p, pmask=%p, swap=%d\n",
        dlen, nelem, src, dest, pmask, swap);

    if (((size_t)src  & alignment) == 0 &&
        ((size_t)dest & alignment) == 0 &&
        (pmask == 0 || ((size_t)pmask & alignment) == 0) &&
        (dlen <= 8))
    {
        /* handle aligned standard element sizes: 1, 2 ,4, 8 bytes */
        switch (dlen + (swap ? SWAP : 0) + (pmask ? MASK : 0))
        {
            case 0:
                return;
            case 1:
            case 1 + SWAP:
                COPY(8, nelem, src, dest);
                return;
            case 1 + MASK:
            case 1 + MASK + SWAP:
                COPY_MASKED(8, nelem, src, dest, pmask);
                return;
            case 2:
                COPY(16, nelem, src, dest);
                return;
            case 2 + SWAP:
                COPY_SWAP(16, nelem, src, dest);
                return;
            case 2 + MASK:
                COPY_MASKED(16, nelem, src, dest, pmask);
                return;
            case 2 + MASK + SWAP:
                COPY_MASKED_SWAP(16, nelem, src, dest, pmask);
                return;
            case 4:
                COPY(32, nelem, src, dest);
                return;
            case 4 + SWAP:
                COPY_SWAP(32, nelem, src, dest);
                return;
            case 4 + MASK:
                COPY_MASKED(32, nelem, src, dest, pmask);
                return;
            case 4 + MASK + SWAP:
                COPY_MASKED_SWAP(32, nelem, src, dest, pmask);
                return;
            case 8:
                COPY(64, nelem, src, dest);
                return;
            case 8 + SWAP:
                COPY_SWAP(64, nelem, src, dest);
                return;
            case 8 + MASK:
                COPY_MASKED(64, nelem, src, dest, pmask);
                return;
            case 8 + MASK + SWAP:
                COPY_MASKED_SWAP(64, nelem, src, dest, pmask);
                return;
        }
    }
    /* unusual element sizes or unaligned buffers */
    switch ((dlen&7) + (swap ? SWAP : 0) + (pmask ? MASK : 0))
    {
        /* multiple of 8: copy qword wise */
        case 0:
            COPY_D(64, dlen>>3, nelem, src, dest);
            return;
        case 0 + SWAP:
            COPY_SWAP_D(64, dlen>>3, nelem, src, dest);
            return;
        case 0 + MASK:
            COPY_MASKED_D(64, dlen>>3, nelem, src, dest, pmask);
            return;
        case 0 + MASK + SWAP:
            COPY_MASKED_SWAP_D(64, dlen>>3, nelem, src, dest, pmask);
            return;

        /* multiple of 4: copy dword wise */
        case 4:
            COPY_D(32, dlen>>2, nelem, src, dest);
            return;
        case 4 + SWAP:
            COPY_SWAP_D(32, dlen>>2, nelem, src, dest);
            return;
        case 4 + MASK:
            COPY_MASKED_D(32, dlen>>2, nelem, src, dest, pmask);
            return;
        case 4 + MASK + SWAP:
            COPY_MASKED_SWAP_D(32, dlen>>2, nelem, src, dest, pmask);
            return;
        /* multiple of 2: copy word wise */
        case 2:
        case 6:
            COPY_D(16, dlen>>1, nelem, src, dest);
            return;
        case 2 + SWAP:
        case 6 + SWAP:
            COPY_SWAP_D(16, dlen>>1, nelem, src, dest);
            return;
        case 2 + MASK:
        case 6 + MASK:
            COPY_MASKED_D(16, dlen>>1, nelem, src, dest, pmask);
            return;
        case 2 + MASK + SWAP:
        case 6 + MASK + SWAP:
            COPY_MASKED_SWAP_D(16, dlen>>1, nelem, src, dest, pmask);
            return;
        /* odd: copy byte wise */
        case 1:
        case 3:
        case 5:
        case 7:
            COPY_D(8, dlen, nelem, src, dest);
            return;
        case 1 + SWAP:
        case 3 + SWAP:
        case 5 + SWAP:
        case 7 + SWAP:
            COPY_SWAP_D(8, dlen, nelem, src, dest);
            return;
        case 1 + MASK:
        case 3 + MASK:
        case 5 + MASK:
        case 7 + MASK:
            COPY_MASKED_D(8, dlen, nelem, src, dest, pmask);
            return;
        case 1 + MASK + SWAP:
        case 3 + MASK + SWAP:
        case 5 + MASK + SWAP:
        case 7 + MASK + SWAP:
            COPY_MASKED_SWAP_D(8, dlen, nelem, src, dest, pmask);
            return;
    }
}

#ifdef TESTCASE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void printfbuf(char*b, int n)
{
    int i;

    n--;
    for(i=0; i<=n; i++)
        printf("0x%02x%c", b[i]&0xff, i==n?'\n':',');
}

#define check(dlen, nelem, m, s, e...) \
do { \
    char expect[] = e; \
    assert(dlen*nelem<=sizeof(testpattern)); \
    memset(buffer, 0x55, dlen*nelem); \
    regDevCopy(dlen, nelem, testpattern, buffer, m, s); \
    if (memcmp(buffer, expect, dlen*nelem)) { \
        printf ("fail in check(dlen=%d, nelem=%d, mask=%s, swap=%s)\n", dlen, nelem, #m, #s); \
        printf ("expect: "); \
        printfbuf(expect, dlen*nelem); \
        printf ("got   : "); \
        printfbuf(buffer, dlen*nelem); \
        exit(1); \
    } \
} while (0)

int main()
{
    char mask[] = {
    0x0f,0xf0,0x5a,0xa5,0xff,0x00,0x33,0xcc,0x66,0x99
    };
    char testpattern[] = {
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x21,0x22,0x23
    };
    char buffer[sizeof(testpattern)];

    check( 0, 0, NULL, FALSE, {});
    check( 0, 0, NULL, TRUE,  {});
    check( 0, 0, mask, FALSE, {});
    check( 0, 0, mask, TRUE,  {});
    check( 1, 1, NULL, FALSE, {0x11});
    check( 1, 1, NULL, TRUE,  {0x11});
    check( 1, 1, mask, FALSE, {0x51});
    check( 1, 1, mask, TRUE,  {0x51});
    check( 2, 1, NULL, FALSE, {0x11,0x12});
    check( 2, 1, NULL, TRUE,  {0x12,0x11});
    check( 2, 1, mask, FALSE, {0x51,0x15});
    check( 2, 1, mask, TRUE,  {0x15,0x51});
    check( 3, 1, NULL, FALSE, {0x11,0x12,0x13});
    check( 3, 1, NULL, TRUE,  {0x13,0x12,0x11});
    check( 3, 1, mask, FALSE, {0x51,0x15,0x17});
    check( 3, 1, mask, TRUE,  {0x17,0x15,0x51});
    check( 4, 1, NULL, FALSE, {0x11,0x12,0x13,0x14});
    check( 4, 1, NULL, TRUE,  {0x14,0x13,0x12,0x11});
    check( 4, 1, mask, FALSE, {0x51,0x15,0x17,0x54});
    check( 4, 1, mask, TRUE,  {0x54,0x17,0x15,0x51});
    check( 5, 1, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15});
    check( 5, 1, NULL, TRUE,  {0x15,0x14,0x13,0x12,0x11});
    check( 5, 1, mask, FALSE, {0x51,0x15,0x17,0x54,0x15});
    check( 5, 1, mask, TRUE,  {0x15,0x54,0x17,0x15,0x51});
    check( 6, 1, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16});
    check( 6, 1, NULL, TRUE,  {0x16,0x15,0x14,0x13,0x12,0x11});
    check( 6, 1, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55});
    check( 6, 1, mask, TRUE,  {0x55,0x15,0x54,0x17,0x15,0x51});
    check( 7, 1, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17});
    check( 7, 1, NULL, TRUE,  {0x17,0x16,0x15,0x14,0x13,0x12,0x11});
    check( 7, 1, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57});
    check( 7, 1, mask, TRUE,  {0x57,0x55,0x15,0x54,0x17,0x15,0x51});
    check( 8, 1, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18});
    check( 8, 1, NULL, TRUE,  {0x18,0x17,0x16,0x15,0x14,0x13,0x12,0x11});
    check( 8, 1, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57,0x19});
    check( 8, 1, mask, TRUE,  {0x19,0x57,0x55,0x15,0x54,0x17,0x15,0x51});
    check( 9, 1, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19});
    check( 9, 1, NULL, TRUE,  {0x19,0x18,0x17,0x16,0x15,0x14,0x13,0x12,0x11});
    check( 9, 1, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57,0x19,0x11});
    check( 9, 1, mask, TRUE,  {0x11,0x19,0x57,0x55,0x15,0x54,0x17,0x15,0x51});

    check( 1, 2, NULL, FALSE, {0x11,0x12});
    check( 1, 2, NULL, TRUE,  {0x11,0x12});
    check( 1, 2, mask, FALSE, {0x51,0x52});
    check( 1, 2, mask, TRUE,  {0x51,0x52});
    check( 2, 2, NULL, FALSE, {0x11,0x12, 0x13,0x14});
    check( 2, 2, NULL, TRUE,  {0x12,0x11, 0x14,0x13});
    check( 2, 2, mask, FALSE, {0x51,0x15, 0x53,0x15});
    check( 2, 2, mask, TRUE,  {0x15,0x51, 0x15,0x53});
    check( 3, 2, NULL, FALSE, {0x11,0x12,0x13, 0x14,0x15,0x16});
    check( 3, 2, NULL, TRUE,  {0x13,0x12,0x11, 0x16,0x15,0x14});
    check( 3, 2, mask, FALSE, {0x51,0x15,0x17, 0x54,0x15,0x17});
    check( 3, 2, mask, TRUE,  {0x17,0x15,0x51, 0x17,0x15,0x54});
    check( 4, 2, NULL, FALSE, {0x11,0x12,0x13,0x14, 0x15,0x16,0x17,0x18});
    check( 4, 2, NULL, TRUE,  {0x14,0x13,0x12,0x11, 0x18,0x17,0x16,0x15});
    check( 4, 2, mask, FALSE, {0x51,0x15,0x17,0x54, 0x55,0x15,0x17,0x50});
    check( 4, 2, mask, TRUE,  {0x54,0x17,0x15,0x51, 0x50,0x17,0x15,0x55});
    check( 5, 2, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15, 0x16,0x17,0x18,0x19,0x1a});
    check( 5, 2, NULL, TRUE,  {0x15,0x14,0x13,0x12,0x11, 0x1a,0x19,0x18,0x17,0x16});
    check( 5, 2, mask, FALSE, {0x51,0x15,0x17,0x54,0x15, 0x56,0x15,0x1d,0x51,0x1a});
    check( 5, 2, mask, TRUE,  {0x15,0x54,0x17,0x15,0x51, 0x1a,0x51,0x1d,0x15,0x56});
    check( 6, 2, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16, 0x17,0x18,0x19,0x1a,0x1b,0x1c});
    check( 6, 2, NULL, TRUE,  {0x16,0x15,0x14,0x13,0x12,0x11, 0x1c,0x1b,0x1a,0x19,0x18,0x17});
    check( 6, 2, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55, 0x57,0x15,0x1d,0x50,0x1b,0x55});
    check( 6, 2, mask, TRUE,  {0x55,0x15,0x54,0x17,0x15,0x51, 0x55,0x1b,0x50,0x1d,0x15,0x57});
    check( 7, 2, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17, 0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e});
    check( 7, 2, NULL, TRUE,  {0x17,0x16,0x15,0x14,0x13,0x12,0x11, 0x1e,0x1d,0x1c,0x1b,0x1a,0x19,0x18});
    check( 7, 2, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57, 0x58,0x15,0x1f,0x51,0x1c,0x55,0x56});
    check( 7, 2, mask, TRUE,  {0x57,0x55,0x15,0x54,0x17,0x15,0x51, 0x56,0x55,0x1c,0x51,0x1f,0x15,0x58});
    check( 8, 2, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18, 0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x21});
    check( 8, 2, NULL, TRUE,  {0x18,0x17,0x16,0x15,0x14,0x13,0x12,0x11, 0x21,0x1f,0x1e,0x1d,0x1c,0x1b,0x1a,0x19});
    check( 8, 2, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57,0x19, 0x59,0x15,0x1f,0x54,0x1d,0x55,0x57,0x11});
    check( 8, 2, mask, TRUE,  {0x19,0x57,0x55,0x15,0x54,0x17,0x15,0x51, 0x11,0x57,0x55,0x1d,0x54,0x1f,0x15,0x59});
    check( 9, 2, NULL, FALSE, {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19, 0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x21,0x22,0x23});
    check( 9, 2, NULL, TRUE,  {0x19,0x18,0x17,0x16,0x15,0x14,0x13,0x12,0x11, 0x23,0x22,0x21,0x1f,0x1e,0x1d,0x1c,0x1b,0x1a});
    check( 9, 2, mask, FALSE, {0x51,0x15,0x17,0x54,0x15,0x55,0x57,0x19,0x11, 0x5a,0x15,0x1d,0x55,0x1e,0x55,0x65,0x11,0x33});
    check( 9, 2, mask, TRUE,  {0x11,0x19,0x57,0x55,0x15,0x54,0x17,0x15,0x51, 0x33,0x11,0x65,0x55,0x1e,0x55,0x1d,0x15,0x5a});

    printf ("All OK.\n");
    return 0;
}
#endif
