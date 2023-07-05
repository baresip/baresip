#!/bin/bash

target=192.168.110.192
netif=eno1
#target=10.1.0.215
#netif=enp8s0
#once=true

function init_jitter () {
    sudo ip link add ifb1 type ifb || :
    sudo ip link set ifb1 up
    sudo tc qdisc add dev $netif handle ffff: ingress
    sudo tc filter add dev $netif parent ffff: u32 match u32 0 0 action mirred egress redirect dev ifb1
}


function enable_jitter() {
    echo "ENABLE JITTER ..."
    sudo tc qdisc add dev ifb1 root netem delay 100ms 50ms
}


function disable_jitter() {
    echo "DISABLE JITTER ..."
    sudo tc qdisc del dev ifb1 root
}


function cleanup_jitter() {
    echo "CLEANUP jitter"
    sudo tc filter delete dev $netif parent ffff:
    sudo tc qdisc delete dev $netif ingress
    sudo ip link set ifb1 down
    sudo ip link delete ifb1
}


if ! which jq; then
    echo "Install jq"
    exit 1
fi


trap "disable_jitter; cleanup_jitter; killall -q baresip; kill $tid_fm4" EXIT

init_jitter

i=1
for jbuf in 0 1 3 4; do
    for buf in 20 40 60; do
        echo "########### jbuf min $jbuf buffer $buf ###############"

        sed -e "s/audio_buffer\s*[0-9]*\-.*/audio_buffer   $buf-160/" -i mcconfig/config
        sed -e "s/multicast_jbuf_delay\s*[0-9]*\-.*/multicast_jbuf_delay   $jbuf-20/" -i mcconfig/config
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

        cat ajb.json | jq -r '.traceEvents[] | select (.ph == "P") | .args.line' > ajb.dat
        cat ajb.json | jq -r '.traceEvents[] | select (.ph == "U") | .args.line' > underrun.dat
        ./ajb.plot
        if [ ! -d mcplots ]; then
            mkdir mcplots
        fi
        cp ajb.eps mcplots/jbuf${i}_min${jbuf}_buf_${buf}.eps
        i=$(( i+1 ))

        if [ "$once" == "true" ]; then
            exit 0
        fi
    done
done
