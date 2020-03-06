CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -Werror
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

SOURCE = dpu_snappy.c

.PHONY: default all dpu host clean

default: all

all: dpu host

clean:
	$(RM) decompress_host
	$(MAKE) -C decompress $@

dpu:
	$(MAKE) -C decompress

host: decompress_host
	
decompress_host: $(SOURCE)
	$(CC) $(CFLAGS) $^ -o $@ $(DPU_OPTS)

