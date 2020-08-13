#!/usr/bin/env python3
import re
import pandas as pd
import numpy as np
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
	fig, ax = plt.subplots(figsize=(6.8, 3))
	ax2 = ax.twiny()

	# pick out host results, which we assume is always just one number
	host_results = results[results['version'] == 'host']
	if len(host_results) == 0:
		# if no host results present, just assume times are relative to host
		host_results = pd.DataFrame.from_dict({'version': ['host'], 'time': [1]})

	# remove host results
	results = results[results['version'] != 'host']

	# print(host_results)
	# print(results)
	ax.axhline(host_results['time'][0], label="Host", linestyle="--")

	# for more: https://matplotlib.org/3.2.2/api/markers_api.html
	markers = ['o', 'v', 's', 'p', '*', 'D', 'x']

	# extract files and sizes to plot
	testfiles = set()
	sizes = set()
	versions = results['version'].unique()
	for version in versions:
		testfiles.add(re.findall('^[^0-9]*', version)[0])
		sizes.add(int(re.findall('[0-9]+', version)[0]))
	sizes = sorted(sizes)
	testfiles = sorted(testfiles)

	# find the maximum time for each size and #of dpus for the iteration
	data = dict()
	dpus = []
	get_dpu = True
	for testfile in testfiles:
		data[testfile] = []
		for i, size in enumerate(sizes):
			subset = results[results['version'] == f"{testfile}{size}MB"]
			
			# get iteration with the max time
			max_time = 0.0
			max_dpu = 0
			for idx, item in subset.iterrows():
				if get_dpu:
					if item['time'] > max_time:
						max_time = item['time']
						max_dpu = item['dpus']	
				else:
					if item['dpus'] == dpus[i]:
						max_time = item['time']	
			if get_dpu:
				dpus.append(max_dpu)
			data[testfile].append(max_time)
		get_dpu = False

	for idx, version in enumerate(data):
		# not a great idea to re-use markers, usually better to reduce your lines
		marker = markers[idx % len(markers)]
		ax.plot(sizes, data[version], label=version, marker=marker)

	# set up legend
	ncol = kwargs.get('ncol', 1)
	ax.legend(bbox_to_anchor=(1, 1.04), ncol=ncol, loc='upper left', fontsize=legend_fontsize)

	# configure ticks to be what is in the columns
	loc = plticker.FixedLocator(sizes)
	ax.xaxis.set_major_locator(loc)
	ax.yaxis.set_ticks(np.arange(0, max(results['time'] + 0.5), 0.5))

	ax.grid(True, linestyle=':')

	# set the #dpus axis
	ax2.set_xlim(ax.get_xlim())
	ax2.xaxis.set_major_locator(loc)
	ax2.set_xticklabels(dpus)
	ax2.set_xlabel("Number of DPUs")

	if 'xlab' in kwargs:
		ax.set_xlabel(kwargs['xlab'], fontsize=fontsize)
	if 'ylab' in kwargs:
		ax.set_ylabel(kwargs['ylab'], fontsize=fontsize)

	print(f"writing file to {filename}")
	plt.savefig(filename, bbox_inches='tight', dpi=500)


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
