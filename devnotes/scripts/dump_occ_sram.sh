#!/bin/bash

# How to use this script to extract strings from PkTraceBuffer:
# - one time preparation (use appropriate file from hcode/output/obj/*/):
#	awk -F'\\|\\|' '{printf("%x   %s\n", $1, $2)}' trexStringFile > trexhex
#
# Dump and parse:
# ./dump_occ_sram.sh <g_pk_trace_buf+0x38> | tac | awk -f awk_program -n | tac
#
# ...except BMC's busybox doesn't have 'tac' and doesn't support 'awk -n', so
# you have to run dump_occ_sram.sh on BMC and the rest on PC.

if [ $# -lt 1 -o $(($1)) -eq 0 ]; then
	echo "Usage: $0 OCB_address [size]"
	exit
fi

SIZE=$(($2))
if [ $SIZE -eq 0 ]; then
	SIZE=256
fi
SIZE=$(($SIZE/8))

ADDR=`printf "%#x" $(($1 << 32))`

# enable stream mode
pdbg -P pib putscom 0x0006D013 0x0800000000000000

# disable circular mode = enable linear mode
pdbg -P pib putscom 0x0006D012 0x0400000000000000

# set OCB address - must be 8B aligned
pdbg -P pib putscom 0x0006D010 $ADDR

# read data
while [ $SIZE -ne 0 ]; do
	SIZE=$(($SIZE-1))
	pdbg -P pib getscom 0x0006D015
done
