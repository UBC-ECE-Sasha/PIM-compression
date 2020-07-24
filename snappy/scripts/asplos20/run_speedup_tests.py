import os
import sys
import csv
import argparse
import pathlib
from math import ceil
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime, get_avg_prepostproc_time
MAX_DPUS = 640
MAX_TASKLETS = 24


def get_optimal_tasklets(file_path, block_size, num_dpus):
	size = os.path.getsize(file_path)
	num_blocks = ceil(size / block_size)

	num_tasklets = ceil(num_blocks / num_dpus)
	if (num_tasklets > MAX_TASKLETS):
		num_tasklets = MAX_TASKLETS
	return num_tasklets

def run_tasklet_test(files, min_tasklet, max_tasklet, incr, num_dpu):
	for testfile in files:
		os.system('make clean')
		os.system('make')
		os.system(f'./dpu_snappy -i ../test/{testfile}.snappy > results/decompression/{testfile}_host.txt')
		os.system(f'./dpu_snappy -c -i ../test/{testfile}.txt > results/compression/{testfile}_host.txt')

		for i in range(min_tasklet, max_tasklet + 1, incr):
			os.system('make clean')
			os.system(f'make NR_DPUS={num_dpu} NR_TASKLETS={i}')
			os.system(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')
			os.system(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')

	# Write compression results csv
	with open('results/compression_speedup_tasklet.csv', 'w', newline='') as csvfile:
		writer = csv.writer(csvfile, delimiter=',')
		writer.writerow(['version', 'time', 'tasklets'])
		writer.writerow(['host', '1', '0'])

		for testfile in files:
			for i in range(min_tasklet, max_tasklet + 1, incr):
				host = get_avg_host_runtime(pathlib.Path("results/compression"), testfile)
				dpu = float(get_avg_max_cycles(pathlib.Path("results/compression"), testfile, num_dpu, i)) / 266000000
				dpu += get_avg_prepostproc_time(pathlib.Path("results/compression"), testfile, num_dpu, i)

				std_dpu = host / dpu
				writer.writerow([testfile, std_dpu, i])
		
	# Write decompression results csv
	with open('results/decompression_speedup_tasklet.csv', 'w', newline='') as csvfile:
		writer = csv.writer(csvfile, delimiter=',')
		writer.writerow(['version', 'time', 'tasklets'])
		writer.writerow(['host', '1', '0'])

		for testfile in files:
			for i in range(min_tasklet, max_tasklet + 1, incr):
				host = get_avg_host_runtime(pathlib.Path("results/decompression"), testfile)
				dpu = float(get_avg_max_cycles(pathlib.Path("results/decompression"), testfile, num_dpu, i)) / 266000000
				dpu += get_avg_prepostproc_time(pathlib.Path("results/decompression"), testfile, num_dpu, i)

				std_dpu = host / dpu
				writer.writerow([testfile, std_dpu, i])
	

def run_dpu_test(files, min_dpu, max_dpu, incr):
	for testfile in files:
		os.system('make clean')
		os.system('make')
		os.system(f'./dpu_snappy -i ../test/{testfile}.snappy > results/decompression/{testfile}_host.txt')
		os.system(f'./dpu_snappy -c -i ../test/{testfile}.txt > results/compression/{testfile}_host.txt')

		for i in range(min_dpu, max_dpu + 1, incr):
			tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

			os.system('make clean')
			os.system(f'make NR_DPUS={i} NR_TASKLETS={tasklets}')
			os.system(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
			os.system(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={i}_tasklets={tasklets}.txt')

	# Write compression results csv
	with open('results/compression_speedup_dpu.csv', 'w', newline='') as csvfile:
		writer = csv.writer(csvfile, delimiter=',')
		writer.writerow(['version', 'time', 'dpus'])
		writer.writerow(['host', '1', '0'])

		for testfile in files:
			for i in range(min_dpu, max_dpu + 1, incr):
				tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

				host = get_avg_host_runtime(pathlib.Path("results/compression"), testfile)
				dpu = float(get_avg_max_cycles(pathlib.Path("results/compression"), testfile, i, tasklets)) / 266000000
				dpu += get_avg_prepostproc_time(pathlib.Path("results/compression"), testfile, i, tasklets)

				std_dpu = host / dpu
				writer.writerow([testfile, std_dpu, i])
	
	# Write decompression results csv
	with open('results/decompression_speedup_dpu.csv', 'w', newline='') as csvfile:
		writer = csv.writer(csvfile, delimiter=',')
		writer.writerow(['version', 'time', 'dpus'])
		writer.writerow(['host', '1', '0'])

		for testfile in files:
			for i in range(min_dpu, max_dpu + 1, incr):
				tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

				host = get_avg_host_runtime(pathlib.Path("results/decompression"), testfile)
				dpu = float(get_avg_max_cycles(pathlib.Path("results/decompression"), testfile, i, tasklets)) / 266000000
				dpu += get_avg_prepostproc_time(pathlib.Path("results/decompression"), testfile, i, tasklets)

				std_dpu = host / dpu
				writer.writerow([testfile, std_dpu, i])

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description='Run tests measuring host speedup that vary either #DPUs or #tasklets')
	parser.add_argument('-d', help='Keep number of DPUs constant to this number')
	requiredArgs = parser.add_argument_group('required arguments')
	requiredArgs.add_argument('-f', '--files', nargs='+', required=True, help='List of test files to run, without file endings')
	requiredArgs.add_argument('-r', '--range', nargs='+', help='Range of DPUs or tasklets to test')
	requiredArgs.add_argument('-i', '--incr', help='Increment to test within the range')

	args = parser.parse_args()
	files = args.files
	range_min = int(args.range[0])
	range_max = int(args.range[1])
	incr = int(args.incr)

	# Set up the folders to hold output files
	script_dir = os.path.dirname(os.path.realpath(sys.argv[0]))
	os.chdir(script_dir + "/../../")
	os.makedirs("results/compression", exist_ok=True)
	os.makedirs("results/decompression", exist_ok=True)

	# Set up the test conditions
	if args.d is None:
		run_dpu_test(files, range_min, range_max, incr)	
	else:
		run_tasklet_test(files, range_min, range_max, incr, args.d)
	
