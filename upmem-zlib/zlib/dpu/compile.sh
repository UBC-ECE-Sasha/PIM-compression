#!/bin/bash
   
# compile host_simple_example
gcc host_simple_example.c -o host_driver -w `dpu-pkg-config --cflags --libs dpu` -I../lib/ -L../lib/ -lz

