CC           = dpu-upmem-dpurte-clang
CFLAGS       = -O2 -flto -g -fstack-size-section -Wall -Wextra -Werror -DSTACK_SIZE_DEFAULT=4096

SOURCES = dpu_task.c dpu_decompress.c
DECOMPRESS_DPU = decompress.dpu

.PHONY: default all clean

default: all

all: $(DECOMPRESS_DPU)

clean:
	$(RM) $(DECOMPRESS_DPU)

$(DECOMPRESS_DPU): $(SOURCES)
	$(CC) $(CFLAGS)  $^ -o $@

