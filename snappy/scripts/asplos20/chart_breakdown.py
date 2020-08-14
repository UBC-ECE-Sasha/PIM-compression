import argparse
from typing import List, Dict, Tuple, Optional
import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.rc('hatch', linewidth=0.5)
hatches = [" ", "/////", "\\\\\\\\\\", "xxxxx", "||||||", "+++++", "//", "*", "+", "O", "o"]

def split_by_tasklet(
	includetasklet: Optional[List[int]],
	chart_type: str,
	dataset: Dict[str, List[float]],
) -> Dict[int, Dict[int, Dict[str, float]]]:
	"""
	Split up data by tasklet
	:param includetasklet:	 Tasklets to explicitly include (None for all)
	:param chart_type:		 Chart type 'line' or 'bar'
	:param dataset:			 Input data
	:return:				 Tasklet separated data ready for plotting
	"""
	times_by_dpu: Dict[int, Dict[int, Dict[str, float]]] = {}
	"""
	{
		"<dpu>": {
			"<tasklet>": {
				"total": float,
				"setup": float,
				"copy in": float,
				"DPU excecution": float,
				"copy out": float
				"free": float
			}
		}
	}
	"""
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
		tl_key = 12
		if chart_type == "line":
			if (includetasklet is not None) and (tl_key not in includetasklet):
				continue
		else:
			if tl_key not in includetasklet:
				continue

		dpu_key = int(nd)
		if dpu_key not in times_by_dpu:
			times_by_dpu[dpu_key] = {}

		times_by_dpu[dpu_key][12] = {
			"DPU total": pp + a + l + ci + de + co + f,
			"Setup": pp + a + l,
			"Copy In": ci,
			"Run": de,
			"Copy Out": co,
			"Free": f,
		}

	return times_by_dpu


def generate_plots(
	no_host: bool,
	includetasklet: Optional[List[int]],
	chart_type: str,
	dataset: Dict[str, List[float]],
) -> Tuple[List[int], Dict[str, Dict[str, List[float]]]]:
	"""
	Generate the different plots for each line or bar graph
	:param no_host:			Do not plot host
	:param includetasklet:	Tasklets to explicitly include
	:param chart_type:		Chart type 'bar' or 'line'
	:param dataset:			Input data
	:return:				DPU separated data ready for plotting
	"""

	nr_dpu_uniq = set()
	for nd in dataset["nr dpus"]:
		nr_dpu_uniq.add(int(nd))

	times_by_dpu: Dict[int, Dict[int, Dict[str, float]]] = split_by_tasklet(
		includetasklet, chart_type, dataset
	)

	plots: Dict[str, Dict[str, List[float]]] = {}

	"""
	{
		"<dpu>": {
			"<tasklet>": {
				"total": float,
				"setup": float,
				"copy in": float,
				"DPU excecution": float,
				"copy out": float
				"free": float
			}
		}
	}
	"""
	for k in times_by_dpu:
		for n in times_by_dpu[k]:
			label = f"{n} tasklets"
			if label not in plots:
				plots[label] = {dsk: [] for dsk in times_by_dpu[k][n].keys()}
			for key in times_by_dpu[k][n].keys():
				if key not in ("nr tasklets", "nr dpus"):
					plots[label][key].append(times_by_dpu[k][n][key])

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
		"--xstepsize", help="Chart x-axis stepsize", default=None, type=float
	)
	parser.add_argument(
		"--ystepsize", help="Chart y-axis stepsize", default=None, type=float
	)
	parser.add_argument(
		"--title", help="Chart title", default="", type=str
	)
	parser.add_argument(
		"--nohost", help="Do not include host", action="store_true", default=False
	)
	parser.add_argument(
		"--outputfile", help="Output chart", default="output.png", type=str
	)
	parser.add_argument(
		"--type", help="Chart type (bar or line)", default="line", type=str
	)
	parser.add_argument(
		"--includetasklet",
		action="append",
		help="Include tasklets",
		required=False,
		default=None,
		type=int,
	)

	args = parser.parse_args()

	if args.type == "bar":
		if args.includetasklet is None:
			parser.error('--type="bar" requires --includetasklet')
		if len(args.includetasklet) != 1:
			parser.error('--type="bar" only supports one --includetasklet')

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
		config.nohost,
		config.includetasklet,
		config.type,
		{
			"prepare": prepare,
			"alloc": alloc,
			"load": load,
			"copy in": copy_in,
			"DPU execution": launch,
			"copy out": copy_out,
			"free": free,
			"nr dpus": nr_dpus,
		},
	)

	fig, ax = plt.subplots(figsize=(6.8, 2.5))

	info_data = []

	for p in plots:
		ymin, ymax = min(plots[p]["DPU total"]), max(plots[p]["DPU total"])
		ymin_ind = plots[p]["DPU total"].index(ymin)
		ymax_ind = plots[p]["DPU total"].index(ymax)
		xmin, xmax = list_dpus[ymin_ind], list_dpus[ymax_ind]

		label_min = f"{xmin} / {ymin}"
		label_max = f"{xmax} / {ymax}"
		info_data.append((p, label_min, label_max))

		if config.type == "line":
			ax.plot(
				list_dpus,
				plots[p]["DPU total"],
				marker="o",
				label=p,
				markevery=[ymin_ind, ymax_ind],
			)
		else:
			bottom = [0.0 for _ in plots[p]["Free"]]

			# compute spacing between DPU bars, first figure out the step
			# guess based on the first two, and assert the rest are the same
			# otherwise rendering will be off
			step = list_dpus[1] - list_dpus[0]
			for a, b in zip(list_dpus, list_dpus[1:]):
				check_step = b-a
	#			assert step == check_step, f"DPU increment isn't consistent, expected {step} based on first two samples, got: {check_step}"
			# add a small amount of spacing for each to get some white between each bar
			step = step - 2

			for idx, k in enumerate((
				"Setup",
				"Copy In",
				"Run",
				"Copy Out",
				"Free",
			)):
				ax.bar(
					list_dpus,
					plots[p][k],
					bottom=bottom,
					label=k,
					width=step,
					hatch=hatches[idx % len(hatches)]
				)
				bottom = calculate_bottom(bottom, plots[p][k])

	plt.title(config.title)

	ax.xaxis.set_major_locator(plt.MultipleLocator(64))

	# add grid 
	ax.set_axisbelow(True)
	ax.yaxis.grid(color='gray', linestyle='dashed')

	header = ("Tasklets", "min dpu/time", "max dpu/time")
	print("| ", " | ".join(header), " |")
	print("| ", " | ".join(["--------" for _ in header]), "|")
	for e in info_data:
		print("| ", " | ".join(e), " |")

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
