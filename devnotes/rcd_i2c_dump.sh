#!/bin/sh

# Script expects I2C register address in $1, see 3.3.3.1 in JESD82-31 for
# mapping between I2C registers and RCD control words. $1 will have its 2 lowest
# bits masked out by RCD, and always 4 consecutive bytes are returned.

# i2c* tools are mostly usable for EEPROM devices, however RCD is not an EEPROM.
# It can be hacked to work, although it requires much more accesses as we do not
# have enough control, e.g. i2c{s,g}et doesn't support DWord reads so we have to
# use byte reads/writes everywhere.

# We need to send 32b address even though most of it is ignored.
# 3 - I2C bus number, 0x5a - chip address, next byte - bus command (see 3.3.2)

i2cset 3 0x5a 0x80 0 -y	  # 0x80 - begin DWord read sequence, byte by byte
i2cset 3 0x5a 0x00 0 -y
i2cset 3 0x5a 0x00 0 -y
i2cset 3 0x5a 0x40 $1 -y  # 0x40 - end

# 5 bytes are returned: 1 status byte (0x01 expected) followed by 4 register
# values. MSB is sent first.

STATUS=`i2cget 3 0x5a 0x80 -y`
B3=`i2cget 3 0x5a 0x00 -y`
B2=`i2cget 3 0x5a 0x00 -y`
B1=`i2cget 3 0x5a 0x00 -y`
B0=`i2cget 3 0x5a 0x40 -y`

echo "Status: $STATUS"

# Reverse byte order for readability.
echo $B0 $B1 $B2 $B3

# Below is an example for writing byte value to RCWs. Larger writes should also
# be possible, not tested as changes to other registers may render the platform
# unstable without further DRAM retraining. Use with caution.
#
# i2cset 3 0x5a 0x84 0 -y      # 0x.4 - write byte, must be consistent below
# i2cset 3 0x5a 0x04 0 -y
# i2cset 3 0x5a 0x04 0 -y
# i2cset 3 0x5a 0x04 0x0b -y   # 0x0b - I2C register holding RC06 and RC07
# i2cset 3 0x5a 0x44 0x0f -y   # 0x0f - byte to be written, NOP for both RCWs
