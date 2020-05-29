#ifndef _DPU_SNAPPY_H_
#define _DPU_SNAPPY_H_

#include "common.h"

#define GET_ELEMENT_TYPE(_tag) (_tag & BITMASK(2))
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

// Max length of the input and output files
#define MAX_FILE_LENGTH 0x1000000

/*
 * Return values; see the documentation for each function to know
 * what each can return.
 */
typedef enum {
	SNAPPY_OK = 0,
	SNAPPY_INVALID_INPUT,
	SNAPPY_BUFFER_TOO_SMALL,
	SNAPPY_OUTPUT_ERROR
} snappy_status;


enum element_type
{
	EL_TYPE_LITERAL,
	EL_TYPE_COPY_1,
	EL_TYPE_COPY_2,
	EL_TYPE_COPY_4
};

#define OUT_BUFFER_FLAG_DIRTY	(1<<0)

typedef struct host_buffer_context
{
	uint8_t *buffer;
	uint8_t *curr;
	uint32_t length;
	unsigned long max;
} host_buffer_context;

#endif	/* _DPU_SNAPPY_H_ */

