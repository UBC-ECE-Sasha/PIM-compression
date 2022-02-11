import os
import sys
import pathlib
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_avg_host_runtime, get_avg_max_cycles, get_avg_prepostproc_time

"""
Defines which files to parse for this graph, format is:
	   'test file' : ('# dpus', '# tasklets')
"""
files = {'alice': ('1', '1'),
        'terror2': ('1', '1'), 
        'plrabn12': ('1', '1'), 
		'world192': ('1', '1'),
		'xml'     : ('1', '1'), 
		'sao'     : ('1', '1'),
		'dickens' : ('1', '1'),
		'nci'     : ('1', '1'), 
		'mozilla' : ('1', '1'), 
		'spamfile': ('1', '1')}


def setup_graph(lz4_path: pathlib.Path, snappy_path: pathlib.Path):
    """
    Parse the output files and create the graph

    :param path: Path holding output files
    """
    # Loop through directory for respective output files and parse them
    snappy_host_times = []
    lz4_host_times = []
    lz4_times = []
    snappy_times = []
    for filename in files:
        params = files[filename]

        lz4_host_time = get_avg_host_runtime(lz4_path, filename)
        snappy_host_time = get_avg_host_runtime(snappy_path, filename)
        snappy_dpu_cycles = get_avg_max_cycles(snappy_path, filename, params[0], params[1])
        lz4_dpu_cycles = get_avg_max_cycles(lz4_path, filename, params[0], params[1])


        if lz4_dpu_cycles == -1:
            print(f"ERROR: File not found for lz4 dpu: {filename}.", file=sys.stderr)
            return
        elif snappy_host_time == -1 or lz4_host_time == -1:
            print(f"ERROR: File not found for host: {filename}.", file=sys.stderr)
            return
        elif snappy_dpu_cycles == -1:
            print(f"ERROR: File not found for snappy dpu: {filename}", file=sys.stderr)
            return
        else:
            lz4_host_times.append(lz4_host_time)
            snappy_host_times.append(snappy_host_time)
            snappy_times.append(float(snappy_dpu_cycles) / 267000000 + get_avg_prepostproc_time(snappy_path, filename, params[0], params[1]))
            lz4_times.append(float(lz4_dpu_cycles) / 267000000 + get_avg_prepostproc_time(lz4_path, filename, params[0], params[1]))


    # Set up plot
    colors = ['#c9b395', '#5c4832', '#2e3d18', '#4e6626']
    plt.rc('font', size=12)
    plt.rc('axes', titlesize=12)
    plt.rc('axes', labelsize=12)
    fig, ax = plt.subplots()
    
    # Set up positions of each bar
    width = 0.13
    lz4_bar = np.arange(len(files.keys()))
    snappy_bar = [x + width for x in lz4_bar]
    lz4_host_bar = [x + 2 * width for x in lz4_bar]
    snappy_host_bar = [x + 3 * width for x in lz4_bar]


    ax.bar(lz4_bar, lz4_times, width, color=colors[0], edgecolor='white', label='LZ4 DPU')
    ax.bar(snappy_bar, snappy_times, width, color=colors[1], edgecolor='white', label='Snappy DPU')
    ax.bar(lz4_host_bar, lz4_host_times, width, color=colors[2], edgecolor='white', label='LZ4 Host')
    ax.bar(snappy_host_bar, snappy_host_times, width, color=colors[3], edgecolor='white', label='Snappy Host')


    # x-axis labels
    xticks = np.arange(len(files))
    ax.set_xticks(xticks)
    ax.set_xticklabels(files)

    # Set up axes labels
    ax.set_ylabel('Runtime (sec)')
    plt.title('Snappy vs LZ4 Runtime (1 DPU, 1 Tasklet, 1 Page)')
    ax.legend(['LZ4 DPU', 'Snappy DPU', 'LZ4 Host', 'Snappy Host'])

    plt.show()



if __name__ == "__main__":
    # Get the output file directory path
    parser = argparse.ArgumentParser(description='Create graph comparing lz4 and snappy compression ratios')
    requiredArgs = parser.add_argument_group('required arguments')
    requiredArgs.add_argument('PATH_LZ4', help='directory holding lz4 output files to parse')
    requiredArgs.add_argument('PATH_SNAPPY', help='directory holding snappy output files to parse')

    args = parser.parse_args()
    path_lz4 = pathlib.Path(args.PATH_LZ4)
    path_snappy = pathlib.Path(args.PATH_SNAPPY)
    if not path_lz4.is_dir() or not path_snappy.is_dir():
        raise argparse.ArgumentTypeError(f"One of the paths is invalid")

    setup_graph(path_lz4, path_snappy)

