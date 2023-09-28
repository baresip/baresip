#!/bin/bash

if [ ! -f .env ]; then
    echo ".env is missing. Copy and edit env-template!"
    exit 1
fi

export `cat .env`

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
for jbuf in 0 1 3 4; do
    for buf in 20 40 60; do
        echo "########### jbuf min $jbuf buffer $buf ###############"

        sed -e "s/audio_buffer\s*[0-9]*\-/audio_buffer   $buf-/" -i mcconfig/config
        sed -e "s/multicast_jbuf_delay\s*[0-9]*\-/multicast_jbuf_delay   $jbuf-/" -i mcconfig/config
        baresip -f mcconfig > /tmp/b.log 2>&1 &

        sleep 1.5

        ./stream_fm4.sh &
        pidfm4=$( pgrep -P $! )
        echo "STARTED STREAM with PID $pidfm4"

        sleep 8

        enable_jitter

        sleep 8
        disable_jitter

        sleep 16

        kill $pidfm4

        echo "/quit" | nc -N localhost 5555

        sleep 1

        ./generate_plot.sh \
            || { exit 1; }

        if [ ! -d mcplots ]; then
            mkdir mcplots
        fi

        tar="mcplots/jbuf${i}_min${jbuf}_buf_${buf}"
        if [ -f ajb.eps ]; then
            cp ajb.eps ${tar}.eps
        fi
        if [ -f ajb.png ]; then
            mv ajb.png ${tar}.png
        fi

        i=$(( i+1 ))

        if [ "$once" == "true" ]; then
            exit 0
        fi
    done
done
