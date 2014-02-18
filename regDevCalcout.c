#include "regDevSup.h"

/* calcout **********************************************************/

#include <postfix.h>
#include <calcoutRecord.h>

long regDevInitRecordCalcout(calcoutRecord *);
long regDevWriteCalcout(calcoutRecord *);

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

long regDevInitRecordCalcout(calcoutRecord* record)
{
    regDevPrivate* priv;
    int status;

    regDevDebugLog(DBG_INIT, "regDevInitRecordCalcout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevDebugLog(DBG_INIT, "regDevInitRecordCalcout(%s) done\n", record->name);
    return status;
}

long regDevWriteCalcout(calcoutRecord* record)
{
    int status;
    
    regDevCheckAsyncWriteResult(record);
    status = regDevWriteNumber((dbCommon*)record, record->oval, record->oval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
