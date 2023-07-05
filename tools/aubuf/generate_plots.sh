#!/bin/bash

if [ ! -f .env ]; then
    echo ".env is missing. Copy and edit env-template!"
    exit 1
fi

export `cat .env`

echo "target: $target"
echo "netif:  $netif"
echo "once:   $once"

function init_jitter () {
    sudo ip link add ifb1 type ifb || :
    sudo ip link set ifb1 up
    sudo tc qdisc add dev $netif handle ffff: ingress
    sudo tc filter add dev $netif parent ffff: u32 match u32 0 0 action mirred egress redirect dev ifb1
}


function enable_jitter() {
    echo "ENABLE JITTER ..."
    sudo tc qdisc add dev ifb1 root netem delay 0ms 150ms
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

trap "disable_jitter; cleanup_jitter; killall -q baresip" EXIT

init_jitter

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

        cat ajb.json | jq -r '.traceEvents[] | select (.ph == "P") | .args.line' > ajb.dat
        cat ajb.json | jq -r '.traceEvents[] | select (.ph == "U") | .args.line' > underrun.dat
        ./ajb.plot
        if [ ! -d plots ]; then
            mkdir plots
        fi
        cp ajb.eps plots/ptime${i}_${ptime}_buf_${buf}_jitter_0.eps
        i=$(( i+1 ))

        if [ $once == "true" ]; then
            exit 0
        fi
    done
done
