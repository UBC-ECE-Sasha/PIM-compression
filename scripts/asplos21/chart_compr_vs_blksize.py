#!/usr/bin/env python3
from math import log2
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as plticker
import argparse

# defaults for nice publication ready rendering
fontsize = 12
legend_fontsize = 10


def read_csv(filename):
	df = pd.read_csv(filename)
	return df


def plot_results(results, filename, **kwargs):
	# 6.8 inch high figure, 2.5 inch across (matches column width)
	fig, ax = plt.subplots(figsize=(6.8, 2.5))
	ax2 = ax.twinx()
	
	ticks = []
	for x in results['blksize']:
		ticks.append(log2(x))
	
	ax.bar(ticks, results['time'], width=0.9, color='#627c98')
	ax2.plot(ticks, results['compratio'], marker='o', color='#08315a')

	# configure first axis
	ax.xaxis.set_ticks(ticks)
	ax.xaxis.set_ticklabels(results['blksize'])
	ax.set_ylim(0, 0.3)
	ax.yaxis.set_major_locator(plt.MultipleLocator(0.05))
	ax.yaxis.grid(linestyle='dashed')
	ax.set_axisbelow(True)
	
	# configure other axis
	ax2.set_ylim(0, 35)
	ax2.yaxis.set_major_locator(plt.MultipleLocator(5))
	ax2.set_ylabel("Compression Ratio (%)", fontsize=fontsize)

	if 'xlab' in kwargs:
		ax.set_xlabel(kwargs['xlab'], fontsize=fontsize)
	if 'ylab' in kwargs:
		ax.set_ylabel(kwargs['ylab'], fontsize=fontsize)

		print(f"writing file to {filename}")
	plt.savefig(filename, bbox_inches='tight', dpi=300)


parser = argparse.ArgumentParser()
parser.add_argument('--results_csv', '-r', help="results csv file", required=True)
parser.add_argument('--output',  '-o', help="output file name", required=True)
# optional args for labels, etc
parser.add_argument('--xlab')
parser.add_argument('--ylab')
# legend stuff
parser.add_argument('--ncol')
args = parser.parse_args()
print(args)

results = read_csv(args.results_csv)
# a rather circuitious way to avoid arguments in the args from being None
kwargs = dict(filter(lambda x: x[1] is not None, vars(args).items()))
plot_results(results, args.output, **kwargs)
