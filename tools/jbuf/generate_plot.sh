#!/bin/bash

if ! which jq; then
    echo "Install jq"
    exit 1
fi

if ! which gnuplot; then
    echo "Install gnuplot"
    exit 1
fi

if [ ! -f re_trace.json ]; then
    echo "re_trace.json does not exist"
    exit 1
fi

function gen_datfile() {
    ph=$1
    filename=$2

    jqc='.traceEvents[] | select (.cat == "jbuf" and .ph == "'
    jqc="${jqc}${ph}"'") | .args.line'
    cat re_trace.json | jq -r "${jqc}" > $filename
}


gen_datfile "P" jbuf.dat
gen_datfile "O" overrun.dat
gen_datfile "U" waiting.dat
gen_datfile "L" toolate.dat
gen_datfile "D" duplicate.dat
gen_datfile "S" oosequence.dat
gen_datfile "T" lost.dat

./jbuf.plot
