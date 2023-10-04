#!/bin/bash

if ! which jq; then
    echo "Install jq"
    exit 1
fi

if ! which gnuplot; then
    echo "Install gnuplot"
    exit 1
fi

if [ ! -f jbuf.json ]; then
    echo "jbuf.json does not exist"
    exit 1
fi

function gen_datfile() {
    ph=$1
    filename=$2

    jqc='.traceEvents[] | select (.ph == "'"${ph}"'") | .args.line'
    cat jbuf.json | jq -r "${jqc}" > $filename
}


gen_datfile "P" jbuf.dat
gen_datfile "O" overrun.dat
gen_datfile "U" underflow.dat
gen_datfile "L" toolate.dat
gen_datfile "D" duplicate.dat
gen_datfile "S" oosequence.dat
gen_datfile "T" lost.dat

./jbuf.plot
