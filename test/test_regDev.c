#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "regDev.h"
#include "regDev.c"
#include "regDevCopy.c"
#include "simRegDev.c"

static int errorcount = 0;

#define PASSED "\033[32mpassed\033[0m"
#define FAILED "\033[31;7;1mfailed\033[0m"

#include "test_regDevIoParse.c"
#include "test_regDevCopy.c"
#include "test_regDevWriteNumber.c"

#ifdef __vxworks
int test_regDev ()
#else
int main (int argc, char** argv)
#endif
{
    regDevDebug=10;
    test_regDevCopy();
    test_regDevIoParse();
    test_regDevWriteNumber();
    printf("%d error%s\n", errorcount, errorcount==1?"":"s");
    return 0;
}
