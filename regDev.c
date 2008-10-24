/* Generic register Device Support */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#define epicsTypesGLOBAL

#include <alarm.h>
#include <dbAccess.h>
#include <recGbl.h>
#include <devLib.h>
#include <devSup.h>
#include <drvSup.h>
#include <dbCommon.h>
#include <assert.h>

#include "regDev.h"

#ifdef EPICS_3_14
#include <epicsExport.h>
#else
#define S_dev_success 0
#define S_dev_badArgument -1
#endif

static char cvsid_regDev[] __attribute__((unused)) =
    "$Id: regDev.c,v 1.3 2008/10/24 07:46:42 zimoch Exp $";

typedef struct regDeviceNode {
    const char* name;              /* Device name */
    const regDevSupport* support;  /* Device function table */
    regDevice* driver;             /* Generic device driver */
    struct regDeviceNode* next;    /* Next registered device */
} regDeviceNode;

typedef struct regDevPrivate{
    regDeviceNode* device;
    unsigned long offset;      /* Offset (in bytes) within device memory */
    unsigned long initoffset;  /* Offset to initialize output records */
    unsigned short bit;        /* Bit number (0-15) for bi/bo */
    unsigned short dtype;      /* Data type */
    unsigned short dlen;       /* Data length (in bytes) */
    short fifopacking;         /* Fifo: elelents in one register */
    epicsInt32 hwLow;          /* Hardware Low limit */
    epicsInt32 hwHigh;         /* Hardware High limit */
} regDevPrivate;

static regDeviceNode* registeredDevices = NULL;

int regDevDebug = 0;

#ifdef EPICS_3_14
epicsExportAddress(int, regDevDebug);
#endif

/***********************************************************************
 * Routine to parse IO arguments
 * IO address line format:
 *
 * <name>/<a>[+<o>][![i[+<o>]]] [T=<type>] [B=<bit>] [L=<low|strLen>] [H=<high>]
 *
 * where: <name>    - symbolic device name
 *        <a+o>     - address (byte number) within memory block
 *        <i+o>     - optional init read address ( for output records )
 *        <type>    - INT8, INT16, INT32,
 *                    UINT16 (or UNSIGN16), UINT32 (or UNSIGN32),
 *                    REAL32 (or FLOAT), REAL64 (or DOUBLE),
 *                    STRING,BCD
 *        <bit>     - least significant bit is 0
 *        <low>     - raw value that mapps to EGUL
 *        <high>    - raw value that mapps to EGUF
 **********************************************************************/

#define DONT_INIT (0xFFFFFFFFUL)
#define regDevBCD8T  (100)
#define regDevBCD16T (101)
#define regDevBCD32T (102)

const static char* bcdTypeNames [] = { 
    "regDevBCD8T",
    "regDevBCD16T",
    "regDevBCD32T"
};

STATIC int regDevIoParse2(
    const char* recordName,
    char* p,
    regDevPrivate* priv)
{
    char devName[255];
    regDeviceNode* device;
    char separator;
    int nchar, i;
    int status = 0;
    
    const struct {char* name; int dlen; epicsType type;} datatypes [] =
    {
        { "INT8",     1, epicsInt8T    },
        
        { "UINT8",    1, epicsUInt8T   },
        { "UNSIGN8",  1, epicsUInt8T   },
        { "BYTE",     1, epicsUInt8T   },
        { "CHAR",     1, epicsUInt8T   },
        
        { "INT16",    2, epicsInt16T   },
        { "SHORT",    2, epicsInt16T   },
        
        { "UINT16",   2, epicsUInt16T  },
        { "UNSIGN16", 2, epicsUInt16T  },
        { "WORD",     2, epicsUInt16T  },
        
        { "INT32",    4, epicsInt32T   },
        { "LONG",     4, epicsInt32T   },
        
        { "UINT32",   4, epicsUInt32T  },
        { "UNSIGN32", 4, epicsUInt32T  },
        { "DWORD",    4, epicsUInt32T  },

        { "REAL32",   4, epicsFloat32T },
        { "FLOAT32",  4, epicsFloat32T },
        { "FLOAT",    4, epicsFloat32T },

        { "REAL64",   8, epicsFloat64T },
        { "FLOAT64",  8, epicsFloat64T },
        { "DOUBLE",   8, epicsFloat64T },

        { "BCD8",     1, regDevBCD8T    },
        { "BCD16",    2, regDevBCD16T   },
        { "BCD32",    4, regDevBCD32T   },
        { "BCD",      1, regDevBCD8T    },
        { "TIME",     1, regDevBCD8T    }
    };

    /* Get rid of leading whitespace and non-alphanumeric chars */
    while (!isalnum((unsigned char)*p))
        if (*p++ == '\0') return S_dev_badArgument;

    /* Get device name */
    nchar = strcspn(p, "/");
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
        errlogSevPrintf(errlogFatal,
            "regDevIoParse %s: device not found\n",
            recordName);
        return S_dev_noDevice;
    }
    priv->device = device;

    /* Check device offset */
    if (separator == '/')
    {
        priv->offset = strtol(p, &p, 0);
        separator = *p++;
        /* Handle any number of optional +o additions to the offset */
        while (separator == '+')
        {
            priv->offset += strtol(p, &p, 0);
            separator = *p++;
        }
        regDevDebugLog(1,
            "regDevIoParse %s: offset=%ld\n", recordName, priv->offset);
    }
    else
    {
        priv->offset = 0;
    }

    /* Check init offset */
    if (separator == '!')
    {
        char* p1;
        priv->initoffset = strtol(p, &p1, 0);
        if (p1 == p)
        {
            priv->initoffset = priv->offset;
            separator = *p++;
        }
        else
        {
            p = p1;
            separator = *p++;
            /* Handle any number of optional +o additions to the init offset */
            while (separator == '+')
            {
                priv->initoffset += strtol(p, &p, 0);
                separator = *p++;
            }
        }
        regDevDebugLog(1,
            "regDevIoParse %s: init offset=%ld\n", recordName, priv->initoffset);
    }
    else
    {
        priv->initoffset = DONT_INIT;
    }

    /* set default values for parameters */
    if (!priv->dtype && !priv->dlen)
    {
        priv->dtype = epicsInt16T;
        priv->dlen = 2;
    }
    priv->bit = 0;
    priv->hwLow = 0;
    priv->hwHigh = 0;
    
    /* allow whitespaces before parameter for device support */
    while ((separator == '\t') || (separator == ' '))
        separator = *p++;

    /* driver parameter for device support if present */
    nchar = 0;
    if (separator != '\'') p--; /* optional quote for compatibility */
    
    /* parse parameters */
    while (p && *p)
    {
        switch (*p)
        {
            case ' ':
            case '\t':
                p++;
                break;
            case 'T': /* T=<datatype> */
                p+=2; 
                
                if (strncmp(p,"STRING",6) == 0)
                {
                    priv->dtype = epicsStringT;
                    p += 6;
                }
                else
                {
                    static int maxtype =
                        sizeof(datatypes)/sizeof(*datatypes);
                    for (i = 0; i < maxtype; i++)
                    {
                        nchar = strlen(datatypes[i].name);
                        if (strncmp(p, datatypes[i].name, nchar) == 0)
                        {
                            priv->dtype = datatypes[i].type;
                            priv->dlen = datatypes[i].dlen;
                            p += nchar;
                            break;
                        }
                    }
                    if (i == maxtype)
                    {
                        errlogSevPrintf(errlogFatal,
                            "regDevIoParse %s: invalid datatype %s\n",
                            recordName, p);
                        return S_dev_badArgument;
                    }
                }
                break;
            case 'B': /* B=<bitnumber> */
                p += 2;
                priv->bit = strtol(p,&p,0);
                break;
            case 'L': /* L=<low raw value> (converts to EGUL)*/
                p += 2;
                priv->hwLow = strtol(p,&p,0);
                break;
            case 'H': /* L=<high raw value> (converts to EGUF)*/
                p += 2;
                priv->hwHigh = strtol(p,&p,0);
                break;
            case 'P': /* P=<packing> (for fifo)*/
                p += 2;
                priv->fifopacking = strtol(p,&p,0);
                break;
            case '\'':
                if (separator == '\'')
                {
                    p = 0;
                    break;
                }
            default:
                errlogSevPrintf(errlogFatal,
                    "regDevIoParse %s: unknown parameter '%c'\n",
                    recordName, *p);
                return S_dev_badArgument;
        }
    }
    
    /* for T=STRING L=... means length, not low */
    if (priv->dtype == epicsStringT && priv->hwLow)
    {
        priv->dlen = priv->hwLow;
        priv->hwLow = 0;
    }
    
    /* check if bit number is in range */
    if (priv->bit && priv->bit >= priv->dlen*8)
    {
        errlogSevPrintf(errlogFatal,
            "regDevIoParse %s: invalid bit number %d (>%d)\n",
            recordName, priv->bit, priv->dlen*8-1);
        return S_dev_badArgument;
    }
    
    /* get default values for L and H if user did'n define them */
    switch (priv->dtype)
    {
        case epicsUInt8T:
            if (priv->hwHigh > 0xFF) status = S_dev_badArgument;
            if (!priv->hwHigh) priv->hwLow = 0x00;
            if (!priv->hwHigh) priv->hwHigh = 0xFF;
            break;
        case epicsUInt16T:
            if (priv->hwHigh > 0xFFFF) status = S_dev_badArgument;
            if (!priv->hwHigh) priv->hwLow = 0x0000;
            if (!priv->hwHigh) priv->hwHigh = 0xFFFF;
            break;
        case epicsUInt32T:
            if (!priv->hwHigh) priv->hwLow = 0x00000000;
            if (!priv->hwHigh) priv->hwHigh = 0xFFFFFFFF;
            break;
        case epicsInt8T:
            if (priv->hwHigh > 0x7F) status = S_dev_badArgument;
            if (!priv->hwHigh) priv->hwLow = 0xFFFFFF81;
            if (!priv->hwHigh) priv->hwHigh = 0x0000007F;
            break;
        case epicsInt16T:
            if (priv->hwHigh > 0x7FFF) status = S_dev_badArgument;
            if (!priv->hwHigh) priv->hwLow = 0xFFFF8001;
            if (!priv->hwHigh) priv->hwHigh = 0x00007FFF;
            break;
        case epicsInt32T:
            if (!priv->hwHigh) priv->hwLow = 0x80000001;
            if (!priv->hwHigh) priv->hwHigh = 0x7FFFFFFF;
            break;
        default:
            if (priv->hwHigh || priv->hwLow) {
                errlogSevPrintf(errlogMinor,
                    "regDevIoParse %s: L or H makes"
                    " no sense with this data type\n",
                    recordName);
            } 
            break;   
    }
    regDevDebugLog(1, "regDevIoParse %s: dlen=%d\n",recordName, priv->dlen);
    regDevDebugLog(1, "regDevIoParse %s: B=%d\n",   recordName, priv->bit);
    regDevDebugLog(1, "regDevIoParse %s: L=%#x\n",  recordName, priv->hwLow);
    regDevDebugLog(1, "regDevIoParse %s: H=%#x\n",  recordName, priv->hwHigh);

    if (status)
    {
        errlogSevPrintf(errlogMinor,
            "regDevIoParse %s: L or H out of range for this data type\n",
            recordName);
        return status;
    }
    
    return 0;
}

STATIC int regDevIoParse(dbCommon* record, struct link* link)
{
    int status;

    if (link->type != INST_IO)
    {
        errlogSevPrintf(errlogFatal,
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

/*********  Diver registration and access ****************************/

int regDevRegisterDevice(const char* name,
    const regDevSupport* support, regDevice* driver)
{
    char* nameCpy;
    
    regDeviceNode **pdevice; 
    for (pdevice=&registeredDevices; *pdevice; pdevice=&(*pdevice)->next);
    *pdevice = (regDeviceNode*) calloc(1, sizeof(regDevPrivate));
    if (*pdevice == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "regDevRegisterDevice %s: out of memory\n",
            name);
        return -1;
    }
    nameCpy = malloc(strlen(name)+1);
    strcpy(nameCpy, name);
    (*pdevice)->name = nameCpy;
    (*pdevice)->support = support;
    (*pdevice)->driver = driver;
    return 0;
}

/*********  Support for "I/O Intr" for input records ******************/

STATIC long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevPrivate* priv = record->dpvt;
    if (priv == NULL)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetInIntInfo %s: uninitialized record",
            record->name);
        return -1;
    }
    if (!priv->device->support->getInScanPvt)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetInIntInfo %s: input I/O Intr unsupported for bus %s\n",
            record->name, priv->device->name);
        return -1;
    }
    *ppvt = priv->device->support->getInScanPvt(
        priv->device->driver, priv->offset);
    if (*ppvt == NULL)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetInIntInfo %s: no I/O Intr for bus %s offset %#lx\n",
            record->name, priv->device->name, priv->offset);
        return -1;
    }
    return 0;
}

/*********  Support for "I/O Intr" for output records ****************/

STATIC long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt)
{
    regDevPrivate* priv = record->dpvt;
    if (priv == NULL)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetOutIntInfo %s: uninitialized record",
            record->name);
        return -1;
    }
    if (!priv->device->support->getOutScanPvt)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetOutIntInfo %s: output I/O Intr unsupported for bus %s\n",
            record->name, priv->device->name);
        return -1;
    }
    *ppvt = priv->device->support->getOutScanPvt(
        priv->device->driver, priv->offset);
    if (*ppvt == NULL)
    {
        errlogSevPrintf(errlogMajor,
            "regDevGetOutIntInfo %s: no I/O Intr for bus %s offset %#lx\n",
            record->name, priv->device->name, priv->offset);
        return -1;
    }
    return 0;
}

/*********  Report routine ********************************************/

long regDevReport(int level)
{
    regDeviceNode *device;
    
    printf("  regDev version: %s\n", cvsid_regDev);
    if (level < 1) return 0;
    printf("  registered Devices:\n");
    for (device=registeredDevices; device; device=device->next)
    {
        printf ("    \"%s\" ", device->name);
        if (level>1 && device->support->report)
            device->support->report(device->driver, level-1);
        else printf ("\n");
    }
    return 0;
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

#ifdef EPICS_3_14
epicsExportAddress(drvet, regDev);
#endif

/* routine to convert bytes from BCD to integer format. */
 
__inline__ unsigned long bcd2i(unsigned long bcd)
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
 
__inline__  unsigned long i2bcd(unsigned long i)
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

STATIC regDevPrivate* regDevAllocPriv(dbCommon *record)
{
    regDevDebugLog(1, "regDevAllocPriv(%s)\n", record->name);
    record->dpvt = calloc(1, sizeof(regDevPrivate));
    if (record->dpvt == NULL)
    {
        errlogSevPrintf(errlogFatal,
            "regDevAllocPriv %s: out of memory\n",
            record->name);
        return NULL;
    }
    return (regDevPrivate*) record->dpvt;
}

#define TYPE_INT    1
#define TYPE_FLOAT  2
#define TYPE_STRING 4
#define TYPE_BCD 4

STATIC long regDevAssertType(dbCommon *record, int types)
{
    unsigned short dtype;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;
    assert(priv);
    dtype = priv->dtype;
    
    regDevDebugLog(1, "regDevAssertType(%s,%s%s%s%s) %s\n",
        record->name,
        types && TYPE_INT ? " INT" : "",
        types && TYPE_FLOAT ? " FLOAT" : "",
        types && TYPE_STRING ? " STRING" : "",
        types && TYPE_BCD ? " BCD" : "",
        dtype < regDevBCD8T ? epicsTypeNames[dtype] : bcdTypeNames[dtype-regDevBCD8T]);
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case epicsInt16T:
        case epicsUInt16T:
        case epicsInt32T:
        case epicsUInt32T:
            if (types & TYPE_INT) return S_dev_success;
            break;
        case epicsFloat32T:
        case epicsFloat64T:
            if (types & TYPE_FLOAT) return S_dev_success;
            break;
        case epicsStringT:
            if (types & TYPE_STRING) return S_dev_success;
            break;
        case regDevBCD8T:
        case regDevBCD16T:
        case regDevBCD32T:
            if (types & TYPE_BCD) return S_dev_success;
            break;
    }
    errlogSevPrintf(errlogFatal,
        "regDevAssertType %s: illegal data type\n",
        record->name);
    free(record->dpvt);
    record->dpvt = NULL;
    return S_db_badField;
}

STATIC int regDevCheckFTVL(dbCommon* record, int ftvl)
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
    errlogSevPrintf(errlogFatal,
        "regDevCheckFTVL %s: illegal FTVL value\n",
        record->name);
    return S_db_badField;
}

STATIC int regDevCheckType(dbCommon* record, int ftvl, int nelm)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    
    regDevDebugLog(1, "regDevCheckType(%s, %s, %i)\n",
        record->name,
        pamapdbfType[ftvl].strvalue,
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
             break;
        case regDevBCD32T:
        case epicsInt32T:
        case epicsUInt32T:
            if ((ftvl == DBF_LONG) || (ftvl == DBF_ULONG))
                return S_dev_success;
            break;
    }
    free(record->dpvt);
    record->dpvt = NULL;
    errlogSevPrintf(errlogFatal,
        "regDevCheckType %s: data type does not match FTVL\n",
        record->name);
    return S_db_badField;
}

STATIC int regDevReadInt(dbCommon* record, epicsInt32* val)
{
    int status;
    epicsUInt8 val8;
    epicsUInt16 val16;
    epicsInt32 val32;
    unsigned short dtype;
    unsigned long offset;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    
    regDevDebugLog(2, "regDevReadInt(%s) start\n", record->name);
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    regDevDebugLog(2, "regDevReadInt(%s) read from %s/0x%lx\n",
        record->name, priv->device->name, offset);
    switch (dtype)
    {
        case epicsInt8T:
        case epicsUInt8T:
        case regDevBCD8T:
            status = regDevRead(priv->device, offset,
                1, &val8);
            regDevDebugLog(3, "%s: read 8bit %02x from %s/0x%lx (status=%x)\n",
                record->name, val8, priv->device->name, offset, status);
            val32 = val8;
            break;
        case epicsInt16T:
        case epicsUInt16T:
        case regDevBCD16T:
            status = regDevRead(priv->device, offset,
                2, &val16);
            regDevDebugLog(3, "%s: read 16bit %04x from %s/0x%lx (status=%x)\n",
                record->name, val16, priv->device->name, offset, status);
            val32 = val16;
            break;
        case epicsInt32T:
        case epicsUInt32T:
        case regDevBCD32T:
            status = regDevRead(priv->device, offset,
                4, &val32);
            regDevDebugLog(3, "%s: read 32bit %04x from %s/0x%lx (status=%x)\n",
                record->name, val32, priv->device->name, offset, status);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            errlogSevPrintf(errlogFatal,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        errlogSevPrintf(errlogFatal,
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
    *val = val32;
    regDevDebugLog(2, "regDevReadInt(%s) done\n", record->name);
    record->udf = FALSE;
    if (!interruptAccept)
    {
        /* initialize output record to valid state */
        record->sevr = NO_ALARM;
        record->stat = NO_ALARM;
    }
    return status;
}

STATIC int regDevWriteInt(dbCommon* record, epicsUInt32 val, epicsUInt32 mask)
{
    int status;
    epicsUInt8 rval8, mask8;
    epicsUInt16 rval16, mask16;
    epicsUInt32 rval32, mask32;
    unsigned short dtype;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    dtype = priv->dtype;
    switch (dtype)
    {
        case regDevBCD8T:
            val = i2bcd(val);
        case epicsInt8T:
        case epicsUInt8T:
            rval8 = val;
            mask8 = mask;
            regDevDebugLog(2, "%s: write 8bit %02x mask %02x to %s/0x%lx\n",
                record->name, rval8, mask8, priv->device->name, priv->offset);
            status = regDevWriteMasked(priv->device, priv->offset,
                1, &rval8, mask8 == 0xFF ? NULL : &mask8);
            break;
        case regDevBCD16T:
            val = i2bcd(val);
        case epicsInt16T:
        case epicsUInt16T:
            rval16 = val;
            mask16 = mask;
            regDevDebugLog(2, "%s: write 16bit %04x mask %04x to %s/0x%lx\n",
                record->name, rval16, mask16,priv->device->name,  priv->offset);
            status = regDevWriteMasked(priv->device, priv->offset,
                2, &rval16, mask16 == 0xFFFF ? NULL : &mask16);
            break;
        case regDevBCD32T:
            val = i2bcd(val);
        case epicsInt32T:
        case epicsUInt32T:
            rval32 = val;
            mask32 = mask;
            regDevDebugLog(2, "%s: write 32bit %08x mask %08x to %s/0x%lx\n",
                record->name, rval32, mask32, priv->device->name, priv->offset);
            status = regDevWriteMasked(priv->device, priv->offset,
                4, &rval32, mask32 == 0xFFFFFFFFUL ? NULL : &mask32);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            errlogSevPrintf(errlogFatal,
                "%s: unexpected data type requested\n",
                record->name);
            return -1;
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: write error\n", record->name);
    }
    return status;
}

STATIC long regDevReadArr(dbCommon* record, void* bptr, long nelm)
{
    int status = 0;
    int i;
    unsigned long offset;
    unsigned long dlen;
    int packing;
    regDevPrivate* priv;
    regDeviceNode* device;
    
    priv = (regDevPrivate*)record->dpvt;
    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    device=priv->device;
    assert(device);
    
    if (!interruptAccept && priv->initoffset != DONT_INIT)
        offset = priv->initoffset;
    else
        offset = priv->offset;
    
    dlen = priv->dlen;
    packing = priv->fifopacking;
    if (packing)
    {
        dlen *= packing;
        for (i = 0; i < nelm/packing; i++)
        {
            status = regDevRead(priv->device, offset,
               dlen, (char*)bptr+i*dlen);
            if (status) break;
        }
    }
    else
    {
        status = regDevReadArray(priv->device, offset,
            dlen, nelm, bptr);
    }
    
    regDevDebugLog(3,
        "%s: read %ld values of %d bytes to %p status=%d\n",
        record->name, nelm, priv->dlen, bptr, status);

    if (status)
    {
        errlogSevPrintf(errlogFatal,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
    }
    else
    {
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
    }
    return status;
}

STATIC long regDevReadAnalog(dbCommon* record, epicsInt32* rval, double* fval)
{
    int status;
    signed char sval8;
    unsigned char uval8;
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
        errlogSevPrintf(errlogFatal, "%s: not initialized\n", record->name);
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
                1, &sval8);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, sval8);
            *rval = sval8;
            break;
        case epicsUInt8T:
            status = regDevRead(priv->device, offset,
                1, &uval8);
            regDevDebugLog(3, "%s: read 8bit %02x\n",
                record->name, uval8);
            *rval = uval8;
            break;
        case epicsInt16T:
            status = regDevRead(priv->device, offset,
                2, &sval16);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, sval16);
            *rval = sval16;
            break;
        case epicsUInt16T:
            status = regDevRead(priv->device, offset,
                2, &uval16);
            regDevDebugLog(3, "%s: read 16bit %04x\n",
                record->name, uval16);
            *rval = uval16;
            break;
        case epicsInt32T:
            status = regDevRead(priv->device, offset,
                4, &sval32);
            regDevDebugLog(3, "ai %s: read 32bit %04x\n",
                record->name, sval32);
            *rval = sval32;
            break;
        case epicsUInt32T:
            status = regDevRead(priv->device, offset,
                4, &uval32);
            regDevDebugLog(3, "ai %s: read 32bit %04x\n",
                record->name, uval32);
            *rval = uval32;
            break;
        case epicsFloat32T:
            status = regDevRead(priv->device, offset,
                4, &val32);
            regDevDebugLog(3, "ai %s: read 32bit %04lx = %g\n",
                record->name, val32.u, val32.f);
            *fval = val32.f;
            break;
        case epicsFloat64T:
            status = regDevRead(priv->device, offset,
                8, &val64);
            regDevDebugLog(3, "ai %s: read 64bit %08Lx = %g\n",
                record->name, val64.u, val64.f);
            *fval = val64.f;
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            errlogSevPrintf(errlogFatal,
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
        errlogSevPrintf(errlogFatal,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
    if (dtype == epicsFloat32T || dtype == epicsFloat64T)
    {
        record->udf = FALSE;
        return 2;
    }
    return 0;
}

STATIC long regDevWriteAnalog(dbCommon* record, epicsInt32 rval, double fval)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    epicsUInt8 rval8;
    epicsUInt16 rval16;
    epicsUInt32 rval32;
    union {epicsFloat32 f; long u;} val32;
    union {epicsFloat64 f; long long u;} val64;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    rval32 = rval;
    switch (priv->dtype)
    {
        case epicsInt8T:
            if (rval > priv->hwHigh) rval32 = priv->hwHigh;
            if (rval < priv->hwLow) rval32 = priv->hwLow;
            rval8 = rval32;
            regDevDebugLog(2, "%s: write 8bit %02x\n",
                record->name, rval8 & 0xff);
            status = regDevWrite(priv->device, priv->offset,
                1, &rval8);
            break;
        case epicsUInt8T:
            if (rval32 > (epicsUInt32)priv->hwHigh) rval32 = priv->hwHigh;
            if (rval32 < (epicsUInt32)priv->hwLow) rval32 = priv->hwLow;
            rval8 = rval32;
            regDevDebugLog(2, "%s: write 8bit %02x\n",
                record->name, rval8 & 0xff);
            status = regDevWrite(priv->device, priv->offset,
                1, &rval8);
            break;
        case epicsInt16T:
            if (rval > priv->hwHigh) rval32 = priv->hwHigh;
            if (rval < priv->hwLow) rval32 = priv->hwLow;
            rval16 = rval32;
            regDevDebugLog(2, "%s: write 16bit %04x\n",
                record->name, rval16 & 0xffff);
            status = regDevWrite(priv->device, priv->offset,
                2, &rval16);
            break;
        case epicsUInt16T:
            if (rval32 > (epicsUInt32)priv->hwHigh) rval32 = priv->hwHigh;
            if (rval32 < (epicsUInt32)priv->hwLow) rval32 = priv->hwLow;
            rval16 = rval32;
            regDevDebugLog(2, "%s: write 16bit %04x\n",
                record->name, rval16 & 0xffff);
            status = regDevWrite(priv->device, priv->offset,
                2, &rval16);
            break;
        case epicsInt32T:
            if (rval > priv->hwHigh) rval32 = priv->hwHigh;
            if (rval < priv->hwLow) rval32 = priv->hwLow;
            regDevDebugLog(2, "%s: write 32bit %08x\n",
                record->name, rval32);
            status = regDevWrite(priv->device, priv->offset,
                4, &rval32);
            break;
        case epicsUInt32T:
            if (rval32 > (epicsUInt32)priv->hwHigh) rval32 = priv->hwHigh;
            if (rval32 < (epicsUInt32)priv->hwLow) rval32 = priv->hwLow;
            regDevDebugLog(2, "%s: write 32bit %08x\n",
                record->name, rval32);
            status = regDevWrite(priv->device, priv->offset,
                4, &rval32);
            break;
        case epicsFloat32T:
            /* emulate scaling */
            val32.f = fval;
            regDevDebugLog(2, "%s: write 32bit %08lx %g\n",
                record->name, val32.u, val32.f);
            status = regDevWrite(priv->device, priv->offset,
                4, &val32);
            break;
        case epicsFloat64T:
            /* emulate scaling */
            val64.f = fval;
            regDevDebugLog(2, "%s: write 64bit %016Lx %g\n",
                record->name, val64.u, val64.f);
            status = regDevWrite(priv->device, priv->offset,
                8, &val64);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            errlogSevPrintf(errlogFatal,
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
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

/* generic device support structure *********************************/

struct devsup {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN io;
};

/* bi for status bit ************************************************/

#include <biRecord.h>

STATIC long regDevInitRecordStat(biRecord *);
STATIC long regDevReadStat(biRecord *);

struct devsup regDevStat =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStat,
    regDevGetInIntInfo,
    regDevReadStat
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevStat);
#endif

STATIC long regDevInitRecordStat(biRecord* record)
{
    int status;

    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    return 0;
}

STATIC long regDevReadStat(biRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevReadArray(priv->device, 0, 0, 0, NULL);
    if (status)
    {
        errlogSevPrintf(errlogFatal,
            "%s: read error\n", record->name);
        record->rval = 0;
        return 0;
    }
    if (status)
    {
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
        return status;
    }
    record->rval = 1;
    return 0;
}

/* bi ***************************************************************/

STATIC long regDevInitRecordBi(biRecord *);
STATIC long regDevReadBi(biRecord *);

struct devsup regDevBi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordBi,
    regDevGetInIntInfo,
    regDevReadBi
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevBi);
#endif

STATIC long regDevInitRecordBi(biRecord* record)
{
    int status;
    regDevPrivate* priv;
    
    regDevDebugLog(1, "regDevInitRecordBi(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    record->mask = 1 << priv->bit;
    regDevDebugLog(1, "regDevInitRecordBi(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadBi(biRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadInt((dbCommon*)record, &rval);
    if (!status) record->rval = rval & record->mask;
    return status;
}

/* bo ***************************************************************/

#include <boRecord.h>

STATIC long regDevInitRecordBo(boRecord *);
STATIC long regDevWriteBo(boRecord *);

struct devsup regDevBo =
{
    5,
    NULL,
    NULL,
    regDevInitRecordBo,
    regDevGetOutIntInfo,
    regDevWriteBo
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevBo);
#endif

STATIC long regDevInitRecordBo(boRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordBo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    record->mask = 1 << priv->bit;
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadInt((dbCommon*)record, &rval);
        if (!status) record->rval = rval & record->mask;
    }
    regDevDebugLog(1, "regDevInitRecordBo(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteBo(boRecord* record)
{
    return regDevWriteInt((dbCommon*)record, record->rval, record->mask);
}

/* mbbi *************************************************************/

#include <mbbiRecord.h>

STATIC long regDevInitRecordMbbi(mbbiRecord *);
STATIC long regDevReadMbbi(mbbiRecord *);

struct devsup regDevMbbi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbi,
    regDevGetInIntInfo,
    regDevReadMbbi
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevMbbi);
#endif

STATIC long regDevInitRecordMbbi(mbbiRecord* record)
{
    int status;

    regDevDebugLog(1, "regDevInitRecordMbbi(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0) record->mask <<= record->shft;
    regDevDebugLog(1, "regDevInitRecordMbbi(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadMbbi(mbbiRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadInt((dbCommon*)record, &rval);
    if (!status) record->rval = rval & record->mask;
    return status;
}

/* mbbo *************************************************************/

#include <mbboRecord.h>

STATIC long regDevInitRecordMbbo(mbboRecord *);
STATIC long regDevWriteMbbo(mbboRecord *);

struct devsup regDevMbbo =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbo,
    regDevGetOutIntInfo,
    regDevWriteMbbo
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevMbbo);
#endif

STATIC long regDevInitRecordMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordMbbo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0) record->mask <<= record->shft;
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadInt((dbCommon*)record, &rval);
        if (!status) record->rval = rval & record->mask;
    }
    regDevDebugLog(1, "regDevInitRecordMbbo(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteMbbo(mbboRecord* record)
{
    return regDevWriteInt((dbCommon*)record, record->rval, record->mask);
}

/* mbbiDirect *******************************************************/

#include <mbbiDirectRecord.h>

STATIC long regDevInitRecordMbbiDirect(mbbiDirectRecord *);
STATIC long regDevReadMbbiDirect(mbbiDirectRecord *);

struct devsup regDevMbbiDirect =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbiDirect,
    regDevGetInIntInfo,
    regDevReadMbbiDirect
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevMbbiDirect);
#endif

STATIC long regDevInitRecordMbbiDirect(mbbiDirectRecord* record)
{
    int status;

    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0) record->mask <<= record->shft;
    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadInt((dbCommon*)record, &rval);
    if (!status) record->rval = rval & record->mask;
    return status;
}

/* mbboDirect *******************************************************/

#include <mbboDirectRecord.h>

STATIC long regDevInitRecordMbboDirect(mbboDirectRecord *);
STATIC long regDevWriteMbboDirect(mbboDirectRecord *);

struct devsup regDevMbboDirect =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbboDirect,
    regDevGetOutIntInfo,
    regDevWriteMbboDirect
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevMbboDirect);
#endif

STATIC long regDevInitRecordMbboDirect(mbboDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0) record->mask <<= record->shft;
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadInt((dbCommon*)record, &rval);
        if (!status) record->rval = rval & record->mask;
    }
    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteMbboDirect(mbboDirectRecord* record)
{
    return regDevWriteInt((dbCommon*)record, record->rval, record->mask);
}

/* longin ***********************************************************/

#include <longinRecord.h>

STATIC long regDevInitRecordLongin(longinRecord *);
STATIC long regDevReadLongin(longinRecord *);

struct devsup regDevLongin =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLongin,
    regDevGetInIntInfo,
    regDevReadLongin
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevLongin);
#endif

STATIC long regDevInitRecordLongin(longinRecord* record)
{
    int status;

    regDevDebugLog(1, "regDevInitRecordLongin(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    regDevDebugLog(1, "regDevInitRecordLongin(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadLongin(longinRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadInt((dbCommon*)record, &rval);
    if (!status) record->val = rval;
    return status;
}

/* longout **********************************************************/

#include <longoutRecord.h>

STATIC long regDevInitRecordLongout(longoutRecord *);
STATIC long regDevWriteLongout(longoutRecord *);

struct devsup regDevLongout =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLongout,
    regDevGetOutIntInfo,
    regDevWriteLongout
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevLongout);
#endif

STATIC long regDevInitRecordLongout(longoutRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordLongout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadInt((dbCommon*)record, &rval);
        if (!status) record->val = rval;
    }
    regDevDebugLog(1, "regDevInitRecordLongout(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteLongout(longoutRecord* record)
{
    return regDevWriteInt((dbCommon*)record, record->val, 0xFFFFFFFFUL);
}

/* ai ***************************************************************/

#include <aiRecord.h>

STATIC long regDevInitRecordAi(aiRecord *);
STATIC long regDevReadAi(aiRecord *);
STATIC long regDevSpecialLinconvAi(aiRecord *, int after);

struct {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read;
    DEVSUPFUN special_linconv;
} regDevAi =
{
    6,
    NULL,
    NULL,
    regDevInitRecordAi,
    regDevGetInIntInfo,
    regDevReadAi,
    regDevSpecialLinconvAi
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevAi);
#endif

STATIC long regDevInitRecordAi(aiRecord* record)
{
    int status;

    regDevDebugLog(1, "regDevInitRecordAi(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAi(record, TRUE);
    regDevDebugLog(1, "regDevInitRecordAi(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevReadAnalog((dbCommon*)record, &rval, &val);
    if (status == 0)
    {
        record->rval = rval;
    }
    if (status == 2)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        if (!record->udf)
        {
            /* emulate smoothing */
            record->val = record->val * record->smoo +
                val * (1.0 - record->smoo);
        }
        else
        {
            /* don't smooth with invalid value */
            record->val = val;
        }
    }
    return status;
}

STATIC long regDevSpecialLinconvAi(aiRecord* record, int after)
{
    epicsUInt32 hwSpan;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (after) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->hwHigh*record->egul - priv->hwLow*record->eguf)
            / hwSpan;
    }
    return 0;
}

/* ao ***************************************************************/

#include <aoRecord.h>

STATIC long regDevInitRecordAo(aoRecord *);
STATIC long regDevWriteAo(aoRecord *);
STATIC long regDevSpecialLinconvAo(aoRecord *, int after);

struct {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write;
    DEVSUPFUN special_linconv;
} regDevAo =
{
    6,
    NULL,
    NULL,
    regDevInitRecordAo,
    regDevGetOutIntInfo,
    regDevWriteAo,
    regDevSpecialLinconvAo
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevAo);
#endif

STATIC long regDevInitRecordAo(aoRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordAo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAo(record, TRUE);
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadAnalog((dbCommon*)record, &rval, &val);
        if (status == 0)
        {
            record->rval = rval;
            record->sevr = 0;
            record->stat = 0;
        }
        if (status == 2)
        {
            /* emulate scaling */
            if (record->aslo != 0.0) val *= record->aslo;
            val += record->aoff;
            record->val = val;
            record->sevr = 0;
            record->stat = 0;
        }
    }
    regDevDebugLog(1, "regDevInitRecordAo(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteAo(aoRecord* record)
{
    double val;
    
    val = record->oval - record->aoff;
    if (record->aslo != 0) val /= record->aslo;
    return regDevWriteAnalog((dbCommon*)record, record->rval, val);
}

STATIC long regDevSpecialLinconvAo(aoRecord* record, int after)
{
    epicsUInt32 hwSpan;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;

    if (after) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff = 
            (priv->hwHigh*record->egul -priv->hwLow*record->eguf)
            / hwSpan;
    }
    return 0;
}

/* stringin *********************************************************/

#include <stringinRecord.h>

STATIC long regDevInitRecordStringin(stringinRecord *);
STATIC long regDevReadStringin(stringinRecord *);

struct devsup regDevStringin =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStringin,
    regDevGetInIntInfo,
    regDevReadStringin
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevStringin);
#endif

STATIC long regDevInitRecordStringin(stringinRecord* record)
{
    regDevPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordStringin(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->dlen = sizeof(record->val);
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_STRING)))
        return status;
    if (priv->dlen >= sizeof(record->val))
    {
        errlogSevPrintf(errlogMinor,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    regDevDebugLog(1, "regDevInitRecordStringin(%s) done\n", record->name);
    return 0;
}

STATIC long regDevReadStringin(stringinRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    status = regDevReadArr((dbCommon*) record, record->val, priv->dlen);
    return status;
}

/* stringout ********************************************************/

#include <stringoutRecord.h>

STATIC long regDevInitRecordStringout(stringoutRecord *);
STATIC long regDevWriteStringout(stringoutRecord *);

struct devsup regDevStringout =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStringout,
    regDevGetOutIntInfo,
    regDevWriteStringout
};


#ifdef EPICS_3_14
epicsExportAddress(dset, regDevStringout);
#endif

STATIC long regDevInitRecordStringout(stringoutRecord* record)
{
    regDevPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordStringout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->dlen = sizeof(record->val);
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_STRING)))
        return status;
    if (priv->dlen >= sizeof(record->val))
    {
        errlogSevPrintf(errlogMinor,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArr((dbCommon*) record, record->val, priv->dlen);
    }
    regDevDebugLog(1, "regDevInitRecordStringout(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteStringout(stringoutRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    regDevDebugLog(2, "stringout %s: write %d 8bit values: \"%.*s\"\n",
        record->name, priv->dlen, priv->dlen, record->val);
    status = regDevWriteArray(priv->device, priv->offset,
        1, priv->dlen, record->val);
    if (status != 0)
    {
        recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
        return 0;
    }
    if (status)
    {
        recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM);
    }
    return status;
}

/* calcout **********************************************************/

#ifdef EPICS_3_14
#include <postfix.h>
#include <calcoutRecord.h>

STATIC long regDevInitRecordCalcout(calcoutRecord *);
STATIC long regDevWriteCalcout(calcoutRecord *);

struct devsup regDevCalcout =
{
    5,
    NULL,
    NULL,
    regDevInitRecordCalcout,
    regDevGetOutIntInfo,
    regDevWriteCalcout
};

epicsExportAddress(dset, regDevCalcout);

STATIC long regDevInitRecordCalcout(calcoutRecord* record)
{
    regDevPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordCalcout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_FLOAT)))
        return status;
    regDevDebugLog(1, "regDevInitRecordCalcout(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteCalcout(calcoutRecord* record)
{
    return regDevWriteAnalog((dbCommon*)record, record->oval, record->oval);
}
#endif

/* waveform *********************************************************/

#include <waveformRecord.h>

STATIC long regDevInitRecordWaveform(waveformRecord *);
STATIC long regDevReadWaveform(waveformRecord *);

struct devsup regDevWaveform =
{
    5,
    NULL,
    NULL,
    regDevInitRecordWaveform,
    regDevGetInIntInfo,
    regDevReadWaveform
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevWaveform);
#endif

STATIC long regDevInitRecordWaveform(waveformRecord* record)
{
    regDevPrivate* priv;
    int status;
    
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) done\n", record->name);
    return status;
}

STATIC long regDevReadWaveform(waveformRecord* record)
{
    int status;

    status = regDevReadArr((dbCommon*) record, record->bptr, record->nelm);
    record->nord = record->nelm;
    return status;
}

/* aai **************************************************************/

#include <aaiRecord.h>

STATIC long regDevInitRecordAai(aaiRecord *);
STATIC long regDevReadAai(aaiRecord *);

struct devsup regDevAai =
{
    5,
    NULL,
    NULL,
    regDevInitRecordAai,
    regDevGetInIntInfo,
    regDevReadAai
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevAai);
#endif

static const int sizeofTypes[] = {MAX_STRING_SIZE,1,1,2,2,4,4,4,8,2};

STATIC long regDevInitRecordAai(aaiRecord* record)
{
    regDevPrivate* priv;
    int status;
    
    regDevDebugLog(1, "regDevInitRecordAai(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAai(%s) link type %d\n", record->name, record->inp.type);
    /* aai record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
    	record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
    }

    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    regDevDebugLog(1, "regDevInitRecordAai(%s) done\n", record->name);
    return status;
}

STATIC long regDevReadAai(aaiRecord* record)
{
    int status;

    status = regDevReadArr((dbCommon*)record, record->bptr, record->nelm);
    record->nord = record->nelm;
    return status;
}

/* aao **************************************************************/

#include <aaoRecord.h>

STATIC long regDevInitRecordAao(aaoRecord *);
STATIC long regDevWriteAao(aaoRecord *);

struct devsup regDevAao =
{
    5,
    NULL,
    NULL,
    regDevInitRecordAao,
    regDevGetInIntInfo,
    regDevWriteAao
};

#ifdef EPICS_3_14
epicsExportAddress(dset, regDevAao);
#endif

STATIC long regDevInitRecordAao(aaoRecord* record)
{
    int status;
    regDevPrivate* priv;
    
    regDevDebugLog(1, "regDevInitRecordAao(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAao(%s) link type %d\n", record->name, record->out.type);
    /* aao record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
    	record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
    }

    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArr((dbCommon*)record, record->bptr, record->nelm);
        return status;
    }
    regDevDebugLog(1, "regDevInitRecordAao(%s) done\n", record->name);
    return status;
}

STATIC long regDevWriteAao(aaoRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int i;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        errlogSevPrintf(errlogFatal,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    switch (priv->dtype)
    {
        case regDevBCD8T:
            for (i = 0; i < record->nord; i++)
                ((epicsUInt8*)record->bptr)[i] = i2bcd(((epicsUInt8*)record->bptr)[i]);
        case epicsInt8T:
        case epicsUInt8T:
        case epicsStringT:
            status = regDevWriteArray(priv->device, priv->offset,
                1, record->nord, record->bptr);
            regDevDebugLog(3,
                "aao %s: write %ld values of 8bit from %p\n",
                record->name, record->nord, record->bptr);
            break;
        case regDevBCD16T:
            for (i = 0; i < record->nord; i++)
                ((epicsUInt16*)record->bptr)[i] = i2bcd(((epicsUInt16*)record->bptr)[i]);
        case epicsInt16T:
        case epicsUInt16T:
            status = regDevWriteArray(priv->device, priv->offset,
                2, record->nord, record->bptr);
            regDevDebugLog(3,
                "aao %s: write %ld values of 16bit from %p\n",
                record->name, record->nord, record->bptr);
            break;
        case regDevBCD32T:
            for (i = 0; i < record->nord; i++)
                ((epicsUInt32*)record->bptr)[i] = i2bcd(((epicsUInt32*)record->bptr)[i]);
        case epicsInt32T:
        case epicsUInt32T:
        case epicsFloat32T:
            status = regDevWriteArray(priv->device, priv->offset,
                4, record->nord, record->bptr);
            regDevDebugLog(3,
                "aao %s: write %ld values of 32bit from %p\n",
                record->name, record->nord, record->bptr);
            break;
        case epicsFloat64T:
            status = regDevWriteArray(priv->device, priv->offset,
                8, record->nord, record->bptr);
            regDevDebugLog(3,
                "aao %s: write %ld values of 64bit from %p\n",
                record->name, record->nord, record->bptr);
            break;
        default:
            recGblSetSevr(record, COMM_ALARM, INVALID_ALARM);
            errlogSevPrintf(errlogFatal,
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
        errlogSevPrintf(errlogFatal,
            "%s: read error\n", record->name);
        recGblSetSevr(record, READ_ALARM, INVALID_ALARM);
    }
    return status;
}
