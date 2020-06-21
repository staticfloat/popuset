#!/bin/bash

CONFIG_ENV="/home/pi/popuset.config"
if [[ -f ${CONFIG_ENV} ]]; then
    source ${CONFIG_ENV}
fi

SPEAKER_GROUP=${POPUSET_SPEAKER_GROUP:-0}
CHANNEL=${POPUSET_CHANNEL:-0}
SUBCHANNEL=${POPUSET_SUBCHANNEL:-0}
SSID=${POPUSET_SSID:-popuNET}
FREQ=${POPUSET_WIFI_FREQ:-2462}
WIFI_DEV=${POPUSET_WIFI_DEV:-wlan1}
WAN_DEV=${POPUSET_WAN_DEV:-wlan0}

# args: speakergroup
multicast_addr() {
    printf 'ff12:5041::%04x' ${1}
}
# args: speakergroup, channel, subchannel
dev_addr() {
    printf 'fd37:5041::%04x:%04x:%04x' ${1} ${2} ${3}
}
# We only use ipv4 for internet routing.  Args: speakergroup, channel, subchannel
# We're going to assume that if you need internet, you're a n00b, and don't yet have
# a full deployment of 1000s of devices, so it's okay to just assume that we are
# only dealing with indices < 255, to make it easy to overlay onto an ipv4 address:
dev_ipv4_addr() {
    printf '10.%d.%d.%d' $((${1} & 0xff)) $((${2} & 0xff)) $((${3} & 0xff))
}
is_master() {
    [[ ${1} == 65535 && ${2} == 65535 ]]
}
# args: speakergroup, channel, subchannel
hostname() {
    # Special-case master nodes
    if is_master ${2} ${3}; then
        printf 'popuset-grp%x' ${1}
    else
        printf 'popuset-grp%x-chn%x-sub%x' ${1} ${2} ${3}
    fi
}
check_root() {
    if [ $(id -u) != 0 ]; then
        echo "must run as root!" >&2
        exit 1
    fi
}
