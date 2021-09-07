Hostboot porting
================

# Links

* [POWER9 IPL flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)
* [POWER9 Processor Registers Specification Vol 1 (TP, TB, PB)](https://wiki.raptorcs.com/w/images/0/04/POWER9_Registers_vol1_version1.1_pub.pdf)
* [POWER9 Processor Registers Specification Vol 2 (XBUS, OB, PCI, CACHE, CORE)](https://wiki.raptorcs.com/w/images/c/c7/POWER9_Registers_vol2_version1.2_pub.pdf)
* [POWER9 Processor Registers Specification Vol 3 (Memory, DDRPHY)](https://wiki.raptorcs.com/w/images/9/95/POWER9_Registers_vol3_version1.2_pub.pdf)
* [POWER9 Sforza Single-Chip Module datasheet](https://ibm.ent.box.com/s/obvpezg37sb7absy6ck1lj8nrcoptvw8)
  (version from Raptor's wiki does not include DD 2.3)

# Hostboot project structure

```
├── config.mk
├── doxywarnings.log
├── env.bash
├── hb -> src/build/tools/hb
├── img
├── LICENSE
├── LICENSE_PROLOG
├── makefile
├── NOTICE
├── obj
│   ├── beam
│   ├── cscope
│   ├── doxygen
│   ├── genfiles
│   └── modules
├── procedure.rules.mk
├── README.md
└── src
    ├── bootloader
    ├── bootloader.ld
    ├── build
    ├── HBconfig
    ├── import
    ├── include
    ├── kernel
    ├── kernel.ld
    ├── lib
    ├── libc++
    ├── makefile
    ├── module.ld
    ├── runtime
    ├── securerom
    ├── securerom.ld
    ├── sys
    └── usr
```

hostboot consists of two main directories:

- `obj` - contains doxygen, unit tests and other stuff
- `src` - main source directory containing all files necessary for build

The `src` direcotry is divided into modules, each responsible for different
thing in boot process:

- `bootloader` - source for the hostboot bootloader, it is a port of SBE
  SEEPROM and is responsible for loading and executing the hostboot kernel.
  Probably we should look here how the state of the processor should look like
  when entering the coreboot's bootblock.
- `build` - contains various tools necessary for build process
- `import` - chip specific initialization code for P8, P9, centaur. Contains
  IO, memory and other hardware procedures.
- `include` - header files for whole project
- `kernel` - hostboot itself is a micro-kernel being which behaves similarily
  as other kernels. It has support for memory management, interrupts, task
  control, error handling, exceptions, etc. It was designed with mind to
  perform hardware initialization in the userspace in a init-like manner.
- `lib` - general purpose libraries for asserts, syscalls, std\*, string, etc.
- `libc++` - contains a sigle C++ file with operator definitions, memory
  barriers etc.
- `runtime` - as the name suggests, it provides runtime services and interfaces
- `securerom` - used for early verification when Secure Boot is enabled
- `sys` - contains the VFS and init main, starts the initialization services
- `usr` - all user space is located here: device drivers, istep dispatcher and
  init services

At first glance the codebase seem huge, but in fact the core initialization is
only a small part of it, because of the error handling, interrupts, kernel
implementation, etc.

## Hostboot bootloader (HBBL)

A small application that launches the main hostboot kernel. When SBE launches
the HBBL it passes certain information placed at zero address of HBBL. See more
in `src/import/chips/p9/procedures/hwp/nest/p9_sbe_hb_structures.H`:

```
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

The layout of the HBBL can be found in `src/bootloader.ld`. The assembly that
starts the HBBL is located in `src/bootloader/bl_start.S`. It sets up some
registers, exception vectors and jumps to bootloader main in
`src/bootloader/bootloader.C`.

### HBBL responsibility

HBBL sets up the exception vectors and parses the information passed from SBE.
It additionally loads the Hostboot Base (HBB) into cache. Initially the HBB
contains ECC. HBBL verifies the ECC and strips it. Then HBB is copied to the
HBB working address and launched.

```
    /** Location of working copy of HBB with ECC */
#define HBB_ECC_WORKING_ADDR    (getHRMOR() - ( 1*MEGABYTE))

    /** Location of working copy of HBB without ECC */
#define HBB_WORKING_ADDR        (getHRMOR() + ( 1*MEGABYTE))

    /** Location of HBBL data */
#define HBBL_DATA_ADDR          (getHRMOR() + HBBL_EXCEPTION_VECTOR_SIZE \
                                 + MAX_HBBL_SIZE)
```

The working addresses are ORed with HRMOR ignore bit (bit 0 in BE, or bit 63
in LE).

For initial bootblock launching the HBB may contain the entire coreboot.rom,
since HBBL does everything what we need for now.

## Enabling console

The serial port is exposed via BMC, base address 0x3f8 on LPC. In order to
enable LPC in coreboot we need:

- basic out\*, in\* ops for IO cycles (`src/arch/ppc64/include/io.h`)
- enable UART_IO and AST2400 driver in coreboot for the board
- call AST2400 early init functions (`src/superio/aspeed/common/*.c`)
- run C code (look at `src/kernel/start.S` in hostboot)
- other prerequisities related to POWER9 bootflow

Relevant hostboot source files:

- `src/usr/console/ast2400.C`
- `src/usr/lpc/lpcdd.C`
- `src/include/arch/memorymap.H`
- `src/usr/targeting/targetservicestart.C`
- `src/include/usr/lpc/lpc_const.H`
- `src/usr/lpc/lpcdd.H`

SBE (Self Boot Engine) seems to be passing some data structure to hostboot
during handoff of the control. It is parsed by hostboot to retrieve data such
as LPC base address. This base address is needed to implement in\* and out\*
functions to generate IO cycles.

TODO: calculate the LPC base address instead of hardcoding it, since the
default values may be overriden (see `BLTOHB_SECURE_OVERRIDES`).

hostboot repo: https://github.com/open-power/hostboot
sbe repo: https://github.com/open-power/sbe
docs: https://github.com/open-power/docs

## FFS tools

https://github.com/open-power/ffs

These tools allow manipulating and preparing PNOR partitions. It contains
various utilities:

- `ecc` - allows manipulation of files to inject or strip ECC bytes (1 ECC byte
  per 8 bytes)
- `fpart` - create, modify, erase PNOR partition table
- `fcp` - erase, modify, write, read contents of the PNOR image partitions

Example usage of `fcp` how to read out a partition or replace contents in a partition:

1. Write HBB partition with coreboot.rom.signed.ecc file:

  `./fcp file:coreboot.rom.signed.ecc file:flash.pnor:HBB -o 0 -W`

2. Read out the contents of HBB partition:

  `./fcp file:flash.pnor:HBB - -o 0 -R |hexdump -C`

## SB Signing utils

https://github.com/open-power/sb-signing-utils

Utilities to sign the images for Secure and Trusted Boot. The utilities create
a container with key hashes and wraps the payload (image) with it. The
container must be created before ECC is applied. HBB must contains such
container for example.

## Flashing the firmware via BMC

Flashing the firmware on openBMC can be done with `pflash` tool.

Sample commands:

1. Erase and program whole PNOR with file:

  `pflash -E -p /tmp/talos.pnor`

2. Read whole PNOR to file:

  `pflash -r /tmp/talos.pnor`

3. Read a single partition to file:

  `pflash -P <partition> -r <partition>.bin`

4. Erase and program single partition with file:

  `pflash -e -P <partition> -p <partition>.bin`

> The file size must be exact the same as partition size on PNOR.

To get a list of possible partition names, invoke `pflash -i`:

```shell
Flash info:
-----------
Name          = /dev/mtd6
Total size    = 64MB	 Flags E:ECC, P:PRESERVED, R:READONLY, B:BACKUP
Erase granule = 64KB           F:REPROVISION, V:VOLATILE, C:CLEARECC

TOC@0x00000000 Partitions:
-----------
ID=00            part 0x00000000..0x00002000 (actual=0x00002000) [----R-----]
ID=01            HBEL 0x00008000..0x0002c000 (actual=0x00024000) [E-----F-C-]
ID=02           GUARD 0x0002c000..0x00031000 (actual=0x00005000) [E--P--F-C-]
ID=03           NVRAM 0x00031000..0x000c1000 (actual=0x00090000) [---P--F---]
ID=04         SECBOOT 0x000c1000..0x000e5000 (actual=0x00024000) [E--P------]
ID=05           DJVPD 0x000e5000..0x0012d000 (actual=0x00048000) [E--P--F-C-]
ID=06            MVPD 0x0012d000..0x001bd000 (actual=0x00090000) [E--P--F-C-]
ID=07            CVPD 0x001bd000..0x00205000 (actual=0x00048000) [E--P--F-C-]
ID=08             HBB 0x00205000..0x00305000 (actual=0x00100000) [EL--R-----]
ID=09             HBD 0x00305000..0x00425000 (actual=0x00120000) [EL--------]
ID=10             HBI 0x00425000..0x019c5000 (actual=0x015a0000) [EL--R-----]
ID=11             SBE 0x019c5000..0x01a81000 (actual=0x000bc000) [ELI-R-----]
ID=12           HCODE 0x01a81000..0x01ba1000 (actual=0x00120000) [EL--R-----]
ID=13            HBRT 0x01ba1000..0x021a1000 (actual=0x00600000) [EL--R-----]
ID=14         PAYLOAD 0x021a1000..0x022a1000 (actual=0x00100000) [-L--R-----]
ID=15      BOOTKERNEL 0x022a1000..0x03821000 (actual=0x01580000) [-L--R-----]
ID=16             OCC 0x03821000..0x03941000 (actual=0x00120000) [EL--R-----]
ID=17         FIRDATA 0x03941000..0x03944000 (actual=0x00003000) [E-----F-C-]
ID=18         VERSION 0x03944000..0x03946000 (actual=0x00002000) [-L--R-----]
ID=19         BMC_INV 0x03968000..0x03971000 (actual=0x00009000) [------F---]
ID=20            HBBL 0x03971000..0x03978000 (actual=0x00007000) [EL--R-----]
ID=21        ATTR_TMP 0x03978000..0x03980000 (actual=0x00008000) [------F---]
ID=22       ATTR_PERM 0x03980000..0x03988000 (actual=0x00008000) [E-----F-C-]
ID=23     IMA_CATALOG 0x03989000..0x039c9000 (actual=0x00040000) [EL--R-----]
ID=24         RINGOVD 0x039c9000..0x039e9000 (actual=0x00020000) [----------]
ID=25         WOFDATA 0x039e9000..0x03ce9000 (actual=0x00300000) [EL--R-----]
ID=26     HB_VOLATILE 0x03ce9000..0x03cee000 (actual=0x00005000) [E-----F-CV]
ID=27            MEMD 0x03cee000..0x03cfc000 (actual=0x0000e000) [EL--R-----]
ID=28            SBKT 0x03d02000..0x03d06000 (actual=0x00004000) [EL--R-----]
ID=29            HDAT 0x03d06000..0x03d0e000 (actual=0x00008000) [EL--R-----]
ID=30          UVISOR 0x03d10000..0x03e10000 (actual=0x00100000) [-L--R-----]
ID=31      BOOTKERNFW 0x03e10000..0x03ff0000 (actual=0x001e0000) [---P------]
ID=32     BACKUP_PART 0x03ff7000..0x03fff000 (actual=0x00000000) [----RB----]
```

### Testing firmware images without flashing

It is possible to test new firmware images without flashing the physical flash
device. This makes testing and switching between two versions (e.g. Hostboot and
coreboot) much faster and safer. There are two ways of doing so, one is
described on [Raptor's wiki](https://wiki.raptorcs.com/wiki/Compiling_Firmware#Running_the_firmware_temporarily)
and requires starting `mboxd` manually, the second one is described below.
v2.00+ BMC firmware requirement still applies.

First, read original flash. For earlier versions it is required to read from
system that booted at least once, since some of the partitions are modified on
the first boot.

```shell
root@talos:~# pflash -r /tmp/talos.pnor
```

> Keep in mind that tmpfs size is limited and exceeding that limit may result in
> unresponsive BMC, which in most severe cases requires hard power cycle.

Next step is "flashing" modified partition, which is similar to flashing real
device with two changes: no need to erase the flash and target file must be
specified. New command looks like this:

```shell
root@talos:~# pflash -P <partition> -p <partition>.bin -F /tmp/talos.pnor
```

> `pflash` can emulate `-e` option when "flashing" to file, but this is not
> required. It only wastes time, as the emulated erase is even slower than the
> real one.

To mount the file as flash device one has to use (on powered down platform):

```shell
root@talos:~# mboxctl --backend file:/tmp/flash.pnor
```

> Partitions can be "flashed" while the file is mounted, as long as host
> platform doesn't try to access it simultaneously.

Optionally, success can be tested with:

```shell
root@talos:~# mboxctl --lpc-state
LPC Bus Maps: BMC Memory
```

`BMC Memory` tells that emulated flash is used instead of real one. Host doesn't
see any difference (except maybe different access times), it still reads and
writes PNOR the same way as with physical device.

To get back to real PNOR one has to use:

```shell
root@talos:~# mboxctl --backend vpnor
Failed to post message: Connection timed out
root@talos:~# mboxctl --lpc-state
LPC Bus Maps: Flash Device
```

Even though that command reports failure, it maps LPC back to flash device. This
can be tested with `mboxctl --lpc-state`.

> There is `mboxctl --point-to-flash` command that is supposed to revert to the
> original mapping, but it doesn't seem to work, neither does `--resume clean`
> nor `--reset`.

## Building OpenPOWER firmware

Sometimes it may be necessary to modify the core OpenPOWER firmware. The easiest
way to do this is to use op-build. To build the image get familiar with the
[README.md](https://git.raptorcs.com/git/talos-op-build/tree/README.md).

```
git clone https://scm.raptorcs.com/scm/git/talos-op-build
cd talos-op-build
git checkout raptor-v2.00
git submodule update --init --checkout
./op-build talos_defconfig && ./op-build
```

> Remember to install dependencies

Then you may modify certain packages after the build. For example, to get own
hostboot to be compiled, edit the `openpower/package/hostboot/hostboot.mk` to
change the repo URL and the go to `openpower/package/hostboot/Config.in` and
change the `BR2_HOSTBOOT_VERSION` default value to your revision. As a last
step, remove the previous hostboot build result in
`output/build/hostboot-<revision>` and invoke the build again with:

`./op-build talos_defconfig && ./op-build`

> Note, some packages may have custom versions enabled in the config. Check the
> `output/.config` file and then change the revision in the defconfig file in
> `openpower/configs/talos_defconfig`: `BR2_<package>_CUSTOM_VERSION_VALUE`.

The result full PNOR image will be placed in `output/images/talos.pnor`.

## Remote debugging

The most obvious way of debugging is serial port. See [Enabling console](#enabling-console)
for information about how it was implemented in coreboot. You can either use a
physical port or BMC to collect the output. The GUI version for Serial over LAN
console does not flush the output after a new line character, so it sometimes
trims a few last characters. It also doesn't support copying or pasting and has
problems with some ANSI escape codes. `obmc-console-client` run from the BMC
shell is more reliable - it simply redirects serial connection to the shell. To
exit from this tool use `<Return>~~.`. [Readme](https://github.com/openbmc/obmc-console)
tells to use `~.`, but this shuts down the entire SSH connection to the BMC.

Istep number is sent to ports 0x81 (major) and 0x82 (minor). These ports can be
read with following commands:
`cat /dev/aspeed-lpc-snoop0 | hexdump -e '/1 "%02x\n"' -v` for port 0x81 and
`cat /dev/aspeed-lpc-snoop1 | hexdump -e '/1 "%02x\n"' -v` for port 0x82. One
may change the printf formatting to decimal for example with `%02d` in the
hexdump expression. Currently coreboot does not implement reporting isteps yet,
but this can be used to debug earlier stages - SBE or HBBL. Those do not print
on serial console by default.

[pdbg](https://github.com/open-power/pdbg) is another powerful debugging tool.
It can be used to read and write SCOM registers, thread registers (both general
purpose and SPRs) and even modify contents of RAM. Most of these commands
require that the threads are stopped, which can be done with `pdbg -P thread stop`.
For other uses refer to tool's README.

After a checkstop or when a watchdog timeout occurs the platform automatically
reboots up to 3 times (so there are 4 attempts in total, this is the maximum
number of reboots required for successful full PNOR + SEEPROM update) before it
is left running (with reported power operation error). This may interfere and
delay debugging, especially for `pdbg`. It can be turned off with:

```shell
busctl set-property xyz.openbmc_project.Settings /xyz/openbmc_project/control/host0/auto_reboot xyz.openbmc_project.Control.Boot.RebootPolicy AutoReboot b false
```

Change `false` to `true` to re-enable this feature. [Source](https://github.com/openbmc/openbmc/issues/2177)

## QEMU

First of all, relatively new version of QEMU (5.0.0 or newer, tested on 5.1.0)
must be used. Default in Ubuntu is 2.11.1, which is almost 3 years old and does
not include support for `hb-mode`.

`hb-mode` is supposed to start firmware image in the same state that Hostboot is
started, i.e. with HRMOR=128MB, CIA=0, however it uses CIA=0x10 instead. This
can be temporarily fixed by placing 4 nop instructions at the very beginning of
bootblock. There is no way to start in a mode similar to that of Hostboot
Bootloader (HRMOR=130MB, CIA=0x3000).

HRMOR dumped with `pdbg` on the hardware is `0xf8000000`, i.e. 4GB - 128MB (+2MB
for HBBL), despite what all of the documentation says. This shouldn't make any
difference, all important addresses are calculated relatively to HRMOR anyway.

It is possible to "mount" whole PNOR image at the place where it should land in
memory space (LPC FW region), assuming the image is exactly 64MB. On the
hardware it is mounted so that the end of PNOR is at the end of FW region (at
at address 0x60300FFFFFFFF), on QEMU it is always mounted beginning at
0x60300FC000000, no matter what the size of image actually is. There is no SBE
in QEMU, so the easiest way to enter HB-like state is to use `hb-mode` and pass
the bootblock image separately, in addition to PNOR:

```shell
./qemu-system-ppc64 -M powernv,hb-mode=on --cpu power9 \
   --bios 'path/to/bootblock.bin' \
   --drive file='path/to/flash.pnor',if=mtd
```

Prepared specially for QEMU `bootblock.bin` may include a code that would put
the normal part of bootblock on its final place and enter the same state as
HBBL has before jumping to it, but given that we already almost run out of
things that can be tested on QEMU, this may no longer be necessary.

It is a good idea to include `-d unimp,guest_errors` in the command line, this
will help to spot accesses to unimplemented parts of hardware, like SPRs or IO
ports.

Bellow are step by step instructions, how to run coreboot on QEMU ppc64 version.

1. Clone the QEMU repository
   ```
   git clone git@github.com:qemu/qemu.git
   # or HTTPS alternatively
   git clone https://github.com/qemu/qemu.git
   ```
2. Build the QEMU ppc64 version
   ```
   cd qemu
    ./configure --target-list=ppc64-softmmu && make
   ```
3. Start QEMU with coreboot image
   ````
   ./qemu/build/qemu-system-ppc64 -M powernv,hb-mode=on --cpu power9 --bios 'coreboot/build/coreboot.rom' -d unimp,guest_errors -serial stdio -drive file=flash.pnor,format=raw,readonly=on,if=mtd
   ````

## Assumptions about target PNOR image

SBE loads HBBL, so unless we choose to modify SBE code we should leave this
partition in the state it originally was, i.e. it should have the same size,
enabled ECC and code in this partition should expect to be started with
HRMOR=130MB, CIA=0x3000. This should be a coreboot's bootblock, prepared in such
way that it properly finds PNOR partition containing CBFS with further stages.

All other HB\* partitions won't be longer necessary. Unfortunately, they are not
continuous in the PNOR, some of the other partitions would have to be moved
around to maximize amount of available space.

CBFS may be put in one PNOR partition, there are no gains from splitting it into
smaller pieces. It may or may not have ECC enabled. Having ECC enabled will
require a bit more work for gluing together coreboot and FFS, but the code is
already available in `3rdparty/ffs` repository.

For compatibility with existing Skiboot payload, it can be loaded from a PNOR
partition for the time being.

# Porting workflow

Always remember that on BE bits are numbered starting with MSB. That means bit 0
is the most significant bit, or `0x8000000000000000`. Variables shorter than 64b
do not start from 0, e.g. `uint32_t` has bits 32:63, `uint16_t` - 48:63. Those
bit numbers are used in documentation.

One of the implications is that writing `uint8_t = 0x12` and `uint64_t = 0x12`
to any given address have different results. Always double check size of
variable, on LE in many cases you could get away with different sizes, on BE you
can't.

## FAPI2

Among other things, FAPI2 implements `buffer` template in `/src/import/hwpf/fapi2/include/buffer.H`.
It is commonly used for preparing and parsing data send to/from SCOM. Contrary
to the registers, here buffer of type `uint16_t` uses bits numbered 0:15, and
similarly for different sizes. This may cause confusion, which they tried to
avoid by creating more methods for `buffer`.

These are the most commonly used methods:

* setBit<N> - set bit N (counting from left)
  - setBit<N, C> - set all bits from N to N+C-1, counting from left
  - setBit(N, C=1)
* clearBit<N> - clear bit N (counting from left)
  - clearBit<N, C> - as above
  - clearBit(N, C=1)
* getBit<N> - get value of bit N (counting from left)
  - getBit<N, C> - returns `true` if _any_ of the bits is set, false otherwise
  - getBit(N, C=1)
* flush<0 or 1> - sets buffer to 0 or ~0, respectively
* invert() - returns ~buffer
* A.insert<TS, L, SS>(B),\
A.insert(B, TS, L, SS):

```
M - bit size of A (-1)
N - bit size of B (-1)

           T
     012...S.....M
    |aaaaaaaaaaaaa|    A before

              L
             / \
             S |
         012.S...N|
        |bbbbXYZbb|    B
            ///
           ///
           |||
           T
     012...S     M|
    |aaaaaaXYZaaaa|    A after
```

* A.insertFromRight<TS, L>(B) = A.insert<TS, L, N-L>(B),\
A.insertFromRight(B, TS, L):

```
M - bit size of A (-1)
N - bit size of B (-1)

           T
     012...S.....M
    |aaaaaaaaaaaaa|    A before

                L
               / \
               | |
         012.....N|
        |bbbbbbXYZ|    B
               ///
              ///
             ///
            ///
           ///
           |||
           T
     012...S     M|
    |aaaaaaXYZaaaa|    A after
```

* A.extract<SS, L, TS>(B) = B.insert<TS, L, SS>(A):\
A.extract(B, SS, L) // TS = 0

```
M - bit size of A (-1)
N - bit size of B (-1)


              L
             / \
             T |
         012.S...N|
        |bbbbbbbbb|    B before

            L
           / \
           S |
     012...S.....M
    |aaaaaaXYZaaaa|    A
            \\\
             \\\
             |||
             T
         012.S...N|
        |bbbbXYZbb|    B after
```

* A.extractToRight<SS, L>(B) = A.extract<SS, L, N-L>(B),\
A.extractToRight(B, SS, L):

```
M - bit size of A (-1)
N - bit size of B (-1)


                L
               / \
               | |
         012.....N|
        |bbbbbbbbb|    B before

            L
           / \
           S |
     012...S.....M
    |aaaaaaXYZaaaa|    A
            \\\
             \\\
              \\\
               \\\
               |||
         012.....N|
        |bbbbbbXYZ|    B after
```

## Code flow analysis

Generally boot process follows [IPL flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf).
There may be some loops or reboots along the way which should be clearly noted
in that document. Hostboot covers isteps 6.* to 21.*, included. In most cases,
a Jira task covers a whole major istep number, sometimes more than one.

Our main goal is to enable Talos2 mainboard, so for the time being we can ignore
code which does not apply to this platform. This platform is:

* Nimbus (not Cumulus or Axone) - `ATTR_NAME == 0x5` in FAPI2
* Sforza (not Morza or LaGrange)
* Scale-Out (not Scale-Up)
* PowerNV SMT4 (not PowerVM SMT8)
* directly attached memory (not through Centaurs or OMI)
* has BMC (not SP-less, usually not FSP but sometimes BMC is treated as FSP)
* DD 2.3, but we should support other 2.* revisions too - `ATTR_EC == 0x20-0x23`

Above conditions are often checked by functions from `/src/import/chips/p9/procedures/hwp/memory/lib/mss_attribute_accessors_manual.H`
(may be different for other systems than memory), which check attributes defined
in `/src/import/chips/p9/procedures/xml/attribute_info/chip_ec_attributes.xml`
and perhaps other XMLs in this directory. Checking these attributes may help
with reducing amount of code to be reviewed (and consequently time) early on.

Except for few earliest (environment setup) and latest (hand-off to payload)
isteps, their entry points are located in `/src/user/isteps/istep<major number>/call_<istep name>.C`.
If there is no such file, check in `/src/include/usr/isteps/istep<major number>list.H`
for a function name for given istep.

Code in `/src/user/isteps/` consists mainly of debug info, exception handling
and, if the step has to be run on multiple cores/controllers/different parts of
hardware, a top level loop calling a platform-specific function. Those functions
can be found somewhere under `/src/import/chips/p9/procedures/hwp/`, in general.
The IPL document usually lists the name(s) of file with the entry point for a
given platform, but sometimes code from additional files is used.

Pointers to all relevant functions in a particular istep are saved in the array
in `src/include/usr/isteps/istepXXlist.H`
then the array of each step is combined into one big array in `src/include/usr/isteps/istepmasterlist.H`
which is iterated in `src/usr/initservice/istepdispatcher/istepdispatcher.C`

> The following paragraph was written after initial research of memory init code
> only, it may or may not be similar to other tasks.

After discarding all debug and exception checks, the "really working" parts of
code are almost only calls to `fapi2::putScom(tgt_chiplet, REG_NAME, l_data64)`,
where `tgt_chiplet` is a (number? ID? handle?) of current chiplet (e.g. memory
controller), `REG_NAME` is a constant holding SCOM address of a register and
`l_data64` is a buffer object holding value that is written to that register.
The buffer is filled by previous calls to methods listed in [FAPI2 section](#fapi2)
with constant data, with rare exceptions where it is set to value depending on
some attribute retrieved with `FAPI_ATTR_GET()` macro.

> TODO: check where those attributes originally come from.

I suggest writing down all the ports and values written to them, with `if`s
where applicable, with a short comment as to what that write does (it can be
copied from debug functions), without all the bloat of a dozen function calls
before the actual write takes place. Example for [p9_sbe_common_align_chiplets](https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/perv/p9_sbe_common.C#L141):

```
p9_sbe_common_align_chiplets:
  - For all chiplets: exit flush
    PERV_CPLT_CTRL0_OR = 0x8..0 >> PERV_1_CPLT_CTRL0_CTRL_CC_FLUSHMODE_INH_DC    // 0x2..0
  - For all chiplets: enable alignement
    PERV_CPLT_CTRL0_OR = 0x8..0 >> PERV_1_CPLT_CTRL0_CTRL_CC_FORCE_ALIGN_DC      // 0x1..0
  - Clear chiplet is aligned
    PERV_SYNC_CONFIG |= 0x8..0 >> PERV_1_SYNC_CONFIG_CLEAR_CHIPLET_IS_ALIGNED    // 0x01..0
  - Unset Clear chiplet is aligned
    PERV_SYNC_CONFIG &= ~(0x8..0 >> PERV_1_SYNC_CONFIG_CLEAR_CHIPLET_IS_ALIGNED) // 0x01..0
  delay(100us)
  - Poll OPCG done bit to check for run-N completeness
    timeout(10*100us):
      if (PERV_CPLT_STAT0 & (0x8..0 >> PERV_1_CPLT_STAT0_CC_CTRL_CHIPLET_IS_ALIGNED_DC) == 1) break    // 0x8..0 >> 9
      delay(100us)
  - For all chiplets: disable alignement
    PERV_CPLT_CTRL0_CLEAR = 0x8..0 >> PERV_1_CPLT_CTRL0_CTRL_CC_FORCE_ALIGN_DC   // 0x1..0
```

### Implementing op-build support for coreboot

Op-build system uses buidroot to create a full PNOR image. It contains a set of
packages to build a working system. It has been leveraged to build kernel for
skiroot. The overview of talos-op-build repository is as follows:

```shell
.
├── buildroot
├── ci
├── CONTRIBUTING.md
├── dl
├── doc
├── LICENSE
├── NOTICE
├── op-build
├── op-build-env
├── openpower
├── output
└── README.md
```

`buildroot` directory is a buildroot submodule. It contains basic packages.
Custom packages are put into `openpower` directory.

The plan assumed following implementation:

1. Hostboot bootloader partition (HBBL) remains unchanged. We cannot put
   bootblock there yet, because it HBBL contains a 9K SECROM, which limits the
   bootblock size to 11K (too small to fit all compiled source code). We would
   have to modify the layout and/or SBE code that launched HBBL for coreboot.
2. We remove hostboot partitions which are contiguous in the PNOR layout. That
   is: HBD and HBI. It gives u 16MB of space for coreboot (+4K for SB header)
   and almost 8MB of unused space (it has been marked as unused in order to
   keep the chanegs to minimum). We put coreboot's bootblock there in the same
   format as HBB.
3. In the newly create partition called COREBOOT we put the coreboot CBFS
   without bootblock.

> In the future maybe we could leave the default layout and simply replace HBB
> with bootblock and HBI with coreboot without layout change.

Let's see how openpower packages look like:

```shell
.
├── Config.in
├── configs
├── custom
├── device_table.txt
├── external.desc
├── external.mk
├── linux
├── overlay
├── package
├── patches
├── platform
└── scripts
```

`configs` directory contains config for whole build and/or single packages.
`package` directory contains package definitions for openpower specific
software:

```shell
.
├── common-p8-xml
│   ├── common-p8-xml.mk
│   └── Config.in
├── Config.in
├── coreboot
│   ├── Config.in
│   └── coreboot.mk
├── hcode
│   ├── Config.in
│   └── hcode.mk
├── hostboot
│   ├── Config.in
│   └── hostboot.mk
├── hostboot-p8
│   ├── 0001-Increase-uart-delay.patch
│   ├── 0002-GCC-4.9-Make-compiler-use-ELFv1-ABI.patch
│   ├── 0003-Default-to-std-gnu-03.patch
│   ├── 0004-fix-build-error-return-statement-with-a-value-in-fun.patch
│   ├── 0005-error-dereferencing-type-punned-pointer-will-break-s.patch
│   ├── 0006-Change-cv_forcedMemPeriodic-to-uint8_t-as-bool-is-in.patch
│   ├── 0007-error-the-compiler-can-assume-that-the-address-of-r-.patch
│   ├── 0008-Fix-compiler-can-assume-address-will-never-be-NULL-e.patch
│   ├── 0009-error-in-C-98-l_vmVersionBuf-must-be-initialized-by-.patch
│   ├── 0010-Use-std-gnu-03-for-host-g-invocations.patch
│   ├── 0012-kernel-Update-assembly-for-modern-binutils.patch
│   ├── Config.in
│   └── hostboot-p8.mk
├── ima-catalog
│   ├── Config.in
│   └── ima-catalog.mk
├── libflash
│   ├── Config.in
│   └── libflash.mk
├── loadkeys
│   ├── backtab-keymap
│   ├── Config.in
│   ├── loadkeys.hash
│   ├── loadkeys.mk
│   └── S16-keymap
├── machine-xml
│   ├── Config.in
│   └── machine-xml.mk
├── occ
│   ├── Config.in
│   └── occ.mk
├── occ-p8
│   ├── Config.in
│   └── occ-p8.mk
├── openpower-ffs
│   ├── Config.in
│   └── openpower-ffs.mk
├── openpower-pnor
│   ├── Config.in
│   └── openpower-pnor.mk
├── openpower-pnor-util
│   └── openpower-pnor-util.mk
├── p8-pore-binutils
│   ├── Config.in
│   └── p8-pore-binutils.mk
├── petitboot
│   ├── 63-md-raid-arrays.rules
│   ├── 65-md-incremental.rules
│   ├── 66-add-sg-module.rules
│   ├── Config.in
│   ├── kexec-restart
│   ├── petitboot-01-autotools-Add-autopoint-generated-files.patch
│   ├── petitboot-console-ui.rules
│   ├── petitboot.mk
│   ├── removable-event-poll.rules
│   ├── S14silence-console
│   ├── S15pb-discover
│   ├── shell_config
│   └── shell_profile
├── pkg-versions.mk
├── pnv-lpc
│   ├── Config.in
│   └── pnv-lpc.mk
├── ppe42-binutils
│   ├── Config.in
│   └── ppe42-binutils.mk
├── ppe42-gcc
│   ├── 0001-2016-02-19-Jakub-Jelinek-jakub-redhat.com.patch
│   ├── Config.in
│   └── ppe42-gcc.mk
├── sbe
│   ├── Config.in
│   └── sbe.mk
├── sb-signing-framework
│   ├── Config.in
│   └── sb-signing-framework.mk
├── sb-signing-utils
│   ├── Config.in
│   ├── keys
│   └── sb-signing-utils.mk
├── skiboot
│   ├── Config.in
│   └── skiboot.mk
└── VERSION.readme
```

As you can see each package contains an `*.mk` and `Config.in` file. There is
also a main `Config.in` file in the `package` directory. In order to add new
package, edit the main `Config.in` file by adding and include for package
specific `Config.in`. Example:

```conf
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/openpower-ffs/Config.in"
+ source "$BR2_EXTERNAL_OP_BUILD_PATH/package/coreboot/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/hostboot/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/common-p8-xml/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/machine-xml/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/openpower-pnor/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/petitboot/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/p8-pore-binutils/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/hcode/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/occ/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/hcode/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/skiboot/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/libflash/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/pnv-lpc/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/loadkeys/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/ppe42-binutils/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/ppe42-gcc/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/ima-catalog/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/sbe/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/sb-signing-utils/Config.in"
source "$BR2_EXTERNAL_OP_BUILD_PATH/package/sb-signing-framework/Config.in"
```

The package specific `Config.in` is almost like a Kconfig system. It defines
various configuration values for package, like revision. Sample file for
coreboot:

```conf
config BR2_PACKAGE_COREBOOT
        bool "coreboot"
        default y if (BR2_OPENPOWER_POWER9)
        select BR2_CPP
        help
            Project to build the coreboot firmware codebase

if BR2_PACKAGE_COREBOOT

choice
	prompt "coreboot version"
	default BR2_COREBOOT_LATEST_VERSION

config BR2_COREBOOT_LATEST_VERSION
	bool "Use latest coreboot"

config BR2_COREBOOT_CUSTOM_VERSION
	bool "Custom version"

endchoice

config BR2_COREBOOT_CUSTOM_VERSION_VALUE
	string "coreboot version"
	depends on BR2_COREBOOT_CUSTOM_VERSION

config BR2_COREBOOT_VERSION
	string
	default "368873988439adcb8307bd73007d263b0317c282" if BR2_COREBOOT_LATEST_VERSION
	default BR2_COREBOOT_CUSTOM_VERSION_VALUE \
		if BR2_COREBOOT_CUSTOM_VERSION

config BR2_COREBOOT_CONFIG_FILE
        string "coreboot configuration file for compilation"
        default default
        help
            String used to define hw specific make config file

endif
```

The `*.mk` file defines operations which should be conducted during the build
of package. I.e. how source should be downloaded, where from, how package
should be built and installed. Example `coreboot.mk` file:

```makefile
################################################################################
#
# coreboot for POWER9
#
################################################################################
COREBOOT_VERSION = $(call qstrip,$(BR2_COREBOOT_VERSION))
COREBOOT_SITE = https://github.com/3mdeb/coreboot.git
COREBOOT_SITE_METHOD = git
COREBOOT_GIT_SUBMODULES = YES

COREBOOT_LICENSE = GPLv2
COREBOOT_LICENSE_FILES = LICENSE
COREBOOT_DEPENDENCIES = host-binutils host-libopenssl

COREBOOT_INSTALL_IMAGES = YES
COREBOOT_INSTALL_TARGET = NO

COREBOOT_ENV_VARS = $(TARGET_MAKE_ENV)
COREBOOT_ENV_VARS += \
	PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
	PKG_CONFIG_SYSROOT_DIR="/" \
	PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 \
	PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 \
	PKG_CONFIG_LIBDIR="$(HOST_DIR)/lib/pkgconfig:$(HOST_DIR)/share/pkgconfig"

define COREBOOT_BUILD_CMDS
    $(COREBOOT_ENV_VARS) bash -c 'cd $(@D) && $(MAKE) crossgcc-ppc64 CPUS=8 && cp configs/$(call qstrip,$(BR2_COREBOOT_CONFIG_FILE)) .config && $(MAKE) olddefconfig && $(MAKE) V=1'
endef

define COREBOOT_INSTALL_IMAGES_CMDS
	mkdir -p $(STAGING_DIR)/coreboot_build_images/ && \
    cd $(@D) &&	cp build/coreboot.rom.signed  $(STAGING_DIR)/coreboot_build_images/ && \
	cp build/bootblock.signed.ecc  $(STAGING_DIR)/coreboot_build_images/
endef

$(eval $(generic-package))
```

Typically each variable in package `*/.mk` file starts with uppercase name of
the package. The `*.mk` file itself must be named as the package. Let's review
a few variables:

- `COREBOOT_VERSION` typically a SHA of the commit
- `COREBOOT_SITE` URL to repo or site where to download
- `COREBOOT_SITE_METHOD` defines the method to use for download
- `COREBOOT_GIT_SUBMODULES` tell buildroot to download submodules
- `COREBOOT_LICENSE` update the license if needed
- `COREBOOT_DEPENDENCIES`defines the dependencies of the package
- `COREBOOT_INSTALL_IMAGES` installs only ready images
- `COREBOOT_INSTALL_TARGET` install to the target rootfs
- `COREBOOT_ENV_VARS` environmental variables for build, by default should
  contain at least basic variables from buildroot (`TARGET_MAKE_ENV`)
- `COREBOOT_BUILD_CMDS` specify the command invoked to build the package
- `COREBOOT_INSTALL_IMAGES_CMDS` specify the command invoked to install the
  package

Basically coreboot is built form the cloned repo, preceded with crossgcc build.
The images are copied to the location where are later consumer by PNOR build
scripts.

The `talos_defconfig` has been updated to point to correct coreboot config file:

```conf
...
BR2_OPENPOWER_POWER9=y
BR2_HOSTBOOT_CONFIG_FILE="talos.config"
+ BR2_COREBOOT_CONFIG_FILE="config.raptor-cs-talos-2"
BR2_OPENPOWER_MACHINE_XML_GITHUB_PROJECT_VALUE="talos-xml"
BR2_OPENPOWER_MACHINE_XML_VERSION="cbd11e9450325378043069d7e638668ea26c2074"
...
```

So invoking the build by `./op-build talos_defconfig && ./op-build` will work as
usual. Repo to use is: https://github.com/3mdeb/talos-op-build/tree/coreboot_support

### New layout and PNOR image stitching

The repository responsible for stitching whole image according to layout is the
PNOR repo: https://github.com/3mdeb/pnor/tree/coreboot_support

Let's take a look at how the repo is organized:

```shell
.
├── create_pnor_image.pl
├── LICENSE
├── NOTICE
├── p8Layouts
│   ├── defaultPnorLayoutSingleSide.xml
│   ├── defaultPnorLayoutWithGoldenSide.xml
│   └── defaultPnorLayoutWithoutGoldenSide.xml
├── p9Layouts
│   └── defaultPnorLayout_64.xml
└── update_image.pl
```

Since Talos II is a POWER9 platform, it uses `p9Layouts`. The XML files contains
the description of partitions and their attributes. The PERL scripts
`update_image.pl` and `create_pnor_image.pl` are invoked by buildroot system to
stitch whole PNOR image.

Let's take a look at `defaultPnorLayout_64.xml` fragment:

```xml
    <metadata>
        <imageSize>0x4000000</imageSize>
        <chipSize>0x4000000</chipSize>
        <blockSize>0x1000</blockSize>
        <tocSize>0x8000</tocSize>
        <arrangement>A-D-B</arrangement>
        <side>
            <id>A</id>
        </side>
    </metadata>
    <section>
        <description>Hostboot Base (1M)</description>
        <eyeCatch>HBB</eyeCatch>
        <physicalOffset>0x205000</physicalOffset>
        <physicalRegionSize>0x100000</physicalRegionSize>
        <side>A</side>
        <sha512Version/>
        <readOnly/>
        <ecc/>
    </section>
    <section>
        <description>coreboot image 16MB (+4K for SB header)</description>
        <eyeCatch>COREBOOT</eyeCatch>
        <physicalOffset>0x305000</physicalOffset>
        <physicalRegionSize>0x1001000</physicalRegionSize>
        <side>A</side>
        <sha512Version/>
        <readOnly/>
    </section>
    <section>
        <description>Unused pad partition</description>
        <eyeCatch>UNUSED</eyeCatch>
        <physicalOffset>0x1306000</physicalOffset>
        <physicalRegionSize>0x6BF000</physicalRegionSize>
        <side>A</side>
        <preserved/>
        <readOnly/>
    </section>
    <section>
        <description>SBE-IPL (Staging Area) (520K)</description>
        <eyeCatch>SBE</eyeCatch>
        <physicalOffset>0x19C5000</physicalOffset>
        <physicalRegionSize>0xBC000</physicalRegionSize>
        <side>A</side>
        <sha512Version/>
        <sha512perEC/>
        <readOnly/>
        <ecc/>
    </section>
```

The definition starts with metadata which contains basic information about
PNOR: chip size, side, block size, TOC size. Then the partitions are described
using `section` marks. Each section has its own description as a human readable
string and an `eyeCatch` a partition label-like string. Each of the partitions
must specify the offset from the start of PNOR and its size. Next partition
must start from `<previous_partition_offset> + <previous_partition_size>` in
hex. In the example above, we put the coreboot partition to have 16MB +4K and
without ECC (ECC protected partitions have `<ecc/>` mark inside the section).

If the layout is ready, we must update the PERL scripts to include our coreboot
image in the PNOR. Each of the scripts take a variable containing path to the
images:

- the declaration of variable

  ```perl
  my $release = "";
  my $op_target_dir = "";
  my $hb_image_dir = "";
  + my $cb_image_dir = "";
  my $scratch_dir = "";
  my $hb_binary_dir = "";
  my $hcode_dir = "";
  ```

- the arg parsing

  ```perl
    elsif (/^-scratch_dir/i){
        $scratch_dir = $ARGV[1] or die "Bad command line arg given: expecting a config type.\n";
        shift;
    }
    elsif (/^-hb_binary_dir/i){
        $hb_binary_dir = $ARGV[1] or die "Bad command line arg given: expecting a config type.\n";
        shift;
    }
    elsif (/^-cb_image_dir/i){
        $cb_image_dir = $ARGV[1] or die "Bad command line arg given: expecting a config type.\n";
        shift;
    }
    elsif (/^-hcode_dir/i){
        $hcode_dir = $ARGV[1] or die "Bad command line arg given: expecting a config type.\n";
        shift;
    }
  ```

This path is later used to locate the images, for example:

- in `create_pnor_image.pl`:

  ```perl
  $build_pnor_command .= " --binFile_SBE $scratch_dir/$sbe_binary_filename";
  $build_pnor_command .= " --binFile_HBB $scratch_dir/bootblock.header.bin.ecc";
  #$build_pnor_command .= " --binFile_HBI $scratch_dir/hostboot_extended.header.bin.ecc";
  $build_pnor_command .= " --binFile_COREBOOT $scratch_dir/coreboot.rom.signed";
  $build_pnor_command .= " --binFile_HBRT $scratch_dir/hostboot_runtime.header.bin.ecc";
  $build_pnor_command .= " --binFile_HBEL $scratch_dir/hbel.bin.ecc";
  $build_pnor_command .= " --binFile_GUARD $scratch_dir/guard.bin.ecc";
  ```

- in `update_image.pl`:

  ```perl
  my %sections=();
  $sections{HBBL}{in}         = "$scratch_dir/hbbl.bin";
  $sections{HBBL}{out}        = "$scratch_dir/hbbl.bin.ecc";
  #$sections{HBB}{in}          = "$hb_image_dir/img/hostboot.bin";
  #$sections{HBB}{out}         = "$scratch_dir/hostboot.header.bin.ecc";
  #$sections{HBI}{in}          = "$hb_image_dir/img/hostboot_extended.bin";
  #$sections{HBI}{out}         = "$scratch_dir/hostboot_extended.header.bin.ecc";
  #$sections{HBD}{in}          = "$op_target_dir/$targeting_binary_source";
  #$sections{HBD}{out}         = "$scratch_dir/$targeting_binary_filename";
  $sections{HBB}{in}          = "$cb_image_dir/bootblock.bin";
  $sections{HBB}{out}         = "$scratch_dir/bootblock.header.bin.ecc";
  $sections{COREBOOT}{in}     = "$cb_image_dir/coreboot.rom";
  $sections{COREBOOT}{out}    = "$scratch_dir/coreboot.rom.signed";
  $sections{SBE}{in}          = "$sbePreEcc";
  $sections{SBE}{out}         = "$scratch_dir/$sbe_binary_filename";
  $sections{PAYLOAD}{in}      = "$payload.bin";
  $sections{PAYLOAD}{out}     = "$scratch_dir/$payload_filename";
  $sections{HCODE}{in}        = "$hcode_dir/${stop_basename}.bin";
  $sections{HCODE}{out}       = "$scratch_dir/${stop_basename}.hdr.bin.ecc";
  ```

  The sections variable defines input and output files for each partition. In
  case of coreboot we define only output files, because we build
  ready-to-include coreboot images.

Unfortunately these scripts assume the file names to be fixed and all paths to
be passed to the script. You have to keep it in sync with pnor package in
buildroot. The `openpower/package/openpower-pnor/openpower-pnor.mk` in
`talos-op-build` is responsible for invoking the scripts:

```makefile
HOSTBOOT_IMAGE_DIR=$(STAGING_DIR)/hostboot_build_images/
+ COREBOOT_IMAGE_DIR=$(STAGING_DIR)/coreboot_build_images/
HOSTBOOT_BINARY_DIR = $(STAGING_DIR)/hostboot_binaries/
HCODE_STAGING_DIR = $(STAGING_DIR)/hcode/
SBE_BINARY_DIR = $(STAGING_DIR)/sbe_binaries/
OPENPOWER_PNOR_SCRATCH_DIR = $(STAGING_DIR)/openpower_pnor_scratch/
OPENPOWER_VERSION_DIR = $(STAGING_DIR)/openpower_version
OPENPOWER_MRW_SCRATCH_DIR = $(STAGING_DIR)/openpower_mrw_scratch
OUTPUT_BUILD_DIR = $(STAGING_DIR)/../../../build/
OUTPUT_IMAGES_DIR = $(STAGING_DIR)/../../../images/
HOSTBOOT_BUILD_IMAGES_DIR = $(STAGING_DIR)/hostboot_build_images/
```

Note we define the variable with path to coreboot images, where we previously
installed our built images. Then we invoke the scripts:

```makefile
define OPENPOWER_PNOR_INSTALL_IMAGES_CMDS
        mkdir -p $(OPENPOWER_PNOR_SCRATCH_DIR)

        $(TARGET_MAKE_ENV) $(@D)/update_image.pl \
            -release  $(OPENPOWER_RELEASE) \
            -op_target_dir $(HOSTBOOT_IMAGE_DIR) \
            -hb_image_dir $(HOSTBOOT_IMAGE_DIR) \
+            -cb_image_dir $(COREBOOT_IMAGE_DIR) \
            -scratch_dir $(OPENPOWER_PNOR_SCRATCH_DIR) \
            -hb_binary_dir $(HOSTBOOT_BINARY_DIR) \
            -hcode_dir $(HCODE_STAGING_DIR) \
            -targeting_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME) \
            -targeting_binary_source $(BR2_OPENPOWER_TARGETING_BIN_FILENAME) \
            -targeting_RO_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME).protected \
            -targeting_RO_binary_source $(BR2_OPENPOWER_TARGETING_BIN_FILENAME).protected \
            -targeting_RW_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME).unprotected \
            -targeting_RW_binary_source $(BR2_OPENPOWER_TARGETING_BIN_FILENAME).unprotected \
            -sbe_binary_filename $(BR2_HOSTBOOT_BINARY_SBE_FILENAME) \
            -sbe_binary_dir $(SBE_BINARY_DIR) \
            -sbec_binary_filename $(BR2_HOSTBOOT_BINARY_SBEC_FILENAME) \
            -wink_binary_filename $(BR2_HOSTBOOT_BINARY_WINK_FILENAME) \
            -occ_binary_filename $(OCC_STAGING_DIR)/$(BR2_OCC_BIN_FILENAME) \
            -ima_catalog_binary_filename $(BINARIES_DIR)/$(BR2_IMA_CATALOG_FILENAME) \
            -openpower_version_filename $(OPENPOWER_PNOR_VERSION_FILE) \
            -wof_binary_filename $(OPENPOWER_MRW_SCRATCH_DIR)/$(BR2_WOFDATA_FILENAME) \
            -memd_binary_filename $(OPENPOWER_MRW_SCRATCH_DIR)/$(BR2_MEMDDATA_FILENAME) \
            -payload $(BINARIES_DIR)/$(BR2_SKIBOOT_LID_NAME) \
            -payload_filename $(BR2_SKIBOOT_LID_XZ_NAME) \
            -binary_dir $(BINARIES_DIR) \
            -bootkernel_filename $(LINUX_IMAGE_NAME) \
            -pnor_layout $(@D)/"$(OPENPOWER_RELEASE)"Layouts/$(BR2_OPENPOWER_PNOR_XML_LAYOUT_FILENAME) \
            $(XZ_ARG) $(KEY_TRANSITION_ARG) $(SIGN_MODE_ARG) \

        mkdir -p $(STAGING_DIR)/pnor/
        $(TARGET_MAKE_ENV) $(@D)/create_pnor_image.pl \
            -release $(OPENPOWER_RELEASE) \
            -xml_layout_file $(@D)/"$(OPENPOWER_RELEASE)"Layouts/$(BR2_OPENPOWER_PNOR_XML_LAYOUT_FILENAME) \
            -pnor_filename $(STAGING_DIR)/pnor/$(BR2_OPENPOWER_PNOR_FILENAME) \
            -hb_image_dir $(HOSTBOOT_IMAGE_DIR) \
+            -cb_image_dir $(COREBOOT_IMAGE_DIR) \
            -scratch_dir $(OPENPOWER_PNOR_SCRATCH_DIR) \
            -outdir $(STAGING_DIR)/pnor/ \
            -payload $(OPENPOWER_PNOR_SCRATCH_DIR)/$(BR2_SKIBOOT_LID_XZ_NAME) \
            -bootkernel $(OPENPOWER_PNOR_SCRATCH_DIR)/$(LINUX_IMAGE_NAME) \
            -sbe_binary_filename $(BR2_HOSTBOOT_BINARY_SBE_FILENAME) \
            -sbec_binary_filename $(BR2_HOSTBOOT_BINARY_SBEC_FILENAME) \
            -wink_binary_filename $(BR2_HOSTBOOT_BINARY_WINK_FILENAME) \
            -occ_binary_filename $(OCC_STAGING_DIR)/$(OCC_BIN_FILENAME) \
            -targeting_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME) \
            -targeting_RO_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME).protected \
            -targeting_RW_binary_filename $(BR2_OPENPOWER_TARGETING_ECC_FILENAME).unprotected \
            -wofdata_binary_filename $(OPENPOWER_PNOR_SCRATCH_DIR)/$(BR2_WOFDATA_BINARY_FILENAME) \
            -memddata_binary_filename $(OPENPOWER_PNOR_SCRATCH_DIR)/$(BR2_MEMDDATA_BINARY_FILENAME) \
            -openpower_version_filename $(OPENPOWER_PNOR_SCRATCH_DIR)/openpower_pnor_version.bin

        $(INSTALL) $(STAGING_DIR)/pnor/$(BR2_OPENPOWER_PNOR_FILENAME) $(BINARIES_DIR)
...
```
Don't forget to bump the revisions of repositories you modified:

- `openpower/package/openpower-pnor/openpower-pnor.mk`:

  ```makefile
  OPENPOWER_PNOR_VERSION ?= 20fbc061db61c11a3812cd69e48f39ea755009eb
  OPENPOWER_PNOR_SITE ?= https://github.com/3mdeb/pnor.git
  OPENPOWER_PNOR_SITE_METHOD = git
  ```

- `openpower/package/coreboot/Config.in`:

  ```conf
  config BR2_COREBOOT_VERSION
    string
    default "368873988439adcb8307bd73007d263b0317c282" if BR2_COREBOOT_LATEST_VERSION
    default BR2_COREBOOT_CUSTOM_VERSION_VALUE \
      if BR2_COREBOOT_CUSTOM_VERSION
  ```
  Alternatively you may select `BR2_COREBOOT_CUSTOM_VERSION` and specify commit
  SHA as `BR2_COREBOOT_CUSTOM_VERSION_VALUE` in `talos_defconfig`.

Rebuild after the made changes with `./op-build talos_defconfig && ./op-build`. You may use
the container prepared for op-build: https://github.com/3mdeb/op-docker
