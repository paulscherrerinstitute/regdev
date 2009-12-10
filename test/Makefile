.PHONY: test clean
test: test_regDev
	test_regDev

test_regDev: *.c ../*.c ../*.h
	gcc -o $@ test_regDev.c -Wall -Werror \
        -I .. -I/usr/local/epics/base/include -I/usr/local/epics/base/include/os/Linux \
        -L/usr/local/epics/base/lib/$(EPICS_HOST_ARCH) \
        -Wl,-rpath,/usr/local/epics/base/lib/$(EPICS_HOST_ARCH) \
        -ldbStaticIoc -liocsh -lca -lCom 

clean:
	rm -f test_regDev

