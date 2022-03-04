#!/bin/bash

ALG="${1:-lz4}"

DESTDIR=$PWD/outfiles_${ALG}
DECOMPDIR=$PWD/outfiles_decomp_${ALG}

mkdir $DESTDIR
mkdir $DECOMPDIR

cd ../test_4k

SRCDIR=$PWD/

cd ../${ALG}

make clean
make

mkdir outfiles
mkdir decomp

for filepath in ${SRCDIR}*; do
    filename=$(basename ${filepath})
    ./dpu_${ALG} -d -c -i ../test_4k/${filename} -o outfiles/${filename}> ${DESTDIR}/${filename%.*}_dpus=1_tasklets=1.txt
    ./dpu_${ALG} -c -i ../test_4k/${filename} -o outfiles/${filename}> ${DESTDIR}/${filename%.*}_host.txt

    
    ./dpu_${ALG} -d -i outfiles/${filename} -o decomp/${filename}> ${DECOMPDIR}/${filename%.*}_dpus=1_tasklets=1.txt
    ./dpu_${ALG} -i outfiles/${filename} -o decomp/${filename}> ${DECOMPDIR}/${filename%.*}_host.txt
done

rm ../test_4k/output.txt