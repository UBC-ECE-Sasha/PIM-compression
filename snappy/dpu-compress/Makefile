CC           = dpu-upmem-dpurte-clang
CFLAGS       = -O2 -flto -g -Wall -I ../../PIM-common/common/include

STACK_SIZE_DEFAULT = 256
CFLAGS += -DNR_DPUS=$(NR_DPUS)
CFLAGS += -DNR_TASKLETS=$(NR_TASKLETS)
CFLAGS += -DSTACK_SIZE_DEFAULT=$(STACK_SIZE_DEFAULT)

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

SOURCES = dpu_task.c dpu_compress.c
COMPRESS_DPU = compress.dpu

.PHONY: default all clean

default: all

all: $(COMPRESS_DPU)

clean:
	$(RM) $(COMPRESS_DPU)

$(COMPRESS_DPU): $(SOURCES)
	$(CC) $(CFLAGS)  $^ -o $@

