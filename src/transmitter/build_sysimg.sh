#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "${DIR}"
mkdir -p build

julia --project=. --color=yes -e 'using PackageCompiler; create_sysimage([:Opus, :LibSndFile, :Revise]; sysimage_path="build/sysimg.so")'
