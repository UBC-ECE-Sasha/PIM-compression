#ifndef _DPU_SNAPPY_H_
#define _DPU_SNAPPY_H_

#include "common.h"
#include "host_common.h"
#include <sys/time.h>

// Comment out to load data for each DPU individually
#define BULK_XFER

#define GET_ELEMENT_TYPE(_tag)  (_tag & BITMASK(2))
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

#define ALIGN_LONG(_p, _width) (((long)_p + (_width-1)) & (0-_width))

// Max length of the input and output files
#define MAX_FILE_LENGTH MEGABYTE(30)

// Return values
typedef enum {
	SNAPPY_OK = 0,				// Success code
	SNAPPY_INVALID_INPUT,		// Input file has an invalid format
	SNAPPY_BUFFER_TOO_SMALL		// Input or output file size is too large
} snappy_status;

// Snappy tag types
enum element_type
{
	EL_TYPE_LITERAL,
	EL_TYPE_COPY_1,
	EL_TYPE_COPY_2,
	EL_TYPE_COPY_4
};

/**
 * Calculate the difference between two timeval structs.
 */
double get_runtime(struct timeval *start, struct timeval *end);

#endif	/* _DPU_SNAPPY_H_ */

