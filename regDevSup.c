#include <stdio.h>

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
    return 0;
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
    assert(priv->device);
    /* psudo-read (0 bytes) just to get the connection status */
    status = regDevRead(priv->device, 0, 0, 0, NULL, 0); 
    record->rval = (status == 0);
    return 0;
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
    
    regDevDebugLog(1, "regDevInitRecordBi(%s) start\n", record->name);
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
    regDevDebugLog(1, "regDevInitRecordBi(%s) done\n", record->name);
    return 0;
}

long regDevReadBi(biRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
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

    regDevDebugLog(1, "regDevInitRecordBo(%s) start\n", record->name);
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
        status = 2;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    }
    regDevDebugLog(1, "regDevInitRecordBo(%s) done\n", record->name);
    return status;
}

long regDevWriteBo(boRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->rval, record->mask);
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

    regDevDebugLog(1, "regDevInitRecordMbbi(%s) start\n", record->name);
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
    regDevDebugLog(1, "regDevInitRecordMbbi(%s) done\n", record->name);
    return 0;
}

long regDevReadMbbi(mbbiRecord* record)
{
    int status;
    epicsInt32 rval;
    int i;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    if (status) return status;
    /* If any values defined write to RVAL field else to VAL field */
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            record->rval = rval;
            return 0;
        }
    }
    if (record->shft > 0) rval >>= record->shft;
    record->val = rval;
    return 2;
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

    regDevDebugLog(1, "regDevInitRecordMbbo(%s) start\n", record->name);
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
                regDevDebugLog(1, "regDevInitRecordMbbo(%s) done RVAL=%ld\n", record->name, record->rval);
                return 0;
            }
        }
        if (record->shft > 0) rval >>= record->shft;
        record->val = rval;
    }
    regDevDebugLog(1, "regDevInitRecordMbbo(%s) done VAL=%d\n", record->name, record->val);
    return 2;
}

long regDevWriteMbbo(mbboRecord* record)
{
    epicsInt32 rval;
    int i;
    
    if (record->sdef) for (i=0; i<16; i++)
    {
        if ((&record->zrvl)[i])
        {
            /* any values defined ? */
            rval = record->rval;
            return regDevWriteBits((dbCommon*)record, rval, record->mask);
        }
    }
    rval = record->val;
    if (record->shft > 0) rval <<= record->shft;
    return regDevWriteBits((dbCommon*)record, rval, record->mask);
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

    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) start\n", record->name);
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
    regDevDebugLog(1, "regDevInitRecordMbbiDirect(%s) done\n", record->name);
    return 0;
}

long regDevReadMbbiDirect(mbbiDirectRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, record->mask);
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

    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) start\n", record->name);
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
        status = 2;
    } 
    else 
    {
        status = regDevReadBits((dbCommon*)record, &rval, record->mask);
    }
    regDevDebugLog(1, "regDevInitRecordMbboDirect(%s) done\n", record->name);
    return status;
}

long regDevWriteMbboDirect(mbboDirectRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->rval, record->mask);
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

    regDevDebugLog(1, "regDevInitRecordLongin(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    regDevDebugLog(1, "regDevInitRecordLongin(%s) done\n", record->name);
    return 0;
}

long regDevReadLongin(longinRecord* record)
{
    int status;
    epicsInt32 rval;
    
    status = regDevReadBits((dbCommon*)record, &rval, -1);
    if (!status) record->val = rval;
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

    regDevDebugLog(1, "regDevInitRecordLongout(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadBits((dbCommon*)record, &rval, -1);
        if (!status) record->val = rval;
    }
    regDevDebugLog(1, "regDevInitRecordLongout(%s) done\n", record->name);
    return status;
}

long regDevWriteLongout(longoutRecord* record)
{
    return regDevWriteBits((dbCommon*)record, record->val, 0xFFFFFFFFUL);
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

    regDevDebugLog(1, "regDevInitRecordAi(%s) start\n", record->name);
    if (regDevAllocPriv((dbCommon*)record) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAi(record, TRUE);
    regDevDebugLog(1, "regDevInitRecordAi(%s) done\n", record->name);
    return 0;
}

long regDevReadAi(aiRecord* record)
{
    int status;
    double val;
    epicsInt32 rval;
    
    status = regDevReadNumber((dbCommon*)record, &rval, &val);
    if (status == 0)
    {
        record->rval = rval;
    }
    if (status == 2)
    {
        /* emulate scaling */
        if (record->aslo != 0.0) val *= record->aslo;
        val += record->aoff;
        if (!record->udf)
        {
            /* emulate smoothing */
            record->val = record->val * record->smoo +
                val * (1.0 - record->smoo);
        }
        else
        {
            /* don't smooth with invalid value */
            record->val = val;
        }
    }
    return status;
}

long regDevSpecialLinconvAi(aiRecord* record, int after)
{
    double hwSpan;
    regDevPrivate* priv = (regDevPrivate*)record->dpvt;

    if (after) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff =
            (priv->hwHigh*record->egul - priv->hwLow*record->eguf)
            / hwSpan;
    }
    return 0;
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

    regDevDebugLog(1, "regDevInitRecordAo(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    if ((status = regDevAssertType((dbCommon*)record, TYPE_INT|TYPE_BCD|TYPE_FLOAT)))
        return status;
    regDevSpecialLinconvAo(record, TRUE);
    if (priv->initoffset == DONT_INIT)
    {
        status = 2;
    } 
    else 
    {
        status = regDevReadNumber((dbCommon*)record, &rval, &val);
        if (status == 0)
        {
            record->rval = rval;
            record->sevr = 0;
            record->stat = 0;
        }
        if (status == 2)
        {
            /* emulate scaling */
            if (record->aslo != 0.0) val *= record->aslo;
            val += record->aoff;
            record->val = val;
            record->sevr = 0;
            record->stat = 0;
        }
    }
    regDevDebugLog(1, "regDevInitRecordAo(%s) done\n", record->name);
    return status;
}

long regDevWriteAo(aoRecord* record)
{
    double val;
    
    val = record->oval - record->aoff;
    if (record->aslo != 0) val /= record->aslo;
    return regDevWriteNumber((dbCommon*)record, val, record->rval);
}

long regDevSpecialLinconvAo(aoRecord* record, int after)
{
    epicsUInt32 hwSpan;
    regDevPrivate* priv = (regDevPrivate*) record->dpvt;

    if (after) {
        hwSpan = priv->hwHigh - priv->hwLow;
        record->eslo = (record->eguf - record->egul) / hwSpan;
        record->eoff = 
            (priv->hwHigh*record->egul -priv->hwLow*record->eguf)
            / hwSpan;
    }
    return 0;
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

    regDevDebugLog(1, "regDevInitRecordStringin(%s) start\n", record->name);
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
            record->name, priv->dlen, sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    regDevDebugLog(1, "regDevInitRecordStringin(%s) done\n", record->name);
    return 0;
}

long regDevReadStringin(stringinRecord* record)
{
    return regDevReadArr((dbCommon*) record, record->val, 0);
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

    regDevDebugLog(1, "regDevInitRecordStringout(%s) start\n", record->name);
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
            record->name, priv->dlen, sizeof(record->val)-1);
        priv->dlen = sizeof(record->val)-1;
    }
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArr((dbCommon*) record, record->val, 0);
    }
    regDevDebugLog(1, "regDevInitRecordStringout(%s) done\n", record->name);
    return status;
}

long regDevWriteStringout(stringoutRecord* record)
{
    return regDevWriteArr((dbCommon*) record, record->val, 0);
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
    
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) start\n", record->name);
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    regDevDebugLog(1, "regDevInitRecordWaveform(%s) done\n", record->name);
    return status;
}

long regDevReadWaveform(waveformRecord* record)
{
    record->nord = record->nelm;
    return regDevReadArr((dbCommon*) record, record->bptr, record->nelm);
}

