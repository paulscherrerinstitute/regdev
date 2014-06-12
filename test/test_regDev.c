#include <stdio.h>

#include "regDev.h"
#include "test_regDev.h"

int errorcount = 0;

#ifdef __vxworks
int test_regDev ()
#else
int main (int argc, char** argv)
#endif
{
    regDevDebug=0;
    printf ("test_regDevCopy\n");
    test_regDevCopy();

    printf ("test_regDevIoParse\n");
    test_regDevIoParse();

    printf ("test_regDevWriteNumber\n");
    test_regDevWriteNumber();

    printf("%d error%s\n", errorcount, errorcount==1?"":"s");
    return 0;
}
