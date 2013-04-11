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
    "$Id: regDev.c,v 1.32 2013/04/11 12:25:01 zimoch Exp $";

static regDeviceNode* registeredDevices = NULL;

int regDevDebug = 0;
epicsExportAddress(int, regDevDebug);

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

static void regDevCallback(CALLBACK *callback)
{
    struct dbCommon* record;
    callbackGetUser(record, callback);
    if (!interruptAccept)
    {
        regDevPrivate* priv = record->dpvt;
        assert (priv != NULL);
        assert (priv->magic == MAGIC);
        epicsEventSignal(priv->initDone);
    }
    else
    {
        callbackRequestProcessCallback(callback, record->prio, record);
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
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: device=%s\n",
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
            if (dbNameToAddr(recName, priv->offsetRecord) != OK)
            {
                free(priv->offsetRecord);
                priv->offsetRecord = NULL;
                fprintf(stderr,
                    "regDevIoParse %s: record '%s' not found\n",
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
            fprintf(stderr,
                "regDevIoParse %s: offset %ld<0\n",
                recordName, offset);
            return S_dev_badArgument;
        }
        priv->offset = offset;
        if (priv->offsetRecord)
            regDevDebugLog(DBG_INIT,
                "regDevIoParse %s: offset='%s'*%d+%d\n",
                recordName, priv->offsetRecord->precord->name, priv->offsetScale, priv->offset);
        else
            regDevDebugLog(DBG_INIT,
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
        regDevDebugLog(DBG_INIT,
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
            case ')':
                fprintf(stderr,
                    "regDevIoParse %s: unbalanced closing )\n",
                    recordName);
                return S_dev_badArgument;
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
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: L=%#x\n",  recordName, priv->hwLow);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: H=%#x\n",  recordName, priv->hwHigh);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(DBG_INIT, "regDevIoParse %s: X=%#x\n",  recordName, priv->invert);

    if (status)
    {
        fprintf(stderr,
            "regDevIoParse %s: L=%#x (%d) or H=%#x (%d) out of range for T=%s\n",
            recordName, priv->hwLow, priv->hwLow, priv->hwHigh, priv->hwHigh, datatypes[type].name);
        return status;
    }
    
    return OK;
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
        if (status == OK) return OK;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    return status;
}

/*********  Diver registration and access ****************************/

int regDevRegisterDevice2(const char* name,
    const regDevSupport* support, const regDevAsyncSupport* asupport,
    regDevice* driver)
{
    char* nameCpy;
    
    regDeviceNode **pdevice; 
    for (pdevice=&registeredDevices; *pdevice; pdevice=&(*pdevice)->next)
    {
        if (strcmp((*pdevice)->name, name) == 0)
        {
            fprintf(stderr,
            "regDevRegisterDevice %s: device already exists as %ssynchronous device\n",
                name, (*pdevice)->asupport ? "a" : "");
            return ERROR;
        }
    }
    *pdevice = (regDeviceNode*) calloc(1, sizeof(regDeviceNode));
    if (*pdevice == NULL)
    {
        fprintf(stderr,
            "regDevRegisterDevice %s: out of memory\n",
            name);
        return S_dev_noMemory;
    }
    nameCpy = malloc(strlen(name)+1);
    strcpy(nameCpy, name);
    (*pdevice)->name = nameCpy;
    (*pdevice)->support = support;
    (*pdevice)->asupport = asupport;
    (*pdevice)->driver = driver;
    (*pdevice)->accesslock = epicsMutexCreate();
    if ((*pdevice)->accesslock == NULL)
    {
        fprintf(stderr,
            "regDevRegisterDevice %s: out of memory\n",
            name);
        return S_dev_noMemory;
    }
    return OK;
}

int regDevRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver)
{
    return regDevRegisterDevice2(name, support, NULL, driver);
}

int regDevAsyncRegisterDevice(const char* name,
    const regDevAsyncSupport* asupport, regDeviceAsyn* driver)
{
    return regDevRegisterDevice2(name, NULL, asupport, driver);
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
        return ERROR;
    }
    assert (priv->magic == MAGIC);
    device = priv->device;
    assert (device);
    if (device->asupport && device->asupport->getInScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->asupport->getInScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else if (device->support && device->support->getInScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getInScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        fprintf(stderr,
            "regDevGetInIntInfo %s: input I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return ERROR;
    }
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevGetInIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return ERROR;
    }
    return OK;
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
        return ERROR;
    }
    assert (priv->magic == MAGIC);
    device = priv->device;
    assert (device);

    if (device->asupport && device->asupport->getOutScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->asupport->getOutScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else if (device->support && device->support->getOutScanPvt)
    {
        epicsMutexLock(device->accesslock);
        *ppvt = device->support->getOutScanPvt(
            device->driver, priv->offset);
        epicsMutexUnlock(device->accesslock);
    }
    else
    {
        fprintf(stderr,
            "regDevGetOutIntInfo %s: output I/O Intr unsupported for bus %s\n",
            record->name, device->name);
        return ERROR;
    }
    if (*ppvt == NULL)
    {
        fprintf(stderr,
            "regDevGetOutIntInfo %s: no I/O Intr for bus %s offset %#x\n",
            record->name, device->name, priv->offset);
        return ERROR;
    }
    return OK;
}

/*********  Report routine ********************************************/

long regDevReport(int level)
{
    regDeviceNode* device;
    int headerPrinted;
    
    printf("  regDev version: %s\n", cvsid_regDev);
    if (level < 1) return OK;
    headerPrinted = 0;
    for (device = registeredDevices; device; device = device->next)
    {
        if (device->support)
        {
            if (!headerPrinted) {
                printf("    registered synchronous register devices:\n");
                headerPrinted = 1;
            }
            printf ("      \"%s\" ", device->name);
            if (device->support->report)
            {
                epicsMutexLock(device->accesslock);
                device->support->report(device->driver, level-1);
                epicsMutexUnlock(device->accesslock);
            }
            else
                printf ("\n");
        }
    }
    if (!headerPrinted)
    {
        printf("    no registered synchronous register devices\n");
    }
    headerPrinted = 0;
    for (device = registeredDevices; device; device = device->next)
    {
        if (device->asupport)
        {
            if (!headerPrinted) {
                printf("    registered asynchronous register devices:\n");
                headerPrinted = 1;
            }
            printf ("      \"%s\" ", device->name);
            if (device->asupport->report)
            {
                epicsMutexLock(device->accesslock);
                device->asupport->report(device->driver, level-1);
                epicsMutexUnlock(device->accesslock);
            }
            else
                printf ("\n");
        }
    }
    if (!headerPrinted)
    {
        printf("    no registered asynchronous register devices\n");
    }
    return OK;
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
    return (void*)regDevFind(name);
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
    regDevReport,
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

regDevPrivate* regDevAllocPriv(dbCommon *record)
{
    regDevPrivate* priv;

    regDevDebugLog(DBG_INIT, "regDevAllocPriv(%s)\n", record->name);
    priv = calloc(1, sizeof(regDevPrivate));
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAllocPriv %s: try to allocate %d bytes. %s\n",
            record->name, (int)sizeof(regDevPrivate), strerror(errno));
#ifdef vxWorks
        {
            MEM_PART_STATS meminfo;
            memPartInfoGet(memSysPartId, &meminfo);
            printf ("Max free block: %ld bytes\n", meminfo.maxBlockSizeFree);
        }
#endif
        return NULL;
    }
    priv->magic = MAGIC;
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

int regDevAssertType(dbCommon *record, int allowedTypes)
{
    unsigned short dtype;
    regDevPrivate* priv = record->dpvt;

    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevAssertType %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);
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
    fprintf(stderr,
        "regDevAssertType %s: illegal data type %s for this record type\n",
        record->name, regDevTypeName(dtype));
    free(record->dpvt);
    record->dpvt = NULL;
    return S_db_badField;
}

int regDevCheckFTVL(dbCommon* record, int ftvl)
{
    regDevPrivate* priv = record->dpvt;

    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevCheckFTVL %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);

    regDevDebugLog(DBG_INIT, "regDevCheckFTVL(%s, %s)\n",
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

int regDevCheckType(dbCommon* record, int ftvl, int nelm)
{
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevCheckType %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);
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
                return OK;
            break;
        case epicsFloat32T:
            if (ftvl == DBF_FLOAT)
                return OK;
            break;
        case epicsStringT:    
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                if (!priv->dlen) priv->dlen = nelm;
                if (priv->dlen > nelm) priv->dlen = nelm;
                return OK;
            }
            break;
        case regDevBCD8T:
        case epicsInt8T:
        case epicsUInt8T:
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
                return OK;
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return 1;
            break;
        case regDevBCD16T:
        case epicsInt16T:
        case epicsUInt16T:
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
                return OK;
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 2;
                return OK;
            }
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return 1;
            break;
        case regDevBCD32T:
        case epicsInt32T:
        case epicsUInt32T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
                return OK;
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 2;
                return OK;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 4;
                return OK;
            }
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return 1;
            break;
        case regDev64T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
            {
                priv->arraypacking = 2;
                return OK;
            }
            if ((ftvl == DBF_SHORT) || (ftvl == DBF_USHORT))
            {
                priv->arraypacking = 4;
                return OK;
            }
            if ((ftvl == DBF_CHAR) || (ftvl == DBF_UCHAR))
            {
                priv->arraypacking = 8;
                return OK;
            }
            if ((ftvl == DBF_FLOAT) || (ftvl == DBF_DOUBLE))
                return 1;
    }
    fprintf (stderr,
        "regDevCheckType %s: data type %s does not match FTVL %s\n",
         record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return ERROR;
}

int regDevMemAlloc(dbCommon* record, void** bptr, unsigned int size)
{
    int status = 0;
    void* ptr = NULL;
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevMemAlloc %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);
    device = priv->device;
    assert (device);

    if (device->asupport && device->asupport->buff_alloc)
    {
        epicsMutexLock(device->accesslock);
        status = device->asupport->buff_alloc(bptr, &priv->hwPtr, size);
        epicsMutexUnlock(device->accesslock);
        if (status)
        {
            fprintf (stderr,
                "regDevMemAlloc %s: allocating device memory failed.",
                record->name);
            return status;    
        }
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
    return OK;
}

int regDevRead(dbCommon* record, unsigned int dlen, unsigned int nelem, void* buffer)
{
    int status = OK;
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevRead %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);
    device = priv->device;
    assert (device);
    
    if (record->pact)
    {
        /* Second call of asynchronous device */

        regDevDebugLog(DBG_IN, "%s: asynchronous read returned %d\n", record->name, priv->status);
        if (priv->status != OK)
        {
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
            return priv->status;
        }
    }
    else
    {
        int offset;
        /* First call of (probably asynchronous) device */

        if (buffer == NULL || (nelem == 1 && device->asupport))
        {
            /* buffer may point to local var, not suitable for async processing */
            /* use persistent storage */
            /* arrays however are always non-local */
            buffer = &priv->result;
        }

        /* At init we may use a different offset */
        if (!interruptAccept && priv->initoffset != DONT_INIT)
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
                long off = offset;

                status = dbGetField(priv->offsetRecord, DBR_LONG, &buffer, &options, NULL, NULL);
                if (status == OK && buffer.severity == INVALID_ALARM) status = ERROR;
                if (status != OK)
                {
                    recGblSetSevr(record, LINK_ALARM, INVALID_ALARM);
                    regDevDebugLog(DBG_IN, "%s: cannot read offset from '%s'\n",
                        record->name, priv->offsetRecord->precord->name);
                    return status;
                }
                off += buffer.i * priv->offsetScale;
                if (off < 0)
                {
                    regDevDebugLog(DBG_IN, "%s: effective offset '%s'=%d * %d + %d = %ld < 0\n",
                        record->name, priv->offsetRecord->precord->name,
                        buffer.i, priv->offsetScale, offset, off);
                    return ERROR;
                }
                offset = off;
            }
        }
        priv->asyncOffset = offset;

        priv->status = OK;
        epicsMutexLock(device->accesslock);
        if (device->asupport && device->asupport->read)
        {
            if (!interruptAccept && priv->initDone == NULL)
                priv->initDone = epicsEventCreate(epicsEventEmpty);
            callbackSetCallback(regDevCallback, &priv->callback);
            callbackSetUser(record, &priv->callback);
            callbackSetPriority(record->prio, &priv->callback);
            status = device->asupport->read(device->driver,
                offset, dlen, nelem, buffer,
                &priv->callback, record->prio, &priv->status);
        }
        else if (device->support && device->support->read)
            status = device->support->read(device->driver,
                offset, dlen, nelem, buffer,
                record->prio);
        else status = ERROR;
        epicsMutexUnlock(device->accesslock);
    
        /* At init wait for completition of asynchronous device */
        if (!interruptAccept && status == ASYNC_COMPLETITION)
        {
            epicsEventWait(priv->initDone);
            status = priv->status;
        }
    }

    /* Some debug output */
    if (regDevDebug & DBG_IN)
    {
        if (status == ASYNC_COMPLETITION)
        {
            errlogSevPrintf(errlogInfo,
                "%s: async read %d * %d bit from %s:%u\n",
                record->name, nelem, dlen*8,
                device->name, priv->asyncOffset);
        }
        else switch (dlen)
        {
            case 1:
                errlogSevPrintf(errlogInfo,
                    "%s: read %d * 8 bit 0x%02x from %s:%u (status=%x)\n",
                    record->name, nelem, priv->result.uval8,
                    device->name, priv->asyncOffset, status);
                break;
            case 2:
                errlogSevPrintf(errlogInfo,
                    "%s: read %d * 16 bit 0x%04x from %s:%u (status=%x)\n",
                    record->name, nelem, priv->result.uval16,
                    device->name, priv->asyncOffset, status);
                break;
            case 4:
                errlogSevPrintf(errlogInfo,
                    "%s: read %d * 32 bit 0x%08x from %s:%u (status=%x)\n",
                    record->name, nelem, priv->result.uval32,
                    device->name, priv->asyncOffset, status);
                break;
            case 8:
                errlogSevPrintf(errlogInfo,
                    "%s: read %d * 64 bit 0x%016llx from %s:%u (status=%x)\n",
                    record->name, nelem, priv->result.uval64,
                    device->name, priv->asyncOffset, status);
                break;
            default:
                errlogSevPrintf(errlogInfo,
                    "%s: read %d * %d bit from %s:%u (status=%x)\n",
                    record->name, nelem, dlen*8,
                    device->name, priv->asyncOffset, status);
        }
    }

    if (status == ASYNC_COMPLETITION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_IN, "%s: wait for asynchronous read completition\n", record->name);
        record->pact = 1;
        return status;
    }
    
    if (status != OK && nelem != 0)
    {
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_IN, "%s: read error\n", record->name);
        return status;
    }
    
    if (buffer != NULL && nelem == 1 && device->asupport)
    {
        /* Got data after asyncronous completition:
           Copy result back into user buffer */
        memcpy(buffer, &priv->result, dlen);
    }

    return status;
}

int regDevWrite(dbCommon* record, unsigned int dlen, unsigned int nelem, void* pdata, void* mask)
{
    int status;
    unsigned int offset;  
    void* buffer;
    regDevPrivate* priv = record->dpvt;
    regDeviceNode* device;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevWrite %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);
    device = priv->device;
    assert (device);
    assert (pdata != NULL);
    
    if (record->pact)
    {
        /* Second call of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: asynchronous write returned %d\n", record->name, priv->status);

        if (priv->status != OK)
        {
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        }
        return priv->status;
    }
    
    /* First call of (probably asynchronous) device */

    if (nelem == 1 && device->asupport)
    {
        /* buffer may point to local var, not suitable for async processing */
        /* copy user data into persistent storage */
        /* arrays however are always non-local */
        buffer = &priv->result;
        memcpy(buffer, pdata, dlen);
    }
    else
    {
        buffer = pdata;
    }

    offset = priv->offset;
    if (priv->offsetRecord)
    {
        struct {
            DBRstatus
            epicsInt32 i;
        } buffer;
        long options = DBR_STATUS;
        long off = offset;

        status = dbGetField(priv->offsetRecord, DBR_LONG, &buffer, &options, NULL, NULL);
        if (status == OK && buffer.severity == INVALID_ALARM) status = ERROR;
        if (status != OK)
        {
            recGblSetSevr(record, LINK_ALARM, INVALID_ALARM);
            regDevDebugLog(DBG_OUT, "%s: cannot read offset from '%s'\n",
                record->name, priv->offsetRecord->precord->name);
            return status;
        }
        off += buffer.i * priv->offsetScale;
        if (off < 0)
        {
            regDevDebugLog(DBG_OUT, "%s: effective offset '%s'=%d * %d + %d = %ld < 0\n",
                record->name, priv->offsetRecord->precord->name,
                buffer.i, priv->offsetScale, offset, off);
            return ERROR;
        }
        offset = off;
    }

    /* Some debug output */
    if (regDevDebug  & DBG_OUT)
    {
        switch (dlen+(mask?10:0))
        {
            case 1:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 8 bit 0x%02x to %s:%u\n",
                    record->name, nelem, *(epicsUInt8*)buffer,
                    device->name, offset);
                break;
            case 2:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 16 bit 0x%04x to %s:%u\n",
                    record->name, nelem, *(epicsUInt16*)buffer,
                    device->name, offset);
                break;
            case 4:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 32 bit 0x%08x to %s:%u\n",
                    record->name, nelem, *(epicsUInt32*)buffer,
                    device->name, offset);
                break;
            case 8:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 64 bit 0x%016llx to %s:%u\n",
                    record->name, nelem, *(epicsUInt64*)buffer,
                    device->name, offset);
                break;
            case 11:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 8 bit 0x%02x mask 0x%02x to %s:%u\n",
                    record->name, nelem, *(epicsUInt8*)buffer, *(epicsUInt8*)mask,
                    device->name, offset);
            case 12:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 16 bit 0x%04x mask 0x%04x to %s:%u\n",
                    record->name, nelem, *(epicsUInt16*)buffer, *(epicsUInt16*)mask,
                    device->name, offset);
                break;
            case 14:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 32 bit 0x%08x mask 0x%08x to %s:%u\n",
                    record->name, nelem, *(epicsUInt32*)buffer, *(epicsUInt32*)mask,
                    device->name, offset);
                break;
            case 18:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * 64 bit 0x%016llx mask 0x%016llx to %s:%u\n",
                    record->name, nelem, *(epicsUInt64*)buffer, *(epicsUInt64*)mask,
                    device->name, offset);
                break;
            default:
                errlogSevPrintf(errlogInfo,
                    "%s: write %d * %d bit to %s:%u\n",
                    record->name, nelem, dlen*8,
                    device->name, offset);
        }
    }
    
    priv->status = OK;
    epicsMutexLock(device->accesslock);
    if (device->asupport && device->asupport->write)
    {
        if (!interruptAccept && priv->initDone == NULL)
            priv->initDone = epicsEventCreate(epicsEventEmpty);
        callbackSetCallback(regDevCallback, &priv->callback);
        callbackSetUser(record, &priv->callback);
        callbackSetPriority(record->prio, &priv->callback);
        status = device->asupport->write(device->driver,
            offset, dlen, nelem, buffer,
            &priv->callback, mask, record->prio, &priv->status);
    }
    else if (device->support && device->support->write)
        status = device->support->write(device->driver,
            offset, dlen, nelem, buffer,
            mask, record->prio);
    else status = ERROR;
    epicsMutexUnlock(device->accesslock);

    /* At init wait for completition of asynchronous device */
    if (!interruptAccept && status == ASYNC_COMPLETITION)
    {
        epicsEventWait(priv->initDone);
        status = priv->status;
    }

    if (status == ASYNC_COMPLETITION)
    {
        /* Prepare for  completition of asynchronous device */
        regDevDebugLog(DBG_OUT, "%s: wait for asynchronous write completition\n", record->name);
        record->pact = 1;
        return status;
    }

    if (status != OK)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "%s: write error\n", record->name);
    }
    
    return status;
}

int regDevReadScalar(dbCommon* record, epicsInt32* rval, double* fval, epicsUInt32 mask)
{
    int status = OK;
    epicsInt32 rv = 0;
    epicsFloat64 fv = 0.0;
    regDevPrivate* priv = record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr, "%s: not initialized\n", record->name);
        return ERROR;
    }

    status = regDevRead(record, priv->dlen, 1, NULL);
    if (status != OK)
        return status;

    switch (priv->dtype)
    {
        case epicsInt8T:
            rv = priv->result.sval8;
            break;
        case epicsUInt8T:
        case regDevBCD8T:
            rv = priv->result.uval8;
            break;
        case epicsInt16T:
            rv = priv->result.sval16;
            break;
        case epicsUInt16T:
        case regDevBCD16T:
            rv = priv->result.uval16;
            break;
        case epicsInt32T:
            rv = priv->result.sval32;
            break;
        case epicsUInt32T:
        case regDevBCD32T:
            rv = priv->result.uval32;
            break;
        case regDev64T:
            rv = priv->result.uval64; /* cut off high bits */
            break;
        case epicsFloat32T:
            fv = priv->result.fval32;
            break;
        case epicsFloat64T:
            fv = priv->result.fval64;
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return ERROR;
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
    *rval = (rv ^ priv->invert) & mask;
    return OK;
}

int regDevWriteScalar(dbCommon* record, epicsInt32 rval, double fval, epicsUInt32 mask)
{
    int status;
    regDevAnytype v, m;
    regDevPrivate* priv = record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return ERROR;
    }

    regDevCheckAsyncWriteResult(record);
    
    regDevDebugLog(DBG_OUT, "regDevWriteScalar(record=%s, rval=0x%08x , fval=%g, mask=0x%08x)\n",
        record->name, rval, fval, mask);
    
    /* enforce bounds */
    switch (priv->dtype)
    {
        case epicsInt8T:
        case epicsInt16T:
        case epicsInt32T:
            if ((epicsInt32)rval > (epicsInt32)priv->hwHigh)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from 0x%08x to upper bound 0x%08x\n",
                    record->name, rval, priv->hwHigh);
                rval = priv->hwHigh;
            }
            if ((epicsInt32)rval < (epicsInt32)priv->hwLow)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from 0x%08x to lower bound 0x%08x\n",
                    record->name, rval, priv->hwLow);
                rval = priv->hwLow;
            }
            break;
        case epicsUInt8T:
        case epicsUInt16T:
        case epicsUInt32T:
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if ((epicsUInt32)rval > (epicsUInt32)priv->hwHigh)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from 0x%08x to upper bound 0x%08x\n",
                    record->name, (epicsUInt32)rval, (epicsUInt32)priv->hwHigh);
                rval = priv->hwHigh;
            }
            if ((epicsUInt32)rval < (epicsUInt32)priv->hwLow)
            {
                regDevDebugLog(DBG_OUT, "%s: limit output from 0x%08x to lower bound 0x%08x\n",
                    record->name, (epicsUInt32)rval, (epicsUInt32)priv->hwLow);
                rval = priv->hwLow;
            }
            break;
    }    

    rval ^= priv->invert;
    
    switch (priv->dtype)
    {
        case regDevBCD8T:
            rval = i2bcd(rval);
        case epicsInt8T:
        case epicsUInt8T:
            v.uval8 = rval;
            m.uval8 = mask;
            status = regDevWrite(record, 1, 1, &v,
                m.uval8 == 0xFF ? NULL : &m);
            break;
        case regDevBCD16T:
            rval = i2bcd(rval);
        case epicsInt16T:
        case epicsUInt16T:
            v.uval16 = rval;
            m.uval16 = mask;
            status = regDevWrite(record, 2, 1, &v,
                m.uval16 == 0xFFFF ? NULL : &m);
            break;
        case regDevBCD32T:
            rval = i2bcd(rval);
        case epicsInt32T:
        case epicsUInt32T:
            v.uval32 = rval;
            m.uval32 = mask;
            status = regDevWrite(record, 4, 1, &v,
                m.uval32 == 0xFFFFFFFFUL ? NULL : &m);
            break;
        case regDev64T:
            v.uval64 = rval;
            m.uval64 = mask;
            status = regDevWrite(record, 8, 1, &v, &m);
            break;
        case epicsFloat32T:
            v.fval32 = fval;
            status = regDevWrite(record, 4, 1, &v, NULL);
            break;
        case epicsFloat64T:
            v.fval64 = fval;
            status = regDevWrite(record, 8, 1, &v, NULL);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            fprintf(stderr,
                "%s: unexpected data type requested\n",
                record->name);
            return ERROR;
    }
    return status;
}

int regDevReadArray(dbCommon* record, unsigned int nelm)
{
    int status = OK;
    int i;
    unsigned int dlen;
    int packing;
    regDevPrivate* priv = record->dpvt;
    
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return ERROR;
    }
    
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
        char* buffer = priv->result.buffer;
        dlen *= packing;
        for (i = 0; i < nelm/packing; i++)
        {
            status = regDevRead(record,
                dlen, 1, buffer+i*dlen);
            if (status != OK) return status;
            /* probably does not work async */
        }
    }
    else
    {
        packing = priv->arraypacking;
        status = regDevRead(record,
            dlen*packing, nelm/packing, priv->result.buffer);
    }

    if (status != OK) return status;
    
    switch (priv->dtype)
    {
        case regDevBCD8T:
        {
            epicsUInt8* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = bcd2i(buffer[i]);
            break;
        }
    }
    return OK;
}

int regDevWriteArray(dbCommon* record, unsigned int nelm)
{
    int status = 0;
    int i;
    unsigned int dlen;
    int packing;
    regDevPrivate* priv = record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return ERROR;
    }
    
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
            epicsUInt8* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
        case regDevBCD16T:
        {
            epicsUInt16* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
        case regDevBCD32T:
        {
            epicsUInt32* buffer = priv->result.buffer;
            for (i = 0; i < nelm; i++)
                buffer[i] = i2bcd(buffer[i]);
            break;
        }
    }

    packing = priv->fifopacking;
    if (packing)
    {
        /* FIFO: write element wise */
        char* buffer = priv->result.buffer;
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
        status = regDevWrite(record, dlen*packing, nelm/packing, priv->result.buffer, NULL);
    }
    return status;
}

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, unsigned int nelm, double low, double high)
{
    double o, s;
    unsigned int i;
    regDevPrivate* priv = record->dpvt;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevCheckType %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);

    s = (priv->hwHigh - priv->hwLow);
    o = (priv->hwHigh * high - priv->hwLow * low) / s;
    s = (high - low) / s;

    switch (priv->dtype)
    {
        case epicsInt8T:
        {
            epicsInt8* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
        case epicsUInt8T:
        case regDevBCD8T:
        {
            epicsUInt8* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
        case epicsInt16T:
        {
            epicsInt16* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
        case epicsUInt16T:
        case regDevBCD16T:
        {
            epicsUInt16* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
        case epicsInt32T:
        {
            epicsInt32* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            epicsUInt32* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) v[i] = (r[i]+o)*s;
            }
            else break;
            return OK;
        }
    }
    recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
    fprintf(stderr,
        "%s: unexpected conversion from %s to %s\n",
        record->name, regDevTypeName(priv->dtype), pamapdbfType[ftvl].strvalue);
    return ERROR;
}

int regDevScaleToRaw(dbCommon* record, int ftvl, void* val, unsigned int nelm, double low, double high)
{
    double o, s;
    unsigned int i;
    regDevPrivate* priv = record->dpvt;
    
    if (priv == NULL)
    {
        fprintf(stderr,
            "regDevCheckType %s: uninitialized record\n",
            record->name);
        return ERROR;
    }
    assert (priv->magic == MAGIC);

    s = (priv->hwHigh - priv->hwLow);
    o = (priv->hwLow * low - priv->hwHigh * high) / s;
    s = s / (high - low);

    switch (priv->dtype)
    {
        case epicsInt8T:
        {
            epicsInt8* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
        case epicsUInt8T:
        case regDevBCD8T:
        {
            epicsUInt8* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
        case epicsInt16T:
        {
            epicsInt16* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
        case epicsUInt16T:
        case regDevBCD16T:
        {
            epicsUInt16* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
        case epicsInt32T:
        {
            epicsInt32* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
        case epicsUInt32T:
        case regDevBCD32T:
        {
            epicsUInt32* r = priv->result.buffer;
            if (ftvl == DBF_DOUBLE)
            {
                double *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else if (ftvl == DBF_FLOAT)
            {
                float *v = val;
                for (i = 0; i < nelm; i++) r[i] = v[i]*s+o;
            }
            else break;
            return OK;
        }
    }
    recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
    fprintf(stderr,
        "%s: unexpected conversion from %s to %s\n",
        record->name, pamapdbfType[ftvl].strvalue, regDevTypeName(priv->dtype));
    return ERROR;
}
