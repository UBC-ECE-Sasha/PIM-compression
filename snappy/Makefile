CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -I../PIM-common/common/include
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

# Default Parameters
NR_DPUS = 1
NR_TASKLETS = 16

SOURCE = dpu_snappy.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) decompress
	$(MAKE) -C dpu-decompress $@

dpu:
	DEBUG=$(DEBUG) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-decompress

host: decompress
	
decompress: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_DPUS=$(NR_DPUS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . /usr/share/upmem/include
