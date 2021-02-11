# Self-Boot Engine

Self-Boot Engine (SBE) is a term used for both a chip and a firmware that runs
on that chip. Its main task is to validate and copy HBBL to cache, and then
start core 0 on the main CPU. It is also responsible for chip access security
during runtime.

## Hardware

According to [Raptor's wiki](https://wiki.raptorcs.com/wiki/Self-Boot_Engine)
SBE is a dedicated Programmable PowerPC-lite Engine (PPE) chip, which uses a
subset of PPC405. However, the instructions used in assembly do not match any
of the documents listed [here](https://wiki.raptorcs.com/wiki/Power_ISA).

SBE consists also of memory: 96kB RAM (aka PIB MEM or PIBMEM) and 4x64kB ROM
(aka SEEPROM). Access to SEEPROM from BMC is possible only if a _Secure Mode_
_Disable_ jumper on the mainboard is set as disabled.

> TODO: what about access from host OS?

[Wiki improperly states that SEEPROM size is 64kB](https://wiki.raptorcs.com/wiki/SEEPROM),
while the [linker script](https://github.com/open-power/sbe/blob/master/src/build/linkerscripts/power/linkseeprom.cmd#L37)
uses 256kB. There is also a backup SEEPROM that normally is a copy of the main
one, it is used to make updates safer. It is updated by Hostboot, so unless the
new version of SBE and HBBL is able to start Hostboot it won't get updated.

## Firmware

As mentioned, this chip does not use the same ISA as PPC405. Because of that it
is compiled by customised [gcc](https://github.com/open-power/ppe42-gcc) and
[binutils](https://github.com/open-power/ppe42-binutils) forks.

Firmware is located in SEEPROM, along with HBBL, Secure Boot keys and some
metadata like version numbers etc. ECC is used for whole SEEPROM in a way that
no ECC block crosses 64kB, instead a padding (64kB % 9 = 7 bytes) is added at
the end of each 64kB part. It means that the actual size of code and data in
SEEPROM is (64kB - 7) * 4 * 8 / 9 = 232992 bytes = ~227,5 kilobytes.

All offsets in the image should be applied to the image with ECC and padding
removed. This can be done with the following commands, assuming 64kB blocks are
saved into `_seeprom0` through `_seeprom3` files:

```
head -c-7 -q _seeprom? > seeprom.bin.ecc
ecc -R seeprom.bin.ecc -p -o seeprom.bin
```

To add ECC bytes and split it back:

```
ecc -I seeprom.bin -p -o seeprom.bin.ecc
split -d -a 1 -b 65529 seeprom.bin.ecc --filter='dd bs=65536 conv=sync of=$FILE' _seeprom
```

SEEPROM begins with [P9XipHeader structure](https://github.com/open-power/sbe/blob/master/src/import/chips/p9/xip/p9_xip_image.h#L368).
A part of that structure is an array of [P9XipSection structures](https://github.com/open-power/sbe/blob/master/src/import/chips/p9/xip/p9_xip_image.h#L280),
one of such structures is the one describing HBBL. It is located at offset 0xE8
relative to the beginning of SEEPROM (0x105 with ECC). For the current
`07-25-2019` branch of SBE HBBL is 20kB big, located at offset 0x2F008. With ECC
and padding it lands entirely in last quarter of SEEPROM. Note that Secure ROM
(SHA512 algorithm) is a big part of that 20kB.

#### SBE to HBBL hand-off

The following is a dump of first 12KB + 16B of memory, starting at what was the
HRMOR at the beginning of HBBL execution (4GB - 128MB + 2MB, even though every
piece of information says it should be at 128MB + 2MB). The dump was obtained
after HBBL passed execution to coreboot.

```
root@talos:~# pdbg -p0 -c1 -t0 getmem 0xf8200000 $((12*1024 + 16)) 2>/dev/null | hexdump -C
00000000  48 00 30 00 00 09 00 05  00 00 00 00 00 00 00 00  |H.0.............|
00000010  00 00 80 00 00 00 00 00  00 00 00 00 00 06 03 fc  |................|
00000020  00 00 00 00 00 06 03 00  00 00 00 00 ff ff ff ff  |................|
00000030  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
*
00000070  ff ff ff ff 48 00 00 00  48 00 00 00 48 00 00 00  |....H...H...H...|
00000080  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00000100  48 00 30 c0 48 00 00 00  48 00 00 00 48 00 00 00  |H.0.H...H...H...|
00000110  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00000200  48 00 2f d0 48 00 00 00  48 00 00 00 48 00 00 00  |H./.H...H...H...|
00000210  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00000300  48 00 2e e0 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000310  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
(more of the same)
*
00000d00  48 00 25 a0 48 00 00 00  48 00 00 00 48 00 00 00  |H.%.H...H...H...|
00000d10  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00000e00  48 00 24 b0 48 00 00 00  48 00 00 00 48 00 00 00  |H.$.H...H...H...|
00000e10  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000e20  48 00 24 a0 48 00 00 00  48 00 00 00 48 00 00 00  |H.$.H...H...H...|
00000e30  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000e40  48 00 24 90 48 00 00 00  48 00 00 00 48 00 00 00  |H.$.H...H...H...|
00000e50  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000e60  48 00 24 80 48 00 00 00  48 00 00 00 48 00 00 00  |H.$.H...H...H...|
00000e70  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000e80  48 00 24 70 48 00 00 00  48 00 00 00 48 00 00 00  |H.$pH...H...H...|
00000e90  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00000f00  48 00 24 00 48 00 00 00  48 00 00 00 48 00 00 00  |H.$.H...H...H...|
00000f10  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000f20  48 00 23 f0 48 00 00 00  48 00 00 00 48 00 00 00  |H.#.H...H...H...|
00000f30  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000f40  48 00 23 e0 48 00 00 00  48 00 00 00 48 00 00 00  |H.#.H...H...H...|
00000f50  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000f60  48 00 23 d0 48 00 00 00  48 00 00 00 48 00 00 00  |H.#.H...H...H...|
00000f70  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
00000f80  48 00 23 c0 48 00 00 00  48 00 00 00 48 00 00 00  |H.#.H...H...H...|
00000f90  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00001500  48 00 1e 50 48 00 00 00  48 00 00 00 48 00 00 00  |H..PH...H...H...|
00001510  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00001600  48 00 1d 60 48 00 00 00  48 00 00 00 48 00 00 00  |H..`H...H...H...|
00001610  48 00 00 00 48 00 00 00  48 00 00 00 48 00 00 00  |H...H...H...H...|
*
00003000  7c 42 13 78 7c 40 00 a6  78 42 08 40 78 42 f8 02  ||B.x|@..xB.@xB..|
00003010
```

Most of that data was prepared by SBE. At the very beginning we can see a jump
instruction (as almost all instructions, this is 4B long) to 0x3000, where HBBL
image is loaded from SEEPROM. Immediately after that is a structure defined in
[p9_sbe_hb_structures.H](https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/nest/p9_sbe_hb_structures.H#L81),
with misleading comment as for the starting address and alignment of the
structure:

```c
// Structure starts at the bootloader zero address
//   Note - this structure must remain 64-bit aligned to
//          maintain compatibility with Hostboot
struct BootloaderConfigData_t
{
    uint32_t version;         // bytes  4:7  Version identifier
    uint8_t sbeBootSide;      // byte   8    0=SBE side 0, 1=SBE side 1
    //                                         [ATTR_SBE_BOOT_SIDE]
    uint8_t pnorBootSide;     // byte   9    0=PNOR side A, 1=PNOR side B
    //                                         [ATTR_PNOR_BOOT_SIDE]
    uint16_t pnorSizeMB;      // bytes 10:11 Size of PNOR in MB
    //                                         [ATTR_PNOR_SIZE]
    uint64_t blLoadSize;      // bytes 12:19 Size of Load
    //                                         Exception vectors and Bootloader
    BootloaderSecureSettings  secureSettings  ; // byte  20
    uint8_t reserved[7];      // bytes 21:27 Reserved space to maintain 64-bit alignment
    uint64_t xscomBAR;        // bytes 28:35 XSCOM MMIO BAR
    uint64_t lpcBAR;          // bytes 36:43 LPC MMIO BAR
    keyAddrPair_t pair;       // total of 72 Bytes (8+8*8) for Key/Addr Pair
}; // Note: Want to use '__attribute__((packed))' but compiler won't let us
```

`uint64_t` variables are not naturally aligned, which may have implications
later in the code. All of the fields are [filled by SBE](https://github.com/open-power/sbe/blob/master/src/import/chips/p9/procedures/hwp/nest/p9_sbe_load_bootloader.C#L505).
Note that `blLoadSize` includes 12K reserved for exception vectors.

Space between the end of that structure (0x74) and main payload is filled with
`48 00 00 00`, which corresponds to `b .` assembly instruction. Some of these
instructions are later [updated by HBBL](https://github.com/open-power/hostboot/blob/master/src/bootloader/bl_start.S#L167)
to jump into proper handlers.

At `0x3000` HBBL image begins. When CPU is released, it starts executing at
address `0`, but immediately jumps here.

## Reading SEEPROM from BMC

There are at least two ways of reading (and possibly writing) SEEPROM from BMC.
We can either use a kernel driver to read directly by I2C bus, or do this
manually by writing and reading appropriate SCOM registers.

#### I2C

SEEPROM is split into four 64kB parts, each can be accessed under a separate I2C
address: 0x54, 0x55, 0x56, 0x57. SEEPROM for CPU0 is located under bus 0, CPU1
under bus 1.

All parts are mounted by default, unfortunately wrong kernel driver is used and
only the first 32kB of each block is accessible. The easiest way of using a
proper driver is to create a new device. Because we cannot easily remove the
device that was created automatically we have to use 10 bit addressing and set a
bit that is ignored by SEEPROM, otherwise kernel won't let us use the same
address as used by existing driver. To add a new device:

```
echo 24c512 0xa0d4 > /sys/bus/i2c/devices/i2c-0/new_device
```

`0xa0..` tells to use 10 bits addressing mode, `0x..d4` is the address. Note
that its 7 lowest bits are the same as in `0x54`. This must be repeated for each
part of SEEPROM. For CPU1 use `i2c-1` instead. After this command a new file is
created (`/sys/bus/i2c/devices/0-a0d4/eeprom`) which holds the contents of one
part of SEEPROM. It is writable, but a write must be performed in blocks of one
of the supported sizes.

> TODO: how big blocks are supported?

See [dump_seeprom.sh](scripts/dump_seeprom.sh) for commands to merge individual
into one image and vice versa. It is hardcoded to use bus for CPU0. For writing
I suggest first reading the current contents of SEEPROM and write only the parts
that are different to save time.

So far I haven't found a way to access secondary SEEPROM using this approach.

#### SCOM

Internally it also uses I2C. Host must be powered on in order to access SCOM.
After _Secure Mode Disable_ is turned off (i.e. Secure Mode is enabled) SCOM is
no longer accessible from BMC.

This process uses `pdbg` tool which is preinstalled on BMC. It allows to read 8
bytes at a time. It also allows to read from the backup SEEPROM.

```
root@talos:~# pdbg -P pib putscom 0xa0000 0xD8A9009000000000
root@talos:~# pdbg -P pib getscom 0xa0003
p0: 0x00000000000a0003 = 0x614a7ba67c6a1850 (/kernelfsi@0/pib@1000)
```

Technically before reading `0xa0003` we should wait until bit 44 of `0xa0002`
(BUS\_STATUS\_BUSY\_0) is 0. In practice the overhead of running this as two
separate user-space commands seems to be enough, even when run in a script.

`0x614a7ba67c6a1850` is the SBE image magic number, `XIP SEPM` in ASCII. To read
from address `wxyz` write `0xD8A90090wxyz0000` to SCOM `0xa0000`, to read from
the backup SEEPROM use `0xD8A90290wxyz0000` instead. Bits 8-14 are I2C address,
bit 15 specifies that this is read operation when set. This gives 0xA9, 0xAB,
0xAD and 0xAF for I2C 0x54, 0x55, 0x56, 0x57, respectively.

For details see [POWER9 Processor Registers Specification Vol 1](https://wiki.raptorcs.com/w/images/0/04/POWER9_Registers_vol1_version1.1_pub.pdf).
