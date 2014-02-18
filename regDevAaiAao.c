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
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordAai(%s) link type %d\n", record->name, record->inp.type);
    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    status = regDevCheckFTVL((dbCommon*)record, record->ftvl);
    if (status) return status;
    status = regDevIoParse((dbCommon*)record, &record->inp);
    if (status) return status;
    record->nord = record->nelm;
    /* aai record does not allocate bptr. */
    status = regDevMemAlloc((dbCommon*)record, (void*)&record->bptr, record->nelm * priv->dlen);
    if (status) return status;
    priv->data.buffer = record->bptr;
    status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm);
    if (status == ARRAY_CONVERT)
    {
        /* convert to float/double */
        record->bptr = calloc(record->nelm, sizeofTypes[record->ftvl]);
        if (!record->bptr) return S_dev_noMemory;
    }
    return status;
}

long regDevReadAai(aaiRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int status;

    if (!record->bptr)
    {
        fprintf (stderr, "regDevReadAai %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    status = regDevReadArray((dbCommon*)record, record->nelm);
    record->nord = record->nelm;
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (priv->data.buffer != record->bptr)
    {    
         /* convert to float/double */
        return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
            record->bptr, record->nelm, record->lopr, record->hopr);
    }
    return S_dev_success;
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
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordAao(%s) link type %d\n", record->name, record->out.type);
    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    status = regDevCheckFTVL((dbCommon*)record, record->ftvl);
    if (status) return status;
    status = regDevIoParse((dbCommon*)record, &record->out);
    if (status) return status;
    record->nord = record->nelm;
    /* aao record does not allocate bptr. */
    status = regDevMemAlloc((dbCommon*)record, (void *)&record->bptr, record->nelm * priv->dlen);
    if (status) return status;
    priv->data.buffer = record->bptr;
    status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm);
    if (status == ARRAY_CONVERT)
    {
        /* convert from float/double */
        record->bptr = calloc(record->nelm, sizeofTypes[record->ftvl]);
        if (!record->bptr) return S_dev_noMemory;  
    }
    else if (status) return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArray((dbCommon*)record, record->nelm);
        if (status) return status;
        if (priv->data.buffer != record->bptr)
        {    
            /* convert to float/double */
            return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
                record->bptr, record->nelm, record->lopr, record->hopr);
        }
    }
    return S_dev_success;
}

long regDevWriteAao(aaoRecord* record)
{
    int status;

    /* Note: due to the current implementation of aao, we never get here with PACT=1 */
    regDevCheckAsyncWriteResult(record);
    if (!record->bptr)
    {
        fprintf (stderr, "regDevWriteAao %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    if (priv->data.buffer != record->bptr)
    {
         /* convert from float/double */
        status = regDevScaleToRaw((dbCommon*)record, record->ftvl,
                record->bptr, record->nelm, record->lopr, record->hopr);
        if (status) return status;
    }
    status = regDevWriteArray((dbCommon*) record, record->nelm);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
