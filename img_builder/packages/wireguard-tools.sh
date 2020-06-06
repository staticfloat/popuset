#!/bin/bash

version="1.0.20200513"
url="https://git.zx2c4.com/wireguard-tools/snapshot/wireguard-tools-${version}.tar.xz"

# Early-exit if wg-quick is already installed
if [[ -n $(which wg-quick) ]]; then
    exit 0
fi

mkdir -p /tmp/wireguard-tools
curl -L "${url}" -o - | tar -Jx --strip-components=1 -C /tmp/wireguard-tools
pushd /tmp/wireguard-tools/src
ls -la
make WITH_BASHCOMPLETION=yes WITH_SYSTEMDUNITS=yes WITH_WGQUICK=yes -j $(nproc)
make WITH_BASHCOMPLETION=yes WITH_SYSTEMDUNITS=yes WITH_WGQUICK=yes install
popd
rm -rf /tmp/wireguard-tools