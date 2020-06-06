#!/usr/bin/env

import os, sys, parted

if len(sys.argv) < 4:
    sys.stderr.write("Usage: %s <img_file> <partition_number> (offset|size)\n"%(sys.argv[0]))
    sys.exit(1)

img_file = sys.argv[1]
partition_index = int(sys.argv[2])
data_request = sys.argv[3]

if not os.path.isfile(img_file):
    sys.stderr.write("Error: cannot find image file '%s'\n"%(img_file))
    sys.exit(1)

if data_request not in ("offset", "size"):
    sys.stderr.write("Invalid data request '%s': must provide either offset or size\n"%(data_request))
    sys.exit(1)

# Open the device
dev = parted.getDevice(img_file)

# Ensure that sector size is 512, as we expect
if dev.sectorSize != 512:
    sys.stderr.write("Warning: sector size == '%d', not 512 as we expected!\n"%(dev.sector_size))
    sys.exit(1)

# Calculate sector offset and sector size
partitions = parted.newDisk(dev).partitions
if partition_index > len(partitions):
    sys.stderr.write("Error: partition index '%d' exceeds total length of disk partitions (%d)\n"%(partition_index, len(partitions)))
    sys.exit(1)

# Vel'koz would be proud.  So much geometry.
part = partitions[partition_index]
offset = part.geometry.start * dev.sectorSize
size = (part.geometry.end - part.geometry.start + 1)*dev.sectorSize

if data_request == 'offset':
    print(offset)
elif data_request == 'size':
    print(size)

