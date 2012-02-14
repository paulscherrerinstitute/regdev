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
#else
#define S_dev_success 0
#define S_dev_badArgument (M_devLib| 33)
#include <semLib.h>
#define epicsMutexId SEM_ID
#define epicsExportAddress(a,b)
#define epicsMutexCreate() semMCreate(SEM_Q_FIFO)
#define epicsMutexLock(m) semTake(m, WAIT_FOREVER)
#define epicsMutexUnlock(m) semGive(m)
#endif

#define DONT_INIT (0xFFFFFFFFUL)

#define TYPE_INT    1
#define TYPE_FLOAT  2
#define TYPE_STRING 4
#define TYPE_BCD    8


typedef struct regDeviceNode {
    struct regDeviceNode* next;    /* Next registered device */
    const char* name;              /* Device name */
    const regDevSupport* support;  /* Device function table */
    regDevice* driver;             /* Generic device driver */
    epicsMutexId accesslock;       /* Access semaphore */
} regDeviceNode;

typedef struct regDevPrivate{
    regDeviceNode* device;
    unsigned int offset;       /* Offset (in bytes) within device memory */
    unsigned int initoffset;   /* Offset to initialize output records */
    unsigned short bit;        /* Bit number (0-15) for bi/bo */
    unsigned short dtype;      /* Data type */
    unsigned short dlen;       /* Data length (in bytes) */
    short fifopacking;         /* Fifo: elelents in one register */
    short arraypacking;        /* Array: elelents in one register */
    epicsInt32 hwLow;          /* Hardware Low limit */
    epicsInt32 hwHigh;         /* Hardware High limit */
    epicsUInt32 invert;        /* Invert bits for bi,bo,mbbi,... */
    CALLBACK callback;         /* For asynchonous drivers */
    int status;                /* For asynchonous drivers */
} regDevPrivate;

typedef struct regDeviceAsynNode {
    struct regDeviceAsynNode* next;    /* Next registered device */
    const char* name;              /* Device name */
    const regDevAsyncSupport* support;  /* Device function table */
    regDeviceAsyn* driver;             /* Generic device driver */
    epicsMutexId accesslock;       /* Access semaphore */
} regDeviceAsynNode;

typedef struct regDevAsynPrivate{
    regDeviceAsynNode* device;
    unsigned int offset;       /* Offset (in bytes) within device memory */
    unsigned int initoffset;   /* Offset to initialize output records */
    unsigned short bit;        /* Bit number (0-15) for bi/bo */
    unsigned short dtype;      /* Data type */
    unsigned short dlen;       /* Data length (in bytes) */
    short fifopacking;         /* Fifo: elelents in one register */
    short arraypacking;        /* Array: elelents in one register */
    epicsInt32 hwLow;          /* Hardware Low limit */
    epicsInt32 hwHigh;         /* Hardware High limit */
    epicsUInt32 invert;        /* Invert bits for bi,bo,mbbi,... */
    void*     busBufPtr;       /* here we can add the bus address got from buf alloc routine */
    CALLBACK* callback;        /* For asynchonous drivers */
    int status;                /* For asynchonous drivers */
} regDevAsynPrivate;

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
long regDevAssertType(dbCommon *record, int types);
int regDevRead(regDeviceNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, int prio);
int regDevWrite(regDeviceNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, void* mask, int prio);
int regDevReadBits(dbCommon* record, epicsInt32* val, epicsUInt32 mask);
int regDevWriteBits(dbCommon* record, epicsUInt32 val, epicsUInt32 mask);
long regDevReadNumber(dbCommon* record, epicsInt32* rval, double* fval);
long regDevWriteNumber(dbCommon* record, double fval, epicsInt32 rval);
long regDevReadArr(dbCommon* record, void* bptr, unsigned int nelm);
long regDevWriteArr(dbCommon* record, void* bptr, unsigned int nelm);

long regDevAsynMemAlloc(dbCommon* record, void** bptr, unsigned int size);
long regDevAsynGetInIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
long regDevAsynGetOutIntInfo(int cmd, dbCommon *record, IOSCANPVT *ppvt);
regDevAsynPrivate* regDevAsynAllocPriv(dbCommon *record);
int regDevAsynCheckFTVL(dbCommon* record, int ftvl);
int regDevAsynIoParse(dbCommon* record, struct link* link);
int regDevAsynCheckType(dbCommon* record, int ftvl, int nelm);
long regDevAsynAssertType(dbCommon *record, int types);
int regDevAsynRead(regDeviceAsynNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, CALLBACK* sbStruct, int prio);
int regDevAsynWrite(regDeviceAsynNode* device, unsigned int offset,
    unsigned int dlen, unsigned int nelem, void* pdata, CALLBACK* sbStruct, void* mask, int prio);
int regDevAsynReadBits(dbCommon* record, epicsInt32* val, epicsUInt32 mask);
int regDevAsynWriteBits(dbCommon* record, epicsUInt32 val, epicsUInt32 mask);
long regDevAsynReadNumber(dbCommon* record, epicsInt32* rval, double* fval);
long regDevAsynWriteNumber(dbCommon* record, double fval, epicsInt32 rval);
long regDevAsynReadArr(dbCommon* record, void* bptr, unsigned int nelm);
long regDevAsynWriteArr(dbCommon* record, void* bptr, unsigned int nelm);

#endif
