
// Buffer context struct for input and output buffers on host
typedef struct host_buffer_context
{
	const char *file_name;		// File name
	uint8_t *buffer;		// Entire buffer
	uint8_t *curr;			// Pointer to current location in buffer
	unsigned long length;		// Length of buffer
	unsigned long max;		// Maximum allowed length of buffer
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