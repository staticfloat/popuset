#!/bin/bash

source $(dirname ${0})/common.sh
check_root

DEV_ADDR=$(dev_ipv4_addr ${SPEAKER_GROUP} ${CHANNEL} ${SUBCHANNEL})
ip addr change ${DEV_ADDR}/8 dev ${WIFI_DEV}

if is_master ${CHANNEL} ${SUBCHANNEL}; then
    # Enable ipv4/ipv6 forwarding
    sysctl -w net.ipv4.ip_forward=1
    sysctl -w net.ipv6.conf.all.forwarding=1

    # Set up iptables chains for NAT'ing stuff over
    iptables -A FORWARD -i ${WIFI_DEV} -o ${WAN_DEV} -j ACCEPT
    iptables -A FORWARD -i ${WAN_DEV} -o ${WIFI_DEV} -m state --state ESTABLISHED,RELATED -j ACCEPT
    iptables -t nat -A POSTROUTING -o ${WAN_DEV} -j MASQUERADE
else
    # If we're not a master, set up the master as the default route
    GW_ADDR=$(dev_ipv4_addr ${SPEAKER_GROUP} 65535 65535)
    route add default gw ${GW_ADDR} dev ${WIFI_DEV}

    # Suffer no pretenders
    echo "nameserver 8.8.8.8" > /dev/resolv.conf
fi