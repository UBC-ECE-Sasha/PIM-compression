/**
 * This is the dpu entry point for a decompression routine
 */

#include <mram.h>
#include <stdio.h>
/*#include "zlib.h"*/
#include "zlib/zlib.h"
#include <alloc.h>

#define INPUT_SIZE 4096
#define OUTPUT_SIZE 4096
#define CHUNK 16384

// define host accessible variables
__host uint32_t input_size;
__host uint32_t result_size;
__host uint8_t input[INPUT_SIZE];
__host uint8_t output[OUTPUT_SIZE];
__host uint8_t ret;

uint32_t bytes_read = 0;
uint32_t bytes_written = 0;

uint32_t read_input(uint8_t dest[], uint32_t chunk_size) {
    // FIXME: Might need to use LDMA operations?
    uint32_t counter = 0;
    while(counter < chunk_size && bytes_read < input_size) {
        dest[counter++] = input[bytes_read++];
    }
    return counter;
}

uint32_t write_output(uint8_t src[], uint32_t have_size) {
    // FIXME: Might need to use LDMA operations?
    uint32_t counter = 0;
    while(counter < have_size && bytes_written < OUTPUT_SIZE) {
        output[bytes_written++] = src[counter++];
    }
    return counter;
}

int run_decompression(void)
{
   // TODO: Implement this
   uint32_t have;
   z_stream strm;

   // FIXME: It's really bad to allocate arrays like this, need to use DPU
   // memory allocation methods
   uint8_t in_buf[CHUNK];
   uint8_t out_buf[CHUNK];

   /* Allocate inflate state */
   strm.zalloc = Z_NULL;
   strm.zfree = Z_NULL;
   strm.opaque = Z_NULL;
   strm.avail_in = 0;
   strm.next_in = Z_NULL;
   ret = inflateInit(&strm);
   if (ret != Z_OK)
       return ret;

   /* decompress until deflate stream ends or end of input buffer */
   do {
       strm.avail_in = read_input(in_buf, CHUNK);
       if (strm.avail_in == 0)
           break;
       strm.next_in = in_buf;

       /* run inflate() on input until output buffer not full */
       do {
           strm.avail_out = CHUNK;
           strm.next_out = out_buf;
           ret = inflate(&strm, Z_NO_FLUSH);
           /*assert(ret != Z_STREAM_ERROR);   [> state not clobbered <]*/
           switch (ret) {
               case Z_NEED_DICT:
                   ret = Z_DATA_ERROR;      /* and fall through */
               case Z_DATA_ERROR:
               case Z_STREAM_ERROR:
               case Z_MEM_ERROR:
                    (void)inflateEnd(&strm);
                    return ret;
           }
           have = CHUNK - strm.avail_out;
           if (write_output(out_buf, have) != have) {
               (void)inflateEnd(&strm);
               return Z_ERRNO; 
           } 
       } while (strm.avail_out == 0);

       /* done when inflate() says it's done */
   } while (ret != Z_STREAM_END);

   /* clean up and return */
   (void)inflateEnd(&strm);
   return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int main() {
    ret = 0;
    // Start decompression
    printf("Hello DPU!"); 

    // Initialize the buddy allocator
    buddy_init(4096);

    ret = run_decompression;
    return 0;
    
}
