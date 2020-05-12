CFLAGS ?= -Wall -g -O2 -DNDEBUG=1  -DSG=1 -fPIC
# Remove -DSG=1 if you don't need scather-gather support
# NDEBUG=1 is recommended for production

#CFLAGS += -m32
#LDFLAGS += -m32

# just needs the kerneldoc script. if you don't have a kernel source
# download it somewhere and point to it
KDOC := /usr/src/linux/scripts/kernel-doc

all: host_compress

snappy.o: snappy.c compat.h snappy-int.h

host_compress: host_compress.o snappy.o map.o util.o

CLEAN := host_compress.o snappy.o host_compress map.o util.o libsnappyc.so.1

clean: 
	rm -f ${CLEAN}

#LZO := ../comp/lzo.o
LZO := ../comp/minilzo-2.06/minilzo.o

OTHER := ${LZO} ../comp/zlib.o ../comp/lzf.o ../comp/quicklz.o \
	 ../comp/fastlz.o

# incompatible with 32bit
# broken due to namespace collision
#SNAPREF_BASE := ../../src/snappy-1.0.3
#SNAPREF_FL := -I ${SNAPREF_BASE} -D SNAPREF
#SNAPREF := ${SNAPREF_BASE}/snappy-c.o ${SNAPREF_BASE}/snappy.o \
#           ${SNAPREF_BASE}/snappy-sinksource.o \
#           ${SNAPREF_BASE}/snappy-stubs-internal.o \
LDFLAGS += -lstdc++

FTRACER := ../ftracer/ftracer.o

ftracer:
	make clean
	make CFLAGS='-Dstatic= -pg -mfentry -DSG=1 -g' LDFLAGS='-rdynamic ${FTRACER} -ldl' all

libsnappyc.so.1: snappy.o
	$(CC) $(LDFLAGS) -shared -o $@ $^
