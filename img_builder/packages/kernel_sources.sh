#!/bin/bash

cd /usr/src/linux-*
make -j4 default_defconfig
make -j4 scripts