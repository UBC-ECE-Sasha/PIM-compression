import argparse
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
from typing import List, Dict, Tuple

matplotlib.rc('hatch', linewidth=0.5)
hatches = [" ", "/////", "\\\\\\\\\\", "xxxxx", "||||||", "+++++", "//", "*", "+", "O", "o"]
colours = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f', '#bcbd22', '#17becf']

def generate_plots(dataset: Dict[str, List[float]]) -> Tuple[List[int], Dict[str, Dict[str, List[float]]]]:
	"""
	Generate the different plots for each line or bar graph
	:param dataset:			Input data
	:return:				DPU separated data ready for plotting
	"""
	times_by_dpu: Dict[int, Dict[str, float]] = {}
	for (pp, a, l, ci, de, co, f, nd) in zip(
		dataset["prepare"],
		dataset["alloc"],
		dataset["load"],
		dataset["copy in"],
		dataset["DPU execution"],
		dataset["copy out"],
		dataset["free"],
		dataset["nr dpus"],
	):
		dpu_key = int(nd)
		if dpu_key not in times_by_dpu:
			times_by_dpu[dpu_key] = {}

		times_by_dpu[dpu_key] = {
			"DPU total": pp + a + l + ci + de + co + f,
			"Setup": pp + a + l,
			"Copy In": ci,
			"Run": de,
			"Copy Out": co
		}

	plots: Dict[str, List[float]] = {}
	for k in times_by_dpu:
		for key in times_by_dpu[k].keys():
			if key not in plots:
				plots[key] = []
			plots[key].append(times_by_dpu[k][key])

	nr_dpu_uniq = set()
	for nd in dataset["nr dpus"]:
		nr_dpu_uniq.add(int(nd))

	list_dpus = list(nr_dpu_uniq)
	list_dpus.sort()

	return list_dpus, plots


def parse_args() -> argparse.Namespace:
	"""
	Parse commandline arguments
	:return: arguments
	"""
	parser = argparse.ArgumentParser(description="Generate a runtime chart for PIM-HDC")
	parser.add_argument(
		"--csvfile", help="Input CSV data file", required=True, type=str
	)
	parser.add_argument("--xlabel", help="Chart x-axis label", default="DPUs", type=str)
	parser.add_argument(
		"--ylabel", help="Chart y-axis label", default="Time (s)", type=str
	)
	parser.add_argument(
		"--title", help="Chart title", default="", type=str
	)
	parser.add_argument(
		"--outputfile", help="Output chart", default="output.png", type=str
	)

	args = parser.parse_args()

	return args


def calculate_bottom(bottom: List[float], added_bar: List[float]) -> List[float]:
	"""
	Calculate the bottom for a bar graph given the existing bottom
	:param bottom:	   Existing bottom
	:param added_bar:  Bar to add to bottom
	:return:		   New bottom
	"""
	return [sum(x) for x in zip(bottom, added_bar)]


def main():
	config = parse_args()

	(
		prepare,
		alloc,
		load,
		copy_in,
		launch,
		copy_out,
		free,
		nr_dpus,
	) = np.loadtxt(config.csvfile, delimiter=",", unpack=True, skiprows=1)

	list_dpus, plots = generate_plots(
		{
			"prepare": prepare,
			"alloc": alloc,
			"load": load,
			"copy in": copy_in,
			"DPU execution": launch,
			"copy out": copy_out,
			"free": free,
			"nr dpus": nr_dpus,
		}
	)

	fig, ax = plt.subplots(figsize=(6.8, 2.5))

	for p in plots:
		ymin, ymax = min(plots["DPU total"]), max(plots["DPU total"])
		ymin_ind = plots["DPU total"].index(ymin)
		ymax_ind = plots["DPU total"].index(ymax)
		xmin, xmax = list_dpus[ymin_ind], list_dpus[ymax_ind]

		bottom = [0.0 for _ in plots["Copy Out"]]

		# compute spacing between DPU bars, first figure out the step
		# guess based on the first two, and assert the rest are the same
		# otherwise rendering will be off
		step = list_dpus[1] - list_dpus[0]
		for a, b in zip(list_dpus, list_dpus[1:]):
			check_step = b-a
			assert step == check_step, f"DPU increment isn't consistent, expected {step} based on first two samples, got: {check_step}"
		# add a small amount of spacing for each to get some white between each bar
		step = step - 2

		for idx, k in enumerate((
			"Setup",
			"Copy In",
			"Run",
			"Copy Out",
		)):
			ax.bar(
				list_dpus,
				plots[k],
				bottom=bottom,
				label=k,
				width=step,
				color=colours[idx % len(colours)],
				hatch=hatches[idx % len(hatches)]
			)
			bottom = calculate_bottom(bottom, plots[k])

	plt.title(config.title)

	ax.xaxis.set_major_locator(plt.MultipleLocator(64))

	# add grid 
	ax.set_axisbelow(True)
	ax.yaxis.grid(color='gray', linestyle='dashed')

	box = ax.get_position()
	ax.set_position([box.x0, box.y0, box.width * 0.8, box.height])

	# Put a legend to the right of the current axis
	handles, labels = ax.get_legend_handles_labels()
	handles = handles[::-1]
	labels = labels[::-1]
	ax.legend(handles, labels, loc="upper left", prop={'size': 9}, bbox_to_anchor=(1, 1.04))

	plt.xlabel(config.xlabel)
	plt.ylabel(config.ylabel)

	plt.savefig(config.outputfile, bbox_inches='tight', dpi=300)


if __name__ == "__main__":
	main()
