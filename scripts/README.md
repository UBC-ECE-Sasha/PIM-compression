# Scripts
The following set of scripts are used to parse program output and produce graphs out of the results reported:

* **parse\_output\_file.py** Contains helper functions that open the program output files, read their contents, and parse out the host runtime or the maximum DPU cycles reported.
* **host\_speedup.py** Creates a horizontal bar graph of speedup (or slowdown) of the DPU application over the host application for a list of test files.
<img src="https://user-images.githubusercontent.com/25714353/83307868-7066dd80-a1ba-11ea-9adf-bd45f837cfcb.png" alt="host_speedup_graph" width="400"/>

* **dpu\_tasklet\_tradeoff.py** Creates a grouped bar graph of the cycle count reported for different test case combinations of number of DPUs and number of tasklets. The bars are grouped by number of DPUs, and show the differences as different number of tasklets are used on each DPU.
<img src="https://user-images.githubusercontent.com/25714353/83307875-73fa6480-a1ba-11ea-8f39-397608bf5940.png" alt="dpu_tasklet_tradeoff_graph" width="400"/>

* **compr\_ratio\_tradeoff.py** Creates a bar graph of cycle count vs. number of tasklets, with an overlayed line graph showing the compression ratio at each number of tasklets.

* **compr\_ratio\_comparison.py** Creates a grouped bar graph of compression ratio, comparing the host and dpu implementations of lz4 and snappy

* **runtime\_comp.py** Creates a grouped bar graph of compression or decompression speeds, comparing the host and dpu implementations of lz4 and snappy.

## Usage
To use any of these scripts, the program output from running the decompressor must be saved to a specific directory. This can be done by running **generate_output.sh** with the relevant parameters and configuration. The scripts expect the output files to be named a certain way. Additional text may be added to the file name (eg. in the case of multiple trials), but at a minimum the files must be named:
* **Host Output:** \<testfile\>\_\<host\>.txt
* **DPU Output:** \<testfile\>\_dpus=<# DPUs\>\_tasklets=<# Tasklets\>.txt

### host\_speedup.py
This script takes in one argument: the directory holding the program output files.

`python host_speedup.py [PATH]`

To configure the test files that the script uses, or the exact test case for each file (# DPUs and # tasklets) the `files` dictionary must be updated in the script.

### dpu\_tasklets\_tradeoff.py
This script takes in two arguments: the directory holding the program output files and the name of the test file to create the graph for. The test file name should not include any file ending (eg. `mozilla` or `terror2`).

`python dpu_tasklet_tradeoff.py [PATH] [FILENAME]`

To configure the test cases that should be used by the script, the `num_dpus` and `num_tasks` lists must be updated. The script then expects that a program output file exists for every combination of num\_dpus and num\_tasks.

### compr\_ratio\_tradeoff.py
This script takes in two arguments: the directory holding the program output files and the name of the test file to create the graph for. The test file name should not include any file ending (eg. `mozilla` or `terror2`).

`python dpu_tasklet_tradeoff.py [PATH] [FILENAME]`

To configure the test cases that should be used by the script, `num_dpus` and `num_tasks` must be updated. The script then expects that a program output file exists for every combination of num\_dpus and num\_tasks.


### compr\_ratio\_comparison.py and runtime\_comp.py
These scripts take two arguments: a directory holding the program output files from snappy, and another for lz4. Directories holding decompression output should be used with **compr\_ratio\_comparison.py** to see compression ratios, and with **runtime\_comp.py** for decompression performance. Similarly, directories holding compression output should be used with **runtime\_comp.py** for compression performance.

`python3 compr\_ratio\_comparison.py [LZ4 DECOMP PATH] [SNAPPY DECOMP PATH]`
`python3 runtime\_comp.py [LZ4 COMP/DECOMP PATH] [SNAPPY COMP/DECOMP PATH]`

These scripts are currently configured to work with `num_dpus=1` and `num_tasks=1`.
