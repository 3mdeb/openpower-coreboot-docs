#!/bin/bash

# Dump and parse SGPE/CME logs:
# - one time preparation (use appropriate file from hcode/output/obj/*/):
#	awk -F'\\|\\|' '{printf("%8.8x   %s\n", $1, $2)}' trexStringFile > trexhex
#
# - dumping
#	./dump_occ_sram.sh <g_pk_trace_buf+0x38> | tac | awk -f awk_program -n | tac
#
# ...except BMC's busybox doesn't have 'tac' and doesn't support 'awk -n', so
# you have to run dump_occ_sram.sh on BMC and the rest on PC.
#
#
# Dump and parse OCC logs:
# - one time preparation:
#	awk -F'\\|\\|' '{printf("%8.8x   %s:%%d: %s\n", $1, $3, $2)}' occStringFile | \
#	sed "s/%p/%x/g" > trexhex
#
# - dumping
#	./dump_occ_sram.sh <g_trac_{err,inf,imp}_buffer+0x28> 0x1fd8 | \
#	sed "s/.\{27\}\(.\{8\}\)\(.\{8\}\).*/\1\n\2/" | \
#	awk -f awk_program_occ -n
#
# No need for 'tac', OCC's format can be parsed in chronological order. It
# prints timestamps before lines so you can 'cat' all 3 logs together before
# passing it to 'awk' and 'sort' the output.
# Some manual splitting may be required around the wrap-around, especially if
# the entries are not 8B aligned...

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
