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
					if int(line_split[-4]) > max_cycles:
						max_cycles = int(line_split[-4])
						print(max_cycles)
				except ValueError:
					continue

	return max_cycles

def get_postproc_time(path: pathlib.Path):
	"""
	open a program output file and parse out
	the post-processing time of the program.
	
	:param path: file to parse
	"""
	with path.open() as f:
		# read lines
		lines = f.readlines()

		# parse out the runtime
		runtime = 0
		for line in lines:
			if "Post-processing time" in line:
				line_split = line.split(' ')
				runtime = float(line_split[-1])
				break

	return runtime

def get_preproc_time(path: pathlib.Path):
	"""
	open a program output file and parse out
	the pre-processing time of the program.
	
	:param path: file to parse
	"""
	with path.open() as f:
		# read lines
		lines = f.readlines()

		# parse out the runtime
		runtime = 0
		for line in lines:
			if "Pre-processing time" in line:
				line_split = line.split(' ')
				runtime = float(line_split[-1])
				break

	return runtime

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

###################### Public Functions ##########################
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
			total_time += get_preproc_time(filename)
			total_time += get_postproc_time(filename)
			num_files += 1

	if num_files > 0:
		return (total_time / num_files)
	else:
		return -1

def get_avg_prepostproc_time(path: pathlib.Path, testfile, num_dpus, num_tasks):
	"""
	Calculate the average pre- and post- processing time reported by output
	files in a given folder for a particular test case.

	:param path: Directory storing output files
	:param testfile: Name of test file with no file ending
	:param num_dpus: Number of dpus used for the desired test
	:param num_tasks: Number of tasks used for the desired test
	"""
	time = 0.0
	num_files = 0
	for filename in path.iterdir():
		dpus = re.search(rf"dpus={num_dpus}[^0-9]", str(filename))
		tasklets = re.search(rf"tasklets={num_tasks}[^0-9]", str(filename))
		
		if (testfile in str(filename)) and (dpus is not None) and (tasklets is not None):
			time += get_preproc_time(filename)
			time += get_postproc_time(filename)
			num_files += 1

	if num_files > 0:
		return (time / num_files)
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
		dpus = re.search(rf"dpus={num_dpus}[^0-9]", str(filename))
		tasklets = re.search(rf"tasklets={num_tasks}[^0-9]", str(filename))
		
		if (testfile in str(filename)) and (dpus is not None) and (tasklets is not None):
			total_cycles += get_max_cycles(filename)
			num_files += 1

	if num_files > 0:
		return (total_cycles / num_files)
	else:
		return -1

def get_compr_ratio(path: pathlib.Path, testfile, num_dpus, num_tasks):
	"""
	Get the compression ratio of the file for a particular test case.

	:param path: Directory storing output files
	:param testfile: Name of test file with no file ending
	:param num_dpus: Number of dpus used for the designed test
	:param num_tasks: Number of tasks used for the desired test
	"""
	for filename in path.iterdir():
		dpus = re.search(rf"dpus={num_dpus}[^0-9]", str(filename))
		tasklets = re.search(rf"tasklets={num_tasks}[^0-9]", str(filename))

		if (testfile in str(filename)) and (dpus is not None) and (tasklets is not None):
			print(filename)	
			with filename.open() as f:
				# Read lines
				lines = f.readlines()

				# Parse out the compression ratio
				for line in lines:
					if "Compression ratio" in line:
						line_split = line.split(' ')
						print(float(line_split[-1]))
						return float(line_split[-1])
	return -1
