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
    
    regDevDebugLog(1, "regDevInitRecordAai(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAai(%s) link type %d\n", record->name, record->inp.type);
    /* aai record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
    	record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
        if (record->bptr == NULL)
        {
            fprintf (stderr, "regDevInitRecordAai %s: out of memory\n", record->name);
            return S_dev_noMemory;
        }
    }

    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->inp)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    regDevDebugLog(1, "regDevInitRecordAai(%s) done\n", record->name);
    return status;
}

long regDevReadAai(aaiRecord* record)
{
    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevReadAai %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    record->nord = record->nelm;
    return regDevReadArr((dbCommon*)record, record->bptr, record->nelm);
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
    
    regDevDebugLog(1, "regDevInitRecordAao(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAao(%s) link type %d\n", record->name, record->out.type);
    /* aao record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
    	record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
        if (record->bptr == NULL)
        {
            fprintf (stderr, "regDevInitRecordAao %s: out of memory\n", record->name);
            return S_dev_noMemory;
        }
    }
    if ((priv = regDevAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevIoParse((dbCommon*)record, &record->out)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevReadArr((dbCommon*)record, record->bptr, record->nelm);
    }
    regDevDebugLog(1, "regDevInitRecordAao(%s) done\n", record->name);
    return status;
}

long regDevWriteAao(aaoRecord* record)
{
    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevWriteAao %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    return regDevWriteArr((dbCommon*) record, record->bptr, record->nelm);
}

/* Asyncronous **************************************************************/

static void regDevAsyncCallback(CALLBACK *pcallback)
{
    struct dbCommon* 	precord;
    callbackGetUser(precord,  pcallback);
    callbackRequestProcessCallback(pcallback, precord->prio, precord);
}

long regDevAsynInitRecordAai(aaiRecord *);
long regDevAsynReadAai(aaiRecord *);

struct devsup regDevAsynAai =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordAai,
    regDevAsynGetInIntInfo,
    regDevAsynReadAai
};

epicsExportAddress(dset, regDevAsynAai);

long regDevAsynInitRecordAai(aaiRecord* record)
{
    regDevAsynPrivate* priv;
    int status;
    
    regDevDebugLog(1, "regDevInitRecordAai(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAai(%s) link type %d\n", record->name, record->inp.type);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
	
    if ((status = regDevAsynCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->inp)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevAsynCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    /* aai record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
	/* if deivce driver provides buff_alloc routine use it */
	if( regDevAsynMemAlloc((dbCommon*)record, (void*)&record->bptr, record->nelm * priv->dlen) != 0)
	{
	    record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
	}
		
        if (record->bptr == NULL)
        {
            fprintf (stderr, "regDevInitRecordAai %s: out of memory\n", record->name);
            return S_dev_noMemory;
        }
    }
    
    /* async processing makes sense only if we could get mem bus address */
    if( priv->busBufPtr ) 	
      {
        if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
          return -1;
        callbackSetCallback(regDevAsyncCallback, priv->callback);
        callbackSetUser(record, priv->callback);
      }
    
    regDevDebugLog(1, "regDevInitRecordAai(%s) done\n", record->name);
    return status;
}

long regDevAsynReadAai(aaiRecord* record)
{
    int status;

    regDevAsynPrivate* priv = record->dpvt;
    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevAsynReadAai %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    if(!priv->busBufPtr)
    	status = regDevAsynReadArr((dbCommon*)record, record->bptr, record->nelm);
    else 
    	status = regDevAsynReadArr((dbCommon*)record, priv->busBufPtr, record->nelm);
    if (status == ASYNC_COMPLETITION) return OK;
    record->nord = record->nelm;
    return status;
}

/* aao **************************************************************/

#include <aaoRecord.h>

long regDevAsynInitRecordAao(aaoRecord *);
long regDevAsynWriteAao(aaoRecord *);

struct devsup regDevAsynAao =
{
    5,
    NULL,
    NULL,
    regDevAsynInitRecordAao,
    regDevAsynGetInIntInfo,
    regDevAsynWriteAao
};

epicsExportAddress(dset, regDevAsynAao);

long regDevAsynInitRecordAao(aaoRecord* record)
{
    int status;
    regDevAsynPrivate* priv;
    
    regDevDebugLog(1, "regDevInitRecordAao(%s) start\n", record->name);
    regDevDebugLog(1, "regDevInitRecordAao(%s) link type %d\n", record->name, record->out.type);
    if ((priv = regDevAsynAllocPriv((dbCommon*)record)) == NULL)
        return S_dev_noMemory;
    if ((status = regDevAsynCheckFTVL((dbCommon*)record, record->ftvl)))
        return status;
    if ((status = regDevAsynIoParse((dbCommon*)record, &record->out)))
        return status;
    record->nord = record->nelm;
    if ((status = regDevAsynCheckType((dbCommon*)record, record->ftvl, record->nelm)))
        return status;
    /* aao record does not allocate bptr. why not? */
    if (!record->bptr) {
    	if(record->ftvl>DBF_ENUM) record->ftvl=2;
	if( regDevAsynMemAlloc((dbCommon*)record, (void *)&record->bptr, record->nelm * priv->dlen) != 0)
	{
	    record->bptr = (char *)calloc(record->nelm, sizeofTypes[record->ftvl]);
	}
        if (record->bptr == NULL)
        {
            fprintf (stderr, "regDevInitRecordAao %s: out of memory\n", record->name);
            return S_dev_noMemory;
        }
    }
    /* async processing makes sense only if we could get mem bus address */
    if( priv->busBufPtr ) 	
      {
        if((priv->callback = (CALLBACK *)(calloc(1,sizeof(CALLBACK))))==NULL)
            return -1;
        callbackSetCallback(regDevAsyncCallback, priv->callback);
        callbackSetUser(record, priv->callback);
      }
      
    if (priv->initoffset != DONT_INIT)
    {
        status = regDevAsynReadArr((dbCommon*)record, record->bptr, record->nelm);
    }
    regDevDebugLog(1, "regDevInitRecordAao(%s) done\n", record->name);
    return status;
}

long regDevAsynWriteAao(aaoRecord* record)
{
    regDevAsynPrivate* priv = record->dpvt;
    if (record->bptr == NULL)
    {
        fprintf (stderr, "regDevWriteAao %s: private buffer is not allocated\n", record->name);
        return S_dev_noMemory;
    }
    if(!priv->busBufPtr)
    	return regDevAsynWriteArr((dbCommon*) record, record->bptr, record->nelm);
    else 
    	return regDevAsynWriteArr((dbCommon*)record, priv->busBufPtr, record->nelm);
}
