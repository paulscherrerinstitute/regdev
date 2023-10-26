#include <string.h>
#include <stdlib.h>
#include <dbAccess.h>
#include <dbEvent.h>
#include <epicsString.h>
#include "regDevSup.h"

/* lsi **************************************************************/

#include <lsiRecord.h>

long regDevInitRecordLsi(lsiRecord *);
long regDevReadLsi(lsiRecord *);

struct devsup regDevLsi =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLsi,
    regDevGetInIntInfo,
    regDevReadLsi
};

epicsExportAddress(dset, regDevLsi);

long regDevInitRecordLsi(lsiRecord* record)
{
    regDevPrivate* priv;
    int status;

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->L = record->sizv;
    status = regDevIoParse((dbCommon*)record, &record->inp, TYPE_STRING);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    priv->data.buffer = record->val;
    return status;
}

long regDevReadLsi(lsiRecord* record)
{
    char* end;
    int status;

    status = regDevReadArray((dbCommon*)record, record->sizv);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    /* we cannot assume that the string was terminated */
    end = memchr(record->val, 0, record->sizv);
    if (end) record->len = (epicsUInt32)(end - record->val);
    else record->val[record->len = record->sizv-1] = 0;
    return S_dev_success;
}

/* lso **************************************************************/

#include <lsoRecord.h>

long regDevInitRecordLso(lsoRecord *);
long regDevWriteLso(lsoRecord *);
long regDevUpdateLso(lsoRecord *);

struct devsup regDevLso =
{
    5,
    NULL,
    NULL,
    regDevInitRecordLso,
    regDevGetInIntInfo,
    regDevWriteLso
};

epicsExportAddress(dset, regDevLso);

long regDevInitRecordLso(lsoRecord* record)
{
    regDevPrivate* priv;
    char* end;
    int status;

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->L = record->sizv;
    status = regDevIoParse((dbCommon*)record, &record->out, TYPE_STRING);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    priv->data.buffer = record->val;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateLso);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return S_dev_success;
    status = regDevReadArray((dbCommon*)record, record->sizv);
    if (status) return status;
    end = memchr(record->val, 0, record->sizv);
    if (end) record->len = (epicsUInt32)(end - record->val);
    else record->val[record->len = record->sizv-1] = 0;
    return S_dev_success;
}

long regDevUpdateLso(lsoRecord* record)
{
    char* end;
    int status;
    unsigned short monitor_mask;

    status = regDevReadArray((dbCommon*)record, record->sizv);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    end = memchr(record->val, 0, record->sizv);
    if (end) record->len = (epicsUInt32)(end - record->val);
    else record->val[record->len = record->sizv-1] = 0;
    monitor_mask = recGblResetAlarms(record);
    if (record->len != record->olen ||
        memcmp(record->oval, record->val, record->len))
    {
        monitor_mask |= DBE_VALUE | DBE_LOG;
        memcpy(record->oval, record->val, record->len);
    }
    if (record->len != record->olen)
    {
        record->olen = record->len;
        db_post_events(record, &record->len, DBE_VALUE | DBE_LOG);
    }
    if (monitor_mask)
        db_post_events(record, record->val, monitor_mask);
    return status;
}

long regDevWriteLso(lsoRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    if (record->len+1 < record->sizv)
        memset(record->val+record->len+1, 0, record->sizv-record->len-1);
    status = regDevWriteArray((dbCommon*) record, record->sizv);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
