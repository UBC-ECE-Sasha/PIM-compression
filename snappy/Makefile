CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

SOURCE = dpu_snappy.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) decompress_host
	$(MAKE) -C decompress $@

dpu:
	DEBUG=$(DEBUG) $(MAKE) -C decompress 

host: decompress_host
	
decompress_host: $(SOURCE)
	$(CC) $(CFLAGS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . /usr/share/upmem/include
