import os
import sys
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_max_cycles 


# Get the output file directory path
parser = argparse.ArgumentParser(description='Create graph of cycle count by DPU and tasklets')
requiredArgs = parser.add_argument_group('required arguments')
requiredArgs.add_argument('PATH', help='directory holding output files to parse')
requiredArgs.add_argument('FILENAME', help='test file name without file ending')

args = parser.parse_args()
path = args.PATH
testfile = args.FILENAME
if not os.path.isdir(path):
	raise argparse.ArgumentTypeError(f"{path} is not a valid path")

# Loop through directory for respective output files and parse them
num_dpus = ['16', '32', '64', '128']
num_tasks = ['4', '8', '12', '16', '20', '24']

idx = 0
cycles = []
for tasks in num_tasks:
	# 2D list of size num_tasks by num_dpus
	cycles.append([0] * len(num_dpus))

	dpu_idx = 0;
	for dpus in num_dpus:
		cycles[idx][dpu_idx] = get_avg_max_cycles(path, testfile, dpus, tasks) / 1000000
		dpu_idx += 1
	idx += 1	

# Print for easy debugging
print(cycles)

# Set up plot
colors = ['#c9b395', '#5c4832', '#2e3d18', '#4e6625', '#80be1b', '#cbd970'] # Need as many as num_tasks
plt.rc('font', size=12)
plt.rc('axes', titlesize=12)
plt.rc('axes', labelsize=12)
fig, ax = plt.subplots()

# Set up positions of each bar
ypos = []
width = 0.13
ypos.append(np.arange(len(num_dpus)))
for i in range (0, len(num_tasks) - 1):
	ypos.append([x + width for x in ypos[i]])

# Set up axes labels
plt.xticks([r + width for r in range(len(num_dpus))], num_dpus)
ax.set_xlabel('Number of DPUs')
ax.set_ylabel('Cycle Count (in Millions)')

for i in range (0, len(num_tasks)):
	ax.bar(ypos[i], cycles[i], color=colors[i], width=width, edgecolor='white')

ax.legend([x + ' Tasklets' for x in num_tasks])

plt.show()

