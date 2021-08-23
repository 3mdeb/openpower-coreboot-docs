#!/bin/bash

if [ $# -lt 1 -o $(($1)) -eq 0 ]; then
	echo "Usage: $0 OCB_address [size]"
	exit
fi

SIZE=$(($2))
if [ $SIZE -eq 0 ]; then
	SIZE=256
fi
SIZE=$(($SIZE/8))

# need to subtract SRAM base
ADDR=`printf "%#x" $((($1 - 0xffff8000) << 32))`

# enable autoincrementation
pdbg -P pib putscom 0x1001200C 0x8000000000000000
pdbg -P pib putscom 0x1001200D $ADDR

# read data
while [ $SIZE -ne 0 ]; do
	SIZE=$(($SIZE-1))
	pdbg -P pib getscom 0x1001200E
done
