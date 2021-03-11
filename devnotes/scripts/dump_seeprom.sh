#!/bin/sh

# Must add new devices to access 64k
echo 24c512 0xa0d4 > /sys/bus/i2c/devices/i2c-0/new_device
echo 24c512 0xa0d5 > /sys/bus/i2c/devices/i2c-0/new_device
echo 24c512 0xa0d6 > /sys/bus/i2c/devices/i2c-0/new_device
echo 24c512 0xa0d7 > /sys/bus/i2c/devices/i2c-0/new_device

cp /sys/bus/i2c/devices/0-a0d4/eeprom _seeprom0
cp /sys/bus/i2c/devices/0-a0d5/eeprom _seeprom1
cp /sys/bus/i2c/devices/0-a0d6/eeprom _seeprom2
cp /sys/bus/i2c/devices/0-a0d7/eeprom _seeprom3

echo 0xa0d4 > /sys/bus/i2c/devices/i2c-0/delete_device
echo 0xa0d5 > /sys/bus/i2c/devices/i2c-0/delete_device
echo 0xa0d6 > /sys/bus/i2c/devices/i2c-0/delete_device
echo 0xa0d7 > /sys/bus/i2c/devices/i2c-0/delete_device

# Each part has a size that is a multiple of 9 bytes (8 data + 1 ECC), rest is
# padded with zeros. 64k % 9 = 7, that is the number of bytes to skip. Not sure
# if globbing keeps the proper order, use explicit file names just in case.
# Also, output file extension must be '.ecc', otherwise 'ecc' tool complains.
## FIXME: Busybox's 'head' on BMC does not support '-c', find an alternative
head -c-7 -q _seeprom0 _seeprom1 _seeprom2 _seeprom3 > seeprom.bin.ecc

# Remove temporary files
rm _seeprom?

ecc -R seeprom.bin.ecc -p -o seeprom.bin

## To write it back:
#
#ecc -I seeprom.bin -p -o seeprom.bin.ecc
#split -d -a 1 -b 65529 seeprom.bin.ecc --filter='dd bs=65536 conv=sync of=$FILE' _seeprom
#
#echo 24c512 0xa0d4 > /sys/bus/i2c/devices/i2c-0/new_device
#echo 24c512 0xa0d5 > /sys/bus/i2c/devices/i2c-0/new_device
#echo 24c512 0xa0d6 > /sys/bus/i2c/devices/i2c-0/new_device
#echo 24c512 0xa0d7 > /sys/bus/i2c/devices/i2c-0/new_device
#
## This is slow, about 10 min per part. We can use bigger block size (blocks of
## up to 32 bytes were tested), but this saves just a few seconds.
#dd of=/sys/bus/i2c/devices/0-a0d4/eeprom if=_seeprom0 bs=1
#dd of=/sys/bus/i2c/devices/0-a0d5/eeprom if=_seeprom1 bs=1
#dd of=/sys/bus/i2c/devices/0-a0d6/eeprom if=_seeprom2 bs=1
#dd of=/sys/bus/i2c/devices/0-a0d7/eeprom if=_seeprom3 bs=1
#
## To write HBBL part only (may be different if SEEPROM layout changes!):
##dd of=/sys/bus/i2c/devices/0-a0d7/eeprom if=_seeprom3 bs=1 skip=$((0x4e1e)) seek=$((0x4e1e)) count=$((0x5a00))
#
#echo 0xa0d4 > /sys/bus/i2c/devices/i2c-0/delete_device
#echo 0xa0d5 > /sys/bus/i2c/devices/i2c-0/delete_device
#echo 0xa0d6 > /sys/bus/i2c/devices/i2c-0/delete_device
#echo 0xa0d7 > /sys/bus/i2c/devices/i2c-0/delete_device
#
## Remove temporary files
#rm _seeprom?
