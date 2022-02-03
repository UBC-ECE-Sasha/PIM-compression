import os
import sys
import pathlib
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles, get_compr_ratio 

# Defines which files to parse for this graph
dpus = '128'
num_tasks = ['1', '4', '8', '12', '16', '20', '24']

def setup_graph(path: pathlib.Path, testfile):
	"""
	Parse the output files and create the graph
	
	:param path: Path holding output files
	:param testfile: Test file that graph is being created for
	"""
	idx = 0
	cycles = []
	compr_ratio = []
	for tasks in num_tasks:
		amc =  get_avg_max_cycles(path, testfile, dpus, tasks)
		if amc is -1:
			print(f"ERROR: File not found: {testfile} with {dpus} dpus and {tasks} tasklets.",
				file=sys.stderr)
			return

		cr = get_compr_ratio(path, testfile, dpus, tasks)

		compr_ratio.append(cr)
		cycles.append(amc / 1000000)

	# Print for easy debugging
	print(cycles)
	print(compr_ratio)

	# Set up plot
	plt.rc('font', size=12)
	plt.rc('axes', titlesize=12)
	plt.rc('axes', labelsize=12)
	fig, ax1 = plt.subplots()
	
	# Set up axes labels and make duplicate axis
	xpos = np.arange(len(num_tasks))
	plt.xticks(xpos, labels=num_tasks)
	ax1.set_xlabel('Number of Tasklets')
	ax2 = ax1.twinx() 

	# Set up bar graph
	ax1.bar(xpos, cycles, color='#4e6625', width=0.5)
	ax1.set_ylabel('Cycle Count (in Millions)')

	# Set up line graph
	ax2.plot(xpos, compr_ratio, color='#2e3d18', linewidth=2)
	ax2.set_ylabel('Compression Ratio')

	plt.show()


if __name__ == "__main__":
	# Get the output file directory path
	parser = argparse.ArgumentParser(description='Create graph of cycle count and compression ratio by tasklets')
	requiredArgs = parser.add_argument_group('required arguments')
	requiredArgs.add_argument('PATH', help='directory holding output files to parse')
	requiredArgs.add_argument('FILENAME', help='test file name without file ending')

	args = parser.parse_args()
	path = pathlib.Path(args.PATH)
	testfile = args.FILENAME
	if not path.is_dir():
		raise argparse.ArgumentTypeError(f"{path} is not a valid path")

	setup_graph(path, testfile)
