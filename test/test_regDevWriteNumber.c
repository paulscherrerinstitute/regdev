int test_regDevWriteNumber()
{
    struct dbCommon record;
    regDevPrivate* priv;
    regDevice* device;
    int parsestatus;
    int i;

    simRegDevConfigure ("test",100,0);
    device = regDevFind("test");
    priv = regDevAllocPriv(&record);
    assert(priv);

    parsestatus = regDevIoParse2("record", "test/0 T=int8", record.dpvt);
    assert(parsestatus==0);
    printf ("low=%x hight=%x\n", priv->hwLow, priv->hwHigh);
    for (i=-300; i<=300; i++)
    {
        *(epicsInt16*)device->buffer = 0;
        regDevWriteNumber(&record, 0.0, i);
        printf ("%d %x %x\n", i, i & 0xFF , *(epicsInt16*)device->buffer);
    }    
    return 0;
}
