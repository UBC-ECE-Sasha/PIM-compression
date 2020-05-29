import os
import sys
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles, get_avg_host_runtime 


# Get the output file directory path
parser = argparse.ArgumentParser(description='Create graph of DPU speedup over host')
requiredArgs = parser.add_argument_group('required arguments')
requiredArgs.add_argument('PATH', help='directory holding output files to parse')

args = parser.parse_args()
path = args.PATH
if not os.path.isdir(path):
	raise argparse.ArgumentTypeError(f"{path} is not a valid path")

	   # 'test file' : ('num_dpus', 'num_tasklets')
files = {'terror2': ('1',   '4'), 
        'plrabn12': ('2',   '8'), 
		'world192': ('4',   '12'),
		'xml'     : ('15',  '12'), 
		'sao'     : ('21',  '12'),
		'dickens' : ('35',  '12'),
		'nci'     : ('64',  '18'), 
		'mozilla' : ('105', '16'), 
		'spamfile': ('172', '16')}

# Loop through directory for respective output files and parse them
cycles = []
host_time = []
for filename in files:
	params = files[filename]
 
	host_time.append(get_avg_host_runtime(path, filename))
	cycles.append(get_avg_max_cycles(path, filename, params[0], params[1]))

# Calculate the speedup
speedup = []
for i in range (0, len(files)):
	dpu_time = float(cycles[i]) / 267000000 # DPU clock speed i 267MHz

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

