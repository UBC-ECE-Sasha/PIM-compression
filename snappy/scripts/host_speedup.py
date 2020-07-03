import os
import sys
import pathlib
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime 

"""
Defines which files to parse for this graph, format is:
	   'test file' : ('# dpus', '# tasklets')
"""
files = {'terror2': ('1',   '4'), 
        'plrabn12': ('1',   '16'), 
		'world192': ('3',   '16'),
		'xml'     : ('11',  '16'), 
		'sao'     : ('14',  '16'),
		'dickens' : ('20',  '16'),
		'nci'     : ('64',  '16'), 
		'mozilla' : ('98', '16'), 
		'spamfile': ('172', '16')}


def setup_graph(path: pathlib.Path):
	"""
	Parse the output files and create the graph

	:param path: Path holding output files
	"""
	# Loop through directory for respective output files and parse them
	cycles = []
	host_time = []
	for filename in files:
		params = files[filename]

		ahr = get_avg_host_runtime(path, filename)
		amc = get_avg_max_cycles(path, filename, params[0], params[1])

		if ahr is -1:
			print(f"ERROR: File not found fo host: {filename}.", file=sys.stderr)
			return
		elif amc is -1:
			print(f"ERROR: File not found for DPU: {filename} with {params[0]} dpus and {params[1]} tasklets.", file=sys.stderr)
			return
		else:
			host_time.append(ahr)
			cycles.append(amc)

	# Calculate the speedup
	speedup = []
	for i in range (0, len(files)):
		dpu_time = float(cycles[i]) / 267000000 # DPU clock speed is 267MHz

		if host_time[i] < dpu_time:
			speedup.append((host_time[i] / dpu_time - 1) * 100)
		else:
			speedup.append((host_time[i] / dpu_time) * 100)

	# Print for easy debugging
	print(files)
	print(speedup)

	# Set up plot
	plt.rc('font', size=12)
	plt.rc('axes', titlesize=12)
	plt.rc('axes', labelsize=12)
	fig, ax = plt.subplots()

	# y-axis labels
	yticks = np.arange(len(files))
	ax.set_yticks(yticks)
	ax.set_yticklabels(files)

	# x-axis labels
	xticks = np.arange(-100, 800, step=50)
	ax.set_xticks(xticks)
	ax.set_xlabel('Speedup Over Host Application (%)')
	ax.xaxis.grid(True, linestyle="dotted")

	ax.barh(yticks, speedup, color=list(map(lambda x: '#d35e60' if (x < 0) else '#84ba5b', speedup)))

	plt.show()



if __name__ == "__main__":
	# Get the output file directory path
	parser = argparse.ArgumentParser(description='Create graph of DPU speedup over host')
	requiredArgs = parser.add_argument_group('required arguments')
	requiredArgs.add_argument('PATH', help='directory holding output files to parse')

	args = parser.parse_args()
	path = pathlib.Path(args.PATH)
	if not path.is_dir():
		raise argparse.ArgumentTypeError(f"{path} is not a valid path")

	setup_graph(path)
