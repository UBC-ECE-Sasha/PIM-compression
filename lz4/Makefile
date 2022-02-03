CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -I../PIM-common/common/include
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


TEST_LZ4 = $(wildcard ../test/*.lz4)
TEST_HOST_VERIFIED = $(patsubst ../test/%.lz4,test/%.host_verified,$(TEST_LZ4))
TEST_DPU_VERIFIED = $(patsubst ../test/%.lz4,test/%.dpu_verified,$(TEST_LZ4))

.PHONY: test test_dpu test_host
test: test_host test_dpu
test_dpu: test/ $(TEST_DPU_VERIFIED)
test_host: test/ $(TEST_HOST_VERIFIED)

test/:
	mkdir -p test/

# alternative diff command:
# bash -c "diff <(xxd test/$*.host_output) <(xxd ../test/$*.txt)"
test/%.host_verified: ../test/%.lz4 ../test/%.txt all
	./dpu_lz4 -i $< -o test/$*.host_uncompressed 2>&1 | tee test/$*.host_output
	cmp test/$*.host_uncompressed ../test/$*.txt

test/%.dpu_verified: ../test/%.lz4 ../test/%.txt all
	./dpu_lz4 -d -i $< -o test/$*.dpu_uncompressed 2>&1 | tee test/$*.dpu_output
	cmp test/$*.dpu_uncompressed ../test/$*.txt
