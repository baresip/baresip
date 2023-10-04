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

trap "../jitter-off.sh; killall -q baresip" EXIT

source ../jitter.sh
init_jitter $netif

i=1
for ptime in 20 10 5 15 30 40; do

    sed -e "s/ptime=[0-9]*/ptime=$ptime/" -i accounts
    for buf in $(( ptime + ptime/2 )) $(( ptime )) $(( 2*ptime )) $(( 4*ptime )) $(( 6*ptime )); do
        echo "########### ptime $ptime buffer $buf ###############"

        sed -e "s/audio_buffer\s*[0-9]*\-.*/audio_buffer   $buf-250/" -i config
        baresip -v -f . > /tmp/b.log 2>&1 &
        sleep 1
        echo "/dial $target" | nc -N localhost 5555

        sleep 8

        enable_jitter

        sleep $(( ptime ))

        disable_jitter

        sleep $(( ptime ))

        echo "/quit" | nc -N localhost 5555

        sleep 1

        ./generate_plot.sh \
            || { exit 1; }

        if [ ! -d plots ]; then
            mkdir plots
        fi

        tar="plots/ptime${i}_${ptime}_buf_${buf}_jitter_0"
        if [ -f ajb.eps ]; then
            mv ajb.eps ${tar}.eps
        fi
        if [ -f ajb.png ]; then
            mv ajb.png ${tar}.png
        fi

        i=$(( i+1 ))

        if [ $once == "true" ]; then
            exit 0
        fi
    done
done
