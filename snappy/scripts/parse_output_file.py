import os
import sys

'''
Open a DPU program output file and parse out
the maximum cycles that a tasklet took to run. 
'''
def get_max_cycles(path):
	# Open the file
	f = open(path, "r")

	# Read lines
	lines = f.readlines()

	# Parse out the max cycles
	max_cycles = 0
	for line in lines:
		if "Tasklet" in line:
			line_split = line.split(' ')
			try:
				if int(line_split[-2]) > max_cycles:
					max_cycles = int(line_split[-2])
			except ValueError:
				continue

	return max_cycles


'''
Open a host program output file and parse out
the runtime of the program.
'''
def get_host_runtime(path):
	# Open the file
	f = open(path, "r")

	# Read lines
	lines = f.readlines()

	# Parse out the runtime
	runtime = 0
	for line in lines:
		if "Host completed" in line:
			line_split = line.split(' ')
			runtime = float(line_split[-2])
			break

	return runtime

'''
Calculate the average runtime reported in all host output
files in a given folder for a particular test case.

path: directory storing output files
testfile: name of test file with no file ending
'''
def get_avg_host_runtime(path, testfile):
	total_time = 0.0
	num_files = 0
	for filename in os.listdir(path):
		if (testfile in filename) and ('host' in filename):
			total_time += get_host_runtime(path + filename)
			num_files += 1

	return (total_time / num_files)

'''
Calculate the average max cycle count reported by output
files in a given folder for a particular test case.

path: directory storing output files
testfile: name of test file with no file ending
num_dpus: number of dpus used for the desired test
num_tasks: number of tasks used for the desired test
'''
def get_avg_max_cycles(path, testfile, num_dpus, num_tasks):
	total_cycles = 0
	num_files = 0
	for filename in os.listdir(path):
		if (testfile + '_' + num_dpus + 'D_' + num_tasks + 'T') in filename:
			total_cycles += get_max_cycles(path + filename)
			num_files += 1

	return (total_cycles / num_files)
