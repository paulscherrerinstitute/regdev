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
#include <dbAccess.h>
#include "recSup.h"
#include "regDevSup.h"

#include <epicsMessageQueue.h>

#ifndef __GNUC__
#define __attribute__(a)
#endif

#define MAGIC_PRIV 2181699655U /* crc("regDev") */
#define MAGIC_NODE 2055989396U /* crc("regDeviceNode") */
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_EXIT 4


static char cvsid_regDev[] __attribute__((unused)) =
    "$Id: regDev.c,v 1.44 2014/03/03 12:41:16 zimoch Exp $";

static regDeviceNode* registeredDevices = NULL;

int regDevDebug = 0;
epicsExportAddress(int, regDevDebug);

#define regDevGetPriv() \
    regDevPrivate* priv = record->dpvt; \
    if (!priv) { \
        regDevPrintErr("uninitialized record"); \
        return S_dev_badInit; } \
    assert (priv->magic == MAGIC_PRIV)
    
static int startswith(const char *s, const char *key)
{
    int n = 0;
    while (*key) {
        if (*key != tolower((unsigned char)*s)) return 0;
        key++;
        s++;
        n++;
    }
    return n;
}

static void regDevCallback(char* user, int status)
{
    dbCommon* record = (dbCommon*)(user - offsetof(dbCommon, name));
    regDevPrivate* priv;

    assert (user != NULL);
    priv = record->dpvt;
    assert (priv != NULL);
    assert (priv->magic == MAGIC_PRIV);
    
    priv->status = status;

    if (!interruptAccept)
    {
        epicsEventSignal(priv->initDone);
    }
    else
    {
        if (priv->status != S_dev_success)
        {
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            if (priv->updateActive)
            {
                regDevPrintErr("async record update failed");
            }
        }
        dbScanLock(record);
        (*record->rset->process)(record);
        priv->updateActive = 0;
        dbScanUnlock(record);
    }
}

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

const static struct {char* name; unsigned short dlen; epicsType type;} datatypes [] =
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

    { "qword",      8, regDev64T     },
    { "int64",      8, regDev64T     },
    { "uint64",     8, regDev64T     },
    { "unsign64",   8, regDev64T     },
    { "unsigned64", 8, regDev64T     },

    { "bcd8",       1, regDevBCD8T   },
    { "bcd16",      2, regDevBCD16T  },
    { "bcd32",      4, regDevBCD32T  },
    { "bcd",        1, regDevBCD8T   },
    { "time",       1, regDevBCD8T   } /* for backward compatibility */
};

const char* regDevTypeName(unsigned short dtype)
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

long regDevParseExpr(char** pp);

long regDevParseValue(char** pp)
{
    long val;
    char *p = *pp;
    int neg = 0;

    if (*p == '+' || *p == '-') neg = *p++ == '-';
    while (isspace((unsigned char)*p)) p++;
    if (*p == '(')
    {
        p++;
        val = regDevParseExpr(&p);
        if (*p == ')') p++;
    }
    else val = strtol(p, &p, 0);
    while (isspace((unsigned char)*p)) p++;
    *pp = p;
    return neg ? -val : val;
}

long regDevParseProd(char** pp)
{
    long val = 1;
    char *p = *pp;

    while (isspace((unsigned char)*p)) p++;
    while (*p == '*')
    {
        p++;
        val *= regDevParseValue(&p);
    }
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
        val *= regDevParseProd(&p);
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

    regDevDebugLog(DBG_INIT, "regDevIoParse %s: \"%s\"\n", recordName, parameterstring);

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum((unsigned char)*p)) if (*p++ == '\0')
    {
        errlogPrintf("regDevIoParse %s: no device name in parameter string \"%s\"\n",
            recordName, parameterstring);
        return S_dev_badArgument;
    }

    /* Get device name */
    nchar = strcspn(p, ":/ ");
    strncpy(devName, p, nchar);
    devName[nchar] = '\0';
    p += nchar;
    separator = *p++;
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: device=%s\n",
        recordName, devName);

    for (device = registeredDevices; device; device = device->next)
    {
        if (strcmp(device->name, devName) == 0) break;
    }
    if (!device)
    {
        errlogPrintf("regDevIoParse %s: device '%s' not found\n",
            recordName, devName);
        return S_dev_noDevice;
    }
    priv->device = device;

    /* Check device offset (for backward compatibility allow '/') */
    if (separator == ':' || separator == '/')
    {
        long offset = 0;
        while (isspace((unsigned char)*p)) p++;

        if (!isdigit((unsigned char)*p))
        {
            /* expect record name, maybe in ' quotes, maby in () */
            char recName[255];
            int i = 0;
            char q = 0;
            char b = 0;

            if (*p == '(')
            {
                b = *p++;
                while (isspace((unsigned char)*p)) p++;
            }
            if (*p == '\'') q = *p++;
            /* all non-whitespace chars are legal, incl + and *, except quote ' here */
            while (!isspace((unsigned char)*p) && !(q && *p == q)) recName[i++] = *p++;
            if (q && *p == q) p++;
            recName[i] = 0;
            priv->offsetRecord = (struct dbAddr*) malloc(sizeof (struct dbAddr));
            if (dbNameToAddr(recName, priv->offsetRecord) != S_dev_success)
            {
                free(priv->offsetRecord);
                priv->offsetRecord = NULL;
                errlogPrintf("regDevIoParse %s: record '%s' not found\n",
                    recordName, recName);
                return S_dev_badArgument;
            }
            priv->offsetScale = regDevParseProd(&p);
            if (b == '(')
            {
                long scale;
                offset = regDevParseExpr(&p);
                if (*p == ')')
                {
                    p++;
                    scale = regDevParseProd(&p);
                    priv->offsetScale *= scale;
                    offset *= scale;
                }
            }
        }

        offset += regDevParseExpr(&p);
        if (offset < 0 && !priv->offsetRecord)
        {
            errlogPrintf("regDevIoParse %s: offset %ld<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        if (priv->offsetRecord)
            regDevDebugLog(DBG_INIT,
                "regDevIoParse %s: offset='%s'*%"Z"u+%"Z"u\n",
                recordName, priv->offsetRecord->precord->name, priv->offsetScale, priv->offset);
        else
            regDevDebugLog(DBG_INIT,
                "regDevIoParse %s: offset=%"Z"u\n", recordName, priv->offset);
        separator = *p++;
    }
    else
    {
        priv->offset = 0;
    }

    /* Check init offset (for backward compatibility allow '!' and '/') */
    if (separator == ':' || separator == '/' || separator == '!')
    {
        char* p1;
        while (isspace((unsigned char)*p)) p++;
        p1 = p;
        long initoffset = regDevParseExpr(&p);
        if (p1 == p)
        {
            priv->initoffset = priv->offset;
        }
        else
        {
            if (initoffset < 0)
            {
                errlogPrintf("regDevIoParse %s: init offset %ld < 0\n",
                    recordName, initoffset);
                return S_dev_badArgument;
            }
            priv->initoffset = initoffset;
        }
        regDevDebugLog(DBG_INIT,
            "regDevIoParse %s: init offset=%"Z"u\n", recordName, priv->initoffset);
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

    if (separator != '\'') p--; /* optional quote for compatibility */

    /* parse parameters */
    while (p && *p)
    {
        switch (toupper((unsigned char)*p))
        {
            case ' ':
            case '\t':
                p++;
                break;
            case 'T': /* T=<datatype> */
                p+=2;
                for (type = 0; type < maxtype; type++)
                {
                    nchar = startswith(p, datatypes[type].name);
                    if (nchar)
                    {
                        priv->dtype = datatypes[type].type;
                        priv->dlen = datatypes[type].dlen;
                        p += nchar;
                        break;
                    }
                }
                if (type == maxtype)
                {
                    errlogPrintf("regDevIoParse %s: invalid datatype '%s'\n",
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
            case 'L': /* L=<low raw value> (converts to EGUL) */
                p += 2;
                hwLow = regDevParseExpr(&p);
                lset = 1;
                break;
            case 'H': /* L=<high raw value> (converts to EGUF) */
                p += 2;
                hwHigh = regDevParseExpr(&p);
                hset = 1;
                break;
            case 'P': /* P=<packing> (for fifo) */
                p += 2;
                priv->fifopacking = regDevParseExpr(&p);
                break;
            case 'U': /* U=<update period [ms]> */
                p += 2;
                priv->update = regDevParseExpr(&p);
                break;
            case '\'':
                if (separator == '\'')
                {
                    p = 0;
                    break;
                }
            case ')':
                errlogPrintf("regDevIoParse %s: unbalanced closing )\n",
                    recordName);
                return S_dev_badArgument;
            default:
                errlogPrintf("regDevIoParse %s: unknown parameter '%c'\n",
                    recordName, *p);
                return S_dev_badArgument;
        }
    }

    /* check if bit number is in range */
    if (priv->dlen && priv->bit >= priv->dlen*8)
    {
        errlogPrintf("regDevIoParse %s: invalid bit number %d (0...%d)\n",
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
            if (hwHigh > 0x7F || hwLow < -0x80)
                status = S_dev_badArgument;
            if (!lset) hwLow = -0x7F;
            if (!hset) hwHigh = 0x7F;
            break;
        case epicsInt16T:
            if (hwHigh > 0x7FFF || hwLow < -0x8000)
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
                /* dlen is usually element size (here 1)
                   but we use it here for string length.
                   Be careful later because of byte ordering.
                */
                priv->dlen = hwLow;
                hwLow = 0;
                lset = 0;
            }
        default:
            if (lset || hset) {
                errlogPrintf("regDevIoParse %s: %s%s%s makes"
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
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: L=%#x\n",  recordName, priv->hwLow);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: H=%#x\n",  recordName, priv->hwHigh);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: X=%#x\n",  recordName, priv->invert);

    if (status)
    {
        errlogPrintf("regDevIoParse %s: L=%#x (%d) or H=%#x (%d) out of range for T=%s\n",
            recordName, priv->hwLow, priv->hwLow, priv->hwHigh, priv->hwHigh, datatypes[type].name);
        return status;
    }

    return S_dev_success;
}

int regDevIoParse(dbCommon* record, struct link* link)
{
    int status;

    if (link->type != INST_IO)
    {
        regDevPrintErr("illegal link field type %s",
            pamaplinkType[link->type].strvalue);
        status = S_dev_badInpType;
    }
    else
    {
        status = regDevIoParse2(record->name,
            link->value.instio.string,
            (regDevPrivate*) record->dpvt);
        if (status == S_dev_success) return S_dev_success;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    return status;
}

/*********  Diver registration and access ****************************/

int regDevRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver, size_t size)
{
    regDeviceNode **pdevice;

    regDevDebugLog(DBG_INIT, "%s %s: support=%p, driver=%p\n",
        __FUNCTION__, name, support, driver);
    for (pdevice = &registeredDevices; *pdevice; pdevice = &(*pdevice)->next)
    {
        if (strcmp((*pdevice)->name, name) == 0)
        {
            errlogPrintf("%s %s: device already exists\n",
                __FUNCTION__, name);
            return S_dev_multDevice;
        }
    }
    *pdevice = (regDeviceNode*) calloc(1, sizeof(regDeviceNode));
    if (*pdevice == NULL)
    {
        errlogPrintf("%s %s: out of memory\n",
            __FUNCTION__, name);
        return S_dev_noMemory;
    }
    (*pdevice)->magic = MAGIC_NODE;
    (*pdevice)->name = strdup(name);
    (*pdevice)->size = size;
    (*pdevice)->support = support;
    (*pdevice)->driver = driver;
    (*pdevice)->accesslock = epicsMutexCreate();
    if ((*pdevice)->accesslock == NULL)
    {
        errlogPrintf("%s %s: out of memory\n",
            __FUNCTION__, name);
        return S_dev_noMemory;
    }
    return S_dev_success;
}

/* Only for backward compatibility. Don't use! */
int regDevAsyncRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver)
{
    return regDevRegisterDevice(name, support, driver, 0);
}

#define regDevGetDeviceNode(driver) ({ \
    regDeviceNode* device; \
    for (device = registeredDevices; device; device = device->next) \
        if (device->driver == driver) break; \
    assert(device != NULL);\
    assert(device->magic == MAGIC_NODE);\
    device;\
})

int regDevRegisterDmaAlloc(regDevice* driver,
    void* (*dmaAlloc) (regDevice *, void* ptr, size_t))
{
    regDevGetDeviceNode(driver)->dmaAlloc = dmaAlloc;
    return S_dev_success;
}


int regDevLock(regDevice* driver)
{
    return epicsMutexLock(regDevGetDeviceNode(driver)->accesslock);
}

int regDevUnlock(regDevice* driver)
{
    epicsMutexUnlock(regDevGetDeviceNode(driver)->accesslock);
    return S_dev_success;
}

/*********  Support for "I/O Intr" for input records ******************/

long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert (device);

    if (device->support->getInScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getInScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        regDevPrintErr("input I/O Intr unsupported for bus %s",
            device->name);
        return S_dev_badRequest;
    }
    if (*ppvt == NULL)
    {
        regDevPrintErr("no I/O Intr for bus %s offset %#"Z"x",
            device->name, priv->offset);
        return S_dev_badArgument;
    }
    return S_dev_success;
}

/*********  Support for "I/O Intr" for output records ****************/

long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert (device);

    if (device->support->getOutScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getOutScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        regDevPrintErr("output I/O Intr unsupported for bus %s",
            device->name);
        return S_dev_badRequest;
    }
    if (*ppvt == NULL)
    {
        regDevPrintErr("no I/O Intr for bus %s offset %#"Z"x",
            device->name, priv->offset);
        return S_dev_badArgument;
    }
    return S_dev_success;
}

/*********  Report routine ********************************************/

long regDevReport(int level)
{
    regDeviceNode* device;

    printf("  regDev version: %s\n", cvsid_regDev);
    if (!registeredDevices)
    {
        printf("    no registered devices\n");
        return S_dev_success;
    }
    printf("    registered devices:\n");
    for (device = registeredDevices; device; device = device->next)
    {
        if (device->support)
        {
            printf ("      \"%s\" (size=0x%"Z"x=%"Z"d=%"Z"d%s) ", device->name,
                device->size, device->size,
                device->size >= 0x1000000 ? device->size >> 20 : device->size >> 10,
                device->size >= 0x1000000 ? "MB" : "kB");
            if (device->support->report)
            {
                epicsMutexLock(device->accesslock);
                device->support->report(device->driver, level);
                epicsMutexUnlock(device->accesslock);
            }
            else
                printf ("\n");
        }
    }
    return S_dev_success;
}

regDevice* regDevFind(const char* name)
{
    regDeviceNode* device;

    if (!name || !*name) return NULL;
    for (device = registeredDevices; device; device = device->next)
    {
        if (strcmp(name, device->name) == 0)
            return device->driver;
    }
    return NULL;
}

/* Only for backward compatibility. Don't use! */
regDevice* regDevAsynFind(const char* name)
{
    return regDevFind(name);
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

regDevPrivate* regDevAllocPriv(dbCommon *record)
{
    regDevPrivate* priv;

    regDevDebugLog(DBG_INIT, "regDevAllocPriv(%s)\n", record->name);
    priv = calloc(1, sizeof(regDevPrivate));
    if (priv == NULL)
    {
        regDevPrintErr("try to allocate %d bytes. %s",
            (int)sizeof(regDevPrivate), strerror(errno));
#ifdef vxWorks
        {
            MEM_PART_STATS meminfo;
            memPartInfoGet(memSysPartId, &meminfo);
        }
#endif
        return NULL;
    }
    priv->magic = MAGIC_PRIV;
    priv->dtype = epicsInt16T;
    priv->dlen = 2;
    priv->arraypacking = 1;
    record->dpvt = priv;
    return priv;
}

int regDevAssertType(dbCommon *record, int allowedTypes)
{
    unsigned short dtype;

    regDevGetPriv();
    dtype = priv->dtype;

    regDevDebugLog(DBG_INIT, "regDevAssertType(%s,%s%s%s%s) %s\n",
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
    regDevPrintErr("illegal data type %s for this record type",
        regDevTypeName(dtype));
    free(record->dpvt);
    record->dpvt = NULL;
    return S_db_badField;
}

int regDevCheckFTVL(dbCommon* record, int ftvl)
{
    regDevGetPriv();
    regDevDebugLog(DBG_INIT, "regDevCheckFTVL(%s, %s)\n",
        record->name,
        pamapdbfType[ftvl].strvalue);
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
    regDevPrintErr("illegal FTVL value %s",
        pamapdbfType[ftvl].strvalue);
    return S_db_badField;
}

int regDevCheckType(dbCommon* record, int ftvl, int nelm)
{
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert (device);

    regDevDebugLog(DBG_INIT, "regDevCheckType(%s, %s, %i)\n",
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
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return ARRAY_CONVERT;
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
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return ARRAY_CONVERT;
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
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return ARRAY_CONVERT;
            break;
        case regDev64T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
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
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return ARRAY_CONVERT;
    }
    fprintf (stderr,
        "regDevCheckType %s: data type %s does not match FTVL %s\n",
         record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return S_dev_badArgument;
}

/*********  Work dispatcher thread ****************************/

struct regDevWorkMsg {
    unsigned int cmd;
    size_t offset;
    unsigned short dlen;
    size_t nelem;
    void* buffer;
    void* mask;
    regDevTransferComplete callback;
    dbCommon* record;
};

struct regDevDispatcher {
    epicsThreadId tid[3];
    epicsMessageQueueId qid[3];
};


void regDevWorkThread(regDeviceNode* device)
{
    regDevDispatcher *dispatcher = device->dispatcher;
    const regDevSupport* support = device->support;
    regDevice *driver = device->driver;
    struct regDevWorkMsg msg;
    int status;
    int prio;
    
    epicsThreadId tid = epicsThreadGetIdSelf();
    for (prio = 0; prio <= 2; prio ++)
    {
        if (epicsThreadIsEqual(tid, dispatcher->tid[prio])) break;
    }
    assert (prio <= 2);
    
    while (1)
    {
        epicsMessageQueueReceive(dispatcher->qid[prio], &msg, sizeof(msg));
        switch (msg.cmd)
        {
            case CMD_WRITE:
                regDevDebugLog(DBG_OUT, "regDevWorkThread %s-%d %s: doing dispatched write\n", device->name, prio, msg.record->name);
                epicsMutexLock(device->accesslock);
                status = support->write(driver, msg.offset, msg.dlen, msg.nelem,
                    msg.buffer, msg.mask, msg.record->prio, NULL, msg.record->name);
                epicsMutexUnlock(device->accesslock);
                break;
            case CMD_READ:
                regDevDebugLog(DBG_IN, "regDevWorkThread %s-%d %s: doing dispatched read\n", device->name, prio, msg.record->name);
                epicsMutexLock(device->accesslock);
                status = support->read(driver, msg.offset, msg.dlen, msg.nelem,
                    msg.buffer, msg.record->prio, NULL, msg.record->name);
                epicsMutexUnlock(device->accesslock);
                break;
            case CMD_EXIT:
                regDevDebugLog(DBG_INIT, "regDevWorkThread %s-%d exiting\n", device->name, prio);
                return;
            default:
                errlogPrintf("regDevWorkThread %s-%d: illegal command 0x%x\n", device->name, prio, msg.cmd);
                continue;                
        }
        msg.callback(msg.record->name, status);
    }
}

void regDevWorkExit(regDevDispatcher *dispatcher)
{
    struct regDevWorkMsg msg;

    /* destroying the queue cancels all pending requests and terminates the work threads [not true] */
    msg.cmd = CMD_EXIT;
    epicsMessageQueueSend(dispatcher->qid[0], &msg, sizeof(msg));
    epicsMessageQueueSend(dispatcher->qid[1], &msg, sizeof(msg));
    epicsMessageQueueSend(dispatcher->qid[2], &msg, sizeof(msg));

    /* wait until work threads have terminated */
    while (!epicsThreadIsSuspended(dispatcher->tid[0]) &&
            epicsThreadIsSuspended(dispatcher->tid[1]) &&
            epicsThreadIsSuspended(dispatcher->tid[2]))
        epicsThreadSleep(0.1);
}

int regDevInstallWorkQueue(regDevice* driver, size_t maxEntries)
{
    regDevDispatcher *dispatcher;
    regDeviceNode* device = regDevGetDeviceNode(driver);

    dispatcher = malloc(sizeof(regDevDispatcher));
    dispatcher->qid[0] = epicsMessageQueueCreate(maxEntries, sizeof(struct regDevWorkMsg));
    dispatcher->qid[1] = epicsMessageQueueCreate(maxEntries, sizeof(struct regDevWorkMsg));
    dispatcher->qid[2] = epicsMessageQueueCreate(maxEntries, sizeof(struct regDevWorkMsg));
    dispatcher->tid[0] = epicsThreadCreate(device->name, epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC) regDevWorkThread, device);
    dispatcher->tid[1] = epicsThreadCreate(device->name, epicsThreadPriorityMedium,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC) regDevWorkThread, device);
    dispatcher->tid[2] = epicsThreadCreate(device->name, epicsThreadPriorityHigh,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC) regDevWorkThread, device);
    epicsAtExit((void(*)(void*))regDevWorkExit, dispatcher);

    if (!dispatcher->qid[0] || !dispatcher->qid[1] || !dispatcher->qid[2] ||
        !dispatcher->tid[0] || !dispatcher->tid[1] || !dispatcher->tid[2])
    {
        errlogPrintf("%s: out of memory\n",
            __FUNCTION__);
        return S_dev_noMemory;
    }
    device->dispatcher = dispatcher;
    return S_dev_success;
}

/*********  DMA buffers ****************************/

int regDevMemAlloc(dbCommon* record, void** bptr, size_t size)
{
    void* ptr = NULL;
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert (device);

    if (device->dmaAlloc)
    {
        ptr = device->dmaAlloc(device->driver, NULL, size);
        if (ptr == NULL)
        {
            fprintf (stderr,
                "regDevMemAlloc %s: allocating device memory failed.",
                record->name);
            return S_dev_noMemory;
        }
        memset(ptr, 0, size);
        *bptr = ptr;
        return S_dev_success;
    }
    ptr = (char *)calloc(1, size);
    if (ptr == NULL)
    {
        fprintf (stderr,
            "regDevMemAlloc %s: out of memory.",
            record->name);
        return S_dev_noMemory;
    }
    *bptr = ptr;
    return S_dev_success;
}


int regDevGetOffset(dbCommon* record, int read, unsigned short dlen, size_t nelem, size_t *poffset)
{
    int status;
    ssize_t offset;
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device = priv->device;

    /* At init read we may use a different offset */
    if (read && !interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
    {
        offset = priv->offset;
        if (priv->offsetRecord)
        {
            struct {
                DBRstatus
                epicsInt32 i;
            } buffer;
            long options = DBR_STATUS;
            ssize_t off = offset;

            status = dbGetField(priv->offsetRecord, DBR_LONG, &buffer, &options, NULL, NULL);
            if (status == S_dev_success && buffer.severity == INVALID_ALARM) status = S_dev_badArgument;
            if (status != S_dev_success)
            {
                recGblSetSevr(record, LINK_ALARM, INVALID_ALARM);
                regDevDebugLog(DBG_OUT, "%s: cannot read offset from '%s'\n",
                    record->name, priv->offsetRecord->precord->name);
                return status;
            }
            off += buffer.i * priv->offsetScale;
            if (off < 0)
            {
                regDevDebugLog(DBG_OUT, "%s: effective offset '%s'=%d * %"Z"d + %"Z"u = %"Z"d < 0\n",
                    record->name, priv->offsetRecord->precord->name,
                    buffer.i, priv->offsetScale, offset, off);
                return S_dev_badSignalNumber;
            }
            offset = off;
        }
    }

    if (device->size) /* check offset range if size is proviced */
    {
        if (offset > device->size)
        {
            errlogSevPrintf(errlogMajor,
                "%s %s: offset %"Z"u out of range of device %s (0-%"Z"u)\n",
                record->name, read ? "read" : "write", offset, device->name, device->size-1);
            return S_dev_badSignalNumber;
        }
        if (offset + dlen * nelem > device->size)
        {
            errlogSevPrintf(errlogMajor,
                "%s %s: offset %"Z"u + %"Z"u bytes length exceeds device %s size %"Z"u by %"Z"u bytes\n",
                record->name, read ? "read" : "write", offset, nelem*dlen, device->name, device->size,
                offset + dlen * nelem - device->size);
            return S_dev_badSignalCount;
        }
    }
    *poffset = offset;
    return S_dev_success;
}

/*********  I/O functions ****************************/

int regDevRead(dbCommon* record, unsigned short dlen, size_t nelem, void* buffer)
{
    /* buffer must not point to local variable: not suitable for async processing */

    int status = S_dev_success;
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert(device);
    assert(buffer != NULL || nelem == 0 || dlen == 0);

    if (record->pact)
    {
        /* Second call of asynchronous device */

        regDevDebugLog(DBG_IN, "%s: asynchronous read returned %d\n", record->name, priv->status);
        if (priv->status != S_dev_success)
        {
            recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
            return priv->status;
        }
    }
    else
    {
        size_t offset;
        /* First call of (probably asynchronous) device */

        status = regDevGetOffset(record, TRUE, dlen, nelem, &offset);
        if (status) return status;

        priv->asyncOffset = offset;
        priv->status = S_dev_success;
        if (device->support->read)
        {
            if (!interruptAccept && priv->initDone == NULL)
                priv->initDone = epicsEventCreate(epicsEventEmpty);
            if (device->dispatcher)
            {
                struct regDevWorkMsg msg;
                
                msg.cmd = CMD_READ;
                msg.offset = offset;
                msg.dlen = dlen;
                msg.nelem = nelem;
                msg.buffer = buffer;
                msg.callback = regDevCallback;
                msg.record = record;
                if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], (char*)&msg, sizeof(msg)) != 0)
                {
                    recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
                    regDevDebugLog(DBG_IN, "%s: work queue is full\n", record->name);
                    return S_dev_noMemory;
                }
                status = ASYNC_COMPLETION;
            }
            else
            {
                epicsMutexLock(device->accesslock);
                status = device->support->read(device->driver,
                    offset, dlen, nelem, buffer,
                    record->prio, regDevCallback, record->name);
                epicsMutexUnlock(device->accesslock);
            }
        }
        else status = S_dev_badRequest;

        /* At init wait for completition of asynchronous device */
        if (!interruptAccept && status == ASYNC_COMPLETION)
        {
            epicsEventWait(priv->initDone);
            status = priv->status;
        }
    }

    /* Some debug output */
    if (regDevDebug & DBG_IN)
    {
        if (status == ASYNC_COMPLETION)
        {
            errlogSevPrintf(errlogInfo,
                "%s: async read %"Z"d * %d bit from %s:%"Z"u\n",
                record->name, nelem, dlen*8,
                device->name, priv->asyncOffset);
        }
        else switch (dlen)
        {
            case 1:
                errlogSevPrintf(errlogInfo,
                    "%s: read %"Z"u * 8 bit 0x%02x from %s:%"Z"u (status=%x)\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    device->name, priv->asyncOffset, status);
                break;
            case 2:
                errlogSevPrintf(errlogInfo,
                    "%s: read %"Z"u * 16 bit 0x%04x from %s:%"Z"u (status=%x)\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, priv->asyncOffset, status);
                break;
            case 4:
                errlogSevPrintf(errlogInfo,
                    "%s: read %"Z"u * 32 bit 0x%08x from %s:%"Z"u (status=%x)\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, priv->asyncOffset, status);
                break;
            case 8:
                errlogSevPrintf(errlogInfo,
                    "%s: read %"Z"u * 64 bit 0x%016llx from %s:%"Z"u (status=%x)\n",
                    record->name, nelem, *(epicsUInt64*)buffer,
                    device->name, priv->asyncOffset, status);
                break;
            default:
                errlogSevPrintf(errlogInfo,
                    "%s: read %"Z"u * %d bit from %s:%"Z"u (status=%x)\n",
                    record->name, nelem, dlen*8,
                    device->name, priv->asyncOffset, status);
        }
    }

    if (status == ASYNC_COMPLETION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_IN, "%s: wait for asynchronous read completition\n", record->name);
        record->pact = 1;
    }
    else if (status != S_dev_success && nelem != 0) /* nelem == 0 => only status readout */
    {
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_IN, "%s: read error\n", record->name);
    }
    return status;
}

int regDevWrite(dbCommon* record, unsigned short dlen, size_t nelem, void* buffer, void* mask)
{
    /* buffer must not point to local variable: not suitable for async processing */

    int status;
    size_t offset;
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert(device);
    assert(buffer != NULL);

    if (record->pact)
    {
        /* Second call of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: asynchronous write returned %d\n", record->name, priv->status);

        if (priv->status != S_dev_success)
        {
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        }
        return priv->status;
    }

    /* First call of (probably asynchronous) device */
    status = regDevGetOffset(record, FALSE, dlen, nelem, &offset);
    if (status) return status;
        
    /* Some debug output */
    if (regDevDebug  & DBG_OUT)
    {
        switch (dlen+(mask?10:0))
        {
            case 1:
                regDevDebugLog(DBG_OUT, 
                    "%s: write %"Z"u * 8 bit 0x%02x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    device->name, offset);
                break;
            case 2:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 16 bit 0x%04x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset);
                break;
            case 4:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 32 bit 0x%08x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset);
                break;
            case 8:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 64 bit 0x%016llx to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt64*)buffer,
                    device->name, offset);
                break;
            case 11:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 8 bit 0x%02x mask 0x%02x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt8*)buffer, *(epicsUInt8*)mask,
                    device->name, offset);
            case 12:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 16 bit 0x%04x mask 0x%04x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt16*)buffer, *(epicsUInt16*)mask,
                    device->name, offset);
                break;
            case 14:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 32 bit 0x%08x mask 0x%08x to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt32*)buffer, *(epicsUInt32*)mask,
                    device->name, offset);
                break;
            case 18:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 64 bit 0x%016llx mask 0x%016llx to %s:%"Z"u\n",
                    record->name, nelem, *(epicsUInt64*)buffer, *(epicsUInt64*)mask,
                    device->name, offset);
                break;
            default:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * %d bit to %s:%"Z"u\n",
                    record->name, nelem, dlen*8,
                    device->name, offset);
        }
    }

    priv->status = S_dev_success;
    if (device->support->write)
    {
        if (!interruptAccept && priv->initDone == NULL)
            priv->initDone = epicsEventCreate(epicsEventEmpty);
        if (device->dispatcher)
        {
            struct regDevWorkMsg msg;

            msg.cmd = CMD_WRITE;
            msg.offset = offset;
            msg.dlen = dlen;
            msg.nelem = nelem;
            msg.buffer = buffer;
            msg.mask = mask;
            msg.callback = regDevCallback;
            msg.record = record;
            if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], (char*)&msg, sizeof(msg)) != 0)
            {
                recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
                regDevDebugLog(DBG_OUT, "%s: work queue is full\n", record->name);
                return S_dev_noMemory;
            }
            status = ASYNC_COMPLETION;
        }
        else
        {
            epicsMutexLock(device->accesslock);
            status = device->support->write(device->driver,
                offset, dlen, nelem, buffer, mask,
                record->prio, regDevCallback, record->name);
            epicsMutexUnlock(device->accesslock);
        }
    }
    else status = S_dev_badRequest;

    /* At init wait for completition of asynchronous device */
    if (!interruptAccept && status == ASYNC_COMPLETION)
    {
        epicsEventWait(priv->initDone);
        status = priv->status;
    }

    if (status == ASYNC_COMPLETION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: wait for asynchronous write completition\n", record->name);
        record->pact = 1;
    }
    else if (status != S_dev_success)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "%s: write error\n", record->name);
    }
    return status;
}

int regDevReadNumber(dbCommon* record, epicsInt32* rval, double* fval)
{
    int status = S_dev_success;
    epicsInt32 rv = 0;
    epicsFloat64 fv = 0.0;

    regDevGetPriv();
    status = regDevRead(record, priv->dlen, 1, &priv->data);
    if (status != S_dev_success)
        return status;

    switch (priv->dtype)
    {
        case epicsInt8T:
            rv = priv->data.sval8;
            break;
        case epicsUInt8T:
        case regDevBCD8T:
            rv = priv->data.uval8;
            break;
        case epicsInt16T:
            rv = priv->data.sval16;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            rv = priv->data.uval16;
            break;
        case epicsInt32T:
            rv = priv->data.sval32;
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            rv = priv->data.uval32;
            break;
        case regDev64T:
            rv = priv->data.uval64; /* cut off high bits */
            break;
        case epicsFloat32T:
            fv = priv->data.fval32;
            break;
        case epicsFloat64T:
            fv = priv->data.fval64;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data %s type requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }

    if (!interruptAccept)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }

    switch (priv->dtype)
    {
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            rv = bcd2i(rv);
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            assert(fval != NULL);
            *fval = fv;
            record->udf = FALSE;
            return DONT_CONVERT;
    }
    assert(rval != NULL);
    *rval = rv;
    return S_dev_success;
}

int regDevWriteNumber(dbCommon* record, epicsInt32 rval, double fval)
{
    regDevGetPriv();
    regDevDebugLog(DBG_OUT, "regDevWriteNumber(record=%s, rval=%d (0x%08x), fval=%#g)\n",
        record->name, rval, rval, fval);

    /* enforce bounds */
    switch (priv->dtype)
    {
        case epicsFloat32T:
        case epicsFloat64T:
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            if ((epicsUInt32)rval > (epicsUInt32)priv->hwHigh)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %u (0x%08x) to upper bound %u (0x%08x)\n",
                    record->name, rval, rval, priv->hwHigh, priv->hwHigh);
                rval = priv->hwHigh;
            }
            if ((epicsUInt32)rval < (epicsUInt32)priv->hwLow)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %u (0x%08x) to lower bound %u (0x%08x)\n",
                    record->name, rval, rval, priv->hwLow, priv->hwLow);
                rval = priv->hwLow;
            }
            break;
        default:
            if (rval > priv->hwHigh)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %d (0x%08x) to upper bound %d (0x%08x)\n",
                    record->name, rval, rval, priv->hwHigh, priv->hwHigh);
                rval = priv->hwHigh;
            }
            if (rval < priv->hwLow)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %d (0x%08x) to lower bound %d (0x%08x)\n",
                    record->name, rval, rval, priv->hwLow, priv->hwLow);
                rval = priv->hwLow;
            }
    }

    switch (priv->dtype)
    {
        case regDevBCD8T:
            rval = i2bcd(rval);
        case epicsInt8T:
        case epicsUInt8T:
            priv->data.uval8 = rval;
            break;
        case regDevBCD16T:
            rval = i2bcd(rval);
        case epicsInt16T:
        case epicsUInt16T:
            priv->data.uval16 = rval;
            break;
        case regDevBCD32T:
            rval = i2bcd(rval);
        case epicsInt32T:
        case epicsUInt32T:
            priv->data.uval32 = rval;
            break;
        case regDev64T:
            priv->data.uval64 = rval;
            break;
        case epicsFloat32T:
            priv->data.fval32 = fval;
            break;
        case epicsFloat64T:
            priv->data.fval64 = fval;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data type %s requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }
    return regDevWrite(record, priv->dlen, 1, &priv->data, NULL);
}

int regDevReadBits(dbCommon* record, epicsInt32* rval)
{
    int status = S_dev_success;
    epicsInt32 rv = 0;

    regDevGetPriv();
    status = regDevRead(record, priv->dlen, 1, &priv->data);
    if (status != S_dev_success)
        return status;

    switch (priv->dtype)
    {
        case epicsInt8T:
            rv = priv->data.sval8;
            break;
        case epicsUInt8T:
            rv = priv->data.uval8;
            break;
        case epicsInt16T:
            rv = priv->data.sval16;
            break;
        case epicsUInt16T:
            rv = priv->data.uval16;
            break;
        case epicsInt32T:
            rv = priv->data.sval32;
            break;
        case epicsUInt32T:
            rv = priv->data.uval32;
            break;
        case regDev64T:
            rv = priv->data.uval64; /* cut off high bits */
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data %s type requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }

    if (!interruptAccept)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }

    assert(rval != NULL);
    *rval = rv ^ priv->invert;
    return S_dev_success;
}

int regDevWriteBits(dbCommon* record, epicsInt32 rval, epicsUInt32 mask)
{
    regDevGetPriv();
    regDevDebugLog(DBG_OUT, "regDevWriteBits record=%s, rval=0x%08x, mask=0x%08x)\n",
        record->name, rval, mask);

    rval ^= priv->invert;

    switch (priv->dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
            priv->data.uval8 = rval;
            priv->mask.uval8 = (mask & 0xff) == 0xff ? 0 : mask;
            break;
        case epicsInt16T:
        case epicsUInt16T:
            priv->data.uval16 = rval;
            priv->mask.uval16 = (mask & 0xffff) == 0xffff ? 0 : mask;
            break;
        case epicsInt32T:
        case epicsUInt32T:
            priv->data.uval32 = rval;
            priv->mask.uval32 = mask == 0xffffffff ? 0 : mask;
            break;
        case regDev64T:
            priv->data.uval64 = rval;
            priv->mask.uval64 = mask;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data type %s requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }
    return regDevWrite(record, priv->dlen, 1, &priv->data, mask ? &priv->mask : NULL);
}

int regDevReadArray(dbCommon* record, size_t nelm)
{
    int status = S_dev_success;
    int i;
    unsigned short dlen;
    int packing;

    regDevGetPriv();

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->dlen contains string length */
        if (nelm > priv->dlen)
            nelm = priv->dlen;
        dlen = 1;
    }
    else
        dlen = priv->dlen;

    packing = priv->fifopacking;
    if (packing)
    {
        /* FIFO: read element wise */
        char* buffer = priv->data.buffer;
        dlen *= packing;
        for (i = 0; i < nelm/packing; i++)
        {
            status = regDevRead(record,
                dlen, 1, buffer+i*dlen);
            if (status != S_dev_success) return status;
            /* probably does not work async */
        }
    }
    else
    {
        packing = priv->arraypacking;
        status = regDevRead(record,
            dlen*packing, nelm/packing, priv->data.buffer);
    }

    if (status != S_dev_success) return status;

    switch (priv->dtype)
    {
        case regDevBCD8T:
        {
            epicsUInt8* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
    }
    return S_dev_success;
}

int regDevWriteArray(dbCommon* record, size_t nelm)
{
    int status = 0;
    int i;
    unsigned short dlen;
    int packing;

    regDevGetPriv();

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->dlen contains string length */
        if (nelm > priv->dlen)
            nelm = priv->dlen;
        dlen = 1;
    }
    else
    {
        dlen = priv->dlen;
    }

    switch (priv->dtype)
    {
        case regDevBCD8T:
        {
            epicsUInt8* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
    }

    packing = priv->fifopacking;
    if (packing)
    {
        /* FIFO: write element wise */
        char* buffer = priv->data.buffer;
        dlen *= packing;
        for (i = 0; i < nelm/packing; i++)
        {
            status = regDevWrite(record, dlen, 1, buffer+i*dlen, NULL);
            /* probably does not work async */
            if (status) break;
        }
    }
    else
    {
        packing = priv->arraypacking;
        status = regDevWrite(record, dlen*packing, nelm/packing, priv->data.buffer, NULL);
    }
    return status;
}

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high)
{
    double o, s;
    size_t i;

    regDevGetPriv();

    s = (double)priv->hwHigh - priv->hwLow;
    o = (priv->hwHigh * low - priv->hwLow * high) / s;
    s = (high - low) / s;

    switch (priv->dtype)
    {
        case epicsInt8T:
        {
            epicsInt8* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt8T:
        case regDevBCD8T:
        {
            epicsUInt8* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
        case epicsInt16T:
        {
            epicsInt16* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt16T:
        case regDevBCD16T:
        {
            epicsUInt16* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
        case epicsInt32T:
        {
            epicsInt32* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            /* we need to care more about the type of hwHigh and hwLow here */
            epicsUInt32* r = priv->data.buffer;

            s = (double)(epicsUInt32)priv->hwHigh - (epicsUInt32)priv->hwLow;
            o = ((epicsUInt32)priv->hwHigh * low - (epicsUInt32)priv->hwLow * high) / s;
            s = (high - low) / s;

            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else break;
            return S_dev_success;
        }
    }
    recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
    regDevPrintErr("unexpected conversion from %s to %s",
        regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return S_dev_badArgument;
}

int regDevScaleToRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high)
{
    double o, s, x;
    size_t i;

    regDevGetPriv();

    s = (double)priv->hwHigh - priv->hwLow;
    o = (priv->hwLow * high - priv->hwHigh * low) / s;
    s = s / (high - low);

    switch (priv->dtype)
    {
        case epicsInt8T:
        {
            epicsInt8* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt8T:
        case regDevBCD8T:
        {
            epicsUInt8* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsInt16T:
        {
            epicsInt16* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt16T:
        case regDevBCD16T:
        {
            epicsUInt16* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsInt32T:
        {
            epicsInt32* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->hwLow) x = priv->hwLow;
                    if (x > priv->hwHigh) x = priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            /* we need to care more about the type of hwHigh and hwLow here */
            epicsUInt32* r = priv->data.buffer;

            s = (double)(epicsUInt32)priv->hwHigh - (epicsUInt32)priv->hwLow;
            o = ((epicsUInt32)priv->hwLow * high - (epicsUInt32)priv->hwHigh * low) / s;
            s = s / (high - low);

            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < (epicsUInt32)priv->hwLow) x = (epicsUInt32)priv->hwLow;
                    if (x > (epicsUInt32)priv->hwHigh) x = (epicsUInt32)priv->hwHigh;
                    r[i] = x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < (epicsUInt32)priv->hwLow) x = (epicsUInt32)priv->hwLow;
                    if (x > (epicsUInt32)priv->hwHigh) x = (epicsUInt32)priv->hwHigh;
                    r[i] = x;
                }
            }
            else break;
            return S_dev_success;
        }
    }
    recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
    regDevPrintErr("unexpected conversion from %s to %s",
        pamapdbfType[ftvl].strvalue, regDevTypeName(priv->dtype));
    return S_dev_badArgument;
}

/*********  Output updates from hardware ****************************/

void regDevUpdateCallback(dbCommon* record)
{
    int status;
    regDevPrivate* priv = record->dpvt;

    if (interruptAccept)
    {
        regDevDebugLog(DBG_IN, "%s: updating record\n",
            record->name);

        dbScanLock(record);
        if (!record->pact)
        {
            priv->updateActive = 1;
            status = (*record->rset->process)(record);
            if (!record->pact)
            {
                priv->updateActive = 0;
                if (status)
                {
                    regDevPrintErr("record update failed. status = 0x%x",
                        status);
                }
            }
        }
        dbScanUnlock(record);
    }
    /* restart timer */
    epicsTimerStartDelay(priv->updateTimer, priv->update * 0.001);
}

int regDevInstallUpdateFunction(dbCommon* record, DEVSUPFUN updater)
{
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert (device);
    
    regDevDebugLog(DBG_INIT, "%s: regDevInstallUpdateFunction\n", record->name);
    
    if (priv->update)
    {
        if (!device->updateTimerQueue)
        {
            device->updateTimerQueue = epicsTimerQueueAllocate(0, epicsThreadPriorityLow);
            if (!device->updateTimerQueue)
            {
                regDevPrintErr("epicsTimerQueueAllocate failed");
                return S_dev_noMemory;
            }
        }
        /* install periodic update function */
        regDevDebugLog(DBG_INIT,  "%s: install update %f seconds\n", record->name, priv->update * 0.001);
        priv->updater = updater;
        priv->updateTimer = epicsTimerQueueCreateTimer(device->updateTimerQueue,
            (epicsTimerCallback)regDevUpdateCallback, record);
        epicsTimerStartDelay(priv->updateTimer, priv->update * 0.001);
    }
    return S_dev_success;
}


static epicsEventId regDevDisplayEvent;
static int regDevDisplayStatus;

static void regDevDisplayCallback(char* user, int status)
{
    regDevDebugLog(DBG_IN, "DMA complete, status=0x%x\n", status);
    regDevDisplayStatus = status;
    epicsEventSignal(regDevDisplayEvent);
}

int regDevDisplay(const char* devName, size_t start, unsigned int dlen, size_t bytes)
{
    static size_t offset = 0;
    static size_t save_bytes = 128;
    static unsigned int save_dlen = 2;
    static regDeviceNode* save_device = NULL;
    static char* buffer = NULL;
    static size_t bufferSize = 0;
    epicsTimeStamp startTime, endTime;
    
    regDeviceNode* device;
    int status;
    size_t nelem;
        
    if (devName && *devName) {
        for (device = registeredDevices; device; device = device->next)
        {
            if (strcmp(device->name, devName) == 0) break;
        }
        if (device != save_device)
        {
            save_device = device;
            offset = 0;
        }
    }
    device = save_device;
    if (!device)
    {
        errlogPrintf("device %s not found\n", devName);
        return S_dev_noDevice;
    }
    if (start > 0 || dlen || bytes) offset = start;
    if (dlen) save_dlen = dlen; else dlen = save_dlen;
    if (bytes) save_bytes = bytes; else bytes = save_bytes;
    
    if (offset >= device->size)
    {
        errlogPrintf("address 0x%"Z"x out of range\n", offset);
        return S_dev_badArgument;
    }
    if (offset + bytes > device->size)
    {
        bytes = device->size - offset;
    }
    nelem = bytes/dlen;
    
    if (bytes > bufferSize)
    {
        if (device->dmaAlloc)
        {
            buffer = device->dmaAlloc(device->driver, buffer, bytes);
            if (!buffer)
            {
                errlogPrintf("no DMA buffer of that size\n");
                return S_dev_noMemory;
            }
            memset(buffer, 0, bytes);
        }
        else
        {
            free (buffer);
            buffer = calloc(1, bytes);
            if (!buffer)
            {
                errlogPrintf("out of memory\n");
                return S_dev_noMemory;
            }
        }
        bufferSize = bytes;
    }
    
    if (device->support->read)
    {
        if (!regDevDisplayEvent)
            regDevDisplayEvent = epicsEventCreate(epicsEventEmpty);
        epicsMutexLock(device->accesslock);
        epicsTimeGetCurrent(&startTime);
        status = device->support->read(device->driver,
            offset, dlen, nelem, buffer, 0, regDevDisplayCallback, "regDevDisplay");
        epicsMutexUnlock(device->accesslock);
        if (status == ASYNC_COMPLETION)
        {
            regDevDebugLog(DBG_IN, "Wait for DMA completion\n");
            epicsEventWait(regDevDisplayEvent);
        }
        epicsTimeGetCurrent(&endTime);
        status = regDevDisplayStatus;
    }
    else
    {
        errlogPrintf("device has no read method\n");
        status = S_dev_badRequest;
    }
    if (status == S_dev_success)
    {
        int i, j, k;
        int bytesPerLine = dlen <= 16 ? 16 / dlen * dlen : dlen;

        for (i = 0; i < bytes; i += bytesPerLine)
        {
            printf ("%08"Z"x: ", offset + i);
            for (j = 0; j < bytesPerLine; j += dlen)
            {
                for (k = 0; k < dlen; k++)
                {
                    if (i + j < nelem * dlen)
                        printf ("%02x", buffer[i+j+k]);
                    else
                        printf ("  ");
                }
                printf (" ");
            }
            for (j = 0; j < bytesPerLine; j += dlen)
            {
                for (k = 0; k < dlen; k++)
                {
                    if (i + j < nelem * dlen)
                        printf ("%c", isprint((unsigned char)buffer[i+j+k]) ? buffer[i+j+k] : '.');
                    else
                        printf (" ");
                }
            }
            printf ("\n");
        }
    }
    if (regDevDebug & DBG_IN)
    {
        printf ("read took %g milliseconds\n",
            epicsTimeDiffInSeconds(&endTime, &startTime)*1000);
    }
    offset += dlen * nelem;
    return status;
}

int regDevPut(const char* devName, int offset, unsigned short dlen, int value)
{
    regDeviceNode* device = NULL;
    int status;
    regDevAnytype buffer;
    
    if (devName && *devName) {
        for (device = registeredDevices; device; device = device->next)
        {
            if (strcmp(device->name, devName) == 0) break;
        }
    }
    if (!device)
    {
        errlogPrintf("device %s not found\n", devName);
        return S_dev_noDevice;
    }
    switch (dlen)
    {
    
        case 1:
            buffer.sval8 = value;
            break;
        case 2:
            buffer.sval16 = value;
            break;
        case 4:
            buffer.sval32 = value;
            break;
        default:
            errlogPrintf("illegal dlen %d, must be 1, 2, or 4\n", dlen);
            return S_dev_badArgument;
    }
    if (device->support->write)
    {
        if (!regDevDisplayEvent)
            regDevDisplayEvent = epicsEventCreate(epicsEventEmpty);
        epicsMutexLock(device->accesslock);
        status = device->support->write(device->driver,
            offset, dlen, 1, &buffer, NULL, 0, regDevDisplayCallback, "regDevPut");
        epicsMutexUnlock(device->accesslock);
        if (status == ASYNC_COMPLETION)
            epicsEventWait(regDevDisplayEvent);
        status = regDevDisplayStatus;
    }
    else
    {
        errlogPrintf("device has no write method\n");
        status = S_dev_badRequest;
    }
    return status;
}

#ifdef EPICS_3_14
#include <iocsh.h>
static const iocshArg regDevDisplayArg0 = { "devName", iocshArgString };
static const iocshArg regDevDisplayArg1 = { "start", iocshArgInt };
static const iocshArg regDevDisplayArg2 = { "dlen", iocshArgInt };
static const iocshArg regDevDisplayArg3 = { "bytes", iocshArgInt };
static const iocshArg * const regDevDisplayArgs[] = {
    &regDevDisplayArg0,
    &regDevDisplayArg1,
    &regDevDisplayArg2,
    &regDevDisplayArg3,
};

static const iocshFuncDef regDevDisplayDef =
    { "regDevDisplay", 4, regDevDisplayArgs };
    
static void regDevDisplayFunc (const iocshArgBuf *args)
{
    regDevDisplay(
        args[0].sval, args[1].ival, args[2].ival, args[3].ival);
}

static const iocshArg regDevPutArg0 = { "devName", iocshArgString };
static const iocshArg regDevPutArg1 = { "offset", iocshArgInt };
static const iocshArg regDevPutArg2 = { "dlen", iocshArgInt };
static const iocshArg regDevPutArg3 = { "value", iocshArgInt };
static const iocshArg * const regDevPutArgs[] = {
    &regDevPutArg0,
    &regDevPutArg1,
    &regDevPutArg2,
    &regDevPutArg3,
};

static const iocshFuncDef regDevPutDef =
    { "regDevPut", 4, regDevPutArgs };

static void regDevPutFunc (const iocshArgBuf *args)
{
    regDevPut(
        args[0].sval, args[1].ival, args[2].ival, args[3].ival);
}

static void regDevRegistrar ()
{
    iocshRegister(&regDevDisplayDef, regDevDisplayFunc);
    iocshRegister(&regDevPutDef, regDevPutFunc);
}

epicsExportRegistrar(regDevRegistrar);
#endif
