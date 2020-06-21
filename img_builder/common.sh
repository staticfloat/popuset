#!/bin/bash

SOURCE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

RELEASE="2020-05-27"
download_img() {
    URL="http://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2020-05-28/${RELEASE}-raspios-buster-lite-armhf.zip"
    ZIPFILE="${SOURCE_DIR}/downloads/$(basename ${URL})"
    IMGFILE="${ZIPFILE%.zip}.img"

    if [[ ! -f "${ZIPFILE}" ]]; then
        mkdir -p "${SOURCE_DIR}/downloads"
        curl -# -L "${URL}" -o "${ZIPFILE}"
    fi
}

mount_img() {
    MOUNT_NAME="${1:-interactive}"
    ZIPFILE="${SOURCE_DIR}/downloads/${RELEASE}-raspios-buster-lite-armhf.zip"
    IMGFILE="${SOURCE_DIR}/output/${MOUNT_NAME}-${RELEASE}.img"

    # Before mounting anything, ensure this is unmounted.
    unmount_img "${MOUNT_NAME}"

    # Check if pyparted is installed
    if ! python3 -c "import parted" >/dev/null; then
        echo "Attempting to auto-install pyparted"
        sudo apt install -y python3 python3-parted
    fi

    # Extract the .img into our `output` directory
    if [[ ! -f "${IMGFILE}" ]]; then
        TEMP_IMG="${SOURCE_DIR}/downloads/${RELEASE}-raspios-buster-lite-armhf.img"
        unzip "${ZIPFILE}" -d "${SOURCE_DIR}/downloads"
        mkdir -p $(dirname ${IMGFILE})
        mv "${TEMP_IMG}" "${IMGFILE}"

        # We extend the image by 1GB here, as we want some space to work with
        dd if=/dev/zero bs=1M count=1024 >> "${IMGFILE}"
        parted "${IMGFILE}" resizepart 2 100%
    fi

    # Determine the sector size for the image: (should be 512)
    BOOT_OFFSET=$(python3 "${SOURCE_DIR}/get_partition_info.py" "${IMGFILE}" "0" offset)
    BOOT_SIZE=$(python3 "${SOURCE_DIR}/get_partition_info.py" "${IMGFILE}" "0" size)
    ROOT_OFFSET=$(python3 "${SOURCE_DIR}/get_partition_info.py" "${IMGFILE}" "1" offset)
    ROOT_SIZE=$(python3 "${SOURCE_DIR}/get_partition_info.py" "${IMGFILE}" "1" size)

    ROOT="${SOURCE_DIR}/mounts/${MOUNT_NAME}"
    mkdir -p "${ROOT}"

    # First, mount root directory
    sudo mount -o "loop,rw,sync,offset=${ROOT_OFFSET},sizelimit=${ROOT_SIZE}" "${IMGFILE}" "${ROOT}"

    # After mounting we've got a loopback device that we acn use to expand the root partition,
    # since we typically add some extra space during extraction above:
    LOOP_DEVICE=$(df | grep "${ROOT}" | awk '{print $1}' | head -1)
    sudo resize2fs "${LOOP_DEVICE}"

    # Next, mount boot directory
    sudo mount -o "loop,rw,sync,offset=${BOOT_OFFSET},sizelimit=${BOOT_SIZE}" "${IMGFILE}" "${ROOT}/boot"

    # Mount /proc, /dev, /sys, etc...
    sudo mount -t proc /proc "${ROOT}/proc"
    sudo mount -t sysfs /sys "${ROOT}/sys"
    sudo mount -o bind /dev "${ROOT}/dev"

    echo "Mounted at ${SOURCE_DIR}/mounts/${MOUNT_NAME}"
}

unmount_img() {
    MOUNT_NAME="${1:-interactive}"
    ROOT="${SOURCE_DIR}/mounts/${MOUNT_NAME}"
    sudo umount -R "${ROOT}"
}

qemu_launch() {
    ROOT="${1}"
    TARGET="${2}"
    if [[ ! -d "${ROOT}" ]]; then
        echo "Invalid root directory '${ROOT}'" >&2
        return
    fi
    if [[ ! -f "${ROOT}/${TARGET}" ]]; then
        echo "Invalid executable '${ROOT}/${TARGET}'" >&2
        return
    fi

    sudo chroot "${ROOT}" "/${TARGET}"
}

qemu_launch_script() {
    sudo cp -f "${2}" "${1}/$(basename ${2})"
    qemu_launch "${1}" "$(basename ${2})"
    sudo rm -f "${1}/$(basename ${2})"
}

deploy_kernel() {
    CONFIG="${1}"
    if [[ ! -d "${SOURCE_DIR}/kernel/${CONFIG}_config" ]]; then
        echo "Invalid configuration name: ${CONFIG}"
        exit 1
    fi

    # Build kernel, if we need to
    if ! stat --printf='' ${SOURCE_DIR}/kernel/products/Linux-${CONFIG}.v*.tar.gz 2>/dev/null; then
        echo "Couldn't find kernel for ${CONFIG}, building..."
        (cd "${SOURCE_DIR}/kernel" && julia --color=yes build_tarballs.jl --config "${CONFIG}")
    fi
    KERNEL_TARBALL=$(echo ${SOURCE_DIR}/kernel/products/Linux-${CONFIG}.v*.tar.gz)

    # Extract kernel to temporary directory
    echo "Extracting and installing kernel..."
    TMPDIR=$(mktemp -d)
    tar -C "${TMPDIR}" -zxf "${KERNEL_TARBALL}"
    
    # Install it into the config rootfs
    ROOT="${SOURCE_DIR}/mounts/${CONFIG}"
    sudo cp -r ${TMPDIR}/boot/zImage ${ROOT}/boot/kernel7l.img
    sudo cp -r ${TMPDIR}/boot/dts/*rpi*.dtb ${ROOT}/boot/
    sudo cp -r ${TMPDIR}/boot/dts/overlays ${ROOT}/boot/overlays

    # Install modules
    MDIR=$(basename ${TMPDIR}/lib/modules/*)
    sudo mkdir -p ${ROOT}/lib/modules
    sudo rm -rf ${ROOT}/lib/modules/${MDIR}
    sudo cp -r ${TMPDIR}/lib/modules/${MDIR} ${ROOT}/lib/modules/

    # Install kernel sources
    SDIR=$(basename ${TMPDIR}/usr/src/*)
    sudo mkdir -p ${ROOT}/usr/src
    sudo rm -rf ${ROOT}/usr/src/${SDIR}
    sudo cp -r ${TMPDIR}/usr/src/${SDIR} ${ROOT}/usr/src/

    # Cleanup temporary kernel extraction directory
    sudo rm -rf "${TMPDIR}"
}

deploy_packages() {
    CONFIG="${1}"
    SCRIPT="packages-${CONFIG}.sh"
    if [[ ! -f "${SOURCE_DIR}/packages/${SCRIPT}" ]]; then
        echo "Invalid configuration name: ${CONFIG}, couldn't find ${SOURCE_DIR}/packages/${SCRIPT}"
        exit 1
    fi
    ROOT="${SOURCE_DIR}/mounts/${CONFIG}"

    # Deploy first the config-dependent package install script
    echo "Installing packages..."
    qemu_launch_script "${ROOT}" "${SOURCE_DIR}/packages/${SCRIPT}"

    # Next, do common things like install wireguard-tools, reconfigure kernel sources, etc...
    qemu_launch_script "${ROOT}" "${SOURCE_DIR}/packages/kernel_sources.sh"
    qemu_launch_script "${ROOT}" "${SOURCE_DIR}/packages/wireguard-tools.sh"

    # Add popusetNET configs and enable them
    mkdir -p ${ROOT}/usr/local/bin
    sudo cp -r ${SOURCE_DIR}/packages/popusetnet_config/*.sh ${ROOT}/usr/local/bin/
    sudo cp -r ${SOURCE_DIR}/packages/popusetnet_config/*.service ${ROOT}/lib/systemd/system/
    qemu_launch_script "${ROOT}" "${SOURCE_DIR}/packages/popusetnet_enable.sh"
}

build_img() {
    CONFIG="${1}"
    if [[ -z "${CONFIG}" ]]; then
        echo "Invalid configuration name: '${CONFIG}'"
        exit 1
    fi

    echo "Building base image for configuration ${CONFIG}"

    download_img
    mount_img "${CONFIG}"

    # Start by deploying the kernel
    deploy_kernel "${CONFIG}"

    # Next, install packages (which packages depends on the config)
    deploy_packages "${CONFIG}"

    # unmount the image!  (Eventually, we'll probably want to shrink this somehow)
    unmount_img "${CONFIG}"
}