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
    if (!regDevAllocPriv((dbCommon*)record)) return S_dev_noMemory;
    return regDevIoParse((dbCommon*)record, &record->inp);
}

long regDevReadStat(biRecord* record)
{
    int status;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (!priv)
    {
        recGblSetSevr(record, UDF_ALARM, INVALID_ALARM);
        regDevPrintErr("not initialized");
        return -1;
    }
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevRead((dbCommon*)record, 0, 0, NULL);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    record->rval = (status == S_dev_success);
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
    epicsInt32 rval;
    
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
    epicsInt32 rval;

    regDevCommonInit(record, out, TYPE_INT);
    if (!record->mask) record->mask = 1 << priv->bit;
    if (priv->invert) priv->invert = record->mask;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateBo);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadBits((dbCommon*)record, &rval);
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    return S_dev_success;
}

long regDevUpdateBo(boRecord *record)
{
    int status;
    epicsInt32 rval;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    record->val = rval != 0;
    return S_dev_success;
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
    epicsInt32 rval;
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
    record->udf = FALSE;
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
    epicsInt32 rval;
    int i;

    regDevCommonInit(record, out, TYPE_INT);
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateMbbo);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return DONT_CONVERT;
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
    record->udf = FALSE;
    return DONT_CONVERT; 
}

long regDevUpdateMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    /* If any values defined go through RVAL field else use VAL field directly */
    record->rval = rval;
    rval >>= record->shft;
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
                    return S_dev_success;
                }                    
            }
            return S_dev_success;
        }
    }
    /* write VAL directly */
    record->val = rval;
    record->udf = FALSE;
    return S_dev_success;
}

long regDevWriteMbbo(mbboRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    regDevCheckAsyncWriteResult(record);
    rval = record->val;
    if (record->sdef) for (i = 0; i < 16; i++)
    {
        if ((&record->zrvl)[i]) /* any state defined */
        {
            rval = record->rval;
            break;
        }
    }
    rval <<= record->shft;
    status =  regDevWriteBits((dbCommon*)record, rval, record->mask);
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
    epicsInt32 rval;
    
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
    epicsInt32 rval;
    int i;

    regDevCommonInit(record, out, TYPE_INT);
    record->mask <<= record->shft;
    priv->invert <<= record->shft;
    regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateMbboDirect);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadBits((dbCommon*)record, &rval);
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    rval >>= record->shft;
    for (i = 0; i < 16; i++)
    {
        (&record->b0)[i] = rval & 1;
        rval >>= 1;
    }
    return S_dev_success;
}

long regDevUpdateMbboDirect(mbboDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;

    status = regDevReadBits((dbCommon*)record, &rval);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    if (record->mask) rval &= record->mask;
    record->rval = rval;
    rval >>= record->shft;
    record->val = rval;
    for (i = 0; i < 16; i++)
    {
        (&record->b0)[i] = rval & 1;
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
    epicsInt32 val;
    
    status = regDevReadBits((dbCommon*)record, &val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    record->val = val;
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
    epicsInt32 val;

    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD);
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateLongout);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return S_dev_success;
    status = regDevReadBits((dbCommon*)record, &val);
    if (status) return status;
    record->val = val;
    return S_dev_success;
}

long regDevUpdateLongout(longoutRecord* record)
{
    int status;
    epicsInt32 val;

    status = regDevReadBits((dbCommon*)record, &val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status) return status;
    record->val = val;
    return S_dev_success;
}

long regDevWriteLongout(longoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteBits((dbCommon*)record, record->val, 0);
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
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == ASYNC_COMPLETION) return S_dev_success;
    if (status == S_dev_success)
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
    epicsInt32 rval;

    regDevCommonInit(record, out, TYPE_INT|TYPE_BCD|TYPE_FLOAT);
    regDevSpecialLinconvAo(record, TRUE);
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateAo);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return DONT_CONVERT;
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == S_dev_success)
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
    return status;
}

long regDevUpdateAo(aoRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == S_dev_success)
    {
        record->rval = rval;
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
            if (status) return status;
        }
	record->val = val;
    }
    if (status == DONT_CONVERT)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        record->val = val;
        return S_dev_success;
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
    priv->dlen = sizeof(record->val);
    status = regDevIoParse((dbCommon*)record, &record->inp);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    if (priv->dlen >= sizeof(record->val))
    {
        regDevPrintErr("string size reduced from %d to %d",
            priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = (int)sizeof(record->val)-1;
    }
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
    priv->dlen = sizeof(record->val);
    status = regDevIoParse((dbCommon*)record, &record->out);
    if (status) return status;
    status = regDevAssertType((dbCommon*)record, TYPE_STRING);
    if (status) return status;
    if (priv->dlen >= sizeof(record->val))
    {
        regDevPrintErr("string size reduced from %d to %d",
            priv->dlen, (int)sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    priv->data.buffer = record->val;
    status = regDevInstallUpdateFunction((dbCommon*)record, regDevUpdateStringout);
    if (status) return status;
    if (priv->initoffset == DONT_INIT) return S_dev_success;
    return regDevReadArray((dbCommon*) record, sizeof(record->val));
}

long regDevUpdateStringout(stringoutRecord* record)
{
    int status;

    status = regDevReadArray((dbCommon*) record, sizeof(record->val));
    if (status == ASYNC_COMPLETION) return S_dev_success;
    return status;
}

long regDevWriteStringout(stringoutRecord* record)
{
    int status;

    regDevCheckAsyncWriteResult(record);
    status = regDevWriteArray((dbCommon*) record, 0);
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
    status = regDevIoParse((dbCommon*)record, &record->inp);
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
