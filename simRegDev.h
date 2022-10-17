int simRegDevConfigure(
    const char* name,
    size_t size,
    int swapEndianFlag,
    int async,
    int blockDevice);

int simRegDevSetStatus(
    const char* name,
    int connected);

int simRegDevSetData(
    const char* name,
    size_t offset,
    int value);

int simRegDevGetData(
    const char* name,
    size_t offset,
    int *value);

