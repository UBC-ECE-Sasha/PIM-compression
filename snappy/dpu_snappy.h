#ifndef _DPU_SNAPPY_H_
#define _DPU_SNAPPY_H_

#include "common.h"
#include <sys/time.h>

#define BULK_XFER

#define GET_ELEMENT_TYPE(_tag)  (_tag & BITMASK(2))
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

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

// Buffer context struct for input and output buffers on host
typedef struct host_buffer_context
{
	const char *file_name;		// File name
	uint8_t *buffer;		// Entire buffer
	uint8_t *curr;			// Pointer to current location in buffer
	uint32_t length;		// Length of buffer
	unsigned long max;		// Maximum allowed lenght of buffer
} host_buffer_context;

struct program_runtime {
	double pre;
	double d_alloc;
	double load;	
	double copy_in;
	double run;
	double copy_out;
	double d_free;
};

/**
 * Calculate the difference between two timeval structs.
 */
double get_runtime(struct timeval *start, struct timeval *end);

#endif	/* _DPU_SNAPPY_H_ */

