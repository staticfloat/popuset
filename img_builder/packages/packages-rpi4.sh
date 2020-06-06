#!/bin/bash

# Start by installing administration tools
apt update
apt upgrade --all
apt install -y tmux wget curl git

# Download Julia, install into /usr/local:
curl -# -L "https://julialang-s3.julialang.org/bin/linux/armv7l/1.4/julia-1.4.1-linux-armv7l.tar.gz" -o - | tar -C /usr/local --exclude=LICENSE.md --strip-components=1 -zx
