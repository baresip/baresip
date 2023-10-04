#!/bin/bash

if [ ! -f .env ]; then
    echo ".env is missing. Copy and edit env-template!"
    exit 1
fi

export `cat .env`

echo "netif:  $netif"

cd $(dirname $0)
source ./jitter.sh


init_jitter $netif

enable_jitter

