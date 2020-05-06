#ifndef _DPU_SNAPPY_H_
#define _DPU_SNAPPY_H_

#define UNUSED(_x) (_x=_x)
#define BITMASK(_x) ((1 << _x) - 1)
#define MIN(_a, _b) (_a < _b ? _a : _b)

#define ALIGN(_p, _width) (((unsigned int)_p + (_width-1)) & (0-_width))
#define WINDOW_ALIGN(_p, _width) (((unsigned int)_p) & (0-_width))
#define GET_ELEMENT_TYPE(_tag) (_tag & BITMASK(2))
#define GET_LITERAL_LENGTH(_tag) (_tag >> 2)
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))

#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

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
    char *ptr;
    char *buffer;
    char *curr;
    uint32_t length;
    uint32_t max;
} host_buffer_context;

#endif  /* _DPU_SNAPPY_H_ */

