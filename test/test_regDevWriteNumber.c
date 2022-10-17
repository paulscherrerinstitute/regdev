#include <string.h>
#include <stdlib.h>
#include <devLib.h>
#include "epicsTypes.h"
#include "regDevSup.h"
#include "test_regDev.h"
#include "simRegDev.h"

int test_regDevWriteNumber()
{
    struct dbCommon record;
    regDevPrivate* priv;
    int parsestatus;
    int i,r,e;
    struct link link;
    regDevDebug = DBG_OUT;
    
    memset(&record, 0, sizeof(record));
    strcpy(record.name ,"test");
    
    memset(&link,  0, sizeof(link));
    link.type = INST_IO;
    link.value.instio.string = malloc(80);
    strcpy(link.value.instio.string, "test/0 T=int8");

    simRegDevConfigure ("test",100,0,0,0);
    priv = regDevAllocPriv(&record);
    assert(priv);

    parsestatus = regDevIoParse(&record, &link);
    assert(parsestatus==0);
    printf ("low=%llx hight=%llx\n", priv->L, priv->H);
    for (i=-300; i<=300; i++)
    {
        simRegDevSetData("test", 0, 0);
        regDevWriteNumber(&record, i, 0.0);
        simRegDevGetData("test", 0, &r);
        
        e = i;
        if (e > priv->H) e = priv->H;
        if (e < priv->L)  e = priv->L;
        e &= 0xff;
        
        if (r != e)
        {
            printf (FAILED " %i %#x %#x\n", i, r, e);
            errorcount++;
        }
        else
        {
            printf (PASSED " %i %#x %#x\n", i, r, e);
        }
    }    
    return errorcount;
}
