CC           = dpu-upmem-dpurte-clang
CFLAGS       = -O2 -flto -g -Wall

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

SOURCES = dpu_task.c dpu_decompress.c
DECOMPRESS_DPU = decompress.dpu

.PHONY: default all clean

default: all

all: $(DECOMPRESS_DPU)

clean:
	$(RM) $(DECOMPRESS_DPU)

$(DECOMPRESS_DPU): $(SOURCES)
	$(CC) $(CFLAGS)  $^ -o $@

