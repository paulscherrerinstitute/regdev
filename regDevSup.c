#include <stdio.h>
#include <stdlib.h>

#include "regDevSup.h"

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
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevRead((dbCommon*)record, 0, 0, NULL);
    if (status == ASYNC_COMPLETITION) return OK;
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
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordBi(%s) start\n", record->name);
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
    regDevDebugLog(DBG_INIT, "regDevInitRecordBi(%s) done\n", record->name);
    return OK;
}

long regDevReadBi(biRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordBo(%s) start\n", record->name);
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
        status = DONT_CONVERT;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
        if (status == OK) record->rval = rval;
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordBo(%s) done\n", record->name);
    return status;
}

long regDevWriteBo(boRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteBits((dbCommon*)record, record->rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
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
    int status;
    regDevPrivate* priv;

    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbi(%s) start\n", record->name);
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
    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbi(%s) done\n", record->name);
    return OK;
}

long regDevReadMbbi(mbbiRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status != OK) return status;
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
    record->udf = FALSE;
    return DONT_CONVERT;
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbo(%s) start\n", record->name);
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
                regDevDebugLog(DBG_INIT, "regDevInitRecordMbbo(%s) done RVAL=%ld\n", record->name, (long)record->rval);
                return OK;
            }
        }
        if (record->shft > 0) rval >>= record->shft;
        record->val = rval;
        record->udf = FALSE;
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbo(%s) done VAL=%d\n", record->name, record->val);
    return DONT_CONVERT;
}

long regDevWriteMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    regDevCheckAsyncWriteResult(record);
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            /* any values defined ? */
            rval = record->rval;
            goto write;
        }
    }
    rval = record->val;
    if (record->shft > 0) rval <<= record->shft;
write:
    status =  regDevWriteBits((dbCommon*)record, rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
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
    regDevPrivate* priv;
    int status;

    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbiDirect(%s) start\n", record->name);
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
    regDevDebugLog(DBG_INIT, "regDevInitRecordMbbiDirect(%s) done\n", record->name);
    return OK;
}

long regDevReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordMbboDirect(%s) start\n", record->name);
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
        status = DONT_CONVERT;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
        if (status == OK) record->rval = rval;
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordMbboDirect(%s) done\n", record->name);
    return status;
}

long regDevWriteMbboDirect(mbboDirectRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteBits((dbCommon*)record, record->rval, record->mask);
    if (status == ASYNC_COMPLETITION) return OK;
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
    int status;

    regDevDebugLog(DBG_INIT, "regDevInitRecordLongin(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    regDevDebugLog(DBG_INIT, "regDevInitRecordLongin(%s) done\n", record->name);
    return OK;
}

long regDevReadLongin(longinRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, NULL);
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordLongout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadNumber((dbCommon*)record, &rval, NULL);
        if (!status) record->val = rval;
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordLongout(%s) done\n", record->name);
    return status;
}

long regDevWriteLongout(longoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteNumber((dbCommon*)record, record->val, 0.0);
    if (status == ASYNC_COMPLETITION) return OK;
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
    int status;

    regDevDebugLog(DBG_INIT, "regDevInitRecordAi(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAi(record, TRUE);
    regDevDebugLog(DBG_INIT, "regDevInitRecordAi(%s) done\n", record->name);
    return OK;
}

long regDevReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == ASYNC_COMPLETITION) return OK;
    if (status == OK)
    {
        record->rval = rval;
    }
    if (status == DONT_CONVERT)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        if (!record->udf &&
            record->smoo != 0.0 &&
            record->val == record->val) /* don't smooth NAN */
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
    epicsUInt32 hwSpan;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (after && priv) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->hwHigh * record->egul - priv->hwLow * record->eguf)
            / hwSpan;
        switch (priv->dtype)
        {
            case epicsInt8T:
            case epicsInt16T:
            case epicsInt32T:
                record->eoff = 
                    ((epicsInt32)priv->hwHigh * record->egul - (epicsInt32)priv->hwLow * record->eguf)
                    / hwSpan;
                break;
            default:
                record->eoff = 
                    ((epicsUInt32)priv->hwHigh * record->egul - (epicsUInt32)priv->hwLow * record->eguf)
                    / hwSpan;
        }                   
        regDevDebugLog(DBG_INIT, "regDevSpecialLinconvAi(%s, 1): hwHigh=0x%08x=%d, hwLow=0x%08x=%d, hwSpan=%u, ESLO=%g, EOFF=%g\n",
            record->name, priv->hwHigh, priv->hwHigh, priv->hwLow, priv->hwLow, hwSpan, record->eslo, record->eoff);
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordAo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAo(record, TRUE);
    if (priv->initoffset == DONT_INIT)
    {
        status = DONT_CONVERT;
    } 
    else 
    {
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
            record->val = val;
        }
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordAo(%s) done\n", record->name);
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
        record->name, record->rval, record->oval, val);
    status = regDevWriteNumber((dbCommon*)record, record->rval, val);
    if (status == ASYNC_COMPLETITION) return OK;
    return status;
}

long regDevSpecialLinconvAo(aoRecord* record, int after)
{
    epicsUInt32 hwSpan;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;

    if (after) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        switch (priv->dtype)
        {
            case epicsInt8T:
            case epicsInt16T:
            case epicsInt32T:
                record->eoff = 
                    ((epicsInt32)priv->hwHigh * record->egul - (epicsInt32)priv->hwLow * record->eguf)
                    / hwSpan;
                break;
            default:
                record->eoff = 
                    ((epicsUInt32)priv->hwHigh * record->egul - (epicsUInt32)priv->hwLow * record->eguf)
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
           RVAL = H * (EGUL - EGUL) + L * (EGUF - EGUL)] / (EGUF - EGUL)
                = L * (EGUF - EGUL)  / (EGUF - EGUL)
                = L

    OVAL = EGUF:
           RVAL = H * (EGUF - EGUL) + L * (EGUF - EGUF)] / (EGUF - EGUL)
                = H * (EGUF - EGUL) / (EGUF - EGUL)
                = H            
*/        

        regDevDebugLog(DBG_INIT, "regDevSpecialLinconvAo(%s, 1): hwHigh=0x%08x=%d, hwLow=0x%08x=%d, hwSpan=%u, ESLO=%g, EOFF=%g\n",
            record->name, priv->hwHigh, priv->hwHigh, priv->hwLow, priv->hwLow, hwSpan, record->eslo, record->eoff);
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordStringin(%s) start\n", record->name);
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
    priv->data.buffer = record->val;
    regDevDebugLog(DBG_INIT, "regDevInitRecordStringin(%s) done\n", record->name);
    return OK;
}

long regDevReadStringin(stringinRecord* record)
{
    int status;
    
    status = regDevReadArray((dbCommon*) record, sizeof(record->val));
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

    regDevDebugLog(DBG_INIT, "regDevInitRecordStringout(%s) start\n", record->name);
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
    priv->data.buffer = record->val;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArray((dbCommon*) record, sizeof(record->val));
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordStringout(%s) done\n", record->name);
    return status;
}

long regDevWriteStringout(stringoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteArray((dbCommon*) record, 0);
    if (status == ASYNC_COMPLETITION) return OK;
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
    
    regDevDebugLog(DBG_INIT, "regDevInitRecordWaveform(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)) != OK)
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)) != OK)
        return status;
    record->nord = record->nelm;
    priv->data.buffer = record->bptr;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)) != OK)
    {
        if (status != ARRAY_CONVERT) return status;
        /* convert to float/double */
        priv->data.buffer = calloc(1, record->nelm * priv->dlen);
    }
    regDevDebugLog(DBG_INIT, "regDevInitRecordWaveform(%s) done\n", record->name);
    return status;
}

long regDevReadWaveform(waveformRecord* record)
{
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;
    int status;

    status = regDevReadArray((dbCommon*) record, record->nelm);
    record->nord = record->nelm;
    if (status == ASYNC_COMPLETITION) return OK;
    if (status != OK) return status;
    if (priv->data.buffer != record->bptr)
    {    
        return regDevScaleFromRaw((dbCommon*)record, record->ftvl,
            record->bptr, record->nelm, record->lopr, record->hopr);
    }
    return OK;
}
