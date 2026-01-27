#!/bin/sh

GIT_CRED_FILE="$HOME/.git-credentials"
if [ -f "$GIT_CRED_FILE" ]; then
	GIT_CRED_URL=$(cat ~/.git-credentials)
fi

if [ -z "GIT_CRED_URL" ]; then
	echo "Git credentials file $GIT_CRED_FILE missing or empty, aborting..."
	exit 1
fi

docker run -it -v $(pwd):/src \
	--env GIT_CRED_URL="$GIT_CRED_URL" \
	debian:bookworm-slim \
	bash
