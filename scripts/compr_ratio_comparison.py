import os
import sys
import pathlib
import argparse
import numpy as np
import matplotlib.pyplot as plt
from parse_output_file import get_compr_ratio

"""
Defines which files to parse for this graph, format is:
	   'test file' : ('# dpus', '# tasklets')
"""
files = {'plrabn12': ('64', '1'), 
		'world192': ('64', '1'),
		'xml'     : ('64', '1'), 
		'sao'     : ('64', '1'),
		'dickens' : ('64', '1'),
		'nci'     : ('64', '1'), 
		'spamfile': ('64', '1')}


def setup_graph(lz4_path: pathlib.Path, snappy_path: pathlib.Path):
    """
    Parse the output files and create the graph

    :param path: Path holding output files
    """
    # Loop through directory for respective output files and parse them
    lz4_compr = []
    snappy_compr = []
    for filename in files:
        params = files[filename]

        lz4_cpr = get_compr_ratio(lz4_path, filename, params[0], params[1])
        snappy_cpr = get_compr_ratio(snappy_path, filename, params[0], params[1])

        if lz4_cpr == -1:
            print(f"ERROR: File not found for lz4: {filename}.", file=sys.stderr)
            return
        elif snappy_cpr == -1:
            print(f"ERROR: File not found for snappy: {filename}", file=sys.stderr)
            return
        else:
            lz4_compr.append(lz4_cpr)
            snappy_compr.append(snappy_cpr)


    # Set up plot
    colors = ['#c9b395', '#5c4832']
    plt.rc('font', size=12)
    plt.rc('axes', titlesize=12)
    plt.rc('axes', labelsize=12)
    fig, ax = plt.subplots()
    
    # Set up positions of each bar
    width = 0.13
    lz4_bar = np.arange(len(files.keys()))
    snappy_bar = [x + width for x in lz4_bar]

    ax.bar(lz4_bar, lz4_compr, width, color=colors[0], edgecolor='white', label='LZ4')
    ax.bar(snappy_bar, snappy_compr, width, color=colors[1], edgecolor='white', label='Snappy')

    # x-axis labels
    xticks = np.arange(len(files))
    ax.set_xticks(xticks)
    ax.set_xticklabels(files)


    # Set up axes labels
    ax.set_ylabel('Compression Ratio')
    plt.title('Snappy vs LZ4 Compression (1 DPU, 1 Tasklet, 1 Page)')

    ax.legend(['LZ4', 'Snappy'])

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

