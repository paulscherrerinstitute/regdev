#include <dbAccess.h>
#include <dbEvent.h>
#include "regDevSup.h"

/* int64in ***********************************************************/

#include <int64inRecord.h>

long regDevInitRecordInt64in(int64inRecord *);
long regDevReadInt64in(int64inRecord *);

struct devsup regDevInt64in =
{
    5,
    NULL,
    NULL,
    regDevInitRecordInt64in,
    regDevGetInIntInfo,
    regDevReadInt64in
};

epicsExportAddress(dset, regDevInt64in);

long regDevInitRecordInt64in(int64inRecord* record)
{
    regDevCommonInit(record, inp, TYPE_INT|TYPE_BCD);
    return S_dev_success;
}

long regDevReadInt64in(int64inRecord* record)
{
    int status;

    status = regDevReadNumber((dbCommon*)record, &record->val, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    return S_dev_success;
}

/* int64out **********************************************************/

#include <int64outRecord.h>

long regDevInitRecordInt64out(int64outRecord *);
long regDevWriteInt64out(int64outRecord *);
long regDevUpdateInt64out(int64outRecord *);

struct devsup regDevInt64out =
{
    5,
    NULL,
    NULL,
    regDevInitRecordInt64out,
    regDevGetOutIntInfo,
    regDevWriteInt64out
};

epicsExportAddress(dset, regDevInt64out);

long regDevInitRecordInt64out(int64outRecord* record)
{
    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD);
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateInt64out);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return S_dev_success;
    return regDevReadNumber((dbCommon*)record, &record->val, NULL);
}

/* DELTA calculates the absolute difference between its arguments */
#define DELTA(last, val) ((last) > (val) ? (last) - (val) : (val) - (last))

long regDevUpdateInt64out(int64outRecord* record)
{
    int status;
    unsigned short monitor_mask;

    status = regDevReadNumber((dbCommon*)record, &record->val, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    monitor_mask = recGblResetAlarms(record);
    if (DELTA(record->mlst, record->val) > record->mdel)
    {
        monitor_mask |= DBE_VALUE;
        record->mlst = record->val;
    }
    if (DELTA(record->alst, record->val) > record->adel)
    {
        monitor_mask |= DBE_LOG;
        record->alst = record->val;
    }
    if (monitor_mask)
        db_post_events(record, &record->val, monitor_mask);
    return status;
}

long regDevWriteInt64out(int64outRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    regDevDebugLog(DBG_OUT, "%s: val=%lld (0x%llx)\n", record->name, (long long)record->val, (long long)record->val);
    status = regDevWriteNumber((dbCommon*)record, record->val, 0.0);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
