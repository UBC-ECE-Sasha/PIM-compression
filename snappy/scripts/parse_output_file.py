import os
import re
import sys
import pathlib

def get_max_cycles(path: pathlib.Path):
	"""
	Open a DPU program output file and parse out
	the maximum cycles that a tasklet took to run.
 
	:param path: File to parse
	"""
	with path.open() as f:
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


def get_host_runtime(path: pathlib.Path):
	"""
	Open a host program output file and parse out
	the runtime of the program.
	
	:param path: File to parse
	"""
	with path.open() as f:
		# Read lines
		lines = f.readlines()

		# Parse out the runtime
		runtime = 0
		for line in lines:
			if "Host time" in line:
				line_split = line.split(' ')
				runtime = float(line_split[-1])
				break

	return runtime


def get_avg_host_runtime(path: pathlib.Path, testfile):
	"""
	Calculate the average runtime reported in all host output
	files in a given folder for a particular test case.

	:param path: Directory storing output files
	:param testfile: Name of test file with no file ending
	"""
	total_time = 0.0
	num_files = 0
	for filename in path.iterdir():
		if (testfile in str(filename)) and ('host' in str(filename)):
			total_time += get_host_runtime(filename)
			num_files += 1

	if num_files > 0:
		return (total_time / num_files)
	else:
		return -1


def get_avg_max_cycles(path: pathlib.Path, testfile, num_dpus, num_tasks):
	"""
	Calculate the average max cycle count reported by output
	files in a given folder for a particular test case.

	:param path: Directory storing output files
	:param testfile: Name of test file with no file ending
	:param num_dpus: Number of dpus used for the desired test
	:param num_tasks: Number of tasks used for the desired test
	"""
	total_cycles = 0
	num_files = 0
	for filename in path.iterdir():
		dpus = re.search(rf"dpus={num_dpus}", str(filename))
		tasklets = re.search(rf"tasklets={num_tasks}", str(filename))
		
		if (testfile in str(filename)) and (dpus is not None) and (tasklets is not None):
			total_cycles += get_max_cycles(filename)
			num_files += 1

	if num_files > 0:
		return (total_cycles / num_files)
	else:
		return -1
