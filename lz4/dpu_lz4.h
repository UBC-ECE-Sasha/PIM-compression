#ifndef _DPU_LZ4_H_
#define _DPU_LZ4_H_

#include "common.h"
#include "host_common.h"
#include <sys/time.h>

// Comment out to load data for each DPU individually
#define BULK_XFER

#define ALIGN_LONG(_p, _width) (((long)_p + (_width-1)) & (0-_width))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))

// Max length of the input and output files
#define MAX_FILE_LENGTH MEGABYTE(30)
#define BLOCK_SIZE 4096

/* LZ4 constants */
#define MINMATCH 		 4
#define WILDCOPYLENGTH   8
#define MFLIMIT 		 12
#define LASTLITERALS 	 5
#define LZ4_DISTANCE_MAX 65535
#define MATCH_SAFEGUARD_DISTANCE  ((2*WILDCOPYLENGTH) - MINMATCH)   /* ensure it's possible to write 2 x wildcopyLength without overflowing output buffer */

/* Encoding Constants*/
#define ML_BITS 4
#define ML_MASK ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

// Return values
typedef enum {
	LZ4_OK = 0,				// Success code
	LZ4_INVALID_INPUT,		// Input file has an invalid format
	LZ4_BUFFER_TOO_SMALL		// Input or output file size is too large
} lz4_status;


/**
 * Calculate the difference between two timeval structs.
 */
double get_runtime(struct timeval *start, struct timeval *end);

#endif	/* _DPU_LZ4_H_ */

