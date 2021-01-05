# Access to SCOM registers

SCOM registers are responsible for configuration of many platform settings.
To port hostboot into coreboot it is necessery to be able to access these registers.

* [Registers that could be used for tests](https://github.com/3mdeb/openpower-coreboot-docs/blob/main/devnotes/register_for_SCOM_tests.md)

According to information given on the [OpenPower-firmware mailing list](https://lists.ozlabs.org/pipermail/openpower-firmware/2020-December/000602.html),
`SCOM` can be accessed through `XSCOM` with base address `0x000603FC00000000`.

```
There are a few different methods to access SCOM registers depending on what
device its on and what point of the boot you are in.
The easiest way to access a scom register is from the primary processor over
something we call xscom. XSCOM essentially boils down to accessing an MMIO
address offset by the xscom bar passed to us by the SBE via some struct.
The default value for group0's XSCOM base addr is 0x000603FC00000000 which
is a pretty safe bet if you are having a hard time finding the bootloader
structure. Now this base address can change if the SBE sets it to something
else, or we end up in some memory swapping scenario, and its going to be
different when talking to secondary processors.
```
[Source](https://lists.ozlabs.org/pipermail/openpower-firmware/2020-December/000602.html)

## Failed SCOM read attempts

All attempts were made in the `coreboot` bootblock code.\
It was tested on QEMU version `QEMU emulator version 5.2.50 (v5.2.0-463-g657ee88ef3ec)` using following command line
```
./qemu-system-ppc64 -M powernv,hb-mode=on --cpu power9 --bios 'open-power/coreboot/build/coreboot.rom' -d unimp,guest_errors -serial stdio
```
HW tests were made on `Talos II` machine using BMC.

### XSCOM base: **0x800603FC00000000** register: **0xF000F**
Tested code:
```
long long unsigned int *reg = (void *)(0x800603FC00000000 | (0xF000F << 3));
printk(BIOS_EMERG, "SCOM:? %llX\n", *reg);
```
**Talos II**:\
`resets`\
**qemu**:
```
SCOM:? 220D104900008000
```

### XSCOM base: **0x800003FC00000000**, **0x800023FC00000000**
### or **0x800623FC00000000** register: **0xF000F**
Tested code:
```
long long unsigned int *reg = (void *)(0x800603FC00000000 | (0xF000F << 3));
printk(BIOS_EMERG, "SCOM:? %llX\n", *reg);
```
**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr 0x3FC000F0008, size 8, region '(null)', reason: rejected
Invalid access at addr 0x3FC000F0010, size 8, region '(null)', reason: rejected
```


### XSCOM base: **0x800603FC00000000** register: **0xF0010**, **0xF001F** or **0xF0020**
Tested code:
```
long long unsigned int *reg = (void *)(0x800603FC00000000 | 0xF0010 << 3);
printk(BIOS_EMERG, "SCOM:? %llX\n", *reg);
```
**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr <address>, size 8, region '(null)', reason: rejected
XSCOM read failed at @<address> pcba=0x0001e001
Invalid access at addr <address>, size 8, region '(null)', reason: rejected
XSCOM read failed at @<address> pcba=0x0001e002
```

### Assembly instruction **`ldcix`**
Tested code:
```
#include <arch/io.h>

uint64_t buffer;
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800623FC00000000ull), "r"(0xF000F));
eieio();
printk(BIOS_EMERG, "SCOM:? %llX\n", buffer);
```
**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr 0x623FC000F0008, size 8, region '(null)', reason: rejected
Invalid access at addr 0x623FC000F0010, size 8, region '(null)', reason: rejected
```

### Assembly instruction **`ldcix`** with bit shift
Tested code:
```
#include <arch/io.h>

uint64_t buffer;
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800623FC00000000ull), "r"(0xF000F << 3));
eieio();
printk(BIOS_EMERG, "SCOM:? %llX\n", buffer);
```
**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr 0x623FC00780078, size 8, region '(null)', reason: rejected
```

### Adding **`volatile`** keyword to variable type and **`ull`** suffix to register address
Code behavior was unchanged\
**Talos II**: `resets`\
**qemu**: Outputs information about invalid access to stdout

# Analysis of SCOM in hostboot code

To access `SCOM` `putScom()` or `getScom()` is used.
Early analysis of call chain resulted with following:

   [putScom(target, address, data)](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/include/usr/fapi2/hw_access.H#L119)
-> [platPutScom(target, address, data)](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/fapi2/plat_hw_access.C#L148)
-> [deviceWrite(target, data, size, DEVICE_SCOM_ADDRESS(address, opMode))](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/userif.C#L62)
-> [Singleton<Associator>::instance().performOp(WRITE, target, buffer, buflen, accessType, args)](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L161)
-> [findDeviceRoute(opType = WRITE, devType, accessType)(opType = WRITE, target, buffer, buflen, accessType, addr)](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L243)

before findDeviceRoute(), procedure has to be registerd using Associator.registerRoute()

[/src/usr/fsiscom/fsiscom.C:178](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/fsiscom/fsiscom.C#L178) fsiScomPerformOp() is probably a function responsible for reading/writing
