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
#include <epicsStdioRedirect.h>

#include "memDisplay.h"

#include "regDevSup.h"

#define MAGIC_PRIV 2181699655U /* crc("regDev") */
#define MAGIC_NODE 2055989396U /* crc("regDeviceNode") */
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_EXIT 4

#if defined __USE_XOPEN2K8
#undef epicsMutexMustCreate
static epicsMutexId epicsMutexMustCreate() {
    static pthread_mutexattr_t attr;
    static int initialized = 0;
    pthread_mutex_t *lock;
    if (!initialized) {
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        initialized = 1;
        printf("regDev uses PTHREAD_PRIO_INHERIT mutex\n");
    }
    lock = callocMustSucceed(1, sizeof(pthread_mutex_t), "epicsMutexMustCreate");
    pthread_mutex_init(lock, &attr);
    return (epicsMutexId)lock;
}
#define epicsMutexLock(lock) pthread_mutex_lock((pthread_mutex_t*)lock);
#define epicsMutexUnlock(lock) pthread_mutex_unlock((pthread_mutex_t*)lock);
#endif

static regDeviceNode* registeredDevices = NULL;

epicsShareDef int regDevDebug = 0;
epicsExportAddress(int, regDevDebug);

#define regDevGetPriv() \
    regDevPrivate* priv = record->dpvt; \
    if (priv == NULL) { \
        regDevPrintErr("record not initialized"); \
        return S_dev_badInit; } \
    assert(priv->magic == MAGIC_PRIV)

static int startswith(const unsigned char *s, const char *key)
{
    int n = 0;
    while (*key) {
        if (*key != tolower(*s)) return 0;
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
 * <name>:<addr>[:[init]] [T=<type>] [B=<bit>] [I=<invert>] [M=<mask>] [L=<low|strLen>] [H=<high>] [P=<packing>] [F=<feed>] [U=<update>]
 *
 * where: <name>    - symbolic device name
 *        <addr>    - address (byte number) within memory block
 *                    (expressions containing +-*() allowed, no spaces!)
 *        <init>    - optional init read address ( for output records )
 *        <type>    - data type, see table below
 *        <bit>     - bit number (least significant bit is 0)
 *        <invert>  - mask of inverted bits
 *        <mask>    - mask of valid bits
 *        <strLen>  - string length
 *        <low>     - raw value that mapps to EGUL
 *        <high>    - raw value that mapps to EGUF
 *        <packing> - number of array values in one fifo register
 *        <feed>    - bytes to the next array element of interlaces arrays
 *        <update>  - milliseconds for periodic update of output records
 **********************************************************************/

#define epicsInt64T  (98)
#define epicsUInt64T (99)
#define regDevBCD8T  (100)
#define regDevBCD16T (101)
#define regDevBCD32T (102)
#define regDevBCD64T (103)
#define regDevFirstType epicsInt64T
#define regDevLastType  regDevBCD64T

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

    { "longlong",   epicsInt64T   },
    { "int64",      epicsInt64T   },

    { "qword",      epicsUInt64T  },
    { "uint64",     epicsUInt64T  },
    { "unsign64",   epicsUInt64T  },
    { "unsigned64", epicsUInt64T  },

    { "double",     epicsFloat64T },
    { "real64",     epicsFloat64T },
    { "float64",    epicsFloat64T },

    { "single",     epicsFloat32T },
    { "real32",     epicsFloat32T },
    { "float32",    epicsFloat32T },
    { "float",      epicsFloat32T },

    { "string",     epicsStringT  },

    { "bcd8",       regDevBCD8T   },
    { "bcd16",      regDevBCD16T  },
    { "bcd32",      regDevBCD32T  },
    { "bcd64",      regDevBCD64T  },
    { "bcd",        regDevBCD8T   },
    { "time",       regDevBCD8T   } /* for backward compatibility */
};

const char* regDevTypeName(unsigned short dtype)
{
    const char* regDevTypeNames [] = {
        "epicsInt64",
        "epicsUInt64",
        "regDevBCD8",
        "regDevBCD16",
        "regDevBCD32",
        "regDevBCD64",
    };

    if (dtype > regDevLastType) return "invalid";
    return dtype < regDevFirstType ?
        epicsTypeNames[dtype] :
        regDevTypeNames[dtype-regDevFirstType];
}

epicsInt64 regDevParseExpr(unsigned char** pp);

epicsInt64 regDevParseValue(unsigned char** pp)
{
    epicsInt64 val;
    unsigned char *p = *pp;
    int neg = 0;

    while (isspace(*p)) p++;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    while (isspace(*p)) p++;
    if (*p == '(')
    {
        p++;
        val = regDevParseExpr(&p);
        if (*p == ')') p++;
    }
    else val = strtoul((char*)p, (char**)&p, 0);
    while (isspace(*p)) p++;
    *pp = p;
    return neg ? -val : val;
}

epicsInt64 regDevParseProd(unsigned char** pp)
{
    epicsInt64 val = 1;
    unsigned char *p = *pp;

    while (isspace(*p)) p++;
    while (*p == '*')
    {
        p++;
        val *= regDevParseValue(&p);
    }
    *pp = p;
    return val;
}

epicsInt64 regDevParseExpr(unsigned char** pp)
{
    epicsInt64 sum = 0;
    epicsInt64 val;
    unsigned char *p = *pp;

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
    regDevPrivate* priv,
    int types)
{
    char devName[255];
    regDeviceNode* device;
    unsigned char* p = (unsigned char*)parameterstring;
    char separator;
    size_t nchar;

    static const int maxtype = sizeof(datatypes)/sizeof(*datatypes);
    int type = 0;
    int hset = 0;
    int lset = 0;
    epicsInt64 H = 0;
    epicsInt64 L = 0;

    regDevDebugLog(DBG_INIT, "%s: \"%s\"\n", recordName, parameterstring);

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum(*p)) if (*p++ == '\0')
    {
        errlogPrintf("regDevIoParse %s: no device name in parameter string \"%s\"\n",
            recordName, parameterstring);
        return S_dev_badArgument;
    }

    /* Get device name */
    nchar = strcspn((char*)p, ":/ ");
    strncpy(devName, (char*)p, nchar);
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
        while (isspace(*p)) p++;

        if (!isdigit(*p))
        {
            /* expect record name, maybe in ' quotes, maybe in () */
            char recName[PVNAME_STRINGSZ];
            int i = 0;
            char quote = 0;
            char parenthesis = 0;

            if (*p == '(')
            {
                parenthesis = *p++;
                while (isspace(*p)) p++;
            }
            if (*p == '\'') quote = *p++;
            /* all non-whitespace chars are legal here except quote ', incl + and * */
            while (*p && !isspace(*p) && *p != quote && i < sizeof(recName)-1) recName[i++] = *p++;
            if (quote && *p == quote) p++;
            recName[i] = 0;
            priv->offsetRecord = mallocMustSucceed(sizeof (struct dbAddr), "regDevIoParse");
            if (dbNameToAddr(recName, priv->offsetRecord) != S_dev_success)
            {
                free(priv->offsetRecord);
                priv->offsetRecord = NULL;
                errlogPrintf("regDevIoParse %s: record '%s' not found\n",
                    recordName, recName);
                return S_dev_badArgument;
            }
            priv->offsetScale = regDevParseProd(&p);
            if (parenthesis == '(')
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
            errlogPrintf("regDevIoParse %s: offset %" Z "d<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        if (priv->offsetRecord)
            regDevDebugLog(DBG_INIT,
                "%s: offset='%s'*%" Z "d+%" Z "d(0x%" Z "x)\n",
                recordName, priv->offsetRecord->precord->name,
                priv->offsetScale, priv->offset, priv->offset);
        else
            regDevDebugLog(DBG_INIT,
                "%s: offset=%" Z "d(0x%" Z "x)\n",
                recordName, priv->offset, priv->offset);
        separator = *p++;
    }
    else
    {
        priv->offset = 0;
    }

    /* Check readback offset (for backward compatibility allow '!' and '/') */
    if (separator == ':' || separator == '/' || separator == '!')
    {
        unsigned char* p1;
        ptrdiff_t rboffset;

        if (!device->support->read)
        {
            errlogPrintf("regDevIoParse %s: can't read back from device without read function\n",
                recordName);
            return S_dev_wrongDevice;
        }

        while (isspace(*p)) p++;
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
                errlogPrintf("regDevIoParse %s: readback offset %" Z "d < 0\n",
                    recordName, rboffset);
                return S_dev_badArgument;
            }
            priv->rboffset = rboffset;
        }
        regDevDebugLog(DBG_INIT,
            "%s: readback offset=0x%" Z "x\n", recordName, priv->rboffset);
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
    priv->mask = 0;

    /* allow whitespaces before parameter for device support */
    while ((separator == '\t') || (separator == ' '))
        separator = *p++;
    if (separator != '\'') p--; /* optional quote for compatibility */

    /* optional parameters */
    while (p && *p)
    {
        ptrdiff_t val;
        char c = 0;
        int i;
        static const char* const parameter [] = {
            "Ttype",
            "Bbit",
            "Iinvert",
            "Iinv",
            "Mmask",
            "Llow",
            "Llo",
            "Llength",
            "Llen",
            "Hhigh",
            "Hhi",
            "Ppacking",
            "Pfifopacking",
            "Ffeed",
            "Farrayfeed",
            "Finterlace",
            "Uupdate",
            "Vvector",
            "Vvec",
            "Vivec",
            "Virqvec",
            "Virq",
            "Vintvec",
            "Vinterrupt"};

        while (isspace(*p)) p++;
        for (i = 0; i < sizeof(parameter)/sizeof(const char*); i++)
        {
            if (startswith(p, parameter[i]+1))
            {
                c = parameter[i][0];
                regDevDebugLog(DBG_INIT, "%s: verbose parameter '%.*s'=%c\n",
                    recordName, (int)strlen(parameter[i]+1), p, c);
                p += strlen(parameter[i]+1);
                break;
            }
        }
        if (!c) c = toupper(*p++);
        while (isspace(*p)) p++;
        if (*p == '=') p++;
        while (isspace(*p)) p++;
        switch (c)
        {
            case 'T': /* T=<datatype> */
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
                val = regDevParseExpr(&p);
                if (val < 0 || val >= 64)
                {
                    errlogPrintf("regDevIoParse %s: invalid bit number %" Z "d\n",
                        recordName, val);
                    return S_dev_badArgument;
                }
                priv->bit = (epicsUInt8)val;
                break;
            case 'I': /* I=<invert> */
                priv->invert = regDevParseExpr(&p);
                break;
            case 'M': /* I=<mask> */
                priv->mask = regDevParseExpr(&p);
                break;
            case 'L': /* L=<low raw value> (converts to EGUL) */
                L = regDevParseExpr(&p);
                lset = 1;
                break;
            case 'H': /* L=<high raw value> (converts to EGUF) */
                H = regDevParseExpr(&p);
                hset = 1;
                break;
            case 'P': /* P=<packing> (for fifo) */
                priv->fifopacking = (epicsUInt8)regDevParseExpr(&p);
                break;
            case 'F': /* F=<feed> offset to next element in arrays */
                priv->interlace = regDevParseExpr(&p);
                break;
            case 'U': /* U=<update period [ms]> (T = trigger by updater bo record) */
                if (toupper(*p) == 'T')
                {
                    p++;
                    priv->update = -1;
                }
                else
                    priv->update = (epicsInt32)regDevParseExpr(&p);
                break;
            case 'V': /* V=<irq vector> */
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
        case epicsUInt64T:
            priv->dlen = 8;
            if (!hset) H = 0xFFFFFFFFFFFFFFFFLL;
            break;
        case epicsInt8T:
            priv->dlen = 1;
            if (!lset) L = (types & TYPE_FLOAT) ? -0x7F : -0x80;
            if (!hset) H = 0x7F;
            break;
        case epicsInt16T:
            priv->dlen = 2;
            if (!lset) L = (types & TYPE_FLOAT) ? -0x7FFF : -0x8000;
            if (!hset) H = 0x7FFF;
            break;
        case epicsInt32T:
            priv->dlen = 4;
            if (!lset) L = (types & TYPE_FLOAT) ? -0x7FFFFFFFLL : -0x80000000LL;
            if (!hset) H = 0x7FFFFFFF;
            break;
        case epicsInt64T:
            priv->dlen = 8;
            if (!lset) L = (types & TYPE_FLOAT) ? -0x7FFFFFFFFFFFFFFFLL : -0x7FFFFFFFFFFFFFFFLL -1;
            if (!hset) H = 0x7FFFFFFFFFFFFFFFLL;
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
        case regDevBCD64T:
            priv->dlen = 8;
            if (!hset) H = 9999999999999999LL;
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

    regDevDebugLog(DBG_INIT, "%s: T=%s dlen=%d\n",  recordName, datatypes[type].name, priv->dlen);
    regDevDebugLog(DBG_INIT, "%s: L=%lld(%#llx)\n", recordName, (long long)priv->L, (long long)priv->L);
    regDevDebugLog(DBG_INIT, "%s: H=%lld(%#llx)\n", recordName, (long long)priv->H, (long long)priv->H);
    regDevDebugLog(DBG_INIT, "%s: B=%d\n",          recordName, priv->bit);
    regDevDebugLog(DBG_INIT, "%s: I=%#llx\n",       recordName, (long long)priv->invert);
    regDevDebugLog(DBG_INIT, "%s: M=%#llx\n",       recordName, (long long)priv->mask);
    regDevDebugLog(DBG_INIT, "%s: P=%lli\n",        recordName, (long long)priv->fifopacking);
    regDevDebugLog(DBG_INIT, "%s: U=%lli\n",        recordName, (long long)priv->update);
    regDevDebugLog(DBG_INIT, "%s: V=%lli\n",        recordName, (long long)priv->irqvec);
    regDevDebugLog(DBG_INIT, "%s: F=%llu(%#llx)\n", recordName, (unsigned long long)priv->interlace,
                                                                (unsigned long long)priv->interlace);
    return S_dev_success;
}

int regDevIoParse(dbCommon* record, struct link* link, int types)
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
            (regDevPrivate*) record->dpvt,
            types);
        if (status == S_dev_success) return status;
        regDevPrintErr("invalid link field \"%s\"",
            link->value.instio.string);
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

    regDevDebugLog(DBG_INIT, "%s %s prio=%d irqvec=%d cmd=%d\n",
        device->name, record->name, record->prio, priv->irqvec,
        cmd);

    if (priv->irqvec == -1 &&
        (device->blockModes & REGDEV_BLOCK_READ) &&
        record->prio != 2)
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

    if (priv->irqvec == -1 &&
        (device->blockModes & REGDEV_BLOCK_WRITE) &&
        record->prio != 2)
    {
        *ppvt = device->blockSent;
    }
    else if (device->support->getOutScanPvt)
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
        size_t size = device->size;
        printf(" \"%s\" size ", device->name);
        if (size)
        {
            printf("%" Z "d", size);
            if (size > 9) printf("=0x%" Z "x", size);
            if (size > 1024*1024)
                printf("=%" Z "dMiB", size >> 20);
            else if (size > 1024)
                printf("=%" Z "dKiB", size >> 10);
        }
        else
            printf("unknown");

        if (device->blockBuffer)
            printf(" block@%p", device->blockBuffer);
        if (device->support && device->support->report)
        {
            printf(" ");
            fflush(stdout);
            epicsMutexLock(device->accesslock);
            device->support->report(device->driver, level);
            epicsMutexUnlock(device->accesslock);
        }
        else
            printf("\n");
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

static epicsUInt64 bcd2i(epicsUInt64 bcd)
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

static epicsUInt64 i2bcd(epicsUInt64 i)
{
    int bcd = 0;
    int s = 0;

    while (i)
    {
        bcd += (i % 10) << s;
        i /= 10;
        s += 4;
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
    priv->irqvec=-1;
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
        case regDevBCD64T:
            if (allowedTypes & TYPE_BCD) return S_dev_success;
            break;
        case epicsInt64T:
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
#ifdef DBR_INT64
        case DBF_INT64:
            priv->dtype = epicsInt64T;
            priv->dlen = 8;
            return S_dev_success;
        case DBF_UINT64:
            priv->dtype = epicsUInt64T;
            priv->dlen = 8;
            return S_dev_success;
#endif
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
                nelm >>= 1;
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
                nelm >>= 1;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                nelm >>= 2;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
        case regDevBCD64T:
        case epicsInt64T:
        case epicsUInt64T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
            {
                nelm >>= 1;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                nelm >>= 2;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                nelm >>= 3;
                status = S_dev_success;
            }
            else if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                status = ARRAY_CONVERT;
            break;
    }
    priv->nelm = nelm;
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
    epicsUInt64 mask;
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
    int blockModes = device->blockModes;
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
                    epicsThreadGetNameSelf(), msg.record->name, blockModes & REGDEV_BLOCK_WRITE ? "block " : "");
                epicsMutexLock(device->accesslock);
                if (blockModes & REGDEV_BLOCK_WRITE)
                    status = support->write(driver, 0, 1, device->size,
                        device->blockBuffer, NULL, prio, NULL, msg.record->name);
                else
                    status = support->write(driver, msg.offset, msg.dlen, msg.nelem,
                        msg.buffer, msg.mask ? &msg.mask : NULL, prio, NULL, msg.record->name);
                epicsMutexUnlock(device->accesslock);
                break;
            case CMD_READ:
                regDevDebugLog(DBG_IN, "%s %s: doing dispatched %sread\n",
                    epicsThreadGetNameSelf(), msg.record->name,
                    blockModes & REGDEV_BLOCK_READ ? "block " : "");
                epicsMutexLock(device->accesslock);
                if (blockModes & REGDEV_BLOCK_READ)
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
        ptr = device->dmaAlloc(device->driver, *bptr, size);
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
    free (*bptr);
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
            status = regDevAllocBuffer(device, device->name, (void**)&device->blockBuffer, device->size);
            if (status != S_dev_success)
                return status;
        }
        if (modes & REGDEV_BLOCK_READ)
            scanIoInit(&device->blockReceived);
        if (modes & REGDEV_BLOCK_WRITE)
            scanIoInit(&device->blockSent);
    }
    device->swap = swap;
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
                errlogPrintf("%s: effective offset '%s'=%d * %" Z "d + %" Z "u = %" Z "d < 0\n",
                    record->name, priv->offsetRecord->precord->name,
                    buffer.i, priv->offsetScale, offset, off);
                return S_dev_badSignalNumber;
            }
            offset = off;
        }
    }
    if (atInit) regDevDebugLog(DBG_INIT, "%s: init from offset 0x%" Z "x\n",
        record->name, offset);
    if (priv->updating) regDevDebugLog(DBG_IN, "%s: update from offset 0x%" Z "x\n",
        record->name, offset);

    if (device->size) /* check offset range if size is provided */
    {
        if (offset > device->size)
        {
            errlogPrintf("%s: offset 0x%" Z "x out of range of device %s (0-0x%" Z "x)\n",
                record->name, offset, device->name, device->size-1);
            return S_dev_badSignalNumber;
        }
        if (offset + dlen * nelem > device->size)
        {
            errlogPrintf("%s: offset 0x%" Z "x + 0x%" Z "x bytes length exceeds device %s size 0x%" Z "x by 0x%" Z "x bytes\n",
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
        dbScanUnlock(record);
        if (priv->nextUpdate)
            epicsTimerStartDelay(priv->nextUpdate->updateTimer, 0.0);
    }
    else
    {
        (*record->rset->process)(record);
        dbScanUnlock(record);
    }
}

int regDevReadWithDebug(dbCommon* record, size_t offset, unsigned int dlen, size_t nelem, void* buffer, int prio)
{
    regDeviceNode* device;
    int status;
    regDevGetPriv();
    device = priv->device;

    status = device->support->read(device->driver, offset, dlen, nelem, buffer,
        prio, atInit ? NULL : regDevCallback, record->name);
    if (record->tpro >= 2)
    {
        printf("  %s: read %llu * %u bytes from %s\n", record->name, (unsigned long long)nelem, dlen, device->name);
        memDisplay(0, buffer, dlen, dlen * nelem);
    }
    return status;
}

int regDevRead(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buf)
{
    /* buf must not point to local variable: not suitable for async processing */

    char *buffer = buf;
    int status = S_dev_success;
    regDeviceNode* device;
    size_t offset;
    int blockModes;

    regDevGetPriv();
    device = priv->device;
    assert(device != NULL);
    assert(buffer != NULL || nelem == 0 || dlen == 0);
    blockModes = device->blockModes;

    regDevDebugLog(DBG_IN, "%s: dlen=%u, nelm=%" Z "u, buffer=%p\n",
        record->name, dlen, nelem, buffer);

    if (!device->support->read && !(blockModes & REGDEV_BLOCK_READ))
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

        if (!(blockModes & REGDEV_BLOCK_READ) || record->prio == 2)
        {
            /* read from the hardware (directly or to fill the block buffer) */
            record->pact = 1;
            priv->asyncOffset = offset;
            priv->status = S_dev_success;
            if (device->dispatcher && !atInit && device->support->read)
            {
                /* schedule asynchronous read */
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
                if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], &msg, sizeof(msg)) != 0)
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
                /* synchronous read */
                regDevDebugLog(DBG_IN, "%s: reading %s from %s\n",
                    record->name,
                    blockModes & REGDEV_BLOCK_READ ? "block" :
                    priv->interlace ? "interlaced" :
                    nelem > 1 ? "array" : "scalar",
                    device->name);
                epicsMutexLock(device->accesslock);
                if (blockModes & REGDEV_BLOCK_READ)
                {
                    /* read whole data block buffer
                       (directly mapped blocks need no read function)
                    */
                    if (device->support->read)
                        status = regDevReadWithDebug(record,
                            0, 1, device->size, device->blockBuffer, 2);
                }
                else if (priv->interlace)
                {
                    /* read interlaced arrays element-wise */
                    size_t i;
                    for (i = 0; i < nelem; i++)
                    {
                        status = regDevReadWithDebug(record,
                            offset + i*priv->interlace, dlen, 1, buffer + i*dlen, record->prio);
                        if (status) break;
                    }
                }
                else
                    status = regDevReadWithDebug(record,
                        offset, dlen, nelem, buffer, record->prio);

                epicsMutexUnlock(device->accesslock);
                regDevDebugLog(DBG_IN, "%s: read returned status 0x%0x\n", record->name, status);
            }
        }
    }

    if (status == S_dev_success)
    {
        record->udf = FALSE;
        if (blockModes & REGDEV_BLOCK_READ)
        {
            /* copy from blockBuffer */
            if (buffer) /* if not: record without content (status, event) */
            {
                if (device->blockBuffer <= buffer && buffer < device->blockBuffer + device->size)
                {
                    /* array is directly mapped into blockBuffer and needs no copy */
                    regDevDebugLog(DBG_IN, "%s: %" Z "u * %u bytes mapped in %s block buffer %p+0x%" Z "x\n",
                        record->name, nelem, dlen, device->name, device->blockBuffer, offset);
                }
                else
                {
                    /* copy block buffer to record */
                    regDevDebugLog(DBG_IN, "%s: copy %" Z "u * %u bytes from %s block buffer %p+0x%" Z "x to record buffer %p\n",
                        record->name, nelem, dlen, device->name, device->blockBuffer, offset, buffer);
                    if (priv->interlace)
                    {
                        /* copy interlaced arrays element-wise */
                        size_t i;
                        for (i = 0; i < nelem; i++)
                            regDevCopy(dlen, 1,
                                device->blockBuffer + offset + i*priv->interlace,
                                buffer + i*dlen, NULL, device->swap);
                    }
                    else
                        regDevCopy(dlen, nelem, device->blockBuffer + offset, buffer, NULL, device->swap);
                }
            }

            if (record->prio == 2 && !atInit)
            {
                /* inform other input records of new block data available */
                scanIoRequest(device->blockReceived);
            }
        }
    }

    if ((priv->mask || priv->invert) && status == S_dev_success)
    {
        size_t i;
        epicsUInt64 invert = priv->invert;
        epicsUInt64 mask = priv->mask;
        if (!mask) mask = ~mask;
        switch (dlen)
        {
            case 1:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt8*)buffer)[i] =
                        (((epicsUInt8*)buffer)[i] & (epicsUInt8)mask) ^ (epicsUInt8)invert;
                break;
            case 2:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt16*)buffer)[i] =
                        (((epicsUInt16*)buffer)[i] & (epicsUInt16)mask) ^ (epicsUInt16)invert;
                break;
            case 4:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt32*)buffer)[i] =
                        (((epicsUInt32*)buffer)[i] & (epicsUInt32)mask) ^ (epicsUInt32)invert;
                break;
            case 8:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt64*)buffer)[i] =
                        (((epicsUInt64*)buffer)[i] & mask) ^ invert;
                break;
        }
    }

    /* Some debug output */
    if (regDevDebug & DBG_IN)
    {
        if (status == ASYNC_COMPLETION)
        {
            printf("%s %s: async read %" Z "u * %u bit from %s:0x%" Z "x\n",
                _CURRENT_FUNCTION_, record->name, nelem, dlen*8,
                device->name, priv->asyncOffset);
        }
        else if (buffer) switch (dlen)
        {
            case 1:
                regDevDebugLog(DBG_IN,
                    "%s: read %" Z "u * 8 bit 0x%02x \"%.*s\" from %s:0x%" Z "x (status=%x)\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    nelem < 10 ? (int)nelem : 10, isprint((unsigned char)buffer[0]) ? buffer : "",
                    device->name, offset, status);
                break;
            case 2:
                regDevDebugLog(DBG_IN,
                    "%s: read %" Z "u * 16 bit 0x%04x from %s:0x%" Z "x (status=%x)\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset, status);
                break;
            case 4:
                regDevDebugLog(DBG_IN,
                    "%s: read %" Z "u * 32 bit 0x%08x from %s:0x%" Z "x (status=%x)\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset, status);
                break;
            case 8:
                regDevDebugLog(DBG_IN,
                    "%s: read %" Z "u * 64 bit 0x%016llx from %s:0x%" Z "x (status=%x)\n",
                    record->name, nelem, (unsigned long long)*(epicsUInt64*)buffer,
                    device->name, offset, status);
                break;
            default:
                regDevDebugLog(DBG_IN,
                    "%s: read %" Z "u * %d bit from %s:0x%" Z "x (status=%x)\n",
                    record->name, nelem, dlen*8,
                    device->name, offset, status);
        }
    }

    if (status == ASYNC_COMPLETION)
    {
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

int regDevWriteWithDebug(dbCommon* record, size_t offset, unsigned int dlen, size_t nelem, void* buffer, void* pmask, int prio)
{
    regDeviceNode* device;
    regDevGetPriv();
    device = priv->device;

    if (record->tpro >= 2)
    {
        printf("  %s: write %llu * %u bytes %sto %s\n", record->name, (unsigned long long)nelem, dlen, pmask ? " masked" : "", device->name);
        memDisplay(0, buffer, dlen, dlen * nelem);
    }
    return device->support->write(device->driver, offset, dlen, nelem, buffer, pmask,
        prio, atInit ? NULL : regDevCallback, record->name);
}

int regDevWrite(dbCommon* record, epicsUInt8 dlen, size_t nelem, void* buf, epicsUInt64 mask)
{
    /* buf must not point to local variable: not suitable for async processing */

    char* buffer = buf;
    int status;
    size_t offset;
    regDeviceNode* device;
    int blockModes;
    epicsUInt64 m;

    regDevGetPriv();
    device = priv->device;
    assert(device != NULL);
    assert(buffer != NULL || nelem == 0 || dlen == 0);
    blockModes = device->blockModes;

    regDevDebugLog(DBG_OUT, "%s: dlen=%u, nelm=%" Z "u, buffer=%p mask=%llx M=%llx\n",
        record->name, dlen, nelem, buffer, (unsigned long long)mask, (unsigned long long)priv->mask);

    if (!device->support->write && !(blockModes & REGDEV_BLOCK_WRITE))
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "%s: device %s has no write function\n",
            record->name, device->name);
        return S_dev_badRequest;
    }

    if (record->pact)
    {
        /* Second call of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: asynchronous write returned %d\n",
            record->name, priv->status);
        if (priv->status != S_dev_success)
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        else
        if ((blockModes & REGDEV_BLOCK_WRITE) &&
            record->prio == 2 &&
            !atInit)
        {
            /* inform other output records that block has been sent */
            scanIoRequest(device->blockSent);
        }
        return priv->status;
    }

    /* First call of (possibly asynchronous) device */

    /* combine mask from M=<mask> and from record MASK field */
    if ((m = priv->mask) != 0)
    {
        if (mask)
        {
            m &= mask;
            if (!m)
            {
                regDevDebugLog(DBG_OUT, "%s: M=0x%llx & MASK=0x%llx is empty: Nothing to write\n",
                    record->name, (unsigned long long)priv->mask, (unsigned long long)mask);
                return S_dev_success;
            }
        }
        mask = m;
    }

    status = regDevGetOffset(record, dlen, nelem, &offset);
    if (status != S_dev_success)
        return status;

    if (priv->invert)
    {
        size_t i;
        switch (dlen)
        {
            case 1:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt8*)buffer)[i] ^= (epicsUInt8)priv->invert;
                break;
            case 2:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt16*)buffer)[i] ^= (epicsUInt16)priv->invert;
                break;
            case 4:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt32*)buffer)[i] ^= (epicsUInt32)priv->invert;
                break;
            case 8:
                for (i = 0; i < nelem; i++)
                    ((epicsUInt64*)buffer)[i] ^= priv->invert;
                break;
        }
    }

    /* Some debug output */
    if (regDevDebug & DBG_OUT)
    {
        if (buffer) switch (dlen+(mask?10:0))
        {
            case 1:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 8 bit 0x%02x \"%.*s\" to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    nelem < 10 ? (int)nelem : 10, isprint((unsigned char)buffer[0]) ? buffer : "",
                    device->name, offset);
                break;
            case 2:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 16 bit 0x%04x to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset);
                break;
            case 4:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 32 bit 0x%08x to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset);
                break;
            case 8:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 64 bit 0x%016llx to %s:0x%" Z "x\n",
                    record->name, nelem, (unsigned long long)*(epicsUInt64*)buffer,
                    device->name, offset);
                break;
            case 11:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 8 bit 0x%02x mask 0x%02x to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt8*)buffer, (epicsUInt8)mask,
                    device->name, offset);
            case 12:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 16 bit 0x%04x mask 0x%04x to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt16*)buffer, (epicsUInt16)mask,
                    device->name, offset);
                break;
            case 14:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 32 bit 0x%08x mask 0x%08x to %s:0x%" Z "x\n",
                    record->name, nelem, *(epicsUInt32*)buffer, (epicsUInt32)mask,
                    device->name, offset);
                break;
            case 18:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * 64 bit 0x%016llx mask 0x%016llx to %s:0x%" Z "x\n",
                    record->name, nelem,
                    (unsigned long long)*(epicsUInt64*)buffer,
                    (unsigned long long)mask,
                    device->name, offset);
                break;
            default:
                regDevDebugLog(DBG_OUT,
                    "%s: write %" Z "u * %d bit to %s:0x%" Z "x\n",
                    record->name, nelem, dlen*8,
                    device->name, offset);
        }
    }

    if (mask)
    {
        /* fix mask alignment */
        switch (dlen)
        {
            case 1:
                *(epicsUInt8*)&mask = (epicsUInt8)mask;
                break;
            case 2:
                *(epicsUInt16*)&mask = (epicsUInt16)mask;
                break;
            case 4:
                *(epicsUInt32*)&mask = (epicsUInt32)mask;
                break;
        }
    }

    if (blockModes & REGDEV_BLOCK_WRITE)
    {
        /* copy to blockBuffer */
        if (buffer) /* if not: record without content */
        {
            if (device->blockBuffer <= buffer && buffer < device->blockBuffer + device->size)
            {
                 /* array is directly mapped into blockBuffer and needs no copy */
                regDevDebugLog(DBG_OUT, "%s: %" Z "u * %u bytes mapped in %s block buffer %p+0x%" Z "x\n",
                    record->name, nelem, dlen, device->name, device->blockBuffer, offset);
            }
            else
            {
                /* copy record to block buffer */
                regDevDebugLog(DBG_OUT, "%s: copy %" Z "u * %u bytes from record buffer %p to %s block buffer %p+0x%" Z "x\n",
                    record->name, nelem, dlen, buffer, device->name, device->blockBuffer, offset);
                if (priv->interlace)
                {
                    /* copy interlaced arrays element-wise */
                    size_t i;
                    for (i = 0; i < nelem; i++)
                        regDevCopy(dlen, 1, buffer + i*dlen,
                            device->blockBuffer + offset + i*priv->interlace,
                            mask ? &mask : NULL, device->swap);
                }
                else
                    regDevCopy(dlen, nelem, buffer, device->blockBuffer + offset,
                        mask ? &mask : NULL, device->swap);
            }
        }
        if (record->prio != 2)
            return S_dev_success;
    }

    priv->status = S_dev_success;
    record->pact = 1;
    if (device->dispatcher && device->support->write)
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
        if (epicsMessageQueueTrySend(device->dispatcher->qid[record->prio], &msg, sizeof(msg)) != 0)
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
        /* synchronous write */
        regDevDebugLog(DBG_OUT, "%s: writing %s to %s\n",
            record->name,
            blockModes & REGDEV_BLOCK_WRITE ? "block" :
            priv->interlace ? "interlaced" :
            nelem > 1 ? "array" : "scalar",
            device->name);
        epicsMutexLock(device->accesslock);
        if (blockModes & REGDEV_BLOCK_WRITE)
        {
            /* write whole data block buffer
               (directly mapped blocks need no write function)
            */
            if (device->support->write)
                status = regDevWriteWithDebug(record,
                    0, 1, device->size, device->blockBuffer, NULL, 2);
        }
        else if (priv->interlace)
        {
            /* write interlaced arrays element-wise */
            size_t i;
            for (i = 0; i < nelem; i++)
            {
                status = regDevWriteWithDebug(record,
                    offset + i*priv->interlace, dlen, 1, buffer + i*dlen,
                    mask ? &mask : NULL, record->prio);
                if (status) break;
            }
        }
        else
        {
            status = regDevWriteWithDebug(record,
                offset, dlen, nelem, buffer, mask ? &mask : NULL,
                record->prio);
        }
        epicsMutexUnlock(device->accesslock);
        regDevDebugLog(DBG_OUT, "%s: write returned status 0x%0x\n",
            record->name, status);
    }

    if (status == ASYNC_COMPLETION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: wait for asynchronous write completition\n",
            record->name);
        return status;
    }

    record->pact = 0;
    if (status != S_dev_success)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "%s: write error\n", record->name);
    }
    if ((blockModes & REGDEV_BLOCK_WRITE) &&
        record->prio == 2 &&
        !atInit)
    {
        /* inform other output records that block has been sent */
        scanIoRequest(device->blockSent);
    }
    return status;
}

int regDevReadNumber(dbCommon* record, epicsInt64* rval, double* fval)
{
    int status = S_dev_success;
    epicsInt64 rv = 0;
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
        case epicsInt64T:
        case epicsUInt64T:
        case regDevBCD64T:
            rv = priv->data.uval64;
            break;
        case epicsFloat32T:
            fv = priv->data.fval32;
            break;
        case epicsFloat64T:
            fv = priv->data.fval64;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data type %s requested",
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
        case regDevBCD64T:
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
    if (fval) *fval = (double)rv; /* 64 bit may overflow double but what can we do? */
    if (record->tpro)
    {
        if (fval)
            printf("  %s: RVAL = 0x%llx   VAL = %g\n", record->name, (unsigned long long)*rval, *fval);
        else
            printf("  %s: RVAL = 0x%llx\n", record->name, (unsigned long long)*rval);
    }
    return S_dev_success;
}

int regDevWriteNumber(dbCommon* record, epicsInt64 rval, double fval)
{
    regDevGetPriv();

    /* enforce bounds */
    switch (priv->dtype)
    {
        case epicsFloat32T:
        case epicsFloat64T:
            regDevDebugLog(DBG_OUT, "%s: fval=%#g\n",
                record->name, fval);
            break;
        case epicsInt64T:
        case epicsInt32T:
        case epicsInt16T:
        case epicsInt8T:
            regDevDebugLog(DBG_OUT, "%s: signed rval=%lld (0x%llx)\n",
                record->name, (long long)rval, (long long)rval);

            if ((epicsInt64)rval > (epicsInt64)priv->H)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %lld (0x%llx) to upper bound %lld (0x%llx)\n",
                    record->name, (long long)rval, (long long)rval, (long long)priv->H, (long long)priv->H);
                rval = priv->H;
            }
            if ((epicsInt64)rval < (epicsInt64)priv->L)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %lld (0x%llx) to lower bound %lld (0x%llx)\n",
                    record->name, (long long)rval, (long long)rval, (long long)priv->L, (long long)priv->L);
                rval = priv->L;
            }
            break;
        case epicsUInt8T:
            if ((rval & 0xffffffffffffff80LL) == 0xffffffffffffff80LL) rval &= 0xff;
        case epicsUInt16T:
            if ((rval & 0xffffffffffff8000LL) == 0xffffffffffff8000LL) rval &= 0xffff;
        case epicsUInt32T:
            if ((rval & 0xffffffff80000000LL) == 0xffffffff80000000LL) rval &= 0xffffffff;
        default:
            regDevDebugLog(DBG_OUT, "%s: unsigned rval=%llu (0x%llx)\n",
                record->name, (long long)rval, (long long)rval);

            if ((epicsUInt64)rval > (epicsUInt64)priv->H)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %llu (0x%llx) to upper bound %llu (0x%llx)\n",
                    record->name, (long long)rval, (long long)rval, (long long)priv->H, (long long)priv->H);
                rval = priv->H;
            }
            if ((epicsUInt64)rval < (epicsUInt64)priv->L)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from %llu (0x%llx) to lower bound %llu (0x%llx)\n",
                    record->name, (long long)rval, (long long)rval, (long long)priv->L, (long long)priv->L);
                rval = priv->L;
            }
    }

    switch (priv->dtype)
    {
        case regDevBCD8T:
            rval = i2bcd(rval);
        case epicsInt8T:
        case epicsUInt8T:
            priv->data.uval8 = (epicsUInt8)rval;
            break;
        case regDevBCD16T:
            rval = i2bcd(rval);
        case epicsInt16T:
        case epicsUInt16T:
            priv->data.uval16 = (epicsUInt16)rval;
            break;
        case regDevBCD32T:
            rval = i2bcd(rval);
        case epicsInt32T:
        case epicsUInt32T:
            priv->data.uval32 = (epicsUInt32)rval;
            break;
        case regDevBCD64T:
            rval = i2bcd(rval);
        case epicsInt64T:
        case epicsUInt64T:
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
    return regDevWrite(record, priv->dlen, 1, &priv->data, 0);
}

int regDevReadBits(dbCommon* record, epicsUInt32* rval)
{
    int status;
    epicsUInt64 rv;

    status = regDevReadBits64(record, &rv);
    if (status) return status;
    *rval = (epicsUInt32)rv; /* cut off high bits */
    return 0;
}

int regDevReadBits64(dbCommon* record, epicsUInt64* rval)
{
    int status;

    regDevGetPriv();
    status = regDevRead(record, priv->dlen, 1, &priv->data);
    if (status != S_dev_success)
        return status;

    assert(rval != NULL);
    switch (priv->dtype)
    {
        case epicsInt8T:
            *rval = priv->data.sval8;
            break;
        case epicsUInt8T:
            *rval = priv->data.uval8;
            break;
        case epicsInt16T:
            *rval = priv->data.sval16;
            break;
        case epicsUInt16T:
            *rval = priv->data.uval16;
            break;
        case epicsInt32T:
            *rval = priv->data.sval32;
            break;
        case epicsUInt32T:
            *rval = priv->data.uval32;
            break;
        case epicsInt64T:
            *rval = priv->data.sval64;
            break;
        case epicsUInt64T:
            *rval = priv->data.uval64;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data type %s requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }
    if (record->tpro)
        printf("  %s: RVAL = 0x%llx\n", record->name, (unsigned long long)*rval);

    if (atInit)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }

    return S_dev_success;
}

int regDevWriteBits(dbCommon* record, epicsUInt64 rval, epicsUInt64 mask)
{
    regDevGetPriv();
    regDevDebugLog(DBG_OUT, "%s: rval=0x%08llx, mask=0x%08llx)\n",
        record->name, (unsigned long long)rval, (unsigned long long)mask);
    switch (priv->dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
            priv->data.uval8 = (epicsUInt8)rval;
            break;
        case epicsInt16T:
        case epicsUInt16T:
            priv->data.uval16 = (epicsUInt16)rval;
            break;
        case epicsInt32T:
        case epicsUInt32T:
            priv->data.uval32 = (epicsUInt32)rval;
            break;
        case epicsInt64T:
        case epicsUInt64T:
            priv->data.uval64 = rval;
            break;
        default:
            recGblSetSevr(record, SOFT_ALARM, INVALID_ALARM);
            regDevPrintErr("unexpected data type %s requested",
                regDevTypeName(priv->dtype));
            return S_dev_badArgument;
    }
    return regDevWrite(record, priv->dlen, 1, &priv->data, mask);
}

int regDevReadArray(dbCommon* record, size_t nelm)
{
    int status = S_dev_success;
    size_t i;
    epicsUInt8 dlen;
    epicsUInt8 packing;

    regDevGetPriv();

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->L contains string length */
        if (nelm > (size_t)priv->L)
        {
            nelm = (size_t)priv->L;
            ((char*)priv->data.buffer)[nelm] = 0;
        }
    }
    dlen = priv->dlen;

    packing = priv->fifopacking;
    if (packing)
    {
        /* FIFO: read element-wise */
        char* buffer = priv->data.buffer;
        dlen *= packing;
        nelm /= packing;
        for (i = 0; i < nelm; i++)
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
            dlen, nelm, priv->data.buffer);
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
        case regDevBCD64T:
        {
            epicsUInt64* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
    }
    if (record->tpro)
    {
        if (priv->dtype == epicsStringT)
            printf("  %s: VAL = \"%s\"\n", record->name, (char*)priv->data.buffer);
    }
    return S_dev_success;
}

int regDevWriteArray(dbCommon* record, size_t nelm)
{
    int status = 0;
    size_t i;
    epicsUInt8 dlen;
    int packing;

    regDevGetPriv();

    regDevDebugLog(DBG_OUT, "%s: nelm=%" Z "d dlen=%d\n", record->name, nelm, priv->dlen);

    if (priv->dtype == epicsStringT)
    {
        /* strings are arrays of single bytes but priv->L contains string length */
        if (nelm > (size_t)priv->L)
            nelm = (size_t)priv->L;
        regDevDebugLog(DBG_OUT, "%s: string length %" Z "d\n", record->name, nelm);
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
        case regDevBCD64T:
        {
            epicsUInt64* buffer = priv->data.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
    }

    packing = priv->fifopacking;
    if (packing)
    {
        /* FIFO: write element-wise */
        char* buffer = priv->data.buffer;
        dlen *= packing;
        for (i = 0; i < nelm/packing; i++)
        {
            status = regDevWrite(record, dlen, 1, buffer+i*dlen, 0);
            /* probably does not work async */
            if (status != S_dev_success) break;
        }
    }
    else
    {
        status = regDevWrite(record,
            dlen, nelm, priv->data.buffer, 0);
    }
    return status;
}

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high)
{
    double o, s;
    size_t i;

    regDevGetPriv();

    o = (priv->H * low - priv->L * high) / (epicsUInt64)(priv->H - priv->L);
    s = (high - low) / (epicsUInt64)(priv->H - priv->L);

    regDevDebugLog(DBG_IN, "%s: scaling from %s at %p to %s at %p\n",
        record->name, regDevTypeName(priv->dtype), priv->data.buffer, pamapdbfType[ftvl].strvalue+4, val);

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
            epicsUInt32* r = priv->data.buffer;
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
        case epicsInt64T:
        {
            epicsInt64* r = priv->data.buffer;
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
        case epicsUInt64T:
        case regDevBCD64T:
        {
            epicsUInt64* r = priv->data.buffer;
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
    double o, s;
    size_t i;

    regDevGetPriv();

    o = (priv->L * high - priv->H * low) / (epicsUInt64)(priv->H - priv->L);
    s = (epicsUInt64)(priv->H - priv->L) / (high - low);

    regDevDebugLog(DBG_OUT, "%s: scaling from %s at %p to %s at %p\n",
        record->name, pamapdbfType[ftvl].strvalue+4, val, regDevTypeName(priv->dtype), priv->data.buffer);

    switch (priv->dtype)
    {
        case epicsInt8T:
        {
            epicsInt8* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsInt8)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
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
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsUInt8)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
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
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsInt16)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
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
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsUInt16)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
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
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsInt32)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
                    r[i] = (epicsInt32)x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            epicsUInt32* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsUInt32)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
                    r[i] = (epicsUInt32)x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsInt64T:
        {
            /* these conversions may overflow, but what can we do? */
            epicsInt64* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsInt64)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
                    r[i] = (epicsInt64)x;
                }
            }
            else break;
            return S_dev_success;
        }
        case epicsUInt64T:
        case regDevBCD64T:
        {
            epicsUInt64* r = priv->data.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (v[i]+o)*s;
                    if (x < priv->L) x = (double)priv->L;
                    if (x > priv->H) x = (double)priv->H;
                    r[i] = (epicsUInt64)x;
                }
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val, x;
                for (i = 0; i < nelm; i++) {
                    x = (float)((v[i]+o)*s);
                    if (x < priv->L) x = (float)priv->L;
                    if (x > priv->H) x = (float)priv->H;
                    r[i] = (epicsUInt64)x;
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
    int pact = 0;
    regDevPrivate* priv = record->dpvt;

    if (interruptAccept && !record->pact && !priv->updating) /* scanning allowed and not busy? */
    {
        dbScanLock(record);
        if (!record->pact && !priv->updating)
        {
            regDevDebugLog(DBG_IN, "%s: updating record\n",
                record->name);
            priv->updating = 1;
            if (record->tpro)
                printf ("Update %s\n", record->name);
            status = priv->updater(record);
            recGblGetTimeStamp(record);
            pact = record->pact;
            if (!pact)
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
    if (!pact)
    {
        /* restart timer */
        if (priv->update > 0)
            epicsTimerStartDelay(priv->updateTimer, priv->update * 0.001);
        if (priv->nextUpdate)
            epicsTimerStartDelay(priv->nextUpdate->updateTimer, 0.0);
    }
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
        /* install update function */
        priv->updater = updater;
        priv->updateTimer = epicsTimerQueueCreateTimer(device->updateTimerQueue,
            (epicsTimerCallback)regDevRunUpdater, record);
        if (priv->update > 0)
        {
            regDevDebugLog(DBG_INIT, "%s: install update every %f seconds\n",
                record->name, priv->update * 0.001);
            epicsTimerStartDelay(priv->updateTimer, priv->update * 0.001);
        }
        if (priv->update < 0)
        {
            regDevPrivate** pr;
            regDevDebugLog(DBG_INIT, "%s: install update on trigger\n", record->name);
            for (pr = &device->triggeredUpdates; *pr; pr=&(*pr)->nextUpdate);
            *pr = priv;
        }
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
            errlogPrintf("address 0x%" Z "x out of range\n", offset);
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
        memDisplay(offset, device->blockBuffer + offset, dlen, dlen * nelem);
        offset += dlen * nelem;
        return S_dev_success;
    }
    if (bytes > bufferSize)
    {
        status = regDevAllocBuffer(device, "", (void**)&buffer, bytes);
        if (status) return status;
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
        case 8:
            buffer.sval64 = value;
            break;
        default:
            errlogPrintf("illegal dlen %d, must be 1, 2, 4 or 8\n", dlen);
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
