#!/bin/bash
#
# A single call with local baresip config
# - edit .env
# - start baresip manually

if [ ! -f .env ]; then
    echo ".env is missing. Copy and edit env-template!"
    exit 1
fi

export `cat .env`

trap "disable_jitter; killall -q baresip" EXIT

source ../jitter.sh
init_jitter $netif

echo "/dial $target" | nc -N localhost 5555
sleep 1
enable_jitter
sleep 10
disable_jitter
sleep 15
echo "/quit" | nc -N localhost 5555

sleep 1.2

./generate_plot.sh
../aubuf/generate_plot.sh

