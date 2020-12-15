using BinaryBuilder

name = "popuset_shim"
version = v"0.0.1"

sources = [
    DirectorySource("bundled"),
]

script = raw"""
cd ${WORKSPACE}/srcdir
mkdir -p ${libdir}
${CXX} -std=c++11 -shared -O0 -g -fPIC -o ${libdir}/popuset_shim.${dlext} -I${includedir} popuset_shim.cc -lportaudio
install_license /usr/share/licenses/MIT
"""

platforms = [
    Platform("armv7l", "linux"),
    Platform("x86_64", "linux"),
    Platform("x86_64", "macos"),
]

products = [
    LibraryProduct("popuset_shim", :popuset_shim),
]

dependencies = [
    Dependency("libportaudio_jll"),
]

build_tarballs(ARGS, name, version, sources, script, platforms, products, dependencies)
