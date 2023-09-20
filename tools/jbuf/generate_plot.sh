#!/bin/bash

if [ ! -f .env ]; then
    echo ".env is missing. Copy and edit env-template!"
    exit 1
fi

export `cat .env`

echo "target: $target"
echo "netif:  $netif"
echo "once:   $once"

if ! which jq; then
    echo "Install jq"
    exit 1
fi

trap "./jitter-off.sh; killall -q baresip" EXIT

function gen_datfile() {
    ph=$1
    filename=$2

    jqc='.traceEvents[] | select (.ph == "'"${ph}"'") | .args.line'
    cat jbuf.json | jq -r "${jqc}" > $filename
}

source ./jitter.sh
init_jitter $netif

i=1
for jmin in 0 10 20; do
    echo "########### jitter buffer $jmin ###############"

    sed -e "s/video_jitter_buffer_delay\s*[0-9]*\-.*/video_jitter_buffer_delay   $jmin-500/" -i video/config
    baresip -v -f video > /tmp/b.log 2>&1 &
    sleep 1
    echo "/dial $target" | nc -N localhost 5555

    sleep 3

    enable_jitter

    sleep 5

    disable_jitter

    sleep 25

    echo "/quit" | nc -N localhost 5555

    sleep 1

    gen_datfile "P" jbuf.dat
    gen_datfile "O" overrun.dat
    gen_datfile "U" underflow.dat
    gen_datfile "L" toolate.dat
    gen_datfile "D" duplicate.dat
    gen_datfile "S" oosequence.dat
    gen_datfile "T" lost.dat
#    cat jbuf.json | jq -r '.traceEvents[] | select (.ph == "P") | .args.line' > jbuf.dat

    ./jbuf.plot
    if [ ! -d plots ]; then
        mkdir plots
    fi
    cp jbuf.eps plots/jbuf_${jmin}.eps
    i=$(( i+1 ))

    if [ $once == "true" ]; then
        exit 0
    fi
done
