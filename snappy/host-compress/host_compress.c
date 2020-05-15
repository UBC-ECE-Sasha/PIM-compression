#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "snappy.h"
#include "map.h"
#include "util.h"

void usage(void)
{
	fprintf(stderr, 
		"host_compress [-b block_size] [-s] file [outfile]\n"
		"-b block size to break down input file, default is 32K\n"
		"-s print to standard output\n"
		"Compress or uncompress file with snappy.\n"
		"When no output file is specified write to file.snp\n");
	exit(1);
}

int open_output(char *name, char *oname, char **ofn)
{
	int fd;
	char *file;

	if (oname) {
		file = oname;
	} else {
		int len = strlen(name);

		file = xmalloc(len + 6);
		snprintf(file, len + 6, "%s.snp", name);
	}

	*ofn = file;		
	fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) { 
		fprintf(stderr, "Cannot create %s: %s\n", file,
			strerror(errno));
		exit(1);
	}		
	return fd;
}

int main(int ac, char **av)
{
	int opt;
	int to_stdout = 0;
	int block_size = 32 * 1024; // Default is 32K

	while ((opt = getopt(ac, av, "b:s")) != -1) {
		switch (opt) { 
		case 'b':
			block_size = atoi(optarg);
			break;
		case 's':
			to_stdout = 1;
			break;
		default:
			break;
		}
	}

	char *map;
	size_t size;
	if (!av[optind])
		usage();

	map = mapfile(av[optind], O_RDONLY, &size);
	if (!map) { 
		fprintf(stderr, "Cannot open %s: %s\n", av[1], strerror(errno));
		exit(1);
	}
		
	int err;	
	size_t outlen = snappy_max_compressed_length(size);
	char *out = xmalloc(outlen);

	struct snappy_env env;
	snappy_init_env(&env);
	err = snappy_compress(&env, map, size, block_size, out, &outlen);
	snappy_free_env(&env);

	unmap_file(map, size);

	if (err) {
		fprintf(stderr, "Cannot process %s: %s\n", av[optind], 
			strerror(-err));
		exit(1);
	}

	char *file;
	int fd;
	if (to_stdout) {
		if(av[optind + 1])
			usage();
		fd = 1;
		file = "<stdout>";
	} else {
		if (av[optind + 1] && av[optind + 2])
			usage();
		fd = open_output(av[optind], av[optind + 1], &file);
	}

	err = 0;
	if (write(fd, out, outlen) != outlen) {
		fprintf(stderr, "Cannot write to %s: %s\n", 
			file,
			strerror(errno));
		err = 1;
	}
	if (!(to_stdout || av[optind + 1]))
		free(file);
	free(out);
	
	return err;
}
