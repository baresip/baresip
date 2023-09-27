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

strm="audio"
for jmin in 0 10 20; do
    for i in 0 1; do
        if [ "$i" == "0" ]; then
            strm="audio"
        else
            strm="video"
        fi

        echo "########### jitter buffer $strm $jmin ###############"

        sed -e "s/${strm}_jitter_buffer_delay\s*[0-9]*\-.*/${strm}_jitter_buffer_delay   ${jmin}-500/" -i ${strm}/config
        baresip -v -f ${strm} > /tmp/${strm}.log 2>&1 &
        sleep 1
        echo "/dial $target" | nc -N localhost 5555

        sleep 3

        enable_jitter

        sleep 5

        disable_jitter

        sleep 25

        echo "/quit" | nc -N localhost 5555

        sleep 1

        ./generate_plot.sh \
            || { exit 1; }

        if [ ! -d plots ]; then
            mkdir plots
        fi
        if [ -f jbuf.eps ]; then
            mv jbuf.eps plots/jbuf_${strm}_${jmin}.eps
        fi
        if [ -f jbuf.png ]; then
            mv jbuf.png plots/jbuf_${strm}_${jmin}.png
        fi
    done

    if [ $once == "true" ]; then
        exit 0
    fi
done
