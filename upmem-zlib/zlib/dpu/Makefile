RM 						= rm -rf

DPU_CC 					= dpu-upmem-dpurte-clang
DPU_CFLAGS 				= -w

CC 						= gcc
CFLAGS 					= -w `dpu-pkg-config --cflags --libs dpu`

DPU_COMMON_SOURCES		= ../adler32.c ../crc32.c common/zutil.c
DPU_COMMON_INCLUDES		= -I../ -Icommon -L../

COMMON_SOURCES			=
COMMON_INCLUDES 		= -I../lib/ -L../lib/ -lz 

#DPU_DECOMPRESS_SOURCES 	= dpu_decompress.c decompress/inflate.c 
DPU_DECOMPRESS_SOURCES 	= dpu_decompress.c $(DPU_COMMON_SOURCES) decompress/inflate.c ../inffast.c ../inftrees.c 
DPU_DECOMPRESS_INCLUDES = -Idecompress $(DPU_COMMON_INCLUDES)
DPU_DECOMPRESS_PROGRAM  = decompress.dpu 

HOST_SOURCES			= host_simple_example.c
HOST_PROGRAM			= host_driver

default: all

all: $(HOST_PROGRAM) $(DPU_DECOMPRESS_PROGRAM) 


$(HOST_PROGRAM): $(HOST_SOURCES)
	$(CC) $(HOST_SOURCES) -o $(HOST_PROGRAM) $(CFLAGS) $(COMMON_INCLUDES)

$(DPU_DECOMPRESS_PROGRAM): $(DPU_DECOMPRESS_SOURCES)
	$(DPU_CC) $(DPU_DECOMPRESS_SOURCES) -o $(DPU_DECOMPRESS_PROGRAM) $(DPU_DECOMPRESS_INCLUDES) $(DPU_CFLAGS)

clean:
	$(RM) $(HOST_PROGRAM) $(DPU_DECOMPRESS_PROGRAM) 
