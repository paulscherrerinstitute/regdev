#include <stdlib.h>
#include <dbAccess.h>
#include <dbEvent.h>
#include <epicsString.h>
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

long regDevInitRecordAai(aaiRecord* record)
{
    regDevPrivate* priv;
    int status;

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    status = regDevCheckFTVL((dbCommon*)record, record->ftvl);
    if (status) return status;
    status = regDevIoParse((dbCommon*)record, &record->inp,
        record->ftvl==DBF_FLOAT || record->ftvl==DBF_DOUBLE ? TYPE_FLOAT : 0);
    if (status) return status;
    record->nord = record->nelm;
    /* We can map the record directly into the blockBuffer if
       - we have a blockBuffer
       - we do not need to modify the data (e.g by swapping)
       - the offset is constant (before EPICS 3.15.1)
       - we do not overflow the blockBuffer
    */
    if (priv->device->blockBuffer &&
        !priv->device->swap &&
        priv->dtype < 100 &&  /* not a BCD type */
        !priv->invert &&
        !priv->mask &&
        !priv->fifopacking &&
        !priv->interlace &&
#if EPICSVER < 31501
        !priv->offsetRecord &&
#endif
        priv->offset + record->nelm * priv->dlen <= priv->device->size)
    {
        /* map record directly in block buffer */
        record->bptr = priv->device->blockBuffer + priv->offset;
    }
    else
    {
        /* aai record does not allocate bptr in older EPICS versions. */
        status = regDevMemAlloc((dbCommon*)record, &record->bptr, record->nelm * priv->dlen);
        if (status) return status;
    }
    priv->data.buffer = record->bptr;
    status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm);
    if (status == ARRAY_CONVERT)
    {
        /* convert to float/double */
        record->bptr = calloc(record->nelm, dbValueSize(record->ftvl));
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
        regDevPrintErr("private buffer is not allocated");
        return S_dev_noMemory;
    }
    status = regDevReadArray((dbCommon*)record, record->nelm);
    record->nord = record->nelm;
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (priv->data.buffer == record->bptr) return S_dev_success;
    /* convert to float/double */
    return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
        record->bptr, record->nelm, record->lopr, record->hopr);
}

/* aao **************************************************************/

#include <aaoRecord.h>

long regDevInitRecordAao(aaoRecord *);
long regDevWriteAao(aaoRecord *);
long regDevUpdateAao(aaoRecord *);

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
    regDevPrivate* priv;
    int status;

    regDevDebugLog(DBG_INIT, "%s:\n", record->name);
    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    status = regDevCheckFTVL((dbCommon*)record, record->ftvl);
    if (status) return status;
    status = regDevIoParse((dbCommon*)record, &record->out,
        record->ftvl==DBF_FLOAT || record->ftvl==DBF_DOUBLE ? TYPE_FLOAT : 0);
    if (status) return status;
    record->nord = record->nelm;

    /* We can map the record directly into the blockBuffer if
       - we have a blockBuffer
       - we do not need to modify the data (e.g by swapping)
       - set and readback offset are the same
       - the offset is constant (before EPICS 3.15.1)
       - we do not overflow the blockBuffer
    */
    if (priv->device->blockBuffer &&
        !priv->device->swap &&
        priv->dtype < 100 &&  /* not a BCD type */
        !priv->invert &&
        !priv->mask &&
        !priv->fifopacking &&
        !priv->interlace &&
#if EPICSVER < 31501
        !priv->offsetRecord &&
#endif
        (priv->rboffset == DONT_INIT || priv->rboffset == priv->offset) &&
        priv->offset + record->nelm * priv->dlen <= priv->device->size)
    {
        /* map record directly in block buffer */
        record->bptr = priv->device->blockBuffer + priv->offset;
    }
    else
    {
        /* aao record does not allocate bptr in older EPICS versions. */
        status = regDevMemAlloc((dbCommon*)record, &record->bptr, record->nelm * priv->dlen);
        if (status) return status;
    }
    priv->data.buffer = record->bptr;
    status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm);
    if (status == ARRAY_CONVERT)
    {
        /* convert from float/double */
        record->bptr = calloc(record->nelm, dbValueSize(record->ftvl));
        if (!record->bptr) return S_dev_noMemory;
    }
    else if (status) return status;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateAao);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return S_dev_success;
    status = regDevReadArray((dbCommon*)record, record->nelm);
    if (status) return status;
    if (priv->data.buffer == record->bptr) return S_dev_success;
    /* convert to float/double */
    return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
        record->bptr, record->nelm, record->lopr, record->hopr);
}

long regDevUpdateAao(aaoRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int status;
    unsigned short monitor_mask;

    if (!record->bptr)
    {
        regDevPrintErr("private buffer is not allocated");
        return S_dev_noMemory;
    }
    status = regDevReadArray((dbCommon*)record, record->nelm);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        if (priv->data.buffer != record->bptr) /* convert to float/double */
        {
            status = regDevScaleFromRaw((dbCommon*)record, record->ftvl,
                record->bptr, record->nelm, record->lopr, record->hopr);
        }
    }
    monitor_mask = recGblResetAlarms(record);
#if EPICSVER < 31412
    monitor_mask |= DBE_LOG|DBE_VALUE;
#else
    if (record->mpst == aaoPOST_Always)
        monitor_mask |= DBE_VALUE;
    if (record->apst == aaoPOST_Always)
        monitor_mask |= DBE_LOG;
    /* Calculate hash if we are interested in OnChange events. */
    if ((record->mpst == aaoPOST_OnChange) ||
        (record->apst == aaoPOST_OnChange))
    {
        unsigned int hash = epicsMemHash(record->bptr,
            record->nord * dbValueSize(record->ftvl), 0);

        /* Only post OnChange values if the hash is different. */
        if (hash != record->hash)
        {
            if (record->mpst == aaoPOST_OnChange)
                monitor_mask |= DBE_VALUE;
            if (record->apst == aaoPOST_OnChange)
                monitor_mask |= DBE_LOG;

            /* Store hash for next process. */
            record->hash = hash;
            /* Post HASH. */
            db_post_events(record, &record->hash, DBE_VALUE);
        }
    }
    if (monitor_mask)
#endif
        db_post_events(record, record->val, monitor_mask);
    return status;
}

long regDevWriteAao(aaoRecord* record)
{
    int status;

    /* Note: due to the current implementation of aao, we never get here with PACT=1 */
    regDevCheckAsyncWriteResult(record);
    if (!record->bptr)
    {
        regDevPrintErr("private buffer is not allocated");
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
