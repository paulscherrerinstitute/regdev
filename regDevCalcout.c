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
    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD|TYPE_FLOAT);
    return S_dev_success;
}

long regDevWriteCalcout(calcoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteNumber((dbCommon*)record, (epicsInt64)record->oval, record->oval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
