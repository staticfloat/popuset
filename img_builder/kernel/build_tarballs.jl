using BinaryBuilder

# We can build a few different versions of this kernel
version_mapping = Dict(
    v"4.19.108" => ("2fab54c74bf956951e61c6d4fe473995e8d07010", "bc92c569bbdb53744dee458827c64119a16ee5267cdcba851e1832c3a174571c"),
    v"5.5.8"    => ("487388338282edd6fd4132504bbe9e1494f14593", "c09461fe685b446f4ce9fd5c952763affe60a8dc42c38a631bba7d6ff238f699"),
    v"5.6.14"   => ("415e64a988d4525c4015ff7c1751a17aea1f8d08", "8232a8babe5ebee9cf79b07852b005bbc13a6a112781041b654bec42c30d2777"),
)

configurations = Dict(
    "rpi4" => DirectorySource("rpi4_config"),
    "rpizerow" => DirectorySource("rpizerow_config"),
)

config_idx = findfirst(ARGS .== "--config")
if config_idx === nothing || !(get(ARGS, config_idx + 1, "") in keys(configurations))
    error("Must provide a --config parameter that is one of $(collect(keys(configurations)))")
end
config_name = ARGS[config_idx+1]
deleteat!(ARGS, config_idx)
deleteat!(ARGS, config_idx)

name = "Linux-$(config_name)"
version = v"5.6.14"

# Collection of sources required to build linux
sources = [
    ArchiveSource("https://github.com/raspberrypi/linux/archive/$(version_mapping[version][1]).tar.gz",
                  version_mapping[version][2]),
    configurations[config_name],
]

script = raw"""
cd ${WORKSPACE}/srcdir/linux-*/

# Add necessary packages
apk add binutils bison flex bc openssl-dev gcc linux-headers libelf-dev musl-dev

# Configure the kernel compile system to use native-to-alpine compilers for scripts
# We use these compilers so that we can do things like install `openssl-dev` through
# `apk` for the host scripts, without needing to somehow download a host JLL.
FLAGS=(
    ARCH=arm
    CROSS_COMPILE=arm-linux-gnueabihf-
    INSTALL_MOD_PATH=${prefix}
    HOSTCC="/usr/bin/gcc"
    HOSTLD="/usr/bin/ld"
    HOSTAS="/usr/bin/as"
    CC="cc"
    LD="ld"
    AS="as"
)


# Use the default config file as bundled within the `sources` directory
cp -L ../default.config arch/arm/configs/default_defconfig

# Collect list of files for kernel source package (this taken from `scripts/package/builddeb`)
# Do this before we litter everything with object files
find scripts -type f -o -type l >> /tmp/src.list
find . -name Makefile\* -o -name Kconfig\* -o -name \*.pl -o -name default_defconfig >> /tmp/src.list
find tools/include -name \*.h -type f >> /tmp/src.list

# Next, build the actual modules and whatnot
make "${FLAGS[@]}" default_defconfig
make "${FLAGS[@]}" -j${nproc} zImage modules dtbs

# Install things into `${prefix}` as if it were the root drive:
mkdir -p ${prefix}/boot/dts
cp arch/arm/boot/zImage ${prefix}/boot/zImage
cp arch/arm/boot/dts/*.dtb ${prefix}/boot/dts/

if [ -d arch/arm/boot/dts/overlays ]; then
    mkdir -p ${prefix}/boot/dts/overlays
    cp arch/arm/boot/dts/overlays/*.dtb* ${prefix}/boot/dts/overlays/
fi

KERNELRELEASE=$(cat include/config/kernel.release)
KERNEL_SRCDIR="/usr/src/linux-headers-${KERNELRELEASE}"

# Install modules and symlinks that point to our source package
make "${FLAGS[@]}" modules_install
rm -f ${prefix}/lib/modules/${KERNELRELEASE}/{build,source}
ln -svf ../../..${KERNEL_SRCDIR} ${prefix}/lib/modules/${KERNELRELEASE}/build
ln -svf ../../..${KERNEL_SRCDIR} ${prefix}/lib/modules/${KERNELRELEASE}/source

find arch/*/include include -type f -o -type l >> /tmp/src.list
find arch/arm -name module.lds -o -name Kbuild.platforms -o -name Platform >> /tmp/src.list
find $(find arch/arm -name include -o -name scripts -type d) -type f >> /tmp/src.list
echo Module.symvers >> /tmp/src.list

# Use `tar` to do the actual creation of the kernel source package
mkdir -p ${prefix}${KERNEL_SRCDIR}
tar -c -f - -T /tmp/src.list | (cd ${prefix}${KERNEL_SRCDIR}; tar -x -f -)


# Create an installer script to make it easy to install on a raspberry pi somewhere
cat > ${prefix}/install.sh << 'EOF'
#!/bin/sh

if [ $(id -u) != 0 ]; then
    echo "must run as sudo!" >&2
    exit 1
fi

# Find script directory
prefix=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Make sure our permissions are set properly
chown root:root -R ${prefix}

# First, copy boot stuff to `/boot`.  Layout depends a bit on the target:
echo "Installing to /boot..."
if [ -f /boot/kernel7l.img ]; then
    # If we're on a raspberry pi, then name things accordingly
    cp -r ${prefix}/boot/zImage /boot/kernel7l.img
    cp -r ${prefix}/boot/dts/*rpi*.dtb /boot/
    cp -r ${prefix}/boot/dts/overlays /boot/overlays
elif [ -f /boot/official.cmd ]; then
    NAME=banaudio
    # If we're on a bananapi, then we need to mkimage:
    cp -r ${prefix}/boot/zImage /boot/zImage.${NAME}
    # We just re-use the official dtb:
    ln -sf official.dtb /boot/${NAME}.dtb

    # Modify official.cmd into banana.cmd, mkimage and set it as default
    cat /boot/official.cmd | sed -e "s/official/${NAME}/g" > /boot/${NAME}.cmd
    mkimage -C none -A arm -T script -d /boot/${NAME}.cmd /boot/${NAME}.scr
    ln -sf ${NAME}.scr /boot/boot.scr
fi

# Next, install modules and source
MDIR=$(basename ${prefix}/lib/modules/*)
echo "Installing to /lib/modules/${MDIR}..."
mkdir -p /lib/modules
rm -rf /lib/modules/${MDIR}
cp -r ${prefix}/lib/modules/${MDIR} /lib/modules/

SDIR=$(basename ${prefix}/usr/src/*)
echo "Installing to /usr/src/${SDIR}..."
mkdir -p /usr/src
rm -rf /usr/src/${SDIR}
cp -r ${prefix}/usr/src/${SDIR} /usr/src/

echo "Reconfiguring kernel source scripts..."
cd /usr/src/${SDIR} && make -j4 default_defconfig && make -j4 scripts
EOF

chmod +x ${prefix}/install.sh
"""

products = [
    ExecutableProduct("zImage", :zImage, "boot"),
]

platforms = [
    # It's not always quite armv7l, but the kernel knows how do work if we just give it the right compilers.
    Linux(:armv7l),
]

dependencies = [
]

build_tarballs(ARGS, name, version, sources, script, platforms, products, dependencies; skip_audit=true)
rm("build"; force=true, recursive=true)