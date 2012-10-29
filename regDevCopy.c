/* driver helper function ***************************************************/

/* byte swapping facility */
#if (!defined (__vxworks) && __GNUC__ >= 3)
#include <byteswap.h>
#elif defined(_WIN32)
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) \
     ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |		      \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define bswap_64(x) \
     ((((x) & 0xff00000000000000ull) >> 56)				      \
      | (((x) & 0x00ff000000000000ull) >> 40)				      \
      | (((x) & 0x0000ff0000000000ull) >> 24)				      \
      | (((x) & 0x000000ff00000000ull) >> 8)				      \
      | (((x) & 0x00000000ff000000ull) << 8)				      \
      | (((x) & 0x0000000000ff0000ull) << 24)				      \
      | (((x) & 0x000000000000ff00ull) << 40)				      \
      | (((x) & 0x00000000000000ffull) << 56))
#endif

#include <epicsVersion.h>
#include <epicsTypes.h>
#if (EPICS_REVISION<15)
typedef unsigned long long epicsUInt64;
#endif

#define def_regDevCopy(N) \
static void regDevCopy##N(unsigned int nelem, volatile epicsUInt##N* src, volatile epicsUInt##N* dest) \
{ \
    while (nelem--) \
    { \
        *dest++ = *src++; \
    } \
}

#define def_regDevCopySwap(N) \
static void regDevCopySwap##N(unsigned int nelem, volatile epicsUInt##N* src, volatile epicsUInt##N* dest) \
{ \
    epicsUInt##N x; \
    while (nelem--) \
    { \
        x = *src++; \
        *dest++ = bswap_##N(x); \
    } \
}

#define def_regDevCopyMasked(N) \
static void regDevCopyMasked##N(unsigned int nelem, volatile epicsUInt##N* src, volatile epicsUInt##N* dest, epicsUInt##N mask) \
{ \
    epicsUInt##N x; \
    while (nelem--) \
    { \
        x = (*src++ & mask) | (*dest & ~mask); \
        *dest++ = x; \
    } \
}

#define def_regDevCopyMaskedSwap(N) \
static void regDevCopyMaskedSwap##N(unsigned int nelem, volatile epicsUInt##N* src, volatile epicsUInt##N* dest, epicsUInt##N mask) \
{ \
    epicsUInt##N x; \
    mask = bswap_##N(mask); \
    while (nelem--) \
    { \
        x = *src++; \
        x = (bswap_##N(x) & mask) | (*dest & ~mask ); \
        *dest++ = x; \
    } \
}

#define def_regDevCopyMaskedSwap(N) \
static void regDevCopyMaskedSwap##N(unsigned int nelem, volatile epicsUInt##N* src, volatile epicsUInt##N* dest, epicsUInt##N mask) \
{ \
    epicsUInt##N x; \
    mask = bswap_##N(mask); \
    while (nelem--) \
    { \
        x = *src++; \
        x = (bswap_##N(x) & mask) | (*dest & ~mask ); \
        *dest++ = x; \
    } \
}

def_regDevCopy(8)
def_regDevCopy(16)
def_regDevCopy(32)
def_regDevCopy(64)
def_regDevCopyMasked(8)
def_regDevCopyMasked(16)
def_regDevCopyMasked(32)
def_regDevCopyMasked(64)
def_regDevCopySwap(16)
def_regDevCopySwap(32)
def_regDevCopySwap(64)
def_regDevCopyMaskedSwap(16)
def_regDevCopyMaskedSwap(32)
def_regDevCopyMaskedSwap(64)

void regDevCopy(unsigned int dlen, unsigned int nelem, volatile void* src, volatile void* dest, void* pmask, int swap)
{
    /* check alignment */
    unsigned int alignment = (1<<dlen)-1;
    if (((unsigned int)src  & alignment) == 0 &&
        ((unsigned int)dest & alignment) == 0 &&
        (pmask == 0 || ((unsigned int)pmask & alignment) == 0))
    {
        /* handle standard element sizes: 1, 2 ,4, 8 bytes */
        switch (dlen<<2 | (pmask!=0)<<1 | (swap!=0))
        {
            case 0:
                return;
            case 1<<2 | 0:
            case 1<<2 | 1:
                regDevCopy8(nelem, src, dest);
                return;
            case 1<<2 | 2:
            case 1<<2 | 3:
                regDevCopyMasked8(nelem, src, dest, *(epicsUInt8*)pmask);
                return;
            case 2<<2 | 0:
                regDevCopy16(nelem, src, dest);
                return;
            case 2<<2 | 1:
                regDevCopySwap16(nelem, src, dest);
                return;
            case 2<<2 | 2:
                regDevCopyMasked16(nelem, src, dest, *(epicsUInt16*)pmask);
                return;
            case 2<<2 | 3:
                regDevCopyMaskedSwap16(nelem, src, dest, *(epicsUInt16*)pmask);
                return;
            case 4<<2 | 0:
                regDevCopy32(nelem, src, dest);
                return;
            case 4<<2 | 1:
                regDevCopySwap32(nelem, src, dest);
                return;
            case 4<<2 | 2:
                regDevCopyMasked32(nelem, src, dest, *(epicsUInt32*)pmask);
                return;
            case 4<<2 | 3:
                regDevCopyMaskedSwap32(nelem, src, dest, *(epicsUInt32*)pmask);
                return;
            case 8<<2 | 0:
                regDevCopy64(nelem, src, dest);
                return;
            case 8<<2 | 1:
                regDevCopySwap64(nelem, src, dest);
                return;
            case 8<<2 | 2:
                regDevCopyMasked64(nelem, src, dest, *(epicsUInt64*)pmask);
                return;
            case 8<<2 | 3:
                regDevCopyMaskedSwap64(nelem, src, dest, *(epicsUInt64*)pmask);
                return;
        }
    }
    /* unusual element sizes and unaligned buffers */
    switch (dlen & 7)
    {
        case 0: /* multiple of 8: copy qword wise */
        {
            epicsUInt64 x;
            volatile epicsUInt64* s = src;
            volatile epicsUInt64* d = dest;
            epicsUInt64* m;
            int i;
            
            dlen>>=3;
            if (pmask)
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        m+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *d;
                            x = bswap_64(x) & ~*--m;
                            x |= *--s & *m;
                            *d++ = bswap_64(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            x = *d & ~*m;
                            x |= *s++ & *m++;
                            *d++ = x;
                        }
                    }
                }
            }
            else
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *--s;
                            *d++ = bswap_64(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            *d++ = *s++;
                        }
                    }
                }
            }
            return;
        }
        case 4: /* multiple of 4: copy dword wise */
        {
            epicsUInt32  x;
            volatile epicsUInt32* s = src;
            volatile epicsUInt32* d = dest;
            epicsUInt32* m;
            int i;
            
            dlen>>=2;
            if (pmask)
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        m+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *d;
                            x = bswap_32(x) & ~*--m;
                            x |= *--s & *m;
                            *d++ = bswap_32(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            x = *d & ~*m;
                            x |= *s++ & *m++;
                            *d++ = x;
                        }
                    }
                }
            }
            else
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *--s;
                            *d++ = bswap_32(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            *d++ = *s++;
                        }
                    }
                }
            }
            return;
        }
        case 2:
        case 6: /* multiple of 2: copy word wise */
        {
            epicsUInt16  x;
            volatile epicsUInt16* s = src;
            volatile epicsUInt16* d = dest;
            epicsUInt16* m;
            int i;
            
            dlen>>=1;
            if (pmask)
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        m+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *d;
                            x = bswap_16(x) & ~*--m;
                            x |= *--s & *m;
                            *d++ = bswap_16(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            x = *d & ~*m;
                            x |= *s++ & *m++;
                            *d++ = x;
                        }
                    }
                }
            }
            else
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *--s;
                            *d++ = bswap_16(x);
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            *d++ = *s++;
                        }
                    }
                }
            }
            return;
        }
        case 1:
        case 3:
        case 5:
        case 7: /* odd: copy byte wise */
        {
            epicsUInt8  x;
            volatile epicsUInt8* s = src;
            volatile epicsUInt8* d = dest;
            epicsUInt8* m;
            int i;
            
            if (pmask)
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        m+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            x = *--s;
                            x &= *--m;
                            x |= *d & ~*m;
                            *d++ = x;
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            x = *s++;
                            x &= *m;
                            x |= *d & ~*m++;
                            *d++ = x;
                        }
                    }
                }
            }
            else
            {
                while (nelem--)
                {
                    m = pmask;
                    if (swap)
                    {
                        s+=dlen;
                        for (i=0; i<dlen; i++)
                        {
                            *d++ = *--s;
                        }
                        s+=dlen;
                    }
                    else
                    {
                        for (i=0; i<dlen; i++)
                        {
                            *d++ = *s++;
                        }
                    }
                }
            }
        }
    }
}
