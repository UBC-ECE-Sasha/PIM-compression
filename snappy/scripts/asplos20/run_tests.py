import os
import sys
import csv
import argparse
import pathlib
from math import ceil
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime, get_avg_overhead_time
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

                for i in [min_tasklet] + list(range(min_tasklet + 1, max_tasklet + 1, incr)):
                        os.system('make clean')
                        os.system(f'make NR_DPUS={num_dpu} NR_TASKLETS={i}')
                        print(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')
                        os.system(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')
                        print(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')
                        os.system(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={num_dpu}_tasklets={i}.txt')

        # Write compression results csv
        with open('results/compression_speedup_tasklet.csv', 'w', newline='') as csvfile:
                writer = csv.writer(csvfile, delimiter=',')
                writer.writerow(['version', 'time', 'tasklets'])
                writer.writerow(['host', '1', '0'])

                for testfile in files:
                        for i in [min_tasklet] + list(range(min_tasklet + 1, max_tasklet + 1, incr)):
                                host = get_avg_host_runtime(pathlib.Path("results/compression"), testfile)
                                dpu = float(get_avg_max_cycles(pathlib.Path("results/compression"), testfile, num_dpu, i)) / 266000000
                                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/compression"), testfile, num_dpu, i)

                                if dpu > 0:
                                    std_dpu = host / (dpu + ovr for ovr in dpu_overhead)
                                    writer.writerow([testfile, std_dpu, i])
                
        # Write decompression results csv
        with open('results/decompression_speedup_tasklet.csv', 'w', newline='') as csvfile:
                writer = csv.writer(csvfile, delimiter=',')
                writer.writerow(['version', 'time', 'tasklets'])
                writer.writerow(['host', '1', '0'])

                for testfile in files:
                        for i in [min_tasklet] + list(range(min_tasklet + 1, max_tasklet + 1, incr)):
                                host = get_avg_host_runtime(pathlib.Path("results/decompression"), testfile)
                                dpu = float(get_avg_max_cycles(pathlib.Path("results/decompression"), testfile, num_dpu, i)) / 266000000
                                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/compression"), testfile, num_dpu, i)

                                if dpu > 0:
                                    std_dpu = host / (dpu + ovr for ovr in dpu_overhead)
                                    writer.writerow([testfile, std_dpu, i])
        

def run_dpu_test(files, min_dpu, max_dpu, incr):
        for testfile in files:
                os.system('make clean')
                os.system('make')
                os.system(f'./dpu_snappy -i ../test/{testfile}.snappy > results/decompression/{testfile}_host.txt')
                os.system(f'./dpu_snappy -c -i ../test/{testfile}.txt > results/compression/{testfile}_host.txt')

                for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
                        tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

                        os.system('make clean')
                        os.system(f'make NR_DPUS={i} NR_TASKLETS={tasklets}')
                        print(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
                        os.system(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
                        print(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
                        os.system(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={i}_tasklets={tasklets}.txt')

        # Write compression results csv
        with open('results/compression_speedup_dpu.csv', 'w', newline='') as csvfile:
                writer = csv.writer(csvfile, delimiter=',')
                writer.writerow(['version', 'time', 'dpus'])
                writer.writerow(['host', '1', '0'])

                for testfile in files:
                    for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
                                tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

                                host = get_avg_host_runtime(pathlib.Path("results/compression"), testfile)
                                dpu = float(get_avg_max_cycles(pathlib.Path("results/compression"), testfile, i, tasklets)) / 266000000
                                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/compression"), testfile, i, tasklets)

                                if dpu > 0:
                                    std_dpu = host / (dpu + sum(dpu_overhead))
                                    writer.writerow([testfile, std_dpu, i])
       
        
        # Write decompression results csv
        with open('results/decompression_speedup_dpu.csv', 'w', newline='') as csvfile:
                writer = csv.writer(csvfile, delimiter=',')
                writer.writerow(['version', 'time', 'dpus'])
                writer.writerow(['host', '1', '0'])

                for testfile in files:
                    for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
                                tasklets = get_optimal_tasklets(f"../test/{testfile}.txt", 32768, i)

                                host = get_avg_host_runtime(pathlib.Path("results/decompression"), testfile)
                                dpu = float(get_avg_max_cycles(pathlib.Path("results/decompression"), testfile, i, tasklets)) / 266000000
                                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/compression"), testfile, i, tasklets)

                                if dpu > 0:
                                    std_dpu = host / (dpu + sum(dpu_overhead))
                                    writer.writerow([testfile, std_dpu, i])
       
def run_breakdown_test(testfile, min_dpu, max_dpu, incr, tasklets):
    for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
        os.system('make clean')
        os.system(f'make NR_DPUS={i} NR_TASKLETS={tasklets}')
        print(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
        os.system(f'./dpu_snappy -d -i ../test/{testfile}.snappy > results/decompression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
        print(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={i}_tasklets={tasklets}.txt')
        os.system(f'./dpu_snappy -d -c -i ../test/{testfile}.txt > results/compression/{testfile}_dpus={i}_tasklets={tasklets}.txt')

    with open(f'results/{testfile}_compression_breakdown.csv', 'w', newline='') as csvfile:
            writer = csv.writer(csvfile, delimiter=',')
            writer.writerow(['prepare', 'alloc', 'load', 'copy_in', 'run', 'copy_out', 'free', 'dpus'])

            for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
                dpu = float(get_avg_max_cycles(pathlib.Path("results/compression"), testfile, i, tasklets)) / 266000000
                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/compression"), testfile, i, tasklets)
                
                if dpu > 0:
                    writer.writerow([dpu_overhead[0], dpu_overhead[1], dpu_overhead[2], dpu_overhead[3],
                        dpu, dpu_overhead[4], dpu_overhead[5], i])
       
    with open(f'results/{testfile}_decompression_breakdown.csv', 'w', newline='') as csvfile:
            writer = csv.writer(csvfile, delimiter=',')
            writer.writerow(['prepare', 'alloc', 'load', 'copy_in', 'run', 'copy_out', 'free', 'dpus'])

            for i in [min_dpu] + list(range(min_dpu - 1 + incr, max_dpu + 1, incr)):
                dpu = float(get_avg_max_cycles(pathlib.Path("results/decompression"), testfile, i, tasklets)) / 266000000
                dpu_overhead = get_avg_overhead_time(pathlib.Path("results/decompression"), testfile, i, tasklets)

                if dpu > 0:
                    writer.writerow([dpu_overhead[0], dpu_overhead[1], dpu_overhead[2], dpu_overhead[3],
                        dpu, dpu_overhead[4], dpu_overhead[5], i])
     

if __name__ == "__main__":
        parser = argparse.ArgumentParser(description='Run a specific test')
        requiredArgs = parser.add_argument_group('required arguments')
		requiredArgs.add_argument('-t', '--test', required=True, help='Which test to run: 1 - vary #DPUs, 2 - vary #tasklets, 3 - breakdown of time spent for one testfile')
        requiredArgs.add_argument('-f', '--files', nargs='+', required=True, help='List of test files to run, without file endings')
        requiredArgs.add_argument('-r', '--range', nargs='+', required=True, help='Range of DPUs or tasklets to test: [MIN] [MAX]')
        requiredArgs.add_argument('-i', '--incr', required=True, help='Increment to test within the range')

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
		try:
			if int(arg.t) == 1:
                run_dpu_test(files, range_min, range_max, incr) 
			elif int(arg.t) == 2:
                run_tasklet_test(files, range_min, range_max, incr, args.d)
			elif int(arg.t) == 3:
                run_breakdown_test(files[0], range_min, range_max, incr, 12)
        except:
			parser.print_help()
