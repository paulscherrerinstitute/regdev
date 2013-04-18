#include <stdlib.h>
#include <stdio.h>
#include "regDevSup.h"

/* aai **************************************************************/

#include <aaiRecord.h>

long regDevInitRecordAai(aaiRecord *);
long regDevReadAai(aaiRecord *);

struct devsup regDevAai =
{
    5,
    NULL,
    NULL,
    regDevInitRecordAai,
    regDevGetInIntInfo,
    regDevReadAai
};

epicsExportAddress(dset, regDevAai);

static const int sizeofTypes[] = {MAX_STRING_SIZE,1,1,2,2,4,4,4,8,2};

long regDevInitRecordAai(aaiRecord* record)
{
    regDevPrivate* priv;
    int status;
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordAai(%s) start\n", record->name);
    regDevDebugLog(DBG_INIT, "regDevInitRecordAai(%s) link type %d\n", record->name, record->inp.type);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)) != OK)
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)) != OK)
        return status;
    record->nord = record->nelm;
    /* aai record does not allocate bptr. */
    if ((status = regDevMemAlloc((dbCommon*)record, (void*)&record->bptr, record->nelm * priv->dlen)) != OK)
        return status;
    priv->result.buffer = record->bptr;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)) != OK)
    {
        if (status != ARRAY_CONVERT) return status;
        /* convert to float/double */
        record->bptr = calloc(record->nelm, sizeofTypes[record->ftvl]);
         
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordAai(%s) done\n", record->name);
    return status;
}

long regDevReadAai(aaiRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int status;

    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevReadAai %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    status = regDevReadArray((dbCommon*)record, record->nelm);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status != OK) return status;
    if (priv->result.buffer != record->bptr)
    {    
        return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
            record->bptr, record->nelm, record->lopr, record->hopr);
    }
    return OK;
}

/* aao **************************************************************/

#include <aaoRecord.h>

long regDevInitRecordAao(aaoRecord *);
long regDevWriteAao(aaoRecord *);

struct devsup regDevAao =
{
    5,
    NULL,
    NULL,
    regDevInitRecordAao,
    regDevGetInIntInfo,
    regDevWriteAao
};

epicsExportAddress(dset, regDevAao);

long regDevInitRecordAao(aaoRecord* record)
{
    int status;
    regDevPrivate* priv;
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordAao(%s) start\n", record->name);
    regDevDebugLog(DBG_INIT, "regDevInitRecordAao(%s) link type %d\n", record->name, record->out.type);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)) != OK)
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)) != OK)
        return status;
    record->nord = record->nelm;

    /* aao record does not allocate bptr. */
    if ((status = regDevMemAlloc((dbCommon*)record, (void *)&record->bptr, record->nelm * priv->dlen)) != OK)
        return status;
    priv->result.buffer = record->bptr;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)) != OK)
    {
        if (status != ARRAY_CONVERT) return status;
         /* convert from float/double */
        record->bptr = calloc(record->nelm, sizeofTypes[record->ftvl]);
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArray((dbCommon*)record, record->nelm);
        if (status == OK && priv->result.buffer != record->bptr)
        {    
            status = regDevScaleFromRaw((dbCommon*)record, record->ftvl,
                record->bptr, record->nelm, record->lopr, record->hopr);
        }
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordAao(%s) done\n", record->name);
    return status;
}

long regDevWriteAao(aaoRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    /* Note: due to the current implementation of aao, we never get here with PACT=1 */
    regDevCheckAsyncWriteResult(record);
    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevWriteAao %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    if (priv->result.buffer != record->bptr)
    {
        status = regDevScaleToRaw((dbCommon*)record, record->ftvl,
                record->bptr, record->nelm, record->lopr, record->hopr);
        if (status) return status;
    }
    status = regDevWriteArray((dbCommon*) record, record->nelm);
    if (status == ASYNC_COMPLETITION) return OK;
    return status;
}
