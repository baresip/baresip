#!/bin/bash

function init_jitter () {
    netif=$1
    sudo ip link add ifb1 type ifb || :
    sudo ip link set ifb1 up
    sudo tc qdisc add dev $netif handle ffff: ingress
    sudo tc filter add dev $netif parent ffff: u32 match u32 0 0 action mirred egress redirect dev ifb1
}


function enable_jitter() {
    echo "ENABLE JITTER ..."
    sudo tc qdisc add dev ifb1 root netem delay 0ms 50ms
}


function disable_jitter() {
    echo "DISABLE JITTER ..."
    sudo tc qdisc del dev ifb1 root
}


function cleanup_jitter() {
    netif=$1
    echo "CLEANUP jitter"
    sudo tc filter delete dev $netif parent ffff:
    sudo tc qdisc delete dev $netif ingress
    sudo ip link set ifb1 down
    sudo ip link delete ifb1
}
