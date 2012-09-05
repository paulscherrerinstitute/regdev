/* Generic register Device Support */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef vxWorks
#include <memLib.h>
#endif

#define epicsTypesGLOBAL
#include "regDevSup.h"

#ifndef __GNUC__
#define __attribute__(a)
#endif

static char cvsid_regDev[] __attribute__((unused)) =
    "$Id: regDev.c,v 1.24 2012/09/05 08:13:47 kalantari Exp $";

static regDeviceNode* registeredDevices = NULL;
static regDeviceAsynNode* registeredAsynDevices = NULL;

int regDevDebug = 0;
epicsExportAddress(int, regDevDebug);

#ifdef __vxworks
int strncasecmp(const char *s1, const char *s2, size_t n)
{
    int x;
    while (n--) {
        x = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (x != 0) return x;
        s1++;
        s2++;
    }
    return 0;
}
#endif

/***********************************************************************
 * Routine to parse IO arguments
 * IO address line format:
 *
 * <name>:<a>[:[i]] [T=<type>] [B=<bit>] [L=<low|strLen>] [H=<high>]
 *
 * where: <name>    - symbolic device name
 *        <a>       - address (byte number) within memory block
 *                    (expressions containing +-*() allowed, no spaces!)
 *        <i>       - optional init read address ( for output records )
 *        <type>    - INT8, INT16, INT32,
 *                    UINT16 (or UNSIGN16), UINT32 (or UNSIGN32),
 *                    REAL32 (or FLOAT), REAL64 (or DOUBLE),
 *                    STRING,BCD
 *        <bit>     - least significant bit is 0
 *        <low>     - raw value that mapps to EGUL
 *        <high>    - raw value that mapps to EGUF
 **********************************************************************/

#define regDevBCD8T  (100)
#define regDevBCD16T (101)
#define regDevBCD32T (102)
#define regDev64T    (103)
#define regDevFirstType regDevBCD8T
#define regDevLastType  regDev64T

typedef unsigned long long regDev64;

const static struct {char* name; int dlen; epicsType type;} datatypes [] =
{
/* Important for order:
    * The default type must be the first entry.
    * Names that are substrings of others names must come later
*/
    { "short",      2, epicsInt16T   },
    { "int16",      2, epicsInt16T   },

    { "int8",       1, epicsInt8T    },

    { "char",       1, epicsUInt8T   },
    { "byte",       1, epicsUInt8T   },
    { "uint8",      1, epicsUInt8T   },
    { "unsign8",    1, epicsUInt8T   },
    { "unsigned8",  1, epicsUInt8T   },

    { "word",       2, epicsUInt16T  },
    { "uint16",     2, epicsUInt16T  },
    { "unsign16",   2, epicsUInt16T  },
    { "unsigned16", 2, epicsUInt16T  },

    { "long",       4, epicsInt32T   },
    { "int32",      4, epicsInt32T   },

    { "dword",      4, epicsUInt32T  },
    { "uint32",     4, epicsUInt32T  },
    { "unsign32",   4, epicsUInt32T  },
    { "unsigned32", 4, epicsUInt32T  },

    { "double",     8, epicsFloat64T },
    { "real64",     8, epicsFloat64T },
    { "float64",    8, epicsFloat64T },

    { "single",     4, epicsFloat32T },
    { "real32",     4, epicsFloat32T },
    { "float32",    4, epicsFloat32T },
    { "float",      4, epicsFloat32T },

    { "string",     0, epicsStringT  },

    { "qword",      1, regDev64T     },
    
    { "bcd8",       1, regDevBCD8T   },
    { "bcd16",      2, regDevBCD16T  },
    { "bcd32",      4, regDevBCD32T  },
    { "bcd",        1, regDevBCD8T   },
    { "time",       1, regDevBCD8T   } /* for backward compatibility */
};

long regDevParseExpr(char** pp);

long regDevParseValue(char** pp)
{   
    long val;
    char *p = *pp;
      
    if (*p == '(')
    {
        p++;
        val = regDevParseExpr(&p);
        if (*p == ')') p++;
    }
    else val = strtol(p, &p, 0);
    *pp = p;
    return val;
}

long regDevParseExpr(char** pp)
{
    long sum = 0;
    long val;
    char *p = *pp;    
    
    do {
        val = regDevParseValue(&p);
        while (*p == '*')
        {
            p++;
            val *= regDevParseValue(&p);
        }
        sum += val;
    } while (*p == '+' || *p == '-');
    *pp = p;
    return sum;
}

int regDevIoParse2(
    const char* recordName,
    char* parameterstring,
    regDevPrivate* priv)
{
    char devName[255];
    regDeviceNode* device;
    char* p = parameterstring;
    char separator;
    int nchar;
    int status = 0;
    
    static const int maxtype = sizeof(datatypes)/sizeof(*datatypes);
    int type = 0;
    int hset = 0;
    int lset = 0;
    long hwHigh = 0;
    long hwLow = 0;

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum((unsigned char)*p)) if (*p++ == '\0') 
    {
        fprintf(stderr,
            "regDevIoParse %s: no device name in parameter string \"%s\"\n",
            recordName, parameterstring);        
        return S_dev_badArgument;
    }

    /* Get device name */
    nchar = strcspn(p, ":/ ");
    strncpy(devName, p, nchar);
    devName[nchar] = '\0';
    p += nchar;
    separator = *p++;
    regDevDebugLog(1, "regDevIoParse %s: device=%s\n",
        recordName, devName);

    for (device=registeredDevices; device; device=device->next)
    {
        if (strcmp(device->name, devName) == 0) break;
    }
    if (!device)
    {
        fprintf(stderr,
            "regDevIoParse %s: device '%s' not found\n",
            recordName, devName);
        return S_dev_noDevice;
    }
    priv->device = device;

    /* Check device offset (for backward compatibility allow '/') */
    if (separator == ':' || separator == '/')
    {
        long offset = regDevParseExpr(&p);
        if (offset < 0)
        {
            fprintf(stderr,
                "regDevIoParse %s: offset %ld<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        regDevDebugLog(1,
            "regDevIoParse %s: offset=%d\n", recordName, priv->offset);
        separator = *p++;
    }
    else
    {
        priv->offset = 0;
    }

    /* Check init offset (for backward compatibility allow '!' and '/') */
    if (separator == ':' || separator == '/' || separator == '!')
    {
        char* p1 = p;
        long initoffset = regDevParseExpr(&p);
        if (p1 == p)
        {
            priv->initoffset = priv->offset;
        }
        else
        {
            if (initoffset < 0)
            {
                fprintf(stderr,
                    "regDevIoParse %s: init offset %ld<0\n",
                    recordName, initoffset);
                return S_dev_badArgument;
            }
            priv->initoffset = initoffset;
        }
        regDevDebugLog(1,
            "regDevIoParse %s: init offset=%d\n", recordName, priv->initoffset);
        separator = *p++;
    }
    else
    {
        priv->initoffset = DONT_INIT;
    }

    /* set default values for parameters */
    priv->bit = 0;
    priv->hwLow = 0;
    priv->hwHigh = 0;
    priv->invert = 0;
    
    /* allow whitespaces before parameter for device support */
    while ((separator == '\t') || (separator == ' '))
        separator = *p++;

    /* driver parameter for device support if present */
    nchar = 0;
    if (separator != '\'') p--; /* optional quote for compatibility */
    
    /* parse parameters */
    while (p && *p)
    {
        switch (toupper(*p))
        {
            case ' ':
            case '\t':
                p++;
                break;
            case 'T': /* T=<datatype> */
                p+=2; 
                for (type = 0; type < maxtype; type++)
                {
                    int cmp;
                    nchar = strlen(datatypes[type].name);
                    cmp = strncasecmp(p, datatypes[type].name, nchar);
                    if (cmp == 0)
                    {
                        priv->dtype = datatypes[type].type;
                        priv->dlen = datatypes[type].dlen;
                        p += nchar;
                        break;
                    }
                }
                if (type == maxtype)
                {
                    fprintf(stderr,
                        "regDevIoParse %s: invalid datatype '%s'\n",
                        recordName, p);
                    return S_dev_badArgument;
                }
                break;
            case 'B': /* B=<bitnumber> */
                p += 2;
                priv->bit = regDevParseExpr(&p);
                break;
            case 'I': /* I=<invert> */
                p += 2;
                priv->invert = regDevParseExpr(&p);
                break;
            case 'L': /* L=<low raw value> (converts to EGUL)*/
                p += 2;
                hwLow = regDevParseExpr(&p);
                lset = 1;
                break;
            case 'H': /* L=<high raw value> (converts to EGUF)*/
                p += 2;
                hwHigh = regDevParseExpr(&p);
                hset = 1;
                break;
            case 'P': /* P=<packing> (for fifo)*/
                p += 2;
                priv->fifopacking = regDevParseExpr(&p);
                break;
            case '\'':
                if (separator == '\'')
                {
                    p = 0;
                    break;
                }
            default:
                fprintf(stderr,
                    "regDevIoParse %s: unknown parameter '%c'\n",
                    recordName, *p);
                return S_dev_badArgument;
        }
    }
    
    /* check if bit number is in range */
    if (priv->dlen && priv->bit >= priv->dlen*8)
    {
        fprintf(stderr,
            "regDevIoParse %s: invalid bit number %d (0...%d)\n",
            recordName, priv->bit, priv->dlen*8-1);
        return S_dev_badArgument;
    }
    
    /* get default values for L and H if user did'n define them */
    switch (priv->dtype)
    {
        case epicsUInt8T:
            if (hwHigh > 0xFF || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 0xFF;
            break;
        case epicsUInt16T:
            if (hwHigh > 0xFFFF || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 0xFFFF;
            break;
        case epicsUInt32T:
        case regDev64T:
            if (!hset) hwHigh = 0xFFFFFFFF;
            break;
        case epicsInt8T:
            if (priv->hwHigh > 0x7F || priv->hwLow < -0x80)
                status = S_dev_badArgument;
            if (!lset) hwLow = -0x7F;
            if (!hset) hwHigh = 0x7F;
            break;
        case epicsInt16T:
            if (priv->hwHigh > 0x7FFF || priv->hwLow < -0x8000)
                status = S_dev_badArgument;
            if (!lset) hwLow = -0x7FFF;
            if (!hset) hwHigh = 0x7FFF;
            break;
        case epicsInt32T:
            if (!lset) hwLow = -0x7FFFFFFF;
            if (!hset) hwHigh = 0x7FFFFFFF;
            break;
        case regDevBCD8T:
            if (hwHigh > 99 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 99;
            break;
        case regDevBCD16T:
            if (hwHigh > 9999 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 9999;
            break;
        case regDevBCD32T:
            if (hwHigh > 99999999 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 99999999;
            break;
        case epicsStringT:
            /* for T=STRING L=... means length, not low */
            if (lset) {
                priv->dlen = hwLow;
                hwLow = 0;
                lset = 0;
            }
        default:
            if (lset || hset) {
                fprintf(stderr,
                    "regDevIoParse %s: %s%s%s makes"
                    " no sense with T=%s. Ignored.\n",
                    recordName,
                    lset?"L":"",
                    (lset && hset)?" and ":"",
                    hset?"H":"",
                    datatypes[type].name);
            }
            break;   
    }
    priv->hwLow = hwLow;
    priv->hwHigh = hwHigh;
    regDevDebugLog(1, "regDevIoParse %s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(1, "regDevIoParse %s: L=%#x\n",  recordName, priv->hwLow);
    regDevDebugLog(1, "regDevIoParse %s: H=%#x\n",  recordName, priv->hwHigh);
    regDevDebugLog(1, "regDevIoParse %s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(1, "regDevIoParse %s: X=%#x\n",  recordName, priv->invert);

    if (status)
    {
        fprintf(stderr,
            "regDevIoParse %s: L=%#x (%d) or H=%#x (%d) out of range for T=%s\n",
            recordName, priv->hwLow, priv->hwLow, priv->hwHigh, priv->hwHigh, datatypes[type].name);
        return status;
    }
    
    return 0;
}

int regDevAsynIoParse2(
    const char* recordName,
    char* parameterstring,
    regDevAsynPrivate* priv)
{
    char devName[255];
    regDeviceAsynNode* device;
    char* p = parameterstring;
    char separator;
    int nchar;
    int status = 0;
    
    static const int maxtype = sizeof(datatypes)/sizeof(*datatypes);
    int type = 0;
    int hset = 0;
    int lset = 0;
    long hwHigh = 0;
    long hwLow = 0;

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum((unsigned char)*p)) if (*p++ == '\0') 
    {
        fprintf(stderr,
            "regDevAsynIoParse %s: no device name in parameter string \"%s\"\n",
            recordName, parameterstring);        
        return S_dev_badArgument;
    }

    /* Get device name */
    nchar = strcspn(p, ":/ ");
    strncpy(devName, p, nchar);
    devName[nchar] = '\0';
    p += nchar;
    separator = *p++;
    regDevDebugLog(1, "regDevAsynIoParse %s: device=%s\n",
        recordName, devName);

    for (device=registeredAsynDevices; device; device=device->next)
    {
        if (strcmp(device->name, devName) == 0) break;
    }
    if (!device)
    {
        fprintf(stderr,
            "regDevAsynIoParse %s: device '%s' not found\n",
            recordName, devName);
        return S_dev_noDevice;
    }
    priv->device = device;

    /* Check device offset (for backward compatibility allow '/') */
    if (separator == ':' || separator == '/')
    {
        long offset = regDevParseExpr(&p);
        if (offset < 0)
        {
            fprintf(stderr,
                "regDevAsynIoParse %s: offset %ld<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        regDevDebugLog(1,
            "regDevAsynIoParse %s: offset=%d\n", recordName, priv->offset);
        separator = *p++;
    }
    else
    {
        priv->offset = 0;
    }

    /* Check init offset (for backward compatibility allow '!' and '/') */
    if (separator == ':' || separator == '/' || separator == '!')
    {
        char* p1 = p;
        long initoffset = regDevParseExpr(&p);
        if (p1 == p)
        {
            priv->initoffset = priv->offset;
        }
        else
        {
            if (initoffset < 0)
            {
                fprintf(stderr,
                    "regDevAsynIoParse %s: init offset %ld<0\n",
                    recordName, initoffset);
                return S_dev_badArgument;
            }
            priv->initoffset = initoffset;
        }
        regDevDebugLog(1,
            "regDevAsynIoParse %s: init offset=%d\n", recordName, priv->initoffset);
        separator = *p++;
    }
    else
    {
        priv->initoffset = DONT_INIT;
    }

    /* set default values for parameters */
    priv->bit = 0;
    priv->hwLow = 0;
    priv->hwHigh = 0;
    priv->invert = 0;
    
    /* allow whitespaces before parameter for device support */
    while ((separator == '\t') || (separator == ' '))
        separator = *p++;

    /* driver parameter for device support if present */
    nchar = 0;
    if (separator != '\'') p--; /* optional quote for compatibility */
    
    /* parse parameters */
    while (p && *p)
    {
        switch (toupper(*p))
        {
            case ' ':
            case '\t':
                p++;
                break;
            case 'T': /* T=<datatype> */
                p+=2; 
                for (type = 0; type < maxtype; type++)
                {
                    int cmp;
                    nchar = strlen(datatypes[type].name);
                    cmp = strncasecmp(p, datatypes[type].name, nchar);
                    if (cmp == 0)
                    {
                        priv->dtype = datatypes[type].type;
                        priv->dlen = datatypes[type].dlen;
                        p += nchar;
                        break;
                    }
                }
                if (type == maxtype)
                {
                    fprintf(stderr,
                        "regDevAsynIoParse %s: invalid datatype '%s'\n",
                        recordName, p);
                    return S_dev_badArgument;
                }
                break;
            case 'B': /* B=<bitnumber> */
                p += 2;
                priv->bit = regDevParseExpr(&p);
                break;
            case 'I': /* I=<invert> */
                p += 2;
                priv->invert = regDevParseExpr(&p);
                break;
            case 'L': /* L=<low raw value> (converts to EGUL)*/
                p += 2;
                hwLow = regDevParseExpr(&p);
                lset = 1;
                break;
            case 'H': /* L=<high raw value> (converts to EGUF)*/
                p += 2;
                hwHigh = regDevParseExpr(&p);
                hset = 1;
                break;
            case 'P': /* P=<packing> (for fifo)*/
                p += 2;
                priv->fifopacking = regDevParseExpr(&p);
                break;
            case '\'':
                if (separator == '\'')
                {
                    p = 0;
                    break;
                }
            default:
                fprintf(stderr,
                    "regDevAsynIoParse %s: unknown parameter '%c'\n",
                    recordName, *p);
                return S_dev_badArgument;
        }
    }
    
    /* check if bit number is in range */
    if (priv->dlen && priv->bit >= priv->dlen*8)
    {
        fprintf(stderr,
            "regDevAsynIoParse %s: invalid bit number %d (0...%d)\n",
            recordName, priv->bit, priv->dlen*8-1);
        return S_dev_badArgument;
    }
    
    /* get default values for L and H if user did'n define them */
    switch (priv->dtype)
    {
        case epicsUInt8T:
            if (hwHigh > 0xFF || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 0xFF;
            break;
        case epicsUInt16T:
            if (hwHigh > 0xFFFF || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 0xFFFF;
            break;
        case epicsUInt32T:
        case regDev64T:
            if (!hset) hwHigh = 0xFFFFFFFF;
            break;
        case epicsInt8T:
            if (priv->hwHigh > 0x7F || priv->hwLow < -0x80)
                status = S_dev_badArgument;
            if (!lset) hwLow = -0x7F;
            if (!hset) hwHigh = 0x7F;
            break;
        case epicsInt16T:
            if (priv->hwHigh > 0x7FFF || priv->hwLow < -0x8000)
                status = S_dev_badArgument;
            if (!lset) hwLow = -0x7FFF;
            if (!hset) hwHigh = 0x7FFF;
            break;
        case epicsInt32T:
            if (!lset) hwLow = -0x7FFFFFFF;
            if (!hset) hwHigh = 0x7FFFFFFF;
            break;
        case regDevBCD8T:
            if (hwHigh > 99 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 99;
            break;
        case regDevBCD16T:
            if (hwHigh > 9999 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 9999;
            break;
        case regDevBCD32T:
            if (hwHigh > 99999999 || hwLow < 0)
                status = S_dev_badArgument;
            if (!hset) hwHigh = 99999999;
            break;
        case epicsStringT:
            /* for T=STRING L=... means length, not low */
            if (lset) priv->dlen = hwLow;
            hwLow = 0;
            lset = 0;
        default:
            if (lset || hset) {
                fprintf(stderr,
                    "regDevAsynIoParse %s: %s%s%s makes"
                    " no sense with T=%s. Ignored.\n",
                    recordName,
                    lset?"L":"",
                    (lset && hset)?" and ":"",
                    hset?"H":"",
                    datatypes[type].name);
            }
            break;   
    }
    priv->hwLow = hwLow;
    priv->hwHigh = hwHigh;
    regDevDebugLog(1, "regDevAsynIoParse %s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(1, "regDevAsynIoParse %s: L=%#x\n",  recordName, priv->hwLow);
    regDevDebugLog(1, "regDevAsynIoParse %s: H=%#x\n",  recordName, priv->hwHigh);
    regDevDebugLog(1, "regDevAsynIoParse %s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(1, "regDevAsynIoParse %s: X=%#x\n",  recordName, priv->invert);

    if (status)
    {
        fprintf(stderr,
            "regDevAsynIoParse %s: L=%#x (%d) or H=%#x (%d) out of range for T=%s\n",
            recordName, priv->hwLow, priv->hwLow, priv->hwHigh, priv->hwHigh, datatypes[type].name);
        return status;
    }
    
    return 0;
}

int regDevIoParse(dbCommon* record, struct link* link)
{
    int status;

    if (link->type != INST_IO)
    {
        fprintf(stderr,
            "regDevIoParse %s: illegal link field type %s\n",
            record->name, pamaplinkType[link->type].strvalue);
        status = S_dev_badInpType;
    }
    else
    {
        status = regDevIoParse2(record->name,
            link->value.instio.string,
            (regDevPrivate*) record->dpvt);
        if (status == 0) return 0;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    return status;
}

int regDevAsynIoParse(dbCommon* record, struct link* link)
{
    int status;

    if (link->type != INST_IO)
    {
        fprintf(stderr,
            "regDevAsynIoParse %s: illegal link field type %s\n",
            record->name, pamaplinkType[link->type].strvalue);
        status = S_dev_badInpType;
    }
    else
    {
        status = regDevAsynIoParse2(record->name,
            link->value.instio.string,
            (regDevAsynPrivate*) record->dpvt);
        if (status == 0) return 0;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    return status;
}
/*********  Diver registration and access ****************************/

int regDevRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver)
{
    char* nameCpy;
    
    regDeviceNode **pdevice; 
    for (pdevice=&registeredDevices; *pdevice; pdevice=&(*pdevice)->next);
    *pdevice = (regDeviceNode*) calloc(1, sizeof(regDeviceNode));
    if (*pdevice == NULL)
    {
        fprintf(stderr,
            "regDevRegisterDevice %s: out of memory\n",
            name);
        return -1;
    }
    nameCpy = malloc(strlen(name)+1);
    strcpy(nameCpy, name);
    (*pdevice)->name = nameCpy;
    (*pdevice)->support = support;
    (*pdevice)->driver = driver;
    (*pdevice)->accesslock = epicsMutexCreate();
    return 0;
}

int regDevAsyncRegisterDevice(const char* name,
    const regDevAsyncSupport* support, regDeviceAsyn* driver)
{
    char* nameCpy;
    
    regDeviceAsynNode **pdevice; 
    for (pdevice=&registeredAsynDevices; *pdevice; pdevice=&(*pdevice)->next);
    *pdevice = (regDeviceAsynNode*) calloc(1, sizeof(regDeviceAsynNode));
    if (*pdevice == NULL)
    {
        fprintf(stderr,
            "regDevRegisterDevice %s: out of memory\n",
            name);
        return -1;
    }
    nameCpy = malloc(strlen(name)+1);
    strcpy(nameCpy, name);
    (*pdevice)->name = nameCpy;
    (*pdevice)->support = support;
    (*pdevice)->driver = driver;
    (*pdevice)->accesslock = epicsMutexCreate();
    return 0;
}

/*********  Support for "I/O Intr" for input records ******************/

long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevGetInIntInfo %s: uninitialized record\n",
            record->name);
        return -1;
    }
    device = priv->device;
    if (!device->support->getInScanPvt)
    {
        fprintf(stderr,
            "regDevGetInIntInfo %s: input I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return -1;
    }
    epicsMutexLock(device->accesslock);
    *ppvt = device->support->getInScanPvt(
        device->driver, priv->offset);
    epicsMutexUnlock(device->accesslock);
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevGetInIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return -1;
    }
    return 0;
}

long regDevAsynGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevAsynPrivate* priv = record->dpvt;
    regDeviceAsynNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAsynGetInIntInfo %s: uninitialized record\n",
            record->name);
        return -1;
    }
    device = priv->device;
    if (!device->support->getInScanPvt)
    {
        fprintf(stderr,
            "regDevAsynGetInIntInfo %s: input I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return -1;
    }
    epicsMutexLock(device->accesslock);
    *ppvt = device->support->getInScanPvt(
        device->driver, priv->offset);
    epicsMutexUnlock(device->accesslock);
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevAsynGetInIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return -1;
    }
    return 0;
}

/*********  Support for "I/O Intr" for output records ****************/

long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevGetOutIntInfo %s: uninitialized record\n",
            record->name);
        return -1;
    }
    device = priv->device;
    if (!device->support->getOutScanPvt)
    {
        fprintf(stderr,
            "regDevGetOutIntInfo %s: output I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return -1;
    }
    epicsMutexLock(device->accesslock);
    *ppvt = device->support->getOutScanPvt(
        device->driver, priv->offset);
    epicsMutexUnlock(device->accesslock);
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevGetOutIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return -1;
    }
    return 0;
}

long regDevAsynGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevAsynPrivate* priv = record->dpvt;
    regDeviceAsynNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAsynGetOutIntInfo %s: uninitialized record\n",
            record->name);
        return -1;
    }
    device = priv->device;
    if (!device->support->getOutScanPvt)
    {
        fprintf(stderr,
            "regDevAsynGetOutIntInfo %s: output I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return -1;
    }
    epicsMutexLock(device->accesslock);
    *ppvt = device->support->getOutScanPvt(
        device->driver, priv->offset);
    epicsMutexUnlock(device->accesslock);
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevAsynGetOutIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return -1;
    }
    return 0;
}

/*********  Report routine ********************************************/

long regDevAsynReport(int level)
{
    regDeviceAsynNode* device;
    
    printf("  regDev version: %s\n", cvsid_regDev);
    if (level < 1) return 0;
    printf("  registered Asynchronous devices:\n");
    for (device=registeredAsynDevices; device; device=device->next)
    {
        printf ("    \"%s\" ", device->name);
        if (device->support->report)
        {
            epicsMutexLock(device->accesslock);
            device->support->report(device->driver, level-1);
            epicsMutexUnlock(device->accesslock);
        }
        else printf ("\n");
    }
    return 0;
}

long regDevReport(int level)
{
    regDeviceNode* device;
    
    printf("  regDev version: %s\n", cvsid_regDev);
    if (level < 1) return 0;
    printf("  registered Synchronous devices:\n");
    for (device=registeredDevices; device; device=device->next)
    {
        printf ("    \"%s\" ", device->name);
        if (device->support->report)
        {
            epicsMutexLock(device->accesslock);
            device->support->report(device->driver, level-1);
            epicsMutexUnlock(device->accesslock);
        }
        else printf ("\n");
    }
    return 0;
}

regDevice* regDevFind(const char* name)
{
    regDeviceNode* device;

    for (device=registeredDevices; device; device=device->next)
    {
        if (strcmp(name, device->name) == 0)
            return device->driver;
    }
    return NULL;
}

regDeviceAsyn* regDevAsynFind(const char* name)
{
    regDeviceAsynNode* device;

    for (device=registeredAsynDevices; device; device=device->next)
    {
        if (strcmp(name, device->name) == 0)
            return device->driver;
    }
    return NULL;
}


struct drvsup {
    long number;
    long (*report)();
    long (*init)();
} regDev = {
    2,
    regDevReport,
    NULL
};

epicsExportAddress(drvet, regDev);

struct drvsupAsyn {
    long number;
    long (*report)();
    long (*init)();
} regDevAsyn = {
    2,
    regDevAsynReport,
    NULL
};

epicsExportAddress(drvet, regDevAsyn);

/* routine to convert bytes from BCD to integer format. */
 
static unsigned long bcd2i(unsigned long bcd)
{
    unsigned long i = 0;
    unsigned long m = 1;
    
    while (bcd)
    {
        i += (bcd & 0xF) * m;
        m *= 10;
        bcd >>= 4;
    }
    return i;
}

/* routine to convert bytes from integer to BCD format. */
 
static unsigned long i2bcd(unsigned long i)
{
    int bcd = 0;
    int s = 0;
    
    while (i)
    {
        bcd += (i % 10) << s;
        s += 4;
        i /= 10;
    }
    return bcd;
}

/* generic device support init functions ****************************/
/****

UNTILL HERE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
****/

regDevPrivate* regDevAllocPriv(dbCommon *record)
{
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevAllocPriv(%s)\n", record->name);
    priv = calloc(1, sizeof(regDevPrivate));
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAllocPriv %s: try to allocate %d bytes. %s\n",
            record->name, sizeof(regDevPrivate), strerror(errno));
#ifdef vxWorks
        {
            MEM_PART_STATS meminfo;
            memPartInfoGet(memSysPartId, &meminfo);
            printf ("Max free block: %ld bytes\n", meminfo.maxBlockSizeFree);
        }
#endif
        return NULL;
    }
    priv->dtype = epicsInt16T;
    priv->dlen = 2;
    priv->arraypacking = 1;
    record->dpvt = priv;
    return priv;
}

regDevAsynPrivate* regDevAsynAllocPriv(dbCommon *record)
{
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevAllocPriv(%s)\n", record->name);
    priv = calloc(1, sizeof(regDevAsynPrivate));
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAllocPriv %s: try to allocate %d bytes. %s\n",
            record->name, sizeof(regDevAsynPrivate), strerror(errno));
#ifdef vxWorks
        {
            MEM_PART_STATS meminfo;
            memPartInfoGet(memSysPartId, &meminfo);
            printf ("Max free block: %ld bytes\n", meminfo.maxBlockSizeFree);
        }
#endif
        return NULL;
    }
    priv->dtype = epicsInt16T;
    priv->dlen = 2;
    priv->arraypacking = 1;
    record->dpvt = priv;
    return priv;
}

const char* regDevTypeName(int dtype)
{
    const char* regDevTypeNames [] = { 
        "regDevBCD8T",
        "regDevBCD16T",
        "regDevBCD32T",
        "regDev64T"
    };

    if (dtype > regDevLastType) return "invalid";
    return dtype < regDevFirstType ?
        epicsTypeNames[dtype] :
        regDevTypeNames[dtype-regDevFirstType];
}

long regDevAssertType(dbCommon *record, int allowedTypes)
{
    unsigned short dtype;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;
    assert(priv);
    dtype = priv->dtype;
    
    regDevDebugLog(1, "regDevAssertType(%s,%s%s%s%s) %s\n",
        record->name,
        allowedTypes && TYPE_INT ? " INT" : "",
        allowedTypes && TYPE_FLOAT ? " FLOAT" : "",
        allowedTypes && TYPE_STRING ? " STRING" : "",
        allowedTypes && TYPE_BCD ? " BCD" : "",
        regDevTypeName(dtype));
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case epicsInt16T:
        case epicsUInt16T:
        case epicsInt32T:
        case epicsUInt32T:
            if (allowedTypes & TYPE_INT) return S_dev_success;
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            if (allowedTypes & TYPE_FLOAT) return S_dev_success;
            break;
        case epicsStringT:
            if (allowedTypes & TYPE_STRING) return S_dev_success;
            break;
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if (allowedTypes & TYPE_BCD) return S_dev_success;
            break;
        case regDev64T:
            if (allowedTypes & (TYPE_INT|TYPE_FLOAT)) return S_dev_success;
            break;
    }
    fprintf(stderr,
        "regDevAssertType %s: illegal data type %s for this record type\n",
        record->name, regDevTypeName(dtype));
    free(record->dpvt);
    record->dpvt = NULL;
    return S_db_badField;
}

long regDevAsynAssertType(dbCommon *record, int allowedTypes)
{
    unsigned short dtype;
    regDevAsynPrivate* priv = (regDevAsynPrivate*) record->dpvt;
    assert(priv);
    dtype = priv->dtype;
    
    regDevDebugLog(1, "regDevAsynAssertType(%s,%s%s%s%s) %s\n",
        record->name,
        allowedTypes && TYPE_INT ? " INT" : "",
        allowedTypes && TYPE_FLOAT ? " FLOAT" : "",
        allowedTypes && TYPE_STRING ? " STRING" : "",
        allowedTypes && TYPE_BCD ? " BCD" : "",
        regDevTypeName(dtype));
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case epicsInt16T:
        case epicsUInt16T:
        case epicsInt32T:
        case epicsUInt32T:
            if (allowedTypes & TYPE_INT) return S_dev_success;
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            if (allowedTypes & TYPE_FLOAT) return S_dev_success;
            break;
        case epicsStringT:
            if (allowedTypes & TYPE_STRING) return S_dev_success;
            break;
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if (allowedTypes & TYPE_BCD) return S_dev_success;
            break;
        case regDev64T:
            if (allowedTypes & (TYPE_INT|TYPE_FLOAT)) return S_dev_success;
            break;
    }
    fprintf(stderr,
        "regDevAsynAssertType %s: illegal data type %s for this record type\n",
        record->name, regDevTypeName(dtype));
    free(record->dpvt);
    record->dpvt = NULL;
    return S_db_badField;
}

int regDevCheckFTVL(dbCommon* record, int ftvl)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    regDevDebugLog(1, "regDevCheckFTVL(%s, %s)\n",
        record->name,
        pamapdbfType[ftvl].strvalue);
    assert(priv);
    switch (ftvl)
    {
        case DBF_CHAR:
            priv->dtype = epicsInt8T;
            priv->dlen = 1;
            return S_dev_success;
        case DBF_UCHAR:
            priv->dtype = epicsUInt8T;
            priv->dlen = 1;
            return S_dev_success;
        case DBF_SHORT:
            priv->dtype = epicsInt16T;
            priv->dlen = 2;
            return S_dev_success;
        case DBF_USHORT:
            priv->dtype = epicsUInt16T;
            priv->dlen = 2;
            return S_dev_success;
        case DBF_LONG:
            priv->dtype = epicsInt32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_ULONG:
            priv->dtype = epicsUInt32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_FLOAT:
            priv->dtype = epicsFloat32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_DOUBLE:
            priv->dtype = epicsFloat64T;
            priv->dlen = 8;
            return S_dev_success;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    fprintf(stderr,
        "regDevCheckFTVL %s: illegal FTVL value %s\n",
        record->name, pamapdbfType[ftvl].strvalue);
    return S_db_badField;
}

int regDevAsynCheckFTVL(dbCommon* record, int ftvl)
{
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;

    regDevDebugLog(1, "regDevAsynCheckFTVL(%s, %s)\n",
        record->name,
        pamapdbfType[ftvl].strvalue);
    assert(priv);
    switch (ftvl)
    {
        case DBF_CHAR:
            priv->dtype = epicsInt8T;
            priv->dlen = 1;
            return S_dev_success;
        case DBF_UCHAR:
            priv->dtype = epicsUInt8T;
            priv->dlen = 1;
            return S_dev_success;
        case DBF_SHORT:
            priv->dtype = epicsInt16T;
            priv->dlen = 2;
            return S_dev_success;
        case DBF_USHORT:
            priv->dtype = epicsUInt16T;
            priv->dlen = 2;
            return S_dev_success;
        case DBF_LONG:
            priv->dtype = epicsInt32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_ULONG:
            priv->dtype = epicsUInt32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_FLOAT:
            priv->dtype = epicsFloat32T;
            priv->dlen = 4;
            return S_dev_success;
        case DBF_DOUBLE:
            priv->dtype = epicsFloat64T;
            priv->dlen = 8;
            return S_dev_success;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    fprintf(stderr,
        "regDevAsynCheckFTVL %s: illegal FTVL value %s\n",
        record->name, pamapdbfType[ftvl].strvalue);
    return S_db_badField;
}

int regDevCheckType(dbCommon* record, int ftvl, int nelm)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    
    regDevDebugLog(1, "regDevCheckType(%s, %s, %i)\n",
        record->name,
        pamapdbfType[ftvl].strvalue+4,
        nelm);
    assert(priv);
    switch (priv->dtype)
    {
        case epicsFloat64T:
            if (ftvl == DBF_DOUBLE)
                return S_dev_success;
            break;
        case epicsFloat32T:
            if (ftvl == DBF_FLOAT)
                return S_dev_success;
            break;
        case epicsStringT:    
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                if (!priv->dlen) priv->dlen = nelm;
                if (priv->dlen > nelm) priv->dlen = nelm;
                return S_dev_success;
            }
            break;
        case regDevBCD8T:
        case epicsInt8T:
        case epicsUInt8T:
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
                return S_dev_success;
            break;
        case regDevBCD16T:
        case epicsInt16T:
        case epicsUInt16T:
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
                return S_dev_success;
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            break;
        case regDevBCD32T:
        case epicsInt32T:
        case epicsUInt32T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
                return S_dev_success;
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 4;
                return S_dev_success;
            }
            break;
        case regDev64T:
            if (ftvl == DBF_DOUBLE)
                return S_dev_success;
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG) || (ftvl == DBF_FLOAT))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 4;
                return S_dev_success;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 8;
                return S_dev_success;
            }
    }
    free(record->dpvt);
    record->dpvt = NULL;
    fprintf(stderr,
        "regDevCheckType %s: data type %s does not match FTVL %s\n",
        record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return S_db_badField;
}

int regDevAsynCheckType(dbCommon* record, int ftvl, int nelm)
{
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;
    
    regDevDebugLog(1, "regDevAsynCheckType(%s, %s, %i)\n",
        record->name,
        pamapdbfType[ftvl].strvalue+4,
        nelm);
    assert(priv);
    switch (priv->dtype)
    {
        case epicsFloat64T:
            if (ftvl == DBF_DOUBLE)
                return S_dev_success;
            break;
        case epicsFloat32T:
            if (ftvl == DBF_FLOAT)
                return S_dev_success;
            break;
        case epicsStringT:    
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                if (!priv->dlen) priv->dlen = nelm;
                if (priv->dlen > nelm) priv->dlen = nelm;
                return S_dev_success;
            }
            break;
        case regDevBCD8T:
        case epicsInt8T:
        case epicsUInt8T:
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
                return S_dev_success;
            break;
        case regDevBCD16T:
        case epicsInt16T:
        case epicsUInt16T:
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
                return S_dev_success;
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            break;
        case regDevBCD32T:
        case epicsInt32T:
        case epicsUInt32T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
                return S_dev_success;
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 4;
                return S_dev_success;
            }
            break;
        case regDev64T:
            if (ftvl == DBF_DOUBLE)
                return S_dev_success;
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG) || (ftvl == DBF_FLOAT))
            {
                priv->arraypacking = 2;
                return S_dev_success;
            }
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 4;
                return S_dev_success;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 8;
                return S_dev_success;
            }
    }
    free(record->dpvt);
    record->dpvt = NULL;
    fprintf(stderr,
        "regDevAsynCheckType %s: data type %s does not match FTVL %s\n",
        record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return S_db_badField;
}

int regDevRead(regDeviceNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, int prio)
{
    int status;
    
    if (device->support->read == NULL) return -1;
    epicsMutexLock(device->accesslock);
    status = device->support->read(device->driver,
        offset, dlen, nelem, pdata, prio);
    epicsMutexUnlock(device->accesslock);
    return status;
}

int regDevAsynBuffAlloc(regDeviceAsynNode* device, void** pBuff, void** pBusBuff, unsigned int size)
{
    int status;
    
    if (device->support->buff_alloc == NULL) return -1;
    epicsMutexLock(device->accesslock);
    status = device->support->buff_alloc(pBuff, pBusBuff, size);
    epicsMutexUnlock(device->accesslock);
    return status;
}

int regDevAsynRead(regDeviceAsynNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, CALLBACK* cbStruct, int prio, int* rdStat)
{
    int status;
    
    if (device->support->read == NULL) return -1;
    epicsMutexLock(device->accesslock);
    status = device->support->read(device->driver,
        offset, dlen, nelem, pdata, cbStruct, prio, rdStat);
    epicsMutexUnlock(device->accesslock);
    return status;
}

int regDevWrite(regDeviceNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, void* mask, int prio)
{
    int status;
    
    if (device->support->write == NULL) return -1;
    epicsMutexLock(device->accesslock);
    status = device->support->write(device->driver,
        offset, dlen, nelem, pdata, mask, prio);
    epicsMutexUnlock(device->accesslock);
    return status;
}

int regDevAsynWrite(regDeviceAsynNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, CALLBACK* cbStruct, void* mask, int prio)
{
    int status;
    
    if (device->support->write == NULL) return -1;
    epicsMutexLock(device->accesslock);
    status = device->support->write(device->driver,
        offset, dlen, nelem, pdata, cbStruct, mask, prio);
    epicsMutexUnlock(device->accesslock);
    return status;
}


int regDevReadBits(dbCommon* record, epicsInt32* val, epicsUInt32 mask)
{
    int status;
    epicsUInt8 val8;
    epicsUInt16 val16;
    epicsInt32 val32;
    regDev64 val64;
    unsigned short dtype;
    unsigned int offset;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    
    regDevDebugLog(2, "regDevReadBits(%s) start\n", record->name);
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    regDevDebugLog(2, "regDevReadBits(%s) read from %s/0x%x\n",
        record->name, priv->device->name, offset);
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case regDevBCD8T:
            status = regDevRead(priv->device, offset,
                1, 1, &val8, record->prio);
            regDevDebugLog(3, "%s: read 8bit %02x from %s/0x%x (status=%x)\n",
                record->name, val8, priv->device->name, offset, status);
            val32 = val8;
            break;
        case epicsInt16T:
        case epicsUInt16T:
        case regDevBCD16T:
            status = regDevRead(priv->device, offset,
                2, 1, &val16, record->prio);
            regDevDebugLog(3, "%s: read 16bit %04x from %s/0x%x (status=%x)\n",
                record->name, val16, priv->device->name, offset, status);
            val32 = val16;
            break;
        case epicsInt32T:
        case epicsUInt32T:
        case regDevBCD32T:
            status = regDevRead(priv->device, offset,
                4, 1, &val32, record->prio);
            regDevDebugLog(3, "%s: read 32bit %08x from %s/0x%x (status=%x)\n",
                record->name, val32, priv->device->name, offset, status);
            break;
        case regDev64T:
            status = regDevRead(priv->device, offset,
                8, 1, &val64, record->prio);
            regDevDebugLog(3, "%s: read 64bit %016llx from %s/0x%x (status=%x)\n",
                record->name, val64, priv->device->name, offset, status);
            val32 = val64; /* cut off high bits */
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
    }
    switch (dtype)
    {
        case epicsUInt8T:
        case regDevBCD8T:
            val32 &= 0xFF;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            val32 &= 0xFFFF;
            break;
    }
    switch (dtype)
    {
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            val32 = bcd2i(val32);
    }
    *val = (val32 ^ priv->invert) & mask;
    regDevDebugLog(2, "regDevReadBits(%s) done\n", record->name);
    record->udf = FALSE;
    if (!interruptAccept)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }
    return status;
}

int regDevAsynReadBits(dbCommon* record, epicsInt32* val, epicsUInt32 mask)
{
    int status;
    epicsUInt8 val8;
    epicsUInt16 val16;
    epicsInt32 val32;
    regDev64 val64;
    unsigned short dtype;
    unsigned int offset;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;
    
    regDevDebugLog(2, "regDevReadBits(%s) start\n", record->name);
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    regDevDebugLog(2, "regDevReadBits(%s) read from %s/0x%x\n",
        record->name, priv->device->name, offset);
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case regDevBCD8T:
            status = regDevAsynRead(priv->device, offset,
                1, 1, &val8, NULL, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 8bit %02x from %s/0x%x (status=%x)\n",
                record->name, val8, priv->device->name, offset, status);
            val32 = val8;
            break;
        case epicsInt16T:
        case epicsUInt16T:
        case regDevBCD16T:
            status = regDevAsynRead(priv->device, offset,
                2, 1, &val16, NULL, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 16bit %04x from %s/0x%x (status=%x)\n",
                record->name, val16, priv->device->name, offset, status);
            val32 = val16;
            break;
        case epicsInt32T:
        case epicsUInt32T:
        case regDevBCD32T:
            status = regDevAsynRead(priv->device, offset,
                4, 1, &val32, NULL, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 32bit %08x from %s/0x%x (status=%x)\n",
                record->name, val32, priv->device->name, offset, status);
            break;
        case regDev64T:
            status = regDevAsynRead(priv->device, offset,
                8, 1, &val64, NULL, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 64bit %016llx from %s/0x%x (status=%x)\n",
                record->name, val64, priv->device->name, offset, status);
            val32 = val64; /* cut off high bits */
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
    }
    switch (dtype)
    {
        case epicsUInt8T:
        case regDevBCD8T:
            val32 &= 0xFF;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            val32 &= 0xFFFF;
            break;
    }
    switch (dtype)
    {
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            val32 = bcd2i(val32);
    }
    *val = (val32 ^ priv->invert) & mask;
    regDevDebugLog(2, "regDevReadBits(%s) done\n", record->name);
    record->udf = FALSE;
    if (!interruptAccept)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }
    return status;
}


int regDevWriteBits(dbCommon* record, epicsUInt32 val, epicsUInt32 mask)
{
    int status;
    epicsUInt8 rval8, mask8;
    epicsUInt16 rval16, mask16;
    epicsUInt32 rval32, mask32;
    regDev64 rval64, mask64;
    unsigned short dtype;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    val ^= priv->invert;
    switch (dtype)
    {
        case regDevBCD8T:
            val = i2bcd(val);
        case epicsInt8T:
        case epicsUInt8T:
            rval8 = val;
            mask8 = mask;
            regDevDebugLog(2, "%s: write 8bit %02x mask %02x to %s/0x%x\n",
                record->name, rval8, mask8, priv->device->name, priv->offset);
            status = regDevWrite(priv->device, priv->offset, 1, 1, &rval8,
                mask8 == 0xFF ? NULL : &mask8, record->prio);
            break;
        case regDevBCD16T:
            val = i2bcd(val);
        case epicsInt16T:
        case epicsUInt16T:
            rval16 = val;
            mask16 = mask;
            regDevDebugLog(2, "%s: write 16bit %04x mask %04x to %s/0x%x\n",
                record->name, rval16, mask16,priv->device->name,  priv->offset);
            status = regDevWrite(priv->device, priv->offset, 2, 1, &rval16,
                mask16 == 0xFFFF ? NULL : &mask16, record->prio);
            break;
        case regDevBCD32T:
            val = i2bcd(val);
        case epicsInt32T:
        case epicsUInt32T:
            rval32 = val;
            mask32 = mask;
            regDevDebugLog(2, "%s: write 32bit %08x mask %08x to %s/0x%x\n",
                record->name, rval32, mask32, priv->device->name, priv->offset);
            status = regDevWrite(priv->device, priv->offset, 4, 1, &rval32,
                mask32 == 0xFFFFFFFFUL ? NULL : &mask32, record->prio);
            break;
        case regDev64T:
            rval64 = val;
            mask64 = mask;
            regDevDebugLog(2, "%s: write 64bit %016llx mask %016llx to %s/0x%x\n",
                record->name, rval64, mask64, priv->device->name, priv->offset);
            status = regDevWrite(priv->device, priv->offset, 8, 1, &rval64,
                mask == 0xFFFFFFFFUL ? NULL : &mask64, record->prio);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: write error\n", record->name);
    }
    return status;
}

int regDevAsynWriteBits(dbCommon* record, epicsUInt32 val, epicsUInt32 mask)
{
    int status;
    epicsUInt8 rval8, mask8;
    epicsUInt16 rval16, mask16;
    epicsUInt32 rval32, mask32;
    regDev64 rval64, mask64;
    unsigned short dtype;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    val ^= priv->invert;
    switch (dtype)
    {
        case regDevBCD8T:
            val = i2bcd(val);
        case epicsInt8T:
        case epicsUInt8T:
            rval8 = val;
            mask8 = mask;
            regDevDebugLog(2, "%s: write 8bit %02x mask %02x to %s/0x%x\n",
                record->name, rval8, mask8, priv->device->name, priv->offset);
            status = regDevAsynWrite(priv->device, priv->offset, 1, 1, &rval8, priv->callback,
                mask8 == 0xFF ? NULL : &mask8, record->prio);
            break;
        case regDevBCD16T:
            val = i2bcd(val);
        case epicsInt16T:
        case epicsUInt16T:
            rval16 = val;
            mask16 = mask;
            regDevDebugLog(2, "%s: write 16bit %04x mask %04x to %s/0x%x\n",
                record->name, rval16, mask16,priv->device->name,  priv->offset);
            status = regDevAsynWrite(priv->device, priv->offset, 2, 1, &rval16, priv->callback,
                mask16 == 0xFFFF ? NULL : &mask16, record->prio);
            break;
        case regDevBCD32T:
            val = i2bcd(val);
        case epicsInt32T:
        case epicsUInt32T:
            rval32 = val;
            mask32 = mask;
            regDevDebugLog(2, "%s: write 32bit %08x mask %08x to %s/0x%x\n",
                record->name, rval32, mask32, priv->device->name, priv->offset);
            status = regDevAsynWrite(priv->device, priv->offset, 4, 1, &rval32, priv->callback,
                mask32 == 0xFFFFFFFFUL ? NULL : &mask32, record->prio);
            break;
        case regDev64T:
            rval64 = val;
            mask64 = mask;
            regDevDebugLog(2, "%s: write 64bit %016llx mask %016llx to %s/0x%x\n",
                record->name, rval64, mask64, priv->device->name, priv->offset);
            status = regDevAsynWrite(priv->device, priv->offset, 8, 1, &rval64, priv->callback,
                mask == 0xFFFFFFFFUL ? NULL : &mask64, record->prio);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: write error\n", record->name);
    }
    return status;
}

void regDevCallback(void* user, int status)
{
    dbCommon* record = user;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;
    priv->status = status;
    callbackRequestProcessCallback(&priv->callback, record->prio, record);
}

long regDevReadArr(dbCommon* record, void* bptr, unsigned int nelm)
{
    int status = 0;
    int i;
    unsigned int offset;
    unsigned int dlen;
    int packing;
    regDevPrivate* priv;
    regDeviceNode* device;
    
    priv = (regDevPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
    
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    
    if (priv->dtype == epicsStringT)
    {
        nelm =  priv->dlen;
        dlen = 1;
    }
    else
    {
        dlen = priv->dlen;
    }
    
    if (record->pact)
    {
        status = priv->status;
    }
    else
    {
        packing = priv->fifopacking;
        if (packing)
        {
            dlen *= packing;
            for (i = 0; i < nelm/packing; i++)
            {
                status = regDevRead(priv->device, offset,
                   dlen, 1, (char*)bptr+i*dlen, record->prio);
                if (status) break;
            }
        }
        else
        {
            packing = priv->arraypacking;
            status = regDevRead(priv->device, offset,
                dlen*packing, nelm/packing, bptr, record->prio);
            if (status == 1)
            {
                record->pact = 1;
                return 0;
            }
        }
    }
    regDevDebugLog(3,
        "%s: read %d values of %d bytes to %p status=%d\n",
        record->name, nelm, priv->dlen, bptr, status);

    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
    switch (priv->dtype)
    {
        case regDevBCD8T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt8*)bptr)[i] = bcd2i(((epicsUInt8*)bptr)[i]);
            break;
        case regDevBCD16T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt16*)bptr)[i] = bcd2i(((epicsUInt16*)bptr)[i]);
            break;
        case regDevBCD32T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt32*)bptr)[i] = bcd2i(((epicsUInt32*)bptr)[i]);
            break;
    }
    return status;
}

long regDevAsynMemAlloc(dbCommon* record, void** bptr, unsigned int size)
{
    int status = 0;
    regDevAsynPrivate* priv;
    regDeviceAsynNode* device;
    
    priv = (regDevAsynPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
       
    status = regDevAsynBuffAlloc(priv->device, bptr, (void*)&priv->busBufPtr, size);
    return status;    
}

long regDevAsynReadArr(dbCommon* record, void* bptr, unsigned int nelm)
{
    int status = 0;
    int i;
    unsigned int offset;
    unsigned int dlen;
    int packing;
    regDevAsynPrivate* priv;
    regDeviceAsynNode* device;
    
    priv = (regDevAsynPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
    
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    
    if (priv->dtype == epicsStringT)
    {
        nelm =  priv->dlen;
        dlen = 1;
    }
    else
    {
        dlen = priv->dlen;
    }
    
    if (record->pact)
    {
        status = priv->status;
    }
    else
    {
        packing = priv->fifopacking;
        if (packing)
        {
            dlen *= packing;
            for (i = 0; i < nelm/packing; i++)
            {
                status = regDevAsynRead(priv->device, offset,
                   dlen, 1, (char*)bptr+i*dlen, priv->callback, record->prio, &priv->status);
                if (status) break;
            }
        }
        else
        {
            packing = priv->arraypacking;
            status = regDevAsynRead(priv->device, offset,
                dlen*packing, nelm/packing, bptr, priv->callback, record->prio, &priv->status);
            if (status == 1)
            {
                record->pact = 1;
                return 0;
            }
        }
    }
    regDevDebugLog(3,
        "%s: read %d values of %d bytes to %p status=%d\n",
        record->name, nelm, priv->dlen, bptr, status);

    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
    switch (priv->dtype)
    {
        case regDevBCD8T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt8*)bptr)[i] = bcd2i(((epicsUInt8*)bptr)[i]);
            break;
        case regDevBCD16T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt16*)bptr)[i] = bcd2i(((epicsUInt16*)bptr)[i]);
            break;
        case regDevBCD32T:
            for (i = 0; i < nelm; i++)
                ((epicsUInt32*)bptr)[i] = bcd2i(((epicsUInt32*)bptr)[i]);
            break;
    }
    return status;
}

long regDevWriteArr(dbCommon* record, void* bptr, unsigned int nelm)
{
    int status = 0;
    int i;
    unsigned int dlen;
    int packing;
    regDevPrivate* priv;
    regDeviceNode* device;

    priv = (regDevPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
    
    if (priv->dtype == epicsStringT)
    {
        nelm =  priv->dlen;
        dlen = 1;
    }
    else
    {
        dlen = priv->dlen;
    }
    
    if (record->pact)
    {
        status = priv->status;
    }
    else
    {
        switch (priv->dtype)
        {
            case regDevBCD8T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt8*)bptr)[i] = i2bcd(((epicsUInt8*)bptr)[i]);
                break;
            case regDevBCD16T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt16*)bptr)[i] = i2bcd(((epicsUInt16*)bptr)[i]);
                break;
            case regDevBCD32T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt32*)bptr)[i] = i2bcd(((epicsUInt32*)bptr)[i]);
                break;
        }
        packing = priv->fifopacking;
        if (packing)
        {
            dlen *= packing;
            for (i = 0; i < nelm/packing; i++)
            {
                status = regDevWrite(priv->device, priv->offset, dlen, 1, (char*)bptr+i*dlen, NULL, record->prio);
                if (status) break;
            }
        }
        else
        {
            packing = priv->arraypacking;
            status = regDevWrite(priv->device, priv->offset, dlen*packing, nelm/packing, bptr, NULL, record->prio);
            if (status == 1)
            {
                record->pact = 1;
                return 0;
            }
        }
    }
    regDevDebugLog(3,
        "%s: write %d values of %d bytes from %p status=%d\n",
        record->name, nelm, priv->dlen, bptr, status);
    if (status)
    {
        fprintf(stderr,
            "%s: write error\n", record->name);
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

long regDevAsynReadNumber(dbCommon* record, epicsInt32* rval, double* fval)
{
    int status;
    epicsInt32 sval32=0;
    unsigned short dtype;
    unsigned long offset;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr, "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    
    if(record->pact)
    {
      status = priv->status;
    }
    else
    {
      switch (dtype)
      {
        case epicsInt8T:
            status = regDevAsynRead(priv->device, offset,
                1, 1, &priv->alldtype.sval8, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, priv->alldtype.sval8);
            break;
        case epicsUInt8T:
        case regDevBCD8T:
            status = regDevAsynRead(priv->device, offset,
                1, 1, &priv->alldtype.uval8, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, priv->alldtype.uval8);
            break;
        case epicsInt16T:
            status = regDevAsynRead(priv->device, offset,
                2, 1, &priv->alldtype.sval16, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, priv->alldtype.sval16);
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            status = regDevAsynRead(priv->device, offset,
                2, 1, &priv->alldtype.uval16, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, priv->alldtype.uval16);
            break;
        case epicsInt32T:
            status = regDevAsynRead(priv->device, offset,
                4, 1, &priv->alldtype.sval32, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 32bit %04x\n",
                record->name, priv->alldtype.sval32);
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            status = regDevAsynRead(priv->device, offset,
                4, 1, &priv->alldtype.uval32, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 32bit %04x\n",
                record->name, priv->alldtype.uval32);
            break;
        case epicsFloat32T:
            status = regDevAsynRead(priv->device, offset,
                4, 1, &priv->alldtype.val32, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 32bit %04lx = %#g\n",
                record->name, priv->alldtype.val32.u, priv->alldtype.val32.f);
            break;
        case epicsFloat64T:
            status = regDevAsynRead(priv->device, offset,
                8, 1, &priv->alldtype.val64, priv->callback, record->prio, &priv->status);
            regDevDebugLog(3, "%s: read 64bit %08Lx = %#g\n",
                record->name, priv->alldtype.val64.u, priv->alldtype.val64.f);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
      }
      if (status == 1)
      {
          record->pact = 1;
          return 0;
      }
    }
    if (status != 0)
    {
        recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
        return 0;
    }
    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
      switch (dtype)
      {
        case epicsInt8T:
            sval32 = priv->alldtype.sval8;
            break;
        case epicsUInt8T:
        case regDevBCD8T:
            sval32 = priv->alldtype.uval8;
            break;
        case epicsInt16T:
            sval32 = priv->alldtype.sval16;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            sval32 = priv->alldtype.uval16;
            break;
        case epicsInt32T:
           sval32 = priv->alldtype.sval32;
            break;
        case epicsUInt32T:
        case regDevBCD32T:
           sval32 = priv->alldtype.uval32;
            break;
        case epicsFloat32T:
            *fval = priv->alldtype.val32.f;
            break;
        case epicsFloat64T:
            *fval = priv->alldtype.val64.f;
            break;
      }
    switch (dtype)
    {
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            sval32 = bcd2i(sval32);
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            record->udf = FALSE;
            return 2;
    }
    *rval = sval32;
    return 0;
}


long regDevReadNumber(dbCommon* record, epicsInt32* rval, double* fval)
{
    int status;
    signed char sval8;
    epicsUInt8 uval8;
    epicsInt16 sval16;
    epicsUInt16 uval16;
    epicsInt32 sval32;
    epicsUInt32 uval32;
    union {epicsFloat32 f; long u;} val32;
    union {epicsFloat64 f; long long u;} val64;
    unsigned short dtype;
    unsigned long offset;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr, "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    switch (dtype)
    {
        case epicsInt8T:
            status = regDevRead(priv->device, offset,
                1, 1, &sval8, record->prio);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, sval8);
            sval32 = sval8;
            break;
        case epicsUInt8T:
        case regDevBCD8T:
            status = regDevRead(priv->device, offset,
                1, 1, &uval8, record->prio);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, uval8);
            sval32 = uval8;
            break;
        case epicsInt16T:
            status = regDevRead(priv->device, offset,
                2, 1, &sval16, record->prio);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, sval16);
            sval32 = sval16;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            status = regDevRead(priv->device, offset,
                2, 1, &uval16, record->prio);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, uval16);
            sval32 = uval16;
            break;
        case epicsInt32T:
            status = regDevRead(priv->device, offset,
                4, 1, &sval32, record->prio);
            regDevDebugLog(3, "%s: read 32bit %04x\n",
                record->name, sval32);
            sval32 = sval32;
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            status = regDevRead(priv->device, offset,
                4, 1, &uval32, record->prio);
            regDevDebugLog(3, "%s: read 32bit %04x\n",
                record->name, uval32);
            sval32 = uval32;
            break;
        case epicsFloat32T:
            status = regDevRead(priv->device, offset,
                4, 1, &val32, record->prio);
            regDevDebugLog(3, "%s: read 32bit %04lx = %#g\n",
                record->name, val32.u, val32.f);
            *fval = val32.f;
            break;
        case epicsFloat64T:
            status = regDevRead(priv->device, offset,
                8, 1, &val64, record->prio);
            regDevDebugLog(3, "%s: read 64bit %08Lx = %#g\n",
                record->name, val64.u, val64.f);
            *fval = val64.f;
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status != 0)
    {
        recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
        return 0;
    }
    if (status)
    {
        fprintf(stderr,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
    switch (dtype)
    {
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            sval32 = bcd2i(sval32);
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            record->udf = FALSE;
            return 2;
    }
    *rval = sval32;
    return 0;
}

/*
Not yet async - TO BE DONE
*/

long regDevAsynWriteArr(dbCommon* record, void* bptr, unsigned int nelm)
{
    int status = 0;
    int i;
    unsigned int dlen;
    int packing;
    regDevAsynPrivate* priv;
    regDeviceAsynNode* device;

    priv = (regDevAsynPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
    
    if (priv->dtype == epicsStringT)
    {
        nelm =  priv->dlen;
        dlen = 1;
    }
    else
    {
        dlen = priv->dlen;
    }
    
    if (record->pact)
    {
        status = priv->status;
    }
    else
    {
        switch (priv->dtype)
        {
            case regDevBCD8T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt8*)bptr)[i] = i2bcd(((epicsUInt8*)bptr)[i]);
                break;
            case regDevBCD16T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt16*)bptr)[i] = i2bcd(((epicsUInt16*)bptr)[i]);
                break;
            case regDevBCD32T:
                for (i = 0; i < nelm; i++)
                    ((epicsUInt32*)bptr)[i] = i2bcd(((epicsUInt32*)bptr)[i]);
                break;
        }
        packing = priv->fifopacking;
        if (packing)
        {
            dlen *= packing;
            for (i = 0; i < nelm/packing; i++)
            {
                status = regDevAsynWrite(priv->device, priv->offset, dlen, 1, (char*)bptr+i*dlen, priv->callback, NULL, record->prio);
                if (status) break;
            }
        }
        else
        {
            packing = priv->arraypacking;
            status = regDevAsynWrite(priv->device, priv->offset, dlen*packing, nelm/packing, bptr, priv->callback, NULL, record->prio);
            if (status == 1)
            {
                record->pact = 1;
                return 0;
            }
        }
    }
    regDevDebugLog(3,
        "%s: write %d values of %d bytes from %p status=%d\n",
        record->name, nelm, priv->dlen, bptr, status);
    if (status)
    {
        fprintf(stderr,
            "%s: write error\n", record->name);
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

long regDevWriteNumber(dbCommon* record, double fval, epicsInt32 rval)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    epicsInt8 rval8;
    epicsInt16 rval16;
    union {epicsFloat32 f; long u;} val32;
    union {epicsFloat64 f; long long u;} val64;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    regDevDebugLog(3,
        "%s: write fval=%#g rval=%d (0x%x) low=%d high=%d\n",
        record->name, fval, rval, rval, priv->hwLow, priv->hwHigh);

    switch (priv->dtype)
    {
        case epicsInt8T:
        case epicsInt16T:
        case epicsInt32T:
            if ((epicsInt32)rval > (epicsInt32)priv->hwHigh) rval = priv->hwHigh;
            if ((epicsInt32)rval < (epicsInt32)priv->hwLow) rval = priv->hwLow;
            break;
        case epicsUInt8T:
        case epicsUInt16T:
        case epicsUInt32T:
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if ((epicsUInt32)rval > (epicsUInt32)priv->hwHigh) rval = priv->hwHigh;
            if ((epicsUInt32)rval < (epicsUInt32)priv->hwLow) rval = priv->hwLow;
            break;
    }    
    switch (priv->dtype)
    {
        case regDevBCD8T:
            rval = i2bcd(rval);
        case epicsInt8T:
        case epicsUInt8T:
            rval8 = rval;
            regDevDebugLog(2, "%s: write 8bit %02x\n",
                record->name, rval8 & 0xFF);
            status = regDevWrite(priv->device, priv->offset, 1, 1, &rval8, NULL, record->prio);
            break;
        case regDevBCD16T:
            rval = i2bcd(rval);
        case epicsInt16T:
        case epicsUInt16T:
            rval16 = rval;
            regDevDebugLog(2, "%s: write 16bit %04x\n",
                record->name, rval16 & 0xFFFF);
            status = regDevWrite(priv->device, priv->offset, 2, 1, &rval16, NULL, record->prio);
            break;
        case regDevBCD32T:
            rval = i2bcd(rval);
        case epicsInt32T:
        case epicsUInt32T:
            regDevDebugLog(2, "%s: write 32bit %08x\n",
                record->name, rval);
            status = regDevWrite(priv->device, priv->offset, 4, 1, &rval, NULL, record->prio);
            break;
        case epicsFloat32T:
            /* emulate scaling */
            val32.f = fval;
            regDevDebugLog(2, "%s: write 32bit %08lx %#g\n",
                record->name, val32.u, val32.f);
            status = regDevWrite(priv->device, priv->offset, 4, 1, &val32, NULL, record->prio);
            break;
        case epicsFloat64T:
            /* emulate scaling */
            val64.f = fval;
            regDevDebugLog(2, "%s: write 64bit %016Lx %#g\n",
                record->name, val64.u, val64.f);
            status = regDevWrite(priv->device, priv->offset, 8, 1, &val64, NULL, record->prio);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

long regDevAsynWriteNumber(dbCommon* record, double fval, epicsInt32 rval)
{
    int status;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;
    epicsInt8 rval8;
    epicsInt16 rval16;
    union {epicsFloat32 f; long u;} val32;
    union {epicsFloat64 f; long long u;} val64;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    regDevDebugLog(3,
        "%s: write fval=%#g rval=%d (0x%x) low=%d high=%d\n",
        record->name, fval, rval, rval, priv->hwLow, priv->hwHigh);

    switch (priv->dtype)
    {
        case epicsInt8T:
        case epicsInt16T:
        case epicsInt32T:
            if ((epicsInt32)rval > (epicsInt32)priv->hwHigh) rval = priv->hwHigh;
            if ((epicsInt32)rval < (epicsInt32)priv->hwLow) rval = priv->hwLow;
            break;
        case epicsUInt8T:
        case epicsUInt16T:
        case epicsUInt32T:
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if ((epicsUInt32)rval > (epicsUInt32)priv->hwHigh) rval = priv->hwHigh;
            if ((epicsUInt32)rval < (epicsUInt32)priv->hwLow) rval = priv->hwLow;
            break;
    }    
    if(record->pact)
    {
      status = priv->status;
    }
    else
    {
      switch (priv->dtype)
      {
        case regDevBCD8T:
            rval = i2bcd(rval);
        case epicsInt8T:
        case epicsUInt8T:
            rval8 = rval;
            regDevDebugLog(2, "%s: write 8bit %02x\n",
                record->name, rval8 & 0xFF);
            status = regDevAsynWrite(priv->device, priv->offset, 1, 1, &rval8, priv->callback, NULL, record->prio);
            break;
        case regDevBCD16T:
            rval = i2bcd(rval);
        case epicsInt16T:
        case epicsUInt16T:
            rval16 = rval;
            regDevDebugLog(2, "%s: write 16bit %04x\n",
                record->name, rval16 & 0xFFFF);
            status = regDevAsynWrite(priv->device, priv->offset, 2, 1, &rval16, priv->callback, NULL, record->prio);
            break;
        case regDevBCD32T:
            rval = i2bcd(rval);
        case epicsInt32T:
        case epicsUInt32T:
            regDevDebugLog(2, "%s: write 32bit %08x\n",
                record->name, rval);
            status = regDevAsynWrite(priv->device, priv->offset, 4, 1, &rval, priv->callback, NULL, record->prio);
            break;
        case epicsFloat32T:
            /* emulate scaling */
            val32.f = fval;
            regDevDebugLog(2, "%s: write 32bit %08lx %#g\n",
                record->name, val32.u, val32.f);
            status = regDevAsynWrite(priv->device, priv->offset, 4, 1, &val32, priv->callback, NULL, record->prio);
            break;
        case epicsFloat64T:
            /* emulate scaling */
            val64.f = fval;
            regDevDebugLog(2, "%s: write 64bit %016Lx %#g\n",
                record->name, val64.u, val64.f);
            status = regDevAsynWrite(priv->device, priv->offset, 8, 1, &val64, priv->callback, NULL, record->prio);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
      }
      if (status == 1)
      {
          record->pact = 1;
          return 0;
      }
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

