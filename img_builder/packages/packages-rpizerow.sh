#!/bin/bash

# Start by installing administration tools
apt update
apt upgrade --all
apt install -y tmux wget curl git build-essential

# Install Julia, eventually.  :)