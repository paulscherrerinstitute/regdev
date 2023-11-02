/* Device Support for all standard records */

#include <stdlib.h>
#include <string.h>
#include <epicsMath.h>
#include <dbAccess.h>
#include <dbEvent.h>

#include "regDevSup.h"

/* bi for status bit ************************************************/

#include <biRecord.h>

long regDevInitRecordStat(biRecord *);
long regDevReadStat(biRecord *);

struct devsup regDevStat =
{
    5,
    NULL,
    regDevInit,
    regDevInitRecordStat,
    regDevGetInIntInfo,
    regDevReadStat
};

epicsExportAddress(dset, regDevStat);

long regDevInitRecordStat(biRecord* record)
{
    if (!regDevAllocPriv((dbCommon*)record)) return S_dev_noMemory;
    return regDevIoParse((dbCommon*)record, &record->inp, 0);
}

long regDevReadStat(biRecord* record)
{
    int status;

    /* pseudo-read (0 bytes) just to get the connection status */
    status = regDevRead((dbCommon*)record, 0, 0, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    record->rval = (status == S_dev_success);
    return S_dev_success;
}

/* bi to run updates ************************************************/

#include <boRecord.h>

long regDevInitRecordUpdater(boRecord *);
long regDevWriteUpdater(boRecord *);

struct devsup regDevUpdater =
{
    5,
    NULL,
    NULL,
    regDevInitRecordUpdater,
    NULL,
    regDevWriteUpdater
};

epicsExportAddress(dset, regDevUpdater);

long regDevInitRecordUpdater(boRecord* record)
{
    if (!regDevAllocPriv((dbCommon*)record)) return S_dev_noMemory;
    regDevIoParse((dbCommon*)record, &record->out, 0);
    return DONT_CONVERT;
}

long regDevWriteUpdater(boRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)(record->dpvt);
    if (priv == NULL)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        regDevDebugLog(DBG_OUT, "record %s not initialized\n", record->name);
        return S_dev_badInit;
    }
    if (record->val)
    {
        priv = priv->device->triggeredUpdates;
        if (priv) epicsTimerStartDelay(priv->updateTimer, 0.0);
    }
    return S_dev_success;
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
    regDevCommonInit(record, inp, TYPE_INT);
    if (!record->mask) record->mask = 1 << priv->bit;
    if (priv->invert) priv->invert = record->mask;
    return S_dev_success;
}

long regDevReadBi(biRecord* record)
{
    int status;
    epicsUInt32 rval;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    return S_dev_success;
}

/* bo ***************************************************************/

#include <boRecord.h>

long regDevInitRecordBo(boRecord *);
long regDevWriteBo(boRecord *);
long regDevUpdateBo(boRecord *);

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
    epicsUInt32 rval;

    regDevCommonInit(record, out, TYPE_INT);
    if (!record->mask) record->mask = 1 << priv->bit;
    if (priv->invert) priv->invert = record->mask;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateBo);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadBits((dbCommon*)record, &rval);
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    return S_dev_success;
}

long regDevUpdateBo(boRecord *record)
{
    int status;
    epicsUInt32 rval;
    unsigned short monitor_mask;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        if (record->mask) rval &= record->mask;
        record->rval = rval;
        record->rbv = record->val = rval != 0;
    }
    monitor_mask = recGblResetAlarms(record);
    if (record->mlst != record->val)
    {
        monitor_mask |= (DBE_VALUE | DBE_LOG);
        record->mlst = record->val;
    }
    if (monitor_mask)
        db_post_events(record, &record->val, monitor_mask);
    if (record->oraw != record->rval)
    {
        db_post_events(record,&record->rval, monitor_mask|DBE_VALUE|DBE_LOG);
        record->oraw = record->rval;
    }
    if (record->orbv != record->rbv)
    {
        db_post_events(record, &record->rbv, monitor_mask|DBE_VALUE|DBE_LOG);
        record->orbv = record->rbv;
    }
    return status;
}

long regDevWriteBo(boRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteBits((dbCommon*)record, record->rval, record->mask);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
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
    regDevCommonInit(record, inp, TYPE_INT);
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    return S_dev_success;
}

long regDevReadMbbi(mbbiRecord* record)
{
    int status;
    epicsUInt32 rval;
    int i;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    /* If any values defined write to RVAL field else to VAL field */
    if (record->sdef) for (i = 0; i < 16; i++)
    {
        if ((&record->zrvl)[i])
        {
            record->rval = rval;
            return S_dev_success;
        }
    }
    rval >>= record->shft;
    record->val = rval;
    return DONT_CONVERT;
}

/* mbbo *************************************************************/

#include <mbboRecord.h>

long regDevInitRecordMbbo(mbboRecord *);
long regDevWriteMbbo(mbboRecord *);
long regDevUpdateMbbo(mbboRecord *);

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
    epicsUInt32 rval;
    int i;

    regDevCommonInit(record, out, TYPE_INT);
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateMbbo);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadBits((dbCommon*)record, &rval);
    if (status) return status;
    if (record->mask) rval &= record->mask;
    /* If any values defined write to RVAL field else to VAL field */
    if (record->sdef) for (i = 0; i < 16; i++)
    {
        if ((&record->zrvl)[i]) /* any state defined */
        {
            record->rval = rval;
            return S_dev_success;
        }
    }
    rval >>= record->shft;
    record->val = rval;
    return DONT_CONVERT;
}

long regDevUpdateMbbo(mbboRecord* record)
{
    int status;
    epicsUInt32 rval;
    unsigned short monitor_mask;
    int i;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        if (record->mask) rval &= record->mask;
        /* If any values defined go through RVAL field else VAL field is used directly */
        record->rval = rval;
        rval >>= record->shft;
        record->rbv = record->val = rval;
        if (record->sdef) for (i = 0; i < 16; i++)
        {
            if ((&record->zrvl)[i]) /* any state defined */
            {
                record->val = 65535; /* initalize to unknown state*/
                for (i = 0; i < 16; i++)
                {
                    if ((&record->zrvl)[i] == rval)
                    {
                        record->val = i; /* get VAL from RVAL */
                        break;
                    }
                }
                break;
            }
        }
    }
    monitor_mask = recGblResetAlarms(record);
    if (record->mlst != record->val)
    {
        monitor_mask |= (DBE_VALUE | DBE_LOG);
        record->mlst = record->val;
    }
    if (monitor_mask){
        db_post_events(record, &record->val, monitor_mask);
    }
    if (record->oraw != record->rval) {
        db_post_events(record, &record->rval, monitor_mask|DBE_VALUE);
        record->oraw = record->rval;
    }
    if (record->orbv != record->rbv) {
        db_post_events(record, &record->rbv, monitor_mask|DBE_VALUE);
        record->orbv = record->rbv;
    }
    return status;
}

long regDevWriteMbbo(mbboRecord* record)
{
    int status;
    epicsUInt32 rval;
    int i;

    regDevCheckAsyncWriteResult(record);
    rval = record->val << record->shft;
    if (record->sdef) for (i = 0; i < 16; i++)
    {
        if ((&record->zrvl)[i]) /* any state defined */
        {
            rval = record->rval;
            break;
        }
    }
    status = regDevWriteBits((dbCommon*)record, rval, record->mask);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
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
    regDevCommonInit(record, inp, TYPE_INT);
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    return S_dev_success;
}

long regDevReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsUInt32 rval;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    return S_dev_success;
}

/* mbboDirect *******************************************************/

#include <mbboDirectRecord.h>

long regDevInitRecordMbboDirect(mbboDirectRecord *);
long regDevWriteMbboDirect(mbboDirectRecord *);
long regDevUpdateMbboDirect(mbboDirectRecord *);

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
    epicsUInt32 rval;
    unsigned int i;

    regDevCommonInit(record, out, TYPE_INT);
    record->udf = 0;                   /* workaround for mbboDirect bug */
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateMbboDirect);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadBits((dbCommon*)record, &rval);
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    rval >>= record->shft;
    for (i = 0; i < sizeof(record->val)*8 ; i++)
    {
        (&record->b0)[i] = rval & 1;
        rval >>= 1;
    }
    return S_dev_success;
}

long regDevUpdateMbboDirect(mbboDirectRecord* record)
{
    int status;
    epicsUInt32 rval;
    unsigned short monitor_mask;
    unsigned char *bit;
    unsigned int i;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        if (record->mask) rval &= record->mask;
        record->rval = rval;
        rval >>= record->shft;
        record->rbv = record->val = rval;
    }
    monitor_mask = recGblResetAlarms(record);
    if (record->mlst != record->val)
    {
        monitor_mask |= (DBE_VALUE | DBE_LOG);
        record->mlst = record->val;
    }
    if (monitor_mask)
    {
        db_post_events(record, &record->val, monitor_mask);
    }
    if (record->oraw!=record->rval)
    {
        db_post_events(record, &record->rval, monitor_mask|DBE_VALUE|DBE_LOG);
        record->oraw = record->rval;
    }
    if (record->orbv!=record->rbv)
    {
        db_post_events(record, &record->rbv, monitor_mask|DBE_VALUE|DBE_LOG);
        record->orbv = record->rbv;
    }
    /* update the bits */
    for (i = 0; i < sizeof(record->val)*8; i++)
    {
        bit = &(record->b0)+i;
        if ((rval & 1) == !*bit)
        {
            *bit = rval & 1;
            db_post_events(record, bit, monitor_mask |= DBE_VALUE | DBE_LOG);
        }
        else if (monitor_mask)
            db_post_events(record, bit, monitor_mask);
        rval >>= 1;
    }
    return status;
}

long regDevWriteMbboDirect(mbboDirectRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteBits((dbCommon*)record, record->rval, record->mask);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
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
    regDevCommonInit(record, inp, TYPE_INT|TYPE_BCD);
    return S_dev_success;
}

long regDevReadLongin(longinRecord* record)
{
    int status;
    epicsInt64 val;

    status = regDevReadNumber((dbCommon*)record, &val, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    record->val = (epicsInt32)val;
    return S_dev_success;
}

/* longout **********************************************************/

#include <longoutRecord.h>

long regDevInitRecordLongout(longoutRecord *);
long regDevWriteLongout(longoutRecord *);
long regDevUpdateLongout(longoutRecord *);

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
    epicsInt64 val;

    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD);
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateLongout);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return S_dev_success;
    status = regDevReadNumber((dbCommon*)record, &val, NULL);
    if (status) return status;
    record->val = (epicsInt32)val;
    return S_dev_success;
}

/* DELTA calculates the absolute difference between its arguments */
#define DELTA(last, val) ((last) > (val) ? (last) - (val) : (val) - (last))

long regDevUpdateLongout(longoutRecord* record)
{
    int status;
    epicsInt64 val;
    unsigned short monitor_mask;

    status = regDevReadNumber((dbCommon*)record, &val, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        record->val = (epicsInt32)val;
    }
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

long regDevWriteLongout(longoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteNumber((dbCommon*)record, record->val, 0);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
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
    regDevCommonInit(record, inp, TYPE_INT|TYPE_BCD|TYPE_FLOAT);
    regDevSpecialLinconvAi(record, TRUE);
    return S_dev_success;
}

long regDevReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt64 rval;
    int udf;

    udf = record->udf;
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        record->rval = (epicsInt32)rval;
        if (rval > 0x7fffffffLL || rval < -0x80000000LL)
            /* value does not fit in RVAL */
            status = DONT_CONVERT;
    }
    if (status == DONT_CONVERT)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;

        if (record->smoo != 0.0 && !isnan(record->val) && !isinf(record->val) && !udf) {
            /* do not smooth with invalid values, infinity or NaN */
            record->val = val * (1.00 - record->smoo) + (record->val * record->smoo);
        } else
            record->val = val;
        record->udf = isnan(record->val);
    }
    return status;
}

long regDevSpecialLinconvAi(aiRecord* record, int after)
{
    epicsUInt64 hwSpan;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (after && priv) {
        hwSpan = priv->H - priv->L;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->H * record->egul - priv->L * record->eguf)
            / hwSpan;
        switch (priv->dtype)
        {
            case epicsInt8T:
            case epicsInt16T:
            case epicsInt32T:
                record->eoff =
                    ((epicsInt64)priv->H * record->egul - (epicsInt64)priv->L * record->eguf)
                    / hwSpan;
                break;
            default:
                record->eoff =
                    ((epicsUInt64)priv->H * record->egul - (epicsUInt64)priv->L * record->eguf)
                    / hwSpan;
        }
        regDevDebugLog(DBG_INIT, "regDevSpecialLinconvAi(%s, 1): H=0x%llx=%lld, L=0x%llx=%lld, hwSpan=%llu, ESLO=%g, EOFF=%g\n",
            record->name, (long long)priv->H, (long long)priv->H, (long long)priv->L, (long long)priv->L, (unsigned long long)hwSpan, record->eslo, record->eoff);
    }
    return S_dev_success;
}

/* ao ***************************************************************/

#include <aoRecord.h>
#include <menuConvert.h>
#include <cvtTable.h>

long regDevInitRecordAo(aoRecord *);
long regDevWriteAo(aoRecord *);
long regDevUpdateAo(aoRecord *);
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
    double val;
    epicsInt64 rval;

    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD|TYPE_FLOAT);
    regDevSpecialLinconvAo(record, TRUE);
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateAo);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == S_dev_success)
    {
        record->rval = (epicsInt32)rval;
        if (rval > 0x7fffffffLL || rval < -0x80000000LL)
            /* value does not fit in RVAL */
            status = DONT_CONVERT;
    }
    if (status == DONT_CONVERT)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        record->val = val;
    }
    return status;
}

long regDevUpdateAo(aoRecord* record)
{
    int status;
    double val;
    epicsInt64 rval;
    unsigned short monitor_mask;

    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
    {
        record->rbv = record->rval = (epicsInt32)rval;
        if (rval > 0x7fffffffLL || rval < -0x80000000LL)
            /* value does not fit in RVAL */
            status = DONT_CONVERT;
        else
        {
            val = (double)rval + (double)record->roff;
            if (record->aslo != 0.0) val *= record->aslo;
            val += record->aoff;
            if (record->linr == menuConvertNO_CONVERSION) {
                ; /*do nothing*/
            } else if ((record->linr == menuConvertLINEAR) ||
                      (record->linr == menuConvertSLOPE)) {
                val = val * record->eslo + record->eoff;
            } else {
                status = cvtRawToEngBpt(&val, record->linr, 0,
                        (void *)&record->pbrk, &record->lbrk);
            }
        }
    }
    if (status == DONT_CONVERT)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        status = S_dev_success;
    }
    if (status == S_dev_success)
    {
        record->omod = record->oval != val;
        record->orbv = (epicsInt32)(record->oval = record->val = val);
    }
    monitor_mask = recGblResetAlarms(record);
    if (!(fabs(record->mlst - record->val) <= record->mdel)) /* Handles MDEL == NAN */
    {
        monitor_mask |= DBE_VALUE;
        record->mlst = record->val;
    }
    if (!(fabs(record->alst - record->val) <= record->adel)) /* Handles ADEL == NAN */
    {
        monitor_mask |= DBE_LOG;
        record->alst = record->val;
    }
    if (monitor_mask)
        db_post_events(record, &record->val, monitor_mask);
    if (record->omod) monitor_mask |= (DBE_VALUE|DBE_LOG);
    if (monitor_mask)
    {
        record->omod = FALSE;
        db_post_events (record, &record->oval, monitor_mask);
        if (record->oraw != record->rval)
        {
            db_post_events(record, &record->rval, monitor_mask|DBE_VALUE|DBE_LOG);
            record->oraw = record->rval;
        }
        if (record->orbv != record->rbv)
        {
            db_post_events(record, &record->rbv, monitor_mask|DBE_VALUE|DBE_LOG);
            record->orbv = record->rbv;
        }
    }
    return status;
}

long regDevWriteAo(aoRecord* record)
{
    double val;
    int status;

    regDevCheckAsyncWriteResult(record);
    val = record->oval - record->aoff;
    if (record->aslo != 0) val /= record->aslo;
    regDevDebugLog(DBG_OUT, "regDevWriteAo(record=%s): .RVAL=0x%08x, .OVAL=%g, val=%g\n",
        record->name, (unsigned int)record->rval, record->oval, val);
    status = regDevWriteNumber((dbCommon*)record, record->rval, val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}

long regDevSpecialLinconvAo(aoRecord* record, int after)
{
    epicsUInt64 hwSpan;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;

    if (after) {
        hwSpan = priv->H - priv->L;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        switch (priv->dtype)
        {
            case epicsInt8T:
            case epicsInt16T:
            case epicsInt32T:
                record->eoff =
                    ((epicsInt32)priv->H * record->egul - (epicsInt32)priv->L * record->eguf)
                    / hwSpan;
                break;
            default:
                record->eoff =
                    ((epicsUInt32)priv->H * record->egul - (epicsUInt32)priv->L * record->eguf)
                    / hwSpan;
        }

/* Proof:
           RVAL = (OVAL - EOFF) / ESLO
                = [OVAL / ESLO] - [EOFF / ESLO]
                = [OVAL / (EGUF - EGUL) * (H - L)] - [(H * EGUL - L * EGUF) / (H - L)  / (EGUF - EGUL) * (H - L)]
                = [OVAL * (H - L) / (EGUF - EGUL)] - [(H * EGUL - L * EGUF) / (EGUF - EGUL)]
                = [OVAL * (H - L) - (H * EGUL - L * EGUF)] / (EGUF - EGUL)
                = [OVAL * H - OVAL * L - H * EGUL + L * EGUF] / (EGUF - EGUL)
                = [H * (OVAL - EGUL) + L * (EGUF - OVAL)] / (EGUF - EGUL)

    OVAL = EGUL:
           RVAL = [H * (OVAL - EGUL) + L * (EGUF - OVAL)] / (EGUF - EGUL)
                = [H * (EGUL - EGUL) + L * (EGUF - EGUL)] / (EGUF - EGUL)
                = L * (EGUF - EGUL) / (EGUF - EGUL)
                = L

    OVAL = EGUF:
           RVAL = [H * (OVAL - EGUL) + L * (EGUF - OVAL)] / (EGUF - EGUL)
                = [H * (EGUF - EGUL) + L * (EGUF - EGUF)] / (EGUF - EGUL)
                = H * (EGUF - EGUL) / (EGUF - EGUL)
                = H
*/

        regDevDebugLog(DBG_INIT, "regDevSpecialLinconvAo(%s, 1): H=0x%llx=%lld, L=0x%llx=%lld, hwSpan=%llu, ESLO=%g, EOFF=%g\n",
            record->name, (long long)priv->H, (long long)priv->H, (long long)priv->L, (long long)priv->L, (unsigned long long)hwSpan, record->eslo, record->eoff);
    }
    return S_dev_success;
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

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->L = sizeof(record->val);
    status = regDevIoParse((dbCommon*)record, &record->inp, TYPE_STRING);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    priv->data.buffer = record->val;
    return S_dev_success;
}

long regDevReadStringin(stringinRecord* record)
{
    int status;

    status = regDevReadArray((dbCommon*) record, sizeof(record->val));
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;

}

/* stringout ********************************************************/

#include <stringoutRecord.h>

long regDevInitRecordStringout(stringoutRecord *);
long regDevWriteStringout(stringoutRecord *);
long regDevUpdateStringout(stringoutRecord *);

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

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    priv->dtype = epicsStringT;
    priv->L = sizeof(record->val);
    status = regDevIoParse((dbCommon*)record, &record->out, TYPE_STRING);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    priv->data.buffer = record->val;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateStringout);
    if (status) return status;
    if (priv->rboffset == DONT_INIT) return S_dev_success;
    status =  regDevReadArray((dbCommon*) record, sizeof(record->val));
    if (status) return status;
    return S_dev_success;
}

long regDevUpdateStringout(stringoutRecord* record)
{
    int status;
    unsigned short monitor_mask;

    status = regDevReadArray((dbCommon*) record, sizeof(record->val));
    if (status == ASYNC_COMPLETION) return S_dev_success;
    monitor_mask = recGblResetAlarms(record);
    if (strncmp(record->oval, record->val, sizeof(record->oval)))
    {
        monitor_mask |= DBE_VALUE | DBE_LOG;
        strncpy(record->oval, record->val, sizeof(record->oval));
    }
#ifndef EPICS_3_13
    if (record->mpst == stringoutPOST_Always)
        monitor_mask |= DBE_VALUE;
    if (record->apst == stringoutPOST_Always)
        monitor_mask |= DBE_LOG;
#endif
    if (monitor_mask)
        db_post_events(record, record->val, monitor_mask);
    return status;
}

long regDevWriteStringout(stringoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteArray((dbCommon*) record, sizeof(record->val));
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
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

    priv = regDevAllocPriv((dbCommon*)record);
    if (!priv) return S_dev_noMemory;
    status = regDevCheckFTVL((dbCommon*)record, record->ftvl);
    if (status) return status;
    status = regDevIoParse((dbCommon*)record, &record->inp,
        record->ftvl==DBF_FLOAT || record->ftvl==DBF_DOUBLE ? TYPE_FLOAT : 0);
    if (status) return status;
    record->nord = record->nelm;
    priv->data.buffer = record->bptr;
    status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm);
    if (status == ARRAY_CONVERT)
    {
        priv->data.buffer = calloc(1, record->nelm * priv->dlen);
        if (!priv->data.buffer)
        {
            regDevPrintErr("out of memory");
            return S_dev_noMemory;
        }
        return S_dev_success;
    }
    return status;
}

long regDevReadWaveform(waveformRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int status;

    status = regDevReadArray((dbCommon*) record, record->nelm);
    record->nord = record->nelm;
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status != S_dev_success) return status;
    if (priv->data.buffer != record->bptr)
    {
        return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
            record->bptr, record->nelm, record->lopr, record->hopr);
    }
    return S_dev_success;
}

/* event ************************************************************/

#include <eventRecord.h>

long regDevInitRecordEvent(eventRecord *);
long regDevReadEvent(eventRecord *);

struct devsup regDevEvent =
{
    5,
    NULL,
    NULL,
    regDevInitRecordEvent,
    regDevGetInIntInfo,
    regDevReadEvent
};

epicsExportAddress(dset, regDevEvent);

long regDevInitRecordEvent(eventRecord* record)
{
    if (!regDevAllocPriv((dbCommon*)record)) return S_dev_noMemory;
    return regDevIoParse((dbCommon*)record, &record->inp, 0);
}

long regDevReadEvent(eventRecord* record)
{
    int status;

    /* pseudo-read (0 bytes) just to get the connection status */
    status = regDevRead((dbCommon*)record, 0, 0, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}
