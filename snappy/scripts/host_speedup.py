import os
import sys
import pathlib
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime, get_avg_prepostproc_time

"""
Defines which files to parse for this graph, format is:
	   'test file' : ('# dpus', '# tasklets')
"""
files = {'terror2': ('1', '1'), 
        'plrabn12': ('1', '1'), 
		'world192': ('1', '1'),
		'xml'     : ('1', '1'), 
		'sao'     : ('1', '1'),
		'dickens' : ('1', '1'),
		'nci'     : ('1', '1'), 
		'mozilla' : ('1', '1'), 
		'spamfile': ('1', '1')}


def setup_graph(path: pathlib.Path):
	"""
	Parse the output files and create the graph

	:param path: Path holding output files
	"""
	# Loop through directory for respective output files and parse them
	dpu_time = []
	host_time = []
	for filename in files:
		params = files[filename]

		ahr = get_avg_host_runtime(path, filename)
		adr = get_avg_max_cycles(path, filename, params[0], params[1])

		if ahr is -1:
			print(f"ERROR: File not found fo host: {filename}.", file=sys.stderr)
			return
		elif adr is -1:
			print(f"ERROR: File not found for DPU: {filename} with {params[0]} dpus and {params[1]} tasklets.", file=sys.stderr)
			return
		else:
			host_time.append(ahr)
			dpu_time.append(float(adr) / 267000000 + get_avg_prepostproc_time(path, filename, params[0], params[1]))

	# Calculate the speedup
	speedup = []
	for i in range (0, len(files)):
		if host_time[i] < dpu_time[i]:
			speedup.append((host_time[i] / dpu_time[i] - 1) * 100)
		else:
			speedup.append((host_time[i] / dpu_time[i]) * 100)

	# Print for easy debugging
	print(host_time)
	print(dpu_time)
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
