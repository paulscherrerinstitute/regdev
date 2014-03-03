.PHONY: test clean
test: test_regDev
	test_regDev

SRCS=$(wildcard *.c) regDev.c regDevCopy.c simRegDev.c
OBJS=$(SRCS:.c=.o)

test_regDev: $(OBJS)
	gcc -o $@ \
	-Wl,-rpath,/usr/local/epics/base/lib/$(EPICS_HOST_ARCH) \
        -L/usr/local/epics/base/lib/$(EPICS_HOST_ARCH) \
	-ldbStaticIoc -lca -lCom -ldbIoc \
        $(OBJS)

%.o:%.c
	gcc -g -c $< -Wall -Werror \
	-I. -I .. -I/usr/local/epics/base/include -I/usr/local/epics/base/include/os/Linux

vpath %.c ..

clean:
	rm -f test_regDev *.o core*

