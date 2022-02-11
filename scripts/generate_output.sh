#!/bin/bash

ALG="${1:-lz4}"

DESTDIR=$PWD/outfiles_${ALG}

mkdir $DESTDIR

cd ../test_4k

SRCDIR=$PWD/

cd ../${ALG}

make

for filepath in ${SRCDIR}*; do
    filename=$(basename ${filepath})
    ./dpu_${ALG} -d -c -i ../test_4k/${filename} > ${DESTDIR}/${filename%.*}_dpus=1_tasklets=1.txt
    ./dpu_${ALG} -c -i ../test_4k/${filename} > ${DESTDIR}/${filename%.*}_host.txt
done

rm ../test_4k/output.txt