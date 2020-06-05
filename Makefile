CC=gcc
CFLAGS += $(XSP_FLAGS) -D_GNU_SOURCE -g -O0
BASIC_FILES = get_clock.c slabs_buffer.c
BASIC_HEADERS = get_clock.h

LOADLIBS += -lrt -lm -lpthread
LDFLAGS +=
PROGS=xfer_test rdma_bw ib_bench forwarder

all: $(PROGS)
all: BASIC_FILES += xfer_rdma.c
all: LOADLIBS += -libverbs -lrdmacm
all: CFLAGS += -DHAVE_RDMA

nordma: xfer_test

${PROGS}: %: %.c ${BASIC_FILES} ${BASIC_HEADERS}
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< ${BASIC_FILES} $(LOADLIBS) $(LDLIBS) -o $@

clean:
	$(foreach fname,${PROGS}, rm -f ${fname})
.DELETE_ON_ERROR:
.PHONY: all clean
