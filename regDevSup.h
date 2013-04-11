/* header for device supports */

#ifndef regDevSup_h
#define regDevSup_h

#include "regDev.h"
#include <devSup.h>
#include <drvSup.h>
#include <devLib.h>
#include <dbCommon.h>
#include <callback.h>
#include <dbAccess.h>
#include <alarm.h>
#include <recGbl.h>
#include <assert.h>

#ifdef EPICS_3_14
#include <epicsExport.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#else
#define S_dev_success 0
#define S_dev_badArgument (M_devLib| 33)
#include <semLib.h>
#define epicsMutexId SEM_ID
#define epicsExportAddress(a,b) extern int dummy
#define epicsMutexCreate() semMCreate(SEM_Q_PRIORITY)
#define epicsMutexLock(m) semTake(m, WAIT_FOREVER)
#define epicsMutexUnlock(m) semGive(m)
#define epicsEventId SEM_ID
#define epicsEventEmpty SEM_EMPTY
#define epicsEventCreate(i) semBCreate(SEM_Q_FIFO, i)
#define epicsEventWait(e) semTake(e, WAIT_FOREVER)
#define epicsEventSignal(e) semGive(e)
epicsShareExtern struct dbBase *pdbbase;
#endif

#include <sys/types.h>
#include <epicsTypes.h>
#if (EPICS_REVISION<15)
typedef long long epicsInt64;
typedef unsigned long long epicsUInt64;
#endif

#define DONT_INIT (0xFFFFFFFFUL)

#define TYPE_INT    1
#define TYPE_FLOAT  2
#define TYPE_STRING 4
#define TYPE_BCD    8

#define DONT_CONVERT 2

#define MAGIC 2181699655U /* crc("regDev") */

typedef struct regDeviceNode {
    struct regDeviceNode* next;    /* Next registered device */
    const char* name;              /* Device name */
    const regDevSupport* support;  /* Device function table */
    const regDevAsyncSupport* asupport;  /* Device function table */
    void* driver;                  /* Generic device driver */
    epicsMutexId accesslock;       /* Access semaphore */
} regDeviceNode;

typedef union {
    epicsInt8 sval8;
    epicsUInt8 uval8;
    epicsInt16 sval16;
    epicsUInt16 uval16;
    epicsInt32 sval32;
    epicsUInt32 uval32;
    epicsInt64 val64;
    epicsUInt64 uval64;
    epicsFloat32 fval32;
    epicsFloat64 fval64;
    void* buffer;
} regDevAnytype;

typedef struct regDevPrivate{
    epicsInt32 magic;
    regDeviceNode* device;
    size_t offset;               /* Offset (in bytes) within device memory */
    size_t initoffset;           /* Offset to initialize output records */
    struct dbAddr* offsetRecord; /* record to read offset from */
    ssize_t offsetScale;         /* scaling of value from offsetRecord */
    unsigned short bit;          /* Bit number (0-15) for bi/bo */
    unsigned short dtype;        /* Data type */
    unsigned short dlen;         /* Data length (in bytes) */
    short fifopacking;           /* Fifo: elelents in one register */
    short arraypacking;          /* Array: elelents in one register */
    epicsInt32 hwLow;            /* Hardware Low limit */
    epicsInt32 hwHigh;           /* Hardware High limit */
    epicsUInt32 invert;          /* Invert bits for bi,bo,mbbi,... */
    volatile void* hwPtr;        /* here we can add the bus address got from buf alloc routine */
    CALLBACK callback;           /* For asynchonous drivers */
    int status;                  /* For asynchonous drivers */
    epicsEventId initDone;       /* For asynchonous drivers */
    size_t asyncOffset;          /* For asynchonous drivers */
    regDevAnytype result;        /* For asynchonous drivers */
} regDevPrivate;

struct devsup {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN io;
};

long regDevGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
long regDevGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
regDevPrivate* regDevAllocPriv(dbCommon *record);
int regDevCheckFTVL(dbCommon* record, int ftvl);
int regDevIoParse(dbCommon* record, struct link* link);
int regDevCheckType(dbCommon* record, int ftvl, int nelm);
int regDevAssertType(dbCommon *record, int types);
const char* regDevTypeName(int dtype);
int regDevMemAlloc(dbCommon* record, void** bptr, size_t size);

/* returns OK, ERROR, or ASYNC_COMPLETITION */
int regDevRead(dbCommon* record, unsigned short dlen, size_t nelem, void* buffer);
int regDevWrite(dbCommon* record, unsigned short dlen, size_t nelem, void* pdata, void* mask);

int regDevReadScalar(dbCommon* record, epicsInt32* rval, double* fval, epicsUInt32 mask);
int regDevWriteScalar(dbCommon* record, epicsInt32 rval, double fval, epicsUInt32 mask);

#define regDevReadBits(record, val, mask)     regDevReadScalar(record, val, NULL, mask)
#define regDevWriteBits(record, val, mask)    regDevWriteScalar(record, val, 0.0, mask)

#define regDevReadInt(record, val)            regDevReadScalar(record, val, NULL, (epicsUInt32)-1)
#define regDevWriteInt(record, val)           regDevWriteScalar(record, val, 0.0, (epicsUInt32)-1)

#define regDevReadNumber(record, rval, fval)  regDevReadScalar(record, rval, fval, (epicsUInt32)-1)
#define regDevWriteNumber(record, rval, fval) regDevWriteScalar(record, rval, fval, (epicsUInt32)-1)

#define regDevReadStatus(record)              regDevRead(record, 0, 0, NULL)

/* returns OK or ERROR, or ASYNC_COMPLETITION */
int regDevReadArray(dbCommon* record, size_t nelm);
int regDevWriteArray(dbCommon* record, size_t nelm);

int regDevScaleFromRaw(dbCommon* record, int ftvl, void* val, size_t nelm, double low, double high);
int regDevScaleToRaw(dbCommon* record, int ftvl, void* rval, size_t nelm, double low, double high);

#define regDevCheckAsyncWriteResult(record) \
    do if (record->pact) \
    { \
        regDevPrivate* priv = (regDevPrivate*)(record->dpvt); \
        if (priv == NULL) \
        { \
            recGblSetSevr(record, UDF_ALARM, INVALID_ALARM); \
            regDevDebugLog(3, "%s: record not initialized\n", record->name); \
        } \
        if (priv->status != OK) \
        { \
            recGblSetSevr(record, WRITE_ALARM, INVALID_ALARM); \
            regDevDebugLog(3, "%s: asynchronous write error\n", record->name); \
        } \
        return priv->status; \
    } while(0)

#endif
