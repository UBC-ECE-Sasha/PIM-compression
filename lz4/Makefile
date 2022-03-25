CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -I../PIM-common/common/include -I ../common/include
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

# Default Parameters
NR_DPUS = 1
NR_TASKLETS = 1

SOURCE = dpu_lz4.c lz4_compress.c lz4_decompress.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) dpu_lz4
	$(MAKE) -C dpu-decompress $@
	$(MAKE) -C dpu-compress $@

dpu:
	DEBUG=$(DEBUG) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-decompress
	DEBUG=$(DEBUG) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-compress

host: dpu_lz4
	
dpu_lz4: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_DPUS=$(NR_DPUS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . /usr/share/upmem/include
