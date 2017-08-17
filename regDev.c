/* Generic register Device Support */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#define epicsTypesGLOBAL
#include <callback.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <recSup.h>
#include <epicsTimer.h>
#include <epicsMessageQueue.h>
#include <epicsThread.h>
#include <cantProceed.h>
#include <epicsAssert.h>
#include <epicsExit.h>

#include <memDisplay.h>

#include "regDevSup.h"

#define MAGIC_PRIV 2181699655U /* crc("regDev") */
#define MAGIC_NODE 2055989396U /* crc("regDeviceNode") */
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_EXIT 4

static regDeviceNode* registeredDevices = NULL;

epicsShareDef int regDevDebug = 0;
epicsExportAddress(int, regDevDebug);

#define regDevGetPriv() \
    regDevPrivate* priv = record->dpvt; \
    if (priv == NULL) { \
        regDevPrintErr("record not initialized"); \
        return S_dev_badInit; } \
    assert(priv->magic == MAGIC_PRIV)

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

/***********************************************************************
 * Routine to parse IO arguments
 * IO address line format:
 *
 * <name>:<addr>[:[init]] [T=<type>] [B=<bit>] [I=<invert>] [L=<low|strLen>] [H=<high>] [P=<packing>] [U=<update>]
 *
 * where: <name>    - symbolic device name
 *        <addr>    - address (byte number) within memory block
 *                    (expressions containing +-*() allowed, no spaces!)
 *        <init>    - optional init read address ( for output records )
 *        <type>    - data type, see table below
 *        <bit>     - bit number (least significant bit is 0)
 *        <invert>  - mask of inverted bits
 *        <strLen>  - string length
 *        <low>     - raw value that mapps to EGUL
 *        <high>    - raw value that mapps to EGUF
 *        <packing> - number of array values in one fifo register
 *        <update>  - milliseconds for periodic update of output records
 **********************************************************************/

#define regDevBCD8T  (100)
#define regDevBCD16T (101)
#define regDevBCD32T (102)
#define regDev64T    (103)
#define regDevFirstType regDevBCD8T
#define regDevLastType  regDev64T

static const struct {char* name; epicsType type;} datatypes [] =
{
/* Important for order:
    * The default type must be the first entry.
    * Names that are substrings of others names must come later
*/
    { "short",      epicsInt16T   },
    { "int16",      epicsInt16T   },

    { "int8",       epicsInt8T    },

    { "char",       epicsUInt8T   },
    { "byte",       epicsUInt8T   },
    { "uint8",      epicsUInt8T   },
    { "unsign8",    epicsUInt8T   },
    { "unsigned8",  epicsUInt8T   },

    { "word",       epicsUInt16T  },
    { "uint16",     epicsUInt16T  },
    { "unsign16",   epicsUInt16T  },
    { "unsigned16", epicsUInt16T  },

    { "long",       epicsInt32T   },
    { "int32",      epicsInt32T   },

    { "dword",      epicsUInt32T  },
    { "uint32",     epicsUInt32T  },
    { "unsign32",   epicsUInt32T  },
    { "unsigned32", epicsUInt32T  },

    { "double",     epicsFloat64T },
    { "real64",     epicsFloat64T },
    { "float64",    epicsFloat64T },

    { "single",     epicsFloat32T },
    { "real32",     epicsFloat32T },
    { "float32",    epicsFloat32T },
    { "float",      epicsFloat32T },

    { "string",     epicsStringT  },

    { "qword",      regDev64T     },
    { "int64",      regDev64T     },
    { "uint64",     regDev64T     },
    { "unsign64",   regDev64T     },
    { "unsigned64", regDev64T     },

    { "bcd8",       regDevBCD8T   },
    { "bcd16",      regDevBCD16T  },
    { "bcd32",      regDevBCD32T  },
    { "bcd",        regDevBCD8T   },
    { "time",       regDevBCD8T   } /* for backward compatibility */
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

ptrdiff_t regDevParseExpr(char** pp);

ptrdiff_t regDevParseValue(char** pp)
{
    ptrdiff_t val;
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

ptrdiff_t regDevParseProd(char** pp)
{
    ptrdiff_t val = 1;
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

ptrdiff_t regDevParseExpr(char** pp)
{
    ptrdiff_t sum = 0;
    ptrdiff_t val;
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
    size_t nchar;

    static const int maxtype = sizeof(datatypes)/sizeof(*datatypes);
    int type = 0;
    int hset = 0;
    int lset = 0;
    long H = 0;
    long L = 0;

    regDevDebugLog(DBG_INIT, "%s: \"%s\"\n", recordName, parameterstring);

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
    regDevDebugLog(DBG_INIT, "%s: device=%s\n",
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
        ptrdiff_t offset = 0;
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
            priv->offsetRecord = (struct dbAddr*) mallocMustSucceed(sizeof (struct dbAddr), "regDevIoParse");
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
                ptrdiff_t scale;
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
            errlogPrintf("regDevIoParse %s: offset %"Z"d<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        if (priv->offsetRecord)
            regDevDebugLog(DBG_INIT,
                "%s: offset='%s'*0x%"Z"x+0x%"Z"x\n",
                recordName, priv->offsetRecord->precord->name, priv->offsetScale, priv->offset);
        else
            regDevDebugLog(DBG_INIT,
                "%s: offset=0x%"Z"x\n", recordName, priv->offset);
        separator = *p++;
    }
    else
    {
        priv->offset = 0;
    }

    /* Check readback offset (for backward compatibility allow '!' and '/') */
    if (separator == ':' || separator == '/' || separator == '!')
    {
        char* p1;
        ptrdiff_t rboffset;

        if (!device->support->read)
        {
            errlogPrintf("regDevIoParse %s: can't read back from device without read function\n",
                recordName);
            return S_dev_badArgument;
        }

        while (isspace((unsigned char)*p)) p++;
        p1 = p;
        rboffset = regDevParseExpr(&p);
        if (p1 == p)
        {
            if (priv->offsetRecord)
            {
                errlogPrintf("regDevIoParse %s: cannot read back from variable offset\n",
                    recordName);
                return S_dev_badArgument;
            }
            priv->rboffset = priv->offset;
        }
        else
        {
            if (rboffset < 0)
            {
                errlogPrintf("regDevIoParse %s: readback offset %"Z"d < 0\n",
                    recordName, rboffset);
                return S_dev_badArgument;
            }
            priv->rboffset = rboffset;
        }
        regDevDebugLog(DBG_INIT,
            "%s: readback offset=0x%"Z"x\n", recordName, priv->rboffset);
        separator = *p++;
    }
    else
    {
        regDevDebugLog(DBG_INIT,
            "%s: no readback offset\n", recordName);
        priv->rboffset = DONT_INIT;
    }

    /* set default values for parameters */
    priv->bit = 0;
    priv->L = 0;
    priv->H = 0;
    priv->invert = 0;

    /* allow whitespaces before parameter for device support */
    while ((separator == '\t') || (separator == ' '))
        separator = *p++;

    /* driver parameter for device support if present */

    if (separator != '\'') p--; /* optional quote for compatibility */

    /* parse parameters */
    while (p && *p)
    {
        ptrdiff_t val;
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
                val = regDevParseExpr(&p);
                if (val < 0 || val >= 64)
                {
                    errlogPrintf("regDevIoParse %s: invalid bit number %"Z"d\n",
                        recordName, val);
                    return S_dev_badArgument;
                }
                priv->bit = (epicsUInt8)val;
                break;
            case 'I': /* I=<invert> */
                p += 2;
                priv->invert = (epicsUInt32)regDevParseExpr(&p);
                break;
            case 'L': /* L=<low raw value> (converts to EGUL) */
                p += 2;
                L = (epicsInt32)regDevParseExpr(&p);
                lset = 1;
                break;
            case 'H': /* L=<high raw value> (converts to EGUF) */
                p += 2;
                H = (epicsInt32)regDevParseExpr(&p);
                hset = 1;
                break;
            case 'P': /* P=<packing> (for fifo) */
                p += 2;
                priv->fifopacking = (epicsUInt8)regDevParseExpr(&p);
                break;
            case 'U': /* U=<update period [ms]> */
                p += 2;
                priv->update = (epicsInt32)regDevParseExpr(&p);
                break;
            case 'V': /* V=<irq vector> */
                p += 2;
                priv->irqvec = (epicsInt32)regDevParseExpr(&p);
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

    /* get default values for L and H if user did not define them */
    switch (priv->dtype)
    {
        case epicsUInt8T:
            priv->dlen = 1;
            if (!hset) H = 0xFF;
            break;
        case epicsUInt16T:
            priv->dlen = 2;
            if (!hset) H = 0xFFFF;
            break;
        case epicsUInt32T:
            priv->dlen = 4;
            if (!hset) H = 0xFFFFFFFF;
            break;
        case regDev64T:
            priv->dlen = 8;
            if (!hset) H = 0xFFFFFFFF;
            break;
        case epicsInt8T:
            priv->dlen = 1;
            if (!lset) L = -0x7F;
            if (!hset) H = 0x7F;
            break;
        case epicsInt16T:
            priv->dlen = 2;
            if (!lset) L = -0x7FFF;
            if (!hset) H = 0x7FFF;
            break;
        case epicsInt32T:
            priv->dlen = 4;
            if (!lset) L = -0x7FFFFFFF;
            if (!hset) H = 0x7FFFFFFF;
            break;
        case regDevBCD8T:
            priv->dlen = 1;
            if (!hset) H = 99;
            break;
        case regDevBCD16T:
            priv->dlen = 2;
            if (!hset) H = 9999;
            break;
        case regDevBCD32T:
            priv->dlen = 4;
            if (!hset) H = 99999999;
            break;
        case epicsFloat32T:
            priv->dlen = 4;
            break;
        case epicsFloat64T:
            priv->dlen = 8;
            break;
        case epicsStringT:
            priv->dlen = 1;
            if (!lset) L = MAX_STRING_SIZE;
            /* for T=STRING L=... means length, not low */
            lset=0;
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
    priv->L = L;
    priv->H = H;
    regDevDebugLog(DBG_INIT, "%s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(DBG_INIT, "%s: L=%d=%#x\n",  recordName, priv->L, priv->L);
    regDevDebugLog(DBG_INIT, "%s: H=%d=%#x\n",  recordName, priv->H, priv->H);
    regDevDebugLog(DBG_INIT, "%s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(DBG_INIT, "%s: X=%#x\n",  recordName, priv->invert);

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
        if (status == S_dev_success) return status;
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

    regDevDebugLog(DBG_INIT, "%s: support=%p, driver=%p\n",
        name, support, driver);
    for (pdevice = &registeredDevices; *pdevice; pdevice = &(*pdevice)->next)
    {
        if (strcmp((*pdevice)->name, name) == 0)
        {
            errlogPrintf("regDevRegisterDevice %s: device already exists\n",
                name);
            return S_dev_multDevice;
        }
    }
    *pdevice = (regDeviceNode*) callocMustSucceed(1, sizeof(regDeviceNode), "regDevRegisterDevice");
    (*pdevice)->magic = MAGIC_NODE;
    (*pdevice)->name = strdup(name);
    (*pdevice)->size = size;
    (*pdevice)->support = support;
    (*pdevice)->driver = driver;
    (*pdevice)->accesslock = epicsMutexMustCreate();
    return S_dev_success;
}

/* Only for backward compatibility. Don't use! */
int regDevAsyncRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver)
{
    return regDevRegisterDevice(name, support, driver, 0);
}

regDeviceNode* regDevGetDeviceNode(regDevice* driver) {
    regDeviceNode* device;
    for (device = registeredDevices; device; device = device->next)
        if (device->driver == driver) break;
    assert(device != NULL);
    assert(device->magic == MAGIC_NODE);
    return device;
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

const char* regDevName(regDevice* driver)
{
    return regDevGetDeviceNode(driver)->name;
}

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
    assert(device != NULL);
    
    regDevDebugLog(DBG_INIT, "%s %s prio=%d irqvec=%d block=%p blockmodes=%d cmd=%d\n",
        device->name, record->name, record->prio, priv->irqvec, device->blockBuffer, device->blockModes, cmd);

    if (priv->irqvec == -1 && (device->blockModes & REGDEV_BLOCK_READ) && record->prio != 2)
    {
        *ppvt = device->blockReceived;
    }
    else if (device->support->getInScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getInScanPvt(
            device->driver, priv->offset, priv->dlen, priv->nelm, priv->irqvec, record->name);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        regDevPrintErr("input I/O Intr unsupported for device %s",
            device->name);
        return S_dev_badRequest;
    }
    if (*ppvt == NULL)
    {
        if (priv->irqvec != -1)
            regDevPrintErr("no input I/O Intr for device %s interrupt vector %#x",
                device->name, priv->irqvec);
        else
            regDevPrintErr("no input I/O Intr for device %s",
                device->name);
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
    assert(device != NULL);

    regDevDebugLog(DBG_INIT, "%s %s prio=%d irqvec=%d cmd=%d\n",
        device->name, record->name, record->prio, priv->irqvec, cmd);

    if (device->support->getOutScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getOutScanPvt(
            device->driver, priv->offset, priv->dlen, priv->nelm, priv->irqvec, record->name);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        regDevPrintErr("output I/O Intr unsupported for device %s",
            device->name);
        return S_dev_badRequest;
    }
    if (*ppvt == NULL)
    {
        if (priv->irqvec != -1)
            regDevPrintErr("no output I/O Intr for device %s interrupt vector %#x",
                device->name, priv->irqvec);
        else
            regDevPrintErr("no output I/O Intr for device %s",
                device->name);
        return S_dev_badArgument;
    }
    return S_dev_success;
}

/*********  Report routine ********************************************/

long regDevReport(int level)
{
    regDeviceNode* device;

    if (!registeredDevices)
    {
        printf("no registered devices\n");
        return S_dev_success;
    }
    printf("registered devices:\n");
    for (device = registeredDevices; device; device = device->next)
    {
        if (device->support)
        {
            size_t size = device->size;
            printf(" \"%s\" size ", device->name);
            if (size)
            {
                printf("%"Z"d", size);
                if (size > 9) printf("=0x%"Z"x", size);
                if (size > 1024*1024)
                    printf("=%"Z"dMiB", size >> 20);
                else if (size > 1024)
                    printf("=%"Z"dKiB", size >> 10);
            }
            else
                printf("unknown");
                
            if (device->blockBuffer)
                printf(" block@%p", device->blockBuffer);
            
            if (device->support->report)
            {
                printf(" ");
                epicsMutexLock(device->accesslock);
                device->support->report(device->driver, level);
                epicsMutexUnlock(device->accesslock);
            }
            else
                printf("\n");
        }
    }
    return S_dev_success;
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

    regDevDebugLog(DBG_INIT, "%s\n", record->name);
    priv = callocMustSucceed(1, sizeof(regDevPrivate),"regDevAllocPriv");
    priv->magic = MAGIC_PRIV;
    priv->dtype = epicsInt16T;
    priv->arraypacking = 1;
    priv->irqvec=-1;
    priv->updating = 0;
    priv->nelm = 1;
    record->dpvt = priv;
    return priv;
}

int regDevAssertType(dbCommon *record, int allowedTypes)
{
    unsigned short dtype;

    regDevGetPriv();
    dtype = priv->dtype;

    regDevDebugLog(DBG_INIT, "%s: allows%s%s%s%s and uses %s\n",
        record->name,
        allowedTypes & TYPE_INT ? " INT" : "",
        allowedTypes & TYPE_FLOAT ? " FLOAT" : "",
        allowedTypes & TYPE_STRING ? " STRING" : "",
        allowedTypes & TYPE_BCD ? " BCD" : "",
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
    regDevDebugLog(DBG_INIT, "%s FTVL=%s\n",
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
    int status = S_dev_badArgument;

    regDevGetPriv();
    assert(priv != NULL);
    device = priv->device;
    assert(device != NULL);
    priv->nelm = nelm;

    switch (priv->dtype)
    {
        case epicsFloat64T:
            if (ftvl == DBF_DOUBLE)
                status = S_dev_success;
            break;
        case epicsFloat32T:
            if (ftvl == DBF_FLOAT)
                status = S_dev_success;
            break;
        case epicsStringT:
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                if (priv->L <= 0 || priv->L > nelm)
                    priv->L = nelm;
                status = S_dev_success;
            }
            break;
        case regDevBCD8T:
        case epicsInt8T:
        case epicsUInt8T:
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
                status = S_dev_success;
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
        case regDevBCD16T:
        case epicsInt16T:
        case epicsUInt16T:
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
                status = S_dev_success;
            else if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 2;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
        case regDevBCD32T:
        case epicsInt32T:
        case epicsUInt32T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
                status = S_dev_success;
            else if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 2;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 4;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
        case regDev64T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
            {
                priv->arraypacking = 2;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 4;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 8;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
    }
    regDevDebugLog(DBG_INIT, "%s: %s[%i] dtyp=%s dlen=%d arraypacking=%d status=%d\n",
        record->name,
        pamapdbfType[ftvl].strvalue+4,
        nelm,
        regDevTypeName(priv->dtype),
        priv->dlen,
        priv->arraypacking,
        status);
    if (status == S_dev_badArgument)
        fprintf(stderr,
            "regDevCheckType %s: data type %s does not match FTVL %s\n",
             record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue+4);
    return status;
}

/*********  Work dispatcher thread ****************************/

struct regDevWorkMsg {
    unsigned int cmd;
    size_t offset;
    epicsUInt8 dlen;
    size_t nelem;
    void* buffer;
    void* mask;
    regDevTransferComplete callback;
    dbCommon* record;
};

struct regDevDispatcher {
    epicsThreadId tid[NUM_CALLBACK_PRIORITIES];
    epicsMessageQueueId qid[NUM_CALLBACK_PRIORITIES];
    unsigned int maxEntries;
};


void regDevWorkThread(regDeviceNode* device)
{
    regDevDispatcher *dispatcher = device->dispatcher;
    const regDevSupport* support = device->support;
    regDevice *driver = device->driver;
    struct regDevWorkMsg msg;
    int status;
    int prio;

    regDevDebugLog(DBG_INIT, "%s: thread \"%s\" starting\n",
        device->name, epicsThreadGetNameSelf());

    prio = epicsThreadGetPrioritySelf();
    if (prio == epicsThreadPriorityLow) prio = 0;
    else
    if (prio == epicsThreadPriorityMedium) prio = 1;
    else
    if (prio == epicsThreadPriorityHigh) prio = 2;
    else
    {
        errlogPrintf("regDevWorkThread %s: illegal priority %d\n",
            epicsThreadGetNameSelf(), prio);
        return;
    }
    regDevDebugLog(DBG_INIT, "%s: prio %d qid=%p\n",
        epicsThreadGetNameSelf(), prio, dispatcher->qid[prio]);

    while (1)
    {
        epicsMessageQueueReceive(dispatcher->qid[prio], &msg, sizeof(msg));
        switch (msg.cmd)
        {
            case CMD_WRITE:
                regDevDebugLog(DBG_OUT, "%s %s: doing dispatched %swrite\n",
                    epicsThreadGetNameSelf(), msg.record->name, device->blockModes & REGDEV_BLOCK_WRITE ? "block " : "");
                epicsMutexLock(device->accesslock);
                if (device->blockModes & REGDEV_BLOCK_WRITE)
                    status = support->write(driver, 0, 1, device->size,
                        device->blockBuffer, NULL, prio, NULL, msg.record->name);
                else
                    status = support->write(driver, msg.offset, msg.dlen, msg.nelem,
                        msg.buffer, msg.mask, prio, NULL, msg.record->name);
                epicsMutexUnlock(device->accesslock);
                break;
            case CMD_READ:
                regDevDebugLog(DBG_IN, "%s %s: doing dispatched %sread\n",
                    epicsThreadGetNameSelf(), msg.record->name, device->blockModes & REGDEV_BLOCK_READ ? "block " : "");
                epicsMutexLock(device->accesslock);
                if (device->blockModes & REGDEV_BLOCK_READ)
                    status = support->read(driver, 0, 1, device->size,
                        device->blockBuffer, prio, NULL, msg.record->name);
                else
                    status = support->read(driver, msg.offset, msg.dlen, msg.nelem,
                        msg.buffer, prio, NULL, msg.record->name);
                epicsMutexUnlock(device->accesslock);
                break;
            case CMD_EXIT:
                regDevDebugLog(DBG_INIT, "%s: stopped\n",
                    epicsThreadGetNameSelf());
#ifndef EPICS_3_13
                epicsThreadSuspendSelf();
#endif
                return;
            default:
                errlogPrintf("%s: illegal command 0x%x\n",
                    epicsThreadGetNameSelf(), msg.cmd);
                continue;
        }
        msg.callback(msg.record->name, status);
    }
}

void regDevWorkExit(regDeviceNode* device)
{
    struct regDevWorkMsg msg;
    regDevDispatcher *dispatcher = device->dispatcher;
    int prio;

    /* destroying the queue cancels all pending requests and terminates the work threads [not true] */
    msg.cmd = CMD_EXIT;
    for (prio = 0; prio < NUM_CALLBACK_PRIORITIES; prio++)
    {
        if (dispatcher->qid[prio])
        {
            regDevDebugLog(DBG_INIT, "%s: sending stop message to prio %d thread\n",
                device->name, prio);
            epicsMessageQueueSend(dispatcher->qid[prio], &msg, sizeof(msg));
        }
    }

    /* wait until work threads have terminated */
    for (prio = 0; prio < NUM_CALLBACK_PRIORITIES; prio++)
    {
        if (dispatcher->tid[prio])
        {
            regDevDebugLog(DBG_INIT, "%s: waiting for prio %d thread to stop\n",
                device->name, prio);
            while (!epicsThreadIsSuspended(dispatcher->tid[prio]))
                epicsThreadSleep(0.1);
            regDevDebugLog(DBG_INIT, "%s: done\n", device->name);
        }
    }
}

int regDevStartWorkQueue(regDeviceNode* device, unsigned int prio)
{
    regDevDispatcher *dispatcher = device->dispatcher;
    if (prio >= 3) prio = 2;
    dispatcher->qid[prio] = epicsMessageQueueCreate(dispatcher->maxEntries, (unsigned int)sizeof(struct regDevWorkMsg));
    dispatcher->tid[prio] = epicsThreadCreate(device->name,
        ((int[3]){epicsThreadPriorityLow, epicsThreadPriorityMedium, epicsThreadPriorityHigh})[prio],
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC) regDevWorkThread, device);
    return dispatcher->tid[prio] != NULL ? S_dev_success : S_dev_internal;
}

int regDevInstallWorkQueue(regDevice* driver, unsigned int maxEntries)
{
    regDeviceNode* device = regDevGetDeviceNode(driver);

    regDevDebugLog(DBG_INIT, "%s: maxEntries=%u\n", device->name, maxEntries);

    device->dispatcher = callocMustSucceed(1, sizeof(regDevDispatcher), "regDevInstallWorkQueue");
    device->dispatcher->maxEntries = maxEntries;
    
    /* actual work queues and threads are created when needed */

    epicsAtExit((void(*)(void*))regDevWorkExit, device);
    return S_dev_success;
}

/*********  DMA buffers ****************************/

int regDevAllocBuffer(regDeviceNode* device, const char* name, void** bptr, size_t size)
{
    void* ptr = NULL;
    if (device->dmaAlloc)
    {
        ptr = device->dmaAlloc(device->driver, NULL, size);
        if (ptr == NULL)
        {
            fprintf(stderr,
                "regDevAllocBuffer %s: allocating device memory failed.\n",
                name);
            return S_dev_noMemory;
        }
        memset(ptr, 0, size);
        *bptr = ptr;
        return S_dev_success;
    }
    ptr = (char *)calloc(1, size);
    if (ptr == NULL)
    {
        fprintf(stderr,
            "regDevAllocBuffer %s: out of memory.\n",
            name);
        return S_dev_noMemory;
    }
    *bptr = ptr;
    return S_dev_success;
}

int regDevMemAlloc(dbCommon* record, void** bptr, size_t size)
{
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert(device != NULL);
    return regDevAllocBuffer(device, record->name, bptr, size);
}

int regDevMakeBlockdevice(regDevice* driver, unsigned int modes, int swap, void* buffer)
{
    int status;
    regDeviceNode* device = regDevGetDeviceNode(driver);
    if (modes & (REGDEV_BLOCK_READ|REGDEV_BLOCK_WRITE))
    {
        if (buffer)
            device->blockBuffer = buffer;
        else
        {
            status = regDevAllocBuffer(device, device->name, &device->blockBuffer, device->size);
            if (status != S_dev_success) return status;
        }
        if (modes & REGDEV_BLOCK_READ) scanIoInit(&device->blockReceived);
    }
    device->blockSwap = swap;
    device->blockModes = modes;
    return S_dev_success;
}

static int atInit = 1;
long regDevInit(int finished)
{
    if (atInit && finished)
    {
        atInit = 0;
        regDevDebugLog(DBG_INIT, "init finished\n");
    }
    return S_dev_success;
}

int regDevGetOffset(dbCommon* record, epicsUInt8 dlen, size_t nelem, size_t *poffset)
{
    int status;
    size_t offset;
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device = priv->device;

    /* For readback we may use a different offset */
    if ((atInit || priv->updating) && priv->rboffset != DONT_INIT)
    {
        offset = priv->rboffset;
    }
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
            ptrdiff_t off = offset;

            status = dbGetField(priv->offsetRecord, DBR_LONG, &buffer, &options, NULL, NULL);
            if (status == S_dev_success && buffer.severity == INVALID_ALARM) status = S_dev_badArgument;
            if (status != S_dev_success)
            {
                recGblSetSevr(record, LINK_ALARM, INVALID_ALARM);
                errlogPrintf("%s: cannot read offset from '%s'\n",
                    record->name, priv->offsetRecord->precord->name);
                return status;
            }
            off += buffer.i * priv->offsetScale;
            if (off < 0)
            {
                errlogPrintf("%s: effective offset '%s'=%d * %"Z"d + %"Z"u = %"Z"d < 0\n",
                    record->name, priv->offsetRecord->precord->name,
                    buffer.i, priv->offsetScale, offset, off);
                return S_dev_badSignalNumber;
            }
            offset = off;
        }
    }
    if (atInit) regDevDebugLog(DBG_INIT, "%s: init from offset 0x%"Z"x\n",
        record->name, offset);
    if (priv->updating) regDevDebugLog(DBG_IN, "%s: update from offset 0x%"Z"x\n",
        record->name, offset);

    if (device->size) /* check offset range if size is provided */
    {
        if (offset > device->size)
        {
            errlogPrintf("%s: offset 0x%"Z"x out of range of device %s (0-0x%"Z"x)\n",
                record->name, offset, device->name, device->size-1);
            return S_dev_badSignalNumber;
        }
        if (offset + dlen * nelem > device->size)
        {
            errlogPrintf("%s: offset 0x%"Z"x + 0x%"Z"x bytes length exceeds device %s size 0x%"Z"x by 0x%"Z"x bytes\n",
                record->name, offset, nelem*dlen, device->name, device->size,
                offset + dlen * nelem - device->size);
            return S_dev_badSignalCount;
        }
    }
    *poffset = offset;
    return S_dev_success;
}

/*********  I/O functions ****************************/

void regDevCallback(const char* user, int status)
{
    dbCommon* record = (dbCommon*)(user - offsetof(dbCommon, name));
    regDevPrivate* priv;

    assert(user != NULL);
    priv = record->dpvt;
    assert(priv != NULL);
    assert(priv->magic == MAGIC_PRIV);

    priv->status = status;

    dbScanLock(record);
    if (!record->pact) regDevPrintErr("callback for non-active record!");
    if (priv->updating)
    {
        if (status != S_dev_success)
        {
            regDevDebugLog(DBG_IN, "%s: async update failed. status=0x%x",
                record->name, status);
        }
        priv->updater(record);
        priv->updating = 0;
    }
    else
    {
        (*record->rset->process)(record);
    }
    dbScanUnlock(record);
}

int regDevRead(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buffer)
{
    /* buffer must not point to local variable: not suitable for async processing */

    int status = S_dev_success;
    regDeviceNode* device;
    size_t offset;

    regDevGetPriv();
    device = priv->device;
    assert(device != NULL);
    assert(buffer != NULL || nelem == 0 || dlen == 0);

    regDevDebugLog(DBG_IN, "%s: dlen=%u, nelm=%"Z"u, buffer=%p\n",
        record->name, dlen, nelem, buffer);

    if (!device->support->read)
    {
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_IN, "%s: device %s has no read function\n",
            record->name, device->name);
        return S_dev_badRequest;
    }

    if (record->pact)
    {
        /* Second call of asynchronous device */

        regDevDebugLog(DBG_IN, "%s: asynchronous read returned 0x%x\n",
            record->name, priv->status);
        if (priv->status != S_dev_success)
        {
            recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
            return priv->status;
        }
        offset = priv->asyncOffset;
    }
    else
    {
        /* First call of (possibly asynchronous) device */
        status = regDevGetOffset(record, dlen, nelem, &offset);
        if (status != S_dev_success)
            return status;
            
        if (!(device->blockModes & REGDEV_BLOCK_READ) || record->prio == 2)
        {
            record->pact = 1;
            priv->asyncOffset = offset;
            priv->status = S_dev_success;
            if (device->dispatcher && !atInit)
            {
                struct regDevWorkMsg msg;

                msg.cmd = CMD_READ;
                msg.offset = offset;
                msg.dlen = dlen;
                msg.nelem = nelem;
                msg.buffer = buffer;
                msg.callback = regDevCallback;
                msg.record = record;
                if (!device->dispatcher->qid[record->prio])
                {
                    regDevDebugLog(DBG_IN, "%s: starting %s prio %d dispatcher\n",
                        record->name, device->name, record->prio);
                    if (regDevStartWorkQueue(device, record->prio) != S_dev_success)
                    {
                        record->pact = 0;
                        return S_dev_badRequest;
                    }
                }
                regDevDebugLog(DBG_IN, "%s: sending read to %s prio %d dispatcher\n",
                    record->name, device->name, record->prio);
                if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], (char*)&msg, sizeof(msg)) != 0)
                {
                    recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
                    regDevDebugLog(DBG_IN, "%s: work queue is full\n", record->name);
                    record->pact = 0;
                    return S_dev_noMemory;
                }
                status = ASYNC_COMPLETION;
            }
            else
            {
                regDevDebugLog(DBG_IN, "%s: reading %sfrom %s\n",
                    record->name, device->blockModes & REGDEV_BLOCK_READ ? "block " : "", device->name);
                epicsMutexLock(device->accesslock);
                if (device->blockModes & REGDEV_BLOCK_READ)
                    status = device->support->read(device->driver,
                        0, 1, device->size, device->blockBuffer,
                        2, atInit ? NULL : regDevCallback, record->name);
                else
                    status = device->support->read(device->driver,
                        offset, dlen, nelem, buffer,
                        record->prio, atInit ? NULL : regDevCallback, record->name);
                epicsMutexUnlock(device->accesslock);
                regDevDebugLog(DBG_IN, "%s: read returned status 0x%0x\n", record->name, status);
            }
        }
    }

    if (status == S_dev_success)
    {
        record->udf = FALSE;
        if (device->blockModes & REGDEV_BLOCK_READ)
        {
            if (buffer)
            {
                if (buffer < device->blockBuffer || buffer >= device->blockBuffer+device->size || offset != priv->offset)
                {
                    /* copy block buffer to record (if not mapped) */
                    regDevDebugLog(DBG_IN, "%s: copy %"Z"u * %u bytes from %s block buffer %p+0x%"Z"x to record buffer %p\n",
                        record->name, nelem, dlen, device->name, device->blockBuffer, offset, buffer);
                    regDevCopy(dlen, nelem, device->blockBuffer+offset, buffer, NULL, device->blockSwap);
                }
                else
                {
                    regDevDebugLog(DBG_IN, "%s: %"Z"u * %u bytes mapped in %s block buffer %p+0x%"Z"x\n",
                        record->name, nelem, dlen, device->name, device->blockBuffer, offset);
                }
            }
            if (record->prio == 2 && !atInit)
            {
                /* inform other records of new block input */
                scanIoRequest(device->blockReceived);
            }
        }
    }
    
    /* Some debug output */
    if (regDevDebug & DBG_IN)
    {
        if (status == ASYNC_COMPLETION)
        {
            printf("%s %s: async read %"Z"u * %u bit from %s:0x%"Z"x\n",
                _CURRENT_FUNCTION_, record->name, nelem, dlen*8,
                device->name, priv->asyncOffset);
        }
        else if (buffer) switch (dlen)
        {
            case 1:
                regDevDebugLog(DBG_IN,
                    "%s: read %"Z"u * 8 bit 0x%02x from %s:0x%"Z"x (status=%x)\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    device->name, offset, status);
                break;
            case 2:
                regDevDebugLog(DBG_IN,
                    "%s: read %"Z"u * 16 bit 0x%04x from %s:0x%"Z"x (status=%x)\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset, status);
                break;
            case 4:
                regDevDebugLog(DBG_IN,
                    "%s: read %"Z"u * 32 bit 0x%08x from %s:0x%"Z"x (status=%x)\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset, status);
                break;
            case 8:
                regDevDebugLog(DBG_IN,
                    "%s: read %"Z"u * 64 bit 0x%016llx from %s:0x%"Z"x (status=%x)\n",
                    record->name, nelem, (unsigned long long)*(epicsUInt64*)buffer,
                    device->name, offset, status);
                break;
            default:
                regDevDebugLog(DBG_IN,
                    "%s: read %"Z"u * %d bit from %s:0x%"Z"x (status=%x)\n",
                    record->name, nelem, dlen*8,
                    device->name, offset, status);
        }
    }

    if (status == ASYNC_COMPLETION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_IN, "%s: wait for asynchronous read completition\n", record->name);
        return status;
    }

    record->pact = 0;
    if (status != S_dev_success && nelem != 0) /* nelem == 0 => only status readout */
    {
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_IN, "%s: read error\n", record->name);
    }
    return status;
}

int regDevWrite(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buffer, void* mask)
{
    /* buffer must not point to local variable: not suitable for async processing */

    int status;
    size_t offset;
    regDeviceNode* device;

    regDevGetPriv();
    device = priv->device;
    assert(device != NULL);
    assert(buffer != NULL || nelem == 0 || dlen == 0);

    regDevDebugLog(DBG_OUT, "%s: dlen=%u, nelm=%"Z"u, buffer=%p mask=%p\n",
        record->name, dlen, nelem, buffer, mask);

    if (!device->support->write)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "%s: device %s has no write function\n",
            record->name, device->name);
        return S_dev_badRequest;
    }

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

    /* First call of (possibly asynchronous) device */
    status = regDevGetOffset(record, dlen, nelem, &offset);
    if (status != S_dev_success) return status;

    /* Some debug output */
    if (regDevDebug & DBG_OUT)
    {
        if (buffer) switch (dlen+(mask?10:0))
        {
            case 1:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 8 bit 0x%02x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    device->name, offset);
                break;
            case 2:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 16 bit 0x%04x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset);
                break;
            case 4:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 32 bit 0x%08x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset);
                break;
            case 8:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 64 bit 0x%016llx to %s:0x%"Z"x\n",
                    record->name, nelem, (unsigned long long)*(epicsUInt64*)buffer,
                    device->name, offset);
                break;
            case 11:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 8 bit 0x%02x mask 0x%02x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt8*)buffer, *(epicsUInt8*)mask,
                    device->name, offset);
            case 12:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 16 bit 0x%04x mask 0x%04x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt16*)buffer, *(epicsUInt16*)mask,
                    device->name, offset);
                break;
            case 14:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 32 bit 0x%08x mask 0x%08x to %s:0x%"Z"x\n",
                    record->name, nelem, *(epicsUInt32*)buffer, *(epicsUInt32*)mask,
                    device->name, offset);
                break;
            case 18:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * 64 bit 0x%016llx mask 0x%016llx to %s:0x%"Z"x\n",
                    record->name, nelem, (unsigned long long)*(epicsUInt64*)buffer, (unsigned long long)*(epicsUInt64*)mask,
                    device->name, offset);
                break;
            default:
                regDevDebugLog(DBG_OUT,
                    "%s: write %"Z"u * %d bit to %s:0x%"Z"x\n",
                    record->name, nelem, dlen*8,
                    device->name, offset);
        }
    }
    
    if (device->blockModes & REGDEV_BLOCK_WRITE)
    {
        if (buffer)
        {
            if (buffer < device->blockBuffer || buffer >= device->blockBuffer+device->size)
            {
                /* copy record to block buffer (if not mapped) */
                regDevDebugLog(DBG_OUT, "%s: copy %"Z"u * %u bytes from record buffer %p to %s block buffer %p+0x%"Z"x\n",
                    record->name, nelem, dlen, buffer, device->name, device->blockBuffer, offset);
                regDevCopy(dlen, nelem, buffer, device->blockBuffer+offset, mask, device->blockSwap);
            }
            else
            {
                regDevDebugLog(DBG_OUT, "%s: %"Z"u * %u bytes mapped in %s block buffer %p+0x%"Z"x\n",
                    record->name, nelem, dlen, device->name, device->blockBuffer, offset);
            }
        }
        if (record->prio != 2)
            return S_dev_success;
    }

    priv->status = S_dev_success;

    record->pact = 1;
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
        if (!device->dispatcher->qid[record->prio])
        {
            regDevDebugLog(DBG_OUT, "%s: starting %s prio %d dispatcher\n",
                record->name, device->name, record->prio);
            if (regDevStartWorkQueue(device, record->prio) != S_dev_success)
            {
                record->pact = 0;
                return S_dev_badRequest;
            }
        }
        regDevDebugLog(DBG_OUT, "%s: sending write to %s prio %d dispatcher\n",
            record->name, device->name, record->prio);
        if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], (char*)&msg, sizeof(msg)) != 0)
        {
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevDebugLog(DBG_OUT, "%s: work queue is full\n", record->name);
            record->pact = 0;
            return S_dev_noMemory;
        }
        status = ASYNC_COMPLETION;
    }
    else
    {
        regDevDebugLog(DBG_OUT, "%s: writing %sto %s\n",
            record->name, device->blockModes & REGDEV_BLOCK_WRITE ? "block " : "", device->name);
        epicsMutexLock(device->accesslock);
        if (device->blockModes & REGDEV_BLOCK_WRITE)
            status = device->support->write(device->driver,
                0, 1, device->size, device->blockBuffer, NULL,
                2, regDevCallback, record->name);
        else
            status = device->support->write(device->driver,
                offset, dlen, nelem, buffer, mask,
                record->prio, regDevCallback, record->name);
        regDevDebugLog(DBG_OUT, "%s: write returned status 0x%0x\n", record->name, status);
        epicsMutexUnlock(device->accesslock);
    }

    if (status == ASYNC_COMPLETION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: wait for asynchronous write completition\n", record->name);
        return status;
    }

    record->pact = 0;
    if (status != S_dev_success)
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
            rv = (epicsInt32)priv->data.uval64; /* cut off high bits */
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

    if (atInit)
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
    regDevDebugLog(DBG_OUT, "%s: rval=%d (0x%08x), fval=%#g\n",
        record->name, rval, rval, fval);

    /* enforce bounds */
    switch (priv->dtype)
    {
        case epicsFloat32T:
        case epicsFloat64T:
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            if ((epicsUInt32)rval > (epicsUInt32)priv->H)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %u (0x%08x) to upper bound %u (0x%08x)\n",
                    record->name, rval, rval, priv->H, priv->H);
                rval = priv->H;
            }
            if ((epicsUInt32)rval < (epicsUInt32)priv->L)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %u (0x%08x) to lower bound %u (0x%08x)\n",
                    record->name, rval, rval, priv->L, priv->L);
                rval = priv->L;
            }
            break;
        default:
            if (rval > priv->H)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %d (0x%08x) to upper bound %d (0x%08x)\n",
                    record->name, rval, rval, priv->H, priv->H);
                rval = priv->H;
            }
            if (rval < priv->L)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %d (0x%08x) to lower bound %d (0x%08x)\n",
                    record->name, rval, rval, priv->L, priv->L);
                rval = priv->L;
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
            priv->data.fval32 = (float)fval;
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

int regDevReadBits(dbCommon* record, epicsUInt32* rval)
{
    int status = S_dev_success;
    epicsUInt32 rv = 0;

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
            rv = (epicsUInt32)priv->data.uval64; /* cut off high bits */
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data %s type requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }

    if (atInit)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }

    assert(rval != NULL);
    *rval = rv ^ priv->invert;
    return S_dev_success;
}

int regDevWriteBits(dbCommon* record, epicsUInt32 rval, epicsUInt32 mask)
{
    regDevGetPriv();
    regDevDebugLog(DBG_OUT, "%s: rval=0x%08x, mask=0x%08x)\n",
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
    unsigned int i;
    epicsUInt8 dlen;
    epicsUInt8 packing;

    regDevGetPriv();

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->L contains string length */
        if (nelm > (size_t)priv->L)
            nelm = (size_t)priv->L;
    }
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
        /* read packed array */
        status = regDevRead(record,
            dlen, nelm/priv->arraypacking, priv->data.buffer);
    }

    if (status != S_dev_success) return status;

    switch (priv->dtype)
    {
        case regDevBCD8T:
        {
            epicsUInt8* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt8)bcd2i(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt16)bcd2i(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt32)bcd2i(buffer[i]);
            break;
        }
    }
    return S_dev_success;
}

int regDevWriteArray(dbCommon* record, size_t nelm)
{
    int status = 0;
    unsigned int i;
    epicsUInt8 dlen;
    int packing;

    regDevGetPriv();

    regDevDebugLog(DBG_OUT, "%s: nelm=%"Z"d dlen=%d\n", record->name, nelm, priv->dlen);

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->L contains string length */
        if (nelm > (size_t)priv->L)
            nelm = (size_t)priv->L;
        regDevDebugLog(DBG_OUT, "%s: string length %"Z"d\n", record->name, nelm);
    }
    dlen = priv->dlen;

    switch (priv->dtype)
    {
        case regDevBCD8T:
        {
            epicsUInt8* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt8)i2bcd(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt16)i2bcd(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = (epicsUInt32)i2bcd(buffer[i]);
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
            if (status != S_dev_success) break;
        }
    }
    else
    {
        status = regDevWrite(record,
            dlen, nelm/priv->arraypacking, priv->data.buffer, NULL);
    }
    return status;
}

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high)
{
    double o, s;
    size_t i;

    regDevGetPriv();

    s = (double)priv->H - priv->L;
    o = (priv->H * low - priv->L * high) / s;
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
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
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
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
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
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
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
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
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
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            /* we need to care more about the type of H and L here */
            epicsUInt32* r = priv->data.buffer;

            s = (double)(epicsUInt32)priv->H - (epicsUInt32)priv->L;
            o = ((epicsUInt32)priv->H * low - (epicsUInt32)priv->L * high) / s;
            s = (high - low) / s;

            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = r[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (float)(r[i]*s+o);
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

    s = (double)priv->H - priv->L;
    o = (priv->L * high - priv->H * low) / s;
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
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt8)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt8)x;
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
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsUInt8)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsUInt8)x;
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
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt16)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt16)x;
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
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsUInt16)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsUInt16)x;
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
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt32)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = priv->L;
                    if (x > priv->H) x = priv->H;
                    r[i] = (epicsInt32)x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            /* we need to care more about the type of H and L here */
            epicsUInt32* r = priv->data.buffer;

            s = (double)(epicsUInt32)priv->H - (epicsUInt32)priv->L;
            o = ((epicsUInt32)priv->L * high - (epicsUInt32)priv->H * low) / s;
            s = s / (high - low);

            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < (epicsUInt32)priv->L) x = (epicsUInt32)priv->L;
                    if (x > (epicsUInt32)priv->H) x = (epicsUInt32)priv->H;
                    r[i] = (epicsUInt32)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < (epicsUInt32)priv->L) x = (epicsUInt32)priv->L;
                    if (x > (epicsUInt32)priv->H) x = (epicsUInt32)priv->H;
                    r[i] = (epicsUInt32)x;
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

void regDevRunUpdater(dbCommon* record)
{
    int status;
    regDevPrivate* priv = record->dpvt;

    if (interruptAccept && !record->pact) /* scanning allowed? */
    {
        dbScanLock(record);
        if (!record->pact)
        {
            regDevDebugLog(DBG_IN, "%s: updating record\n",
                record->name);
            priv->updating = 1;
            status = priv->updater(record);
            if (!record->pact)
            {
                priv->updating = 0;
                if (status != S_dev_success)
                {
                    regDevDebugLog(DBG_IN, "%s: update failed. status=0x%x",
                        record->name, status);
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
    assert(device != NULL);

    if (priv->update && device->support->read)
    {
        regDevDebugLog(DBG_INIT, "%s\n", record->name);
        if (!device->updateTimerQueue)
        {
            device->updateTimerQueue = epicsTimerQueueAllocate(1, epicsThreadPriorityLow);
            if (!device->updateTimerQueue)
            {
                regDevPrintErr("epicsTimerQueueAllocate failed");
                return S_dev_noMemory;
            }
        }
        /* install periodic update function */
        regDevDebugLog(DBG_INIT, "%s: install update every %f seconds\n", record->name, priv->update * 0.001);
        priv->updater = updater;
        priv->updateTimer = epicsTimerQueueCreateTimer(device->updateTimerQueue,
            (epicsTimerCallback)regDevRunUpdater, record);
        epicsTimerStartDelay(priv->updateTimer, priv->update * 0.001);
    }
    return S_dev_success;
}

/*********  Shell utilities ****************************/

int regDevDisplay(const char* devName, int start, unsigned int dlen, size_t bytes)
{
    static size_t offset = 0;
    static size_t save_bytes = 128;
    static unsigned int save_dlen = 2;
    static regDeviceNode* save_device = NULL;
    static unsigned char* buffer = NULL;
    static size_t bufferSize = 0;

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
    if (start >= 0 || dlen || bytes) offset = start;
    if (dlen) save_dlen = dlen; else dlen = save_dlen;
    if (bytes) save_bytes = bytes; else bytes = save_bytes;

    if (device->size)
    {
        if (offset >= device->size)
        {
            errlogPrintf("address 0x%"Z"x out of range\n", offset);
            return S_dev_badArgument;
        }
        if (offset + bytes > device->size)
        {
            bytes = device->size - offset;
        }
    }
    nelem = bytes/dlen;

    if (device->blockBuffer)
    {
        printf("block buffer:\n");
        memDisplay(offset, device->blockBuffer+offset, dlen, dlen * nelem);
        offset += dlen * nelem;
        return S_dev_success;
    }
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

    if (!device->support->read)
    {
        errlogPrintf("device has no read method\n");
        return S_dev_badRequest;
    }
    
    epicsMutexLock(device->accesslock);
    status = device->support->read(device->driver,
        offset, dlen, nelem, buffer, 2, NULL, "regDevDisplay");
    epicsMutexUnlock(device->accesslock);
    if (status != S_dev_success)
    {
        printf("read error 0x%x\n", status);
        return status;
    }

    memDisplay(offset, buffer, dlen, dlen * nelem);
    offset += dlen * nelem;
    return S_dev_success;
}

int regDevPut(const char* devName, int offset, unsigned int dlen, int value)
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
        epicsMutexLock(device->accesslock);
        status = device->support->write(device->driver,
            offset, dlen, 1, &buffer, NULL, 0, NULL, "regDevPut");
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        errlogPrintf("device has no write method\n");
        status = S_dev_badRequest;
    }
    return status;
}

#ifndef EPICS_3_13
#include <iocsh.h>
static const iocshArg regDevDisplayArg0 = { "devName", iocshArgString };
static const iocshArg regDevDisplayArg1 = { "start", iocshArgString };
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
        args[0].sval, args[1].sval ? strtol(args[1].sval, NULL, 0) : -1, args[2].ival, args[3].ival);
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
