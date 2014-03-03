#include <string.h>
#include <stdlib.h>
#include <devLib.h>
#include "epicsTypes.h"
#include "regDevSup.h"
#include "test_regDev.h"

#define regDevBCD8T  (100)
#define regDevBCD16T (101)
#define regDevBCD32T (102)
#define regDev64T    (103)
#define regDevFirstType regDevBCD8T
#define regDevLastType  regDev64T

struct  { char* string;
        int result;    int offs; int init; int dtype; int dlen;    int l;      int h; int b; int p;} 
parameters[] = {
{ "dev1",
    S_dev_success,           0,      -1,  epicsInt16T,    2,     -0x7fff,     0x7fff,    0,     0  },
{ "dev1:0",
    S_dev_success,           0,      -1,  epicsInt16T,    2,     -0x7fff,     0x7fff,    0,     0  },
{ "dev1:51 T=INT8",
    S_dev_success,          51,      -1,  epicsInt8T,     1,       -0x7f,       0x7f,    0,     0  },
{ "dev1:50+1 T=UINT8",
    S_dev_success,          51,      -1,  epicsUInt8T,    1,           0,       0xff,    0,     0  },
{ "dev1:60-9 T=UNSIGN8",
    S_dev_success,          51,      -1,  epicsUInt8T,    1,           0,       0xff,    0,     0  },
{ "dev1:1+6*10-20+10 T=UNSIGNED8",
    S_dev_success,          51,      -1,  epicsUInt8T,    1,           0,       0xff,    0,     0  },
{ "dev1:1+5*(2*(2+3)) T=UINT8",
    S_dev_success,          51,      -1,  epicsUInt8T,    1,           0,       0xff,    0,     0  },
{ "dev1:5*(2*(2+3))+1/2 T=Byte",
    S_dev_success,          51,       2,  epicsUInt8T,    1,           0,       0xff,    0,     0  },
{ "dev1:51/1+1 T=char l=0 H=100",
    S_dev_success,          51,       2,  epicsUInt8T,    1,           0,        100,    0,     0  },
{ "dev1:51 T=INT16",
    S_dev_success,          51,      -1,  epicsInt16T,    2,     -0x7fff,     0x7fff,    0,     0  },
{ "dev1:51 T=short",
    S_dev_success,          51,      -1,  epicsInt16T,    2,     -0x7fff,     0x7fff,    0,     0  },
{ "dev1:51 T=UINT16",
    S_dev_success,          51,      -1,  epicsUInt16T,   2,           0,     0xffff,    0,     0  },
{ "dev1:51 T=UNSIGN16",
    S_dev_success,          51,      -1,  epicsUInt16T,   2,           0,     0xffff,    0,     0  },
{ "dev1:51 T=UNSIGNED16",
    S_dev_success,          51,      -1,  epicsUInt16T,   2,           0,     0xffff,    0,     0  },
{ "dev1:51 T=word",
    S_dev_success,          51,      -1,  epicsUInt16T,   2,           0,     0xffff,    0,     0  },
{ "dev1:51 T=INT32",
    S_dev_success,          51,      -1,  epicsInt32T,    4, -0x7fffffff, 0x7fffffff,    0,     0  },
{ "dev1:51 T=long",
    S_dev_success,          51,      -1,  epicsInt32T,    4, -0x7fffffff, 0x7fffffff,    0,     0  },
{ "dev1:51 T=UINT32",
    S_dev_success,          51,      -1,  epicsUInt32T,   4,           0, 0xffffffff,    0,     0  },
{ "dev1:51 T=UNSIGN32",
    S_dev_success,          51,      -1,  epicsUInt32T,   4,           0, 0xffffffff,    0,     0  },
{ "dev1:51 T=UNSIGNED32",
    S_dev_success,          51,      -1,  epicsUInt32T,   4,           0, 0xffffffff,    0,     0  },
{ "dev1:51 T=DWord",
    S_dev_success,          51,      -1,  epicsUInt32T,   4,           0, 0xffffffff,    0,     0  },
{ "dev1:0x30! T=float",
    S_dev_success,        0x30,    0x30,  epicsFloat32T,  4,           0,          0,    0,     0  },
{ "dev1:0x30: T=single",
    S_dev_success,        0x30,    0x30,  epicsFloat32T,  4,           0,          0,    0,     0  },
{ "dev1:0x30/ T=float32 P=1",
    S_dev_success,        0x30,    0x30,  epicsFloat32T,  4,           0,          0,    0,     1  },
{ "dev1:0x30:0x40 T=real32 P=2",
    S_dev_success,        0x30,    0x40,  epicsFloat32T,  4,           0,          0,    0,     2  },
{ "dev1/0x30/ T=double",
    S_dev_success,        0x30,    0x30,  epicsFloat64T,  8,           0,          0,    0,     0  },
{ "dev1/0x30/ T=float64 P=1",
    S_dev_success,        0x30,    0x30,  epicsFloat64T,  8,           0,          0,    0,     1  },
{ "dev1/0x30!0x40 T=real64 P=2",
    S_dev_success,        0x30,    0x40,  epicsFloat64T,  8,           0,          0,    0,     2  },
{ "dev1/1+20-4-0x3 T=STRING",
    S_dev_success,          14,      -1,  epicsStringT,   0,           0,          0,    0,     0  },
{ "dev1 T=string L=13",
    S_dev_success,           0,      -1,  epicsStringT,  13,           0,          0,    0,     0  },
{ "dev1 T=BCD",
    S_dev_success,           0,      -1,  regDevBCD8T,    1,           0,         99,    0,     0  },
{ "dev1 T=BCD8",
    S_dev_success,           0,      -1,  regDevBCD8T,    1,           0,         99,    0,     0  },
{ "dev1 T=BCD16",
    S_dev_success,           0,      -1,  regDevBCD16T,   2,           0,       9999,    0,     0  },
{ "dev1 T=BCD32",
    S_dev_success,           0,      -1,  regDevBCD32T,   4,           0,   99999999,    0,     0  },

{ "dev2/42",
    S_dev_noDevice,          0,       0,  0,               0,          0,          0,    0,     0  },
{ "dev1/42 T=int8 h=1000",
    S_dev_badArgument,       0,       0,  0,               0,          0,          0,    0,     0  },
{ "dev1/-5",
    S_dev_badArgument,      -5,      -1,  epicsInt16T,   2,      -0x7fff,     0x7fff,    0,     0  },
};    

static regDevSupport dummyRegDevSupport = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

int test_regDevIoParse()
{
    int i;
    int result;
    char errormessage[80];
    char expectedmessage[80];
    dbCommon record;
    struct link link;
    regDevPrivate* priv;
    
    memset(&record, 0, sizeof(record));
    strcpy(record.name ,"test");
    
    memset(&link,  0, sizeof(link));
    link.type = INST_IO;
    link.value.instio.string = malloc(80);
    
    
    regDevRegisterDevice("dev1", &dummyRegDevSupport, NULL, 0);
    
    for (i = 0; i < sizeof(parameters)/sizeof(parameters[0]); i++)
    {
        printf("parse \"%s\" ", parameters[i].string); fflush(stdout);
        strcpy(link.value.instio.string, parameters[i].string);
        free(record.dpvt);
        record.dpvt = NULL;
        if ((priv = regDevAllocPriv(&record)) == NULL)
        {
            printf (FAILED ".\n");
            errorcount++;
            continue;
        }
        result = regDevIoParse(&record, &link);
        if (result != parameters[i].result)
        {
            errSymLookup(result, errormessage, sizeof(errormessage));
            errSymLookup(parameters[i].result, expectedmessage, sizeof(expectedmessage));
            printf (FAILED ": \"%s\" instead of \"%s\"\n", errormessage, expectedmessage);
            errorcount++;
            continue;
        }
        if (result != 0) 
        {
            errSymLookup(result, errormessage, sizeof(errormessage));
            printf (PASSED " (\"%s\").\n", errormessage);
            continue;
        }
        if (priv->offset !=  parameters[i].offs)
        {
            printf (FAILED ": wrong offset %d instead of %d\n",
                priv->offset, parameters[i].offs);
            errorcount++;
            continue;
        }
        if (priv->initoffset !=  parameters[i].init)
        {
            printf (FAILED ": wrong init offset %d instead of %d\n",
                priv->initoffset, parameters[i].init);
            errorcount++;
            continue;
        }
        if (priv->dtype !=  parameters[i].dtype)
        {
            printf (FAILED ": wrong dtype %d instead of %d\n",
                priv->dtype, parameters[i].dtype);
            errorcount++;
            continue;
        }
        if (priv->dlen !=  parameters[i].dlen)
        {
            printf (FAILED ": wrong dlen %d instead of %d\n",
                priv->dlen, parameters[i].dlen);
            errorcount++;
            continue;
        }
        if (priv->hwLow !=  parameters[i].l)
        {
            printf (FAILED ": wrong low limit %#x (%d) instead of %#x (%d) \n",
                priv->hwLow, priv->hwLow, parameters[i].l, parameters[i].l);
            errorcount++;
            continue;
        }
        if (priv->hwHigh !=  parameters[i].h)
        {
            printf (FAILED ": wrong high limit %#x (%d) instead of %#x (%d) \n",
                priv->hwHigh, priv->hwHigh, parameters[i].h, parameters[i].h);
            errorcount++;
            continue;
        }
        printf (PASSED ".\n");
    }
    return errorcount;
}
