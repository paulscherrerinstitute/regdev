#include <stdio.h>
#include <stdlib.h>

#include "regDevSup.h"

static void regDevAsyncCallback(CALLBACK *pcallback)
{
    struct dbCommon* 	precord;
    callbackGetUser(precord,  pcallback);
    callbackRequestProcessCallback(pcallback, precord->prio, precord);
}

/* bi for status bit ************************************************/

#include <biRecord.h>

long regDevInitRecordStat(biRecord *);
long regDevReadStat(biRecord *);

struct devsup regDevStat =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStat,
    regDevGetInIntInfo,
    regDevReadStat
};

epicsExportAddress(dset, regDevStat);

long regDevInitRecordStat(biRecord* record)
{
    int status;

    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    return OK;
}

long regDevReadStat(biRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevRead(priv->device, 0, 0, 0, NULL, 0); 
    record->rval = (status == OK);
    return OK;
}
long regDevAsynInitRecordStat(biRecord *);
long regDevAsynReadStat(biRecord *);

struct devsup regDevAsynStat =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordStat,
    regDevAsynGetInIntInfo,
    regDevAsynReadStat
};

epicsExportAddress(dset, regDevAsynStat);

long regDevAsynInitRecordStat(biRecord* record)
{
    int status;
    regDevAsynPrivate* priv;
    
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    return OK;
}

long regDevAsynReadStat(biRecord* record)
{
    int status;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        fprintf(stderr,
            "%s: not initialized\n", record->name);
        return -1;
    }
    assert(priv->device);
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevAsynRead(priv->device, 0, 0, 0, NULL, NULL, 0, 0); 
    record->rval = (status == OK);
    return OK;
}


/* bi ***************************************************************/

long regDevInitRecordBi(biRecord *);
long regDevReadBi(biRecord *);

struct devsup regDevBi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordBi,
    regDevGetInIntInfo,
    regDevReadBi
};

epicsExportAddress(dset, regDevBi);

long regDevInitRecordBi(biRecord* record)
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
    if (record->mask == 0)
        record->mask = 1 << priv->bit;
    if (priv->invert)
        priv->invert = record->mask;
    regDevDebugLog(1, "regDevInitRecordBi(%s) done\n", record->name);
    return OK;
}

long regDevReadBi(biRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    if (!status) record->rval = rval;
    return status;
}

long regDevAsynInitRecordBi(biRecord *);
long regDevAsynReadBi(biRecord *);

struct devsup regDevAsynBi =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordBi,
    regDevAsynGetInIntInfo,
    regDevAsynReadBi
};

epicsExportAddress(dset, regDevAsynBi);

long regDevAsynInitRecordBi(biRecord* record)
{
    int status;
    regDevAsynPrivate* priv;
    
    regDevDebugLog(1, "regDevInitRecordBi(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->mask == 0)
        record->mask = 1 << priv->bit;
    if (priv->invert)
        priv->invert = record->mask;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordBi(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadBi(biRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK) record->rval = rval;
    return status;
}

/* bo ***************************************************************/

#include <boRecord.h>

long regDevInitRecordBo(boRecord *);
long regDevWriteBo(boRecord *);

struct devsup regDevBo =
{
    5,
    NULL,
    NULL,
    regDevInitRecordBo,
    regDevGetOutIntInfo,
    regDevWriteBo
};

epicsExportAddress(dset, regDevBo);

long regDevInitRecordBo(boRecord* record)
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
    if (record->mask == 0)
        record->mask = 1 << priv->bit;
    if (priv->invert)
        priv->invert = record->mask;
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
        if (!status) record->rval = rval;
    }
    regDevDebugLog(1, "regDevInitRecordBo(%s) done\n", record->name);
    return status;
}

long regDevWriteBo(boRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->rval, record->mask);
}

long regDevAsynInitRecordBo(boRecord *);
long regDevAsynWriteBo(boRecord *);

struct devsup regDevAsynBo =
{
    5,
    NULL,
    NULL,
    regDevInitRecordBo,
    regDevAsynGetOutIntInfo,
    regDevWriteBo
};

epicsExportAddress(dset, regDevAsynBo);

long regDevAsynInitRecordBo(boRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordBo(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->mask == 0)
        record->mask = 1 << priv->bit;
    if (priv->invert)
        priv->invert = record->mask;
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
        if (!status) record->rval = rval;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordBo(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteBo(boRecord* record)
{
    return regDevAsynWriteBits((dbCommon*)record, record->rval, record->mask);
}

/* mbbi *************************************************************/

#include <mbbiRecord.h>

long regDevInitRecordMbbi(mbbiRecord *);
long regDevReadMbbi(mbbiRecord *);

struct devsup regDevMbbi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbi,
    regDevGetInIntInfo,
    regDevReadMbbi
};

epicsExportAddress(dset, regDevMbbi);

long regDevInitRecordMbbi(mbbiRecord* record)
{
    int status;
    regDevPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordMbbi(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    regDevDebugLog(1, "regDevInitRecordMbbi(%s) done\n", record->name);
    return OK;
}

long regDevReadMbbi(mbbiRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    if (status) return status;
    /* If any values defined write to RVAL field else to VAL field */
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            record->rval = rval;
            return OK;
        }
    }
    if (record->shft > 0) rval >>= record->shft;
    record->val = rval;
    return 2;
}

long regDevAsynInitRecordMbbi(mbbiRecord *);
long regDevAsynReadMbbi(mbbiRecord *);

struct devsup regDevAsynMbbi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbi,
    regDevAsynGetInIntInfo,
    regDevAsynReadMbbi
};

epicsExportAddress(dset, regDevAsynMbbi);

long regDevAsynInitRecordMbbi(mbbiRecord* record)
{
    int status;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordMbbi(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordMbbi(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadMbbi(mbbiRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status) return status;
    /* If any values defined write to RVAL field else to VAL field */
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            record->rval = rval;
            return OK;
        }
    }
    if (record->shft > 0) rval >>= record->shft;
    record->val = rval;
    return 2;
}


/* mbbo *************************************************************/

#include <mbboRecord.h>

long regDevInitRecordMbbo(mbboRecord *);
long regDevWriteMbbo(mbboRecord *);

struct devsup regDevMbbo =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbo,
    regDevGetOutIntInfo,
    regDevWriteMbbo
};

epicsExportAddress(dset, regDevMbbo);

long regDevInitRecordMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevPrivate* priv;
    int i;

    regDevDebugLog(1, "regDevInitRecordMbbo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
        if (status) return status;
        /* If any values defined write to RVAL field else to VAL field */
        if (record->sdef) for (i=0; i<16; i++)
        {
            if ((&record->zrvl)[i])
            {
                record->rval = rval;
                regDevDebugLog(1, "regDevInitRecordMbbo(%s) done RVAL=%ld\n", record->name, (long)record->rval);
                return OK;
            }
        }
        if (record->shft > 0) rval >>= record->shft;
        record->val = rval;
    }
    regDevDebugLog(1, "regDevInitRecordMbbo(%s) done VAL=%d\n", record->name, record->val);
    return 2;
}

long regDevWriteMbbo(mbboRecord* record)
{
    epicsInt32 rval;
    int i;
    
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            /* any values defined ? */
            rval = record->rval;
            return regDevWriteBits((dbCommon*)record, rval, record->mask);
        }
    }
    rval = record->val;
    if (record->shft > 0) rval <<= record->shft;
    return regDevWriteBits((dbCommon*)record, rval, record->mask);
}

long regDevAsynInitRecordMbbo(mbboRecord *);
long regDevAsynWriteMbbo(mbboRecord *);

struct devsup regDevAsynMbbo =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordMbbo,
    regDevAsynGetOutIntInfo,
    regDevAsynWriteMbbo
};

epicsExportAddress(dset, regDevAsynMbbo);

long regDevAsynInitRecordMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevAsynPrivate* priv;
    int i;

    regDevDebugLog(1, "regDevInitRecordMbbo(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
        if (status) return status;
        /* If any values defined write to RVAL field else to VAL field */
        if (record->sdef) for (i=0; i<16; i++)
        {
            if ((&record->zrvl)[i])
            {
                record->rval = rval;
                regDevDebugLog(1, "regDevInitRecordMbbo(%s) done RVAL=%ld\n", record->name, (long)record->rval);
                return OK;
            }
        }
        if (record->shft > 0) rval >>= record->shft;
        record->val = rval;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordMbbo(%s) done VAL=%d\n", record->name, record->val);
    return 2;
}

long regDevAsynWriteMbbo(mbboRecord* record)
{
    epicsInt32 rval;
    int i;
    
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            /* any values defined ? */
            rval = record->rval;
            return regDevAsynWriteBits((dbCommon*)record, rval, record->mask);
        }
    }
    rval = record->val;
    if (record->shft > 0) rval <<= record->shft;
    return regDevAsynWriteBits((dbCommon*)record, rval, record->mask);
}


/* mbbiDirect *******************************************************/

#include <mbbiDirectRecord.h>

long regDevInitRecordMbbiDirect(mbbiDirectRecord *);
long regDevReadMbbiDirect(mbbiDirectRecord *);

struct devsup regDevMbbiDirect =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbbiDirect,
    regDevGetInIntInfo,
    regDevReadMbbiDirect
};

epicsExportAddress(dset, regDevMbbiDirect);

long regDevInitRecordMbbiDirect(mbbiDirectRecord* record)
{
    regDevPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) done\n", record->name);
    return OK;
}

long regDevReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    if (!status) record->rval = rval;
    return status;
}

#include <mbbiDirectRecord.h>

long regDevAsynInitRecordMbbiDirect(mbbiDirectRecord *);
long regDevAsynReadMbbiDirect(mbbiDirectRecord *);

struct devsup regDevAsynMbbiDirect =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordMbbiDirect,
    regDevAsynGetInIntInfo,
    regDevAsynReadMbbiDirect
};

epicsExportAddress(dset, regDevAsynMbbiDirect);

long regDevAsynInitRecordMbbiDirect(mbbiDirectRecord* record)
{
    regDevAsynPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK) record->rval = rval;
    return status;
}


/* mbboDirect *******************************************************/

#include <mbboDirectRecord.h>

long regDevInitRecordMbboDirect(mbboDirectRecord *);
long regDevWriteMbboDirect(mbboDirectRecord *);

struct devsup regDevMbboDirect =
{
    5,
    NULL,
    NULL,
    regDevInitRecordMbboDirect,
    regDevGetOutIntInfo,
    regDevWriteMbboDirect
};

epicsExportAddress(dset, regDevMbboDirect);

long regDevInitRecordMbboDirect(mbboDirectRecord* record)
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
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
        if (!status) record->rval = rval;
    }
    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) done\n", record->name);
    return status;
}

long regDevWriteMbboDirect(mbboDirectRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->rval, record->mask);
}

long regDevAsynInitRecordMbboDirect(mbboDirectRecord *);
long regDevAsynWriteMbboDirect(mbboDirectRecord *);

struct devsup regDevAsynMbboDirect =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordMbboDirect,
    regDevAsynGetOutIntInfo,
    regDevAsynWriteMbboDirect
};

epicsExportAddress(dset, regDevAsynMbboDirect);

long regDevAsynInitRecordMbboDirect(mbboDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT)))
        return status;
    if (record->shft > 0)
    {
        record->mask <<= record->shft;
        priv->invert <<= record->shft;
    }
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevAsynReadBits((dbCommon*)record, &rval, record->mask);
        if (!status) record->rval = rval;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteMbboDirect(mbboDirectRecord* record)
{
    return regDevAsynWriteBits((dbCommon*)record, record->rval, record->mask);
}


/* longin ***********************************************************/

#include <longinRecord.h>

long regDevInitRecordLongin(longinRecord *);
long regDevReadLongin(longinRecord *);

struct devsup regDevLongin =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLongin,
    regDevGetInIntInfo,
    regDevReadLongin
};

epicsExportAddress(dset, regDevLongin);

long regDevInitRecordLongin(longinRecord* record)
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
    return OK;
}

long regDevReadLongin(longinRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, -1);
    if (!status) record->val = rval;
    return status;
}

long regDevAsynInitRecordLongin(longinRecord *);
long regDevAsynReadLongin(longinRecord *);

struct devsup regDevAsynLongin =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordLongin,
    regDevAsynGetInIntInfo,
    regDevAsynReadLongin
};

epicsExportAddress(dset, regDevAsynLongin);

long regDevAsynInitRecordLongin(longinRecord* record)
{
    int status;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordLongin(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordLongin(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadLongin(longinRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevAsynReadBits((dbCommon*)record, &rval, -1);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK) record->val = rval;
    return status;
}


/* longout **********************************************************/

#include <longoutRecord.h>

long regDevInitRecordLongout(longoutRecord *);
long regDevWriteLongout(longoutRecord *);

struct devsup regDevLongout =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLongout,
    regDevGetOutIntInfo,
    regDevWriteLongout
};

epicsExportAddress(dset, regDevLongout);

long regDevInitRecordLongout(longoutRecord* record)
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
        status = regDevReadBits((dbCommon*)record, &rval, -1);
        if (!status) record->val = rval;
    }
    regDevDebugLog(1, "regDevInitRecordLongout(%s) done\n", record->name);
    return status;
}

long regDevWriteLongout(longoutRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->val, 0xFFFFFFFFUL);
}

long regDevAsynInitRecordLongout(longoutRecord *);
long regDevAsynWriteLongout(longoutRecord *);

struct devsup regDevAsynLongout =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordLongout,
    regDevAsynGetOutIntInfo,
    regDevAsynWriteLongout
};

epicsExportAddress(dset, regDevAsynLongout);

long regDevAsynInitRecordLongout(longoutRecord* record)
{
    int status;
    epicsInt32 rval;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordLongout(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevAsynReadBits((dbCommon*)record, &rval, -1);
        if (!status) record->val = rval;
    }
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevDebugLog(1, "regDevInitRecordLongout(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteLongout(longoutRecord* record)
{
    return regDevAsynWriteBits((dbCommon*)record, record->val, 0xFFFFFFFFUL);
}


/* ai ***************************************************************/

#include <aiRecord.h>

long regDevInitRecordAi(aiRecord *);
long regDevReadAi(aiRecord *);
long regDevSpecialLinconvAi(aiRecord *, int after);

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

epicsExportAddress(dset, regDevAi);

long regDevInitRecordAi(aiRecord* record)
{
    int status;

    regDevDebugLog(1, "regDevInitRecordAi(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAi(record, TRUE);
    regDevDebugLog(1, "regDevInitRecordAi(%s) done\n", record->name);
    return OK;
}

long regDevReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == OK)
    {
        record->rval = rval;
    }
    if (status == DONT_CONVERT)
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
            record->udf = 0;
        }
    }
    return status;
}

long regDevSpecialLinconvAi(aiRecord* record, int after)
{
    double hwSpan;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (after) {
        hwSpan = (double) priv->hwHigh - priv->hwLow;
        record->eslo = ((double) record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->hwHigh*record->egul - priv->hwLow*record->eguf)
            / hwSpan;
    }
    return OK;
}

long regDevAsynInitRecordAi(aiRecord *);
long regDevAsynReadAi(aiRecord *);
long regDevAsynSpecialLinconvAi(aiRecord *, int after);

struct {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read;
    DEVSUPFUN special_linconv;
} regDevAsynAi =
{
    6,
    NULL,
    NULL,
    regDevAsynInitRecordAi,
    regDevAsynGetInIntInfo,
    regDevAsynReadAi,
    regDevAsynSpecialLinconvAi
};

epicsExportAddress(dset, regDevAsynAi);

long regDevAsynInitRecordAi(aiRecord* record)
{
    int status;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordAi(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevSpecialLinconvAi(record, TRUE);
    regDevDebugLog(1, "regDevInitRecordAi(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevAsynReadNumber((dbCommon*)record, &rval, &val);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK)
    {
        record->rval = rval;
        return OK;
    }
    if (status == DONT_CONVERT)
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
            record->udf = 0;
        }
    }
    return status;
}

long regDevAsynSpecialLinconvAi(aiRecord* record, int after)
{
    double hwSpan;
    regDevAsynPrivate* priv = (regDevAsynPrivate*)record->dpvt;

    if (after) {
        hwSpan = (double) priv->hwHigh - priv->hwLow;
        record->eslo = ((double) record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->hwHigh*record->egul - priv->hwLow*record->eguf)
            / hwSpan;
    }
    return OK;
}


/* ao ***************************************************************/

#include <aoRecord.h>

long regDevInitRecordAo(aoRecord *);
long regDevWriteAo(aoRecord *);
long regDevSpecialLinconvAo(aoRecord *, int after);

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

epicsExportAddress(dset, regDevAo);

long regDevInitRecordAo(aoRecord* record)
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
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAo(record, TRUE);
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadNumber((dbCommon*)record, &rval, &val);
        if (status == OK)
        {
            record->rval = rval;
            record->sevr = 0;
            record->stat = 0;
        }
        if (status == DONT_CONVERT)
        {
            /* emulate scaling */
            if (record->aslo != 0.0) val *= record->aslo;
            val += record->aoff;
            record->val = val;
            record->sevr = 0;
            record->stat = 0;
            record->udf = 0;
        }
    }
    regDevDebugLog(1, "regDevInitRecordAo(%s) done\n", record->name);
    return status;
}

long regDevWriteAo(aoRecord* record)
{
    double val;
    
    val = record->oval - record->aoff;
    if (record->aslo != 0) val /= record->aslo;
    return regDevWriteNumber((dbCommon*)record, val, record->rval);
}

long regDevSpecialLinconvAo(aoRecord* record, int after)
{
    double hwSpan;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;

    if (after) {
        hwSpan = (double) priv->hwHigh - priv->hwLow;
        record->eslo = ((double) record->eguf - record->egul) / hwSpan;
        record->eoff = 
            (priv->hwHigh*record->egul -priv->hwLow*record->eguf)
            / hwSpan;
    }
    return OK;
}

long regDevAsynInitRecordAo(aoRecord *);
long regDevAsynWriteAo(aoRecord *);
long regDevAsynSpecialLinconvAo(aoRecord *, int after);

struct {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write;
    DEVSUPFUN special_linconv;
} regDevAsynAo =
{
    6,
    NULL,
    NULL,
    regDevAsynInitRecordAo,
    regDevAsynGetOutIntInfo,
    regDevAsynWriteAo,
    regDevAsynSpecialLinconvAo
};

epicsExportAddress(dset, regDevAsynAo);

long regDevAsynInitRecordAo(aoRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    regDevAsynPrivate* priv;

    regDevDebugLog(1, "regDevInitRecordAo(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    regDevSpecialLinconvAo(record, TRUE);
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevAsynReadNumber((dbCommon*)record, &rval, &val);
        if (status == OK)
        {
            record->rval = rval;
            record->sevr = 0;
            record->stat = 0;
        }
        if (status == DONT_CONVERT)
        {
            /* emulate scaling */
            if (record->aslo != 0.0) val *= record->aslo;
            val += record->aoff;
            record->val = val;
            record->sevr = 0;
            record->stat = 0;
            record->udf = 0;
        }
    }
    regDevDebugLog(1, "regDevInitRecordAo(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteAo(aoRecord* record)
{
    double val;
    
    val = record->oval - record->aoff;
    if (record->aslo != 0) val /= record->aslo;
    return regDevAsynWriteNumber((dbCommon*)record, val, record->rval);
}

long regDevAsynSpecialLinconvAo(aoRecord* record, int after)
{
    double hwSpan;
    regDevAsynPrivate* priv = (regDevAsynPrivate*) record->dpvt;

    if (after) {
        hwSpan = (double) priv->hwHigh - priv->hwLow;
        record->eslo = ((double) record->eguf - record->egul) / hwSpan;
        record->eoff = 
            (priv->hwHigh*record->egul -priv->hwLow*record->eguf)
            / hwSpan;
    }
    return OK;
}

/* stringin *********************************************************/

#include <stringinRecord.h>

long regDevInitRecordStringin(stringinRecord *);
long regDevReadStringin(stringinRecord *);

struct devsup regDevStringin =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStringin,
    regDevGetInIntInfo,
    regDevReadStringin
};

epicsExportAddress(dset, regDevStringin);

long regDevInitRecordStringin(stringinRecord* record)
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
        fprintf(stderr,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = (int)sizeof(record->val)-1;
    }
    regDevDebugLog(1, "regDevInitRecordStringin(%s) done\n", record->name);
    return OK;
}

long regDevReadStringin(stringinRecord* record)
{
    return regDevReadArr((dbCommon*) record, record->val, 0);
}

long regDevAsynInitRecordStringin(stringinRecord *);
long regDevAsynReadStringin(stringinRecord *);

struct devsup regDevAsynStringin =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordStringin,
    regDevAsynGetInIntInfo,
    regDevAsynReadStringin
};

epicsExportAddress(dset, regDevAsynStringin);

long regDevAsynInitRecordStringin(stringinRecord* record)
{
    regDevAsynPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordStringin(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->dlen = sizeof(record->val);
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_STRING)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    if (priv->dlen >= sizeof(record->val))
    {
        fprintf(stderr,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    regDevDebugLog(1, "regDevInitRecordStringin(%s) done\n", record->name);
    return OK;
}

long regDevAsynReadStringin(stringinRecord* record)
{
    int status = regDevAsynReadArr((dbCommon*) record, record->val, 0);
    if (status == ASYNC_COMPLETITION) return OK;
    return status;
}


/* stringout ********************************************************/

#include <stringoutRecord.h>

long regDevInitRecordStringout(stringoutRecord *);
long regDevWriteStringout(stringoutRecord *);

struct devsup regDevStringout =
{
    5,
    NULL,
    NULL,
    regDevInitRecordStringout,
    regDevGetOutIntInfo,
    regDevWriteStringout
};


epicsExportAddress(dset, regDevStringout);

long regDevInitRecordStringout(stringoutRecord* record)
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
        fprintf(stderr,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArr((dbCommon*) record, record->val, 0);
    }
    regDevDebugLog(1, "regDevInitRecordStringout(%s) done\n", record->name);
    return status;
}

long regDevWriteStringout(stringoutRecord* record)
{
    return regDevWriteArr((dbCommon*) record, record->val, 0);
}

long regDevAsynInitRecordStringout(stringoutRecord *);
long regDevAsynWriteStringout(stringoutRecord *);

struct devsup regDevAsynStringout =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordStringout,
    regDevAsynGetOutIntInfo,
    regDevAsynWriteStringout
};


epicsExportAddress(dset, regDevAsynStringout);

long regDevAsynInitRecordStringout(stringoutRecord* record)
{
    regDevAsynPrivate* priv;
    int status;

    regDevDebugLog(1, "regDevInitRecordStringout(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->dlen = sizeof(record->val);
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAsynAssertType((dbCommon*)record, TYPE_STRING)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    if (priv->dlen >= sizeof(record->val))
    {
        fprintf(stderr,
            "%s: string size reduced from %d to %d\n",
            record->name, priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevAsynReadArr((dbCommon*) record, record->val, 0);
    }
    regDevDebugLog(1, "regDevInitRecordStringout(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteStringout(stringoutRecord* record)
{
    return regDevAsynWriteArr((dbCommon*) record, record->val, 0);
}


/* waveform *********************************************************/

#include <waveformRecord.h>

long regDevInitRecordWaveform(waveformRecord *);
long regDevReadWaveform(waveformRecord *);

struct devsup regDevWaveform =
{
    5,
    NULL,
    NULL,
    regDevInitRecordWaveform,
    regDevGetInIntInfo,
    regDevReadWaveform
};

epicsExportAddress(dset, regDevWaveform);

long regDevInitRecordWaveform(waveformRecord* record)
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

long regDevReadWaveform(waveformRecord* record)
{
    record->nord = record->nelm;
    return regDevReadArr((dbCommon*) record, record->bptr, record->nelm);
}

long regDevAsynInitRecordWaveform(waveformRecord *);
long regDevAsynReadWaveform(waveformRecord *);

struct devsup regDevAsynWaveform =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordWaveform,
    regDevAsynGetInIntInfo,
    regDevAsynReadWaveform
};

epicsExportAddress(dset, regDevAsynWaveform);

long regDevAsynInitRecordWaveform(waveformRecord* record)
{
    regDevAsynPrivate* priv;
    int status;
    
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) start\n", record->name);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
        return -1;
    callbackSetCallback(regDevAsyncCallback, priv->callback);
    callbackSetUser(record, priv->callback);
    record->nord = record->nelm;
    if ((status = regDevAsynCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) done\n", record->name);
    return status;
}

long regDevAsynReadWaveform(waveformRecord* record)
{
    int status = regDevAsynReadArr((dbCommon*) record, record->bptr, record->nelm);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK) record->nord = record->nelm;
    return status;
}

