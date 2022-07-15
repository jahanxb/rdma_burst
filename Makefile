CC=gcc
#DEBUG = -g -O0
CFLAGS += $(XSP_FLAGS) -D_GNU_SOURCE $(DEBUG)
BASIC_FILES = get_clock.c slabs_buffer.c
BASIC_HEADERS = get_clock.h

LOADLIBS += -lrt -lm -lpthread
LDFLAGS +=
PROGS=rxfer_test rdma_bw ib_bench forwarder

all: $(PROGS)
all: BASIC_FILES += rxfer_rdma.c
all: LOADLIBS += -libverbs -lrdmacm
all: CFLAGS += -DHAVE_RDMA

nordma: rxfer_test

${PROGS}: %: %.c ${BASIC_FILES} ${BASIC_HEADERS}
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< ${BASIC_FILES} $(LOADLIBS) $(LDLIBS) -o $@

clean:
	$(foreach fname,${PROGS}, rm -f ${fname})
.DELETE_ON_ERROR:
.PHONY: all clean
