WIFI_DEV=wlan1
SSID=bananaNET
FREQ=2462
DEV_ADDR=fd37:5041::1


ip link set ${WIFI_DEV} down
ip link set ${WIFI_DEV} up
iw ${WIFI_DEV} set type ibss
iw ${WIFI_DEV} ibss join ${SSID} ${FREQ} 0a:0b:0c:0d:0e:0f
ip addr change ${DEV_ADDR}/96 dev ${WIFI_DEV}
echo -e "fd37:5041::2\tspeakerpi0" >> /etc/hosts