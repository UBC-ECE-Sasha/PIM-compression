#!/bin/bash

DESTDIR=$PWD/outfiles

mkdir $DESTDIR

cd ../../test_4k

SRCDIR=$PWD/

cd ../lz4

for filepath in ${SRCDIR}*; do
    filename=$(basename ${filepath})
    ./dpu_lz4 -d -c -i ../test_4k/${filename} > ${DESTDIR}/${filename%.*}_dpus=1_tasklets=1.txt
    ./dpu_lz4 -c -i ../test_4k/${filename} > ${DESTDIR}/${filename%.*}_host.txt
done

rm ../test_4k/output.txt