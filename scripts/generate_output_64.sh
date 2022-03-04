#!/bin/bash

ALG="${1:-lz4}"

DESTDIR=$PWD/outfiles_${ALG}
DECOMPDIR=$PWD/outfiles_decomp_${ALG}

mkdir $DESTDIR
mkdir $DECOMPDIR

cd ../test_64_dpus

SRCDIR=$PWD/

cd ../${ALG}

make clean
make NR_DPUS=64

mkdir outfiles
mkdir decomp

for filepath in ${SRCDIR}*; do
    filename=$(basename ${filepath})
    ./dpu_${ALG} -d -c -b 4096 -i ../test_64_dpus/${filename} -o outfiles/${filename}> ${DESTDIR}/${filename%.*}_dpus=64_tasklets=1.txt
    ./dpu_${ALG} -d -b 4096 -i outfiles/${filename} -o decomp/${filename}> ${DECOMPDIR}/${filename%.*}_dpus=64_tasklets=1.txt
    
    if [[ "$ALG" == "snappy" ]]; then
        ./dpu_${ALG} -c -b 4096 -i ../test_64_dpus/${filename} -o outfiles/${filename}> ${DESTDIR}/${filename%.*}_host.txt
        ./dpu_${ALG} -b 4096 -i outfiles/${filename} -o decomp/${filename}> ${DECOMPDIR}/${filename%.*}_host.txt
    fi
done