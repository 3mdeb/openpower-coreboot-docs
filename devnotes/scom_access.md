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

## How to access SCOM

To read register at offset 0xF000F:
```
uint64_t buffer;
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800603FC00000000), "r"(0xF000F << 3));
eieio();
printk(BIOS_EMERG, "SCOM:? %llX\n", buffer);
```
* 0x800603FC00000000 is the `XSCOM` base address.
* 0xF000F is the register address. Refer to POWER9_Registers_vol1\2\3 for more information
* `ldcix` performs cache inhibited read

To write SCOM register at offset 0xF0008:
```
printk(BIOS_EMERG, "hw trying to write 0xF0008\n");
uint64_t buffer;
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800603FC00000000), "r"(0xF0008 << 3));
eieio();
printk(BIOS_EMERG, "SCOM before: %llX\n", buffer);
asm volatile("stdcix %0, %1, %2" :: "b"(0xAAAAAAAAAAAAAAAA), "b"(0x800603FC00000000), "r"(0xF0008 << 3));
eieio();
printk(BIOS_EMERG, "just wrote new value\n");
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800603FC00000000), "r"(0xF0008 << 3));
eieio();
printk(BIOS_EMERG, "SCOM after: %llX\n", buffer);
```

## Failed SCOM read attempts during research

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
*`ldcix`* - Load Doubleword Caching Inhibited Indexed - loads doubleword from memory avoiding loading to cache.\
`0x800623FC00000000ull` base address is not working because it tagets processor with id 2. Base address should be `0x800603FC00000000`.\
\
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

[putScom()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/include/usr/fapi2/hw_access.H#L119)<br/>
-> [platPutScom()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/fapi2/plat_hw_access.C#L148)<br/>
-> [deviceWrite()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/userif.C#L62)<br/>
-> [Singleton<Associator>::instance().performOp()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L161)<br/>
-> [call procedure returned by findDeviceRoute()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L243)

before [findDeviceRoute()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L243), procedure has to be registerd using [Associator.registerRoute()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L55)

Call chain to register XSCOM procedure:

[Associator.registerRoute()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/associator.C#L55)<br/>
<- [DeviceFW_deviceRegisterRoute()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/driverif.C#L53)<br/>
<- [alias deviceRegisterRoute](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/driverif.C#L75)<br/>
<- [macro DEVICE_REGISTER_ROUTE](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/devicefw/driverif.H#L432)<br/>
<- [DEVICE_REGISTER_ROUTE()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/xscom/xscom.C#L69)<br/>
<- [called on xscomPerformOp()](https://github.com/open-power/hostboot/blob/a4af0cc2d6432eff344e28335560dd72409b4d50/src/usr/xscom/xscom.C#L665)

## Indirect form 1

Analysis assumptions:
* `#undef __HOSTBOOT_RUNTIME`
* secure boot checks aren't interesting

```cpp
// src/usr/scom/scom.C
errlHndl_t checkIndirectAndDoScom(DeviceFW::OperationType i_opType,
                                  TARGETING::Target* i_target,
                                  void* io_buffer,
                                  size_t& io_buflen,
                                  int64_t i_accessType,
                                  uint64_t i_addr)
{
    bool l_runIndirectLogic = (i_addr & 0x8000000000000000);
    if (!l_runIndirectLogic) {
        return doScomOp(i_opType,
                        i_target,
                        io_buffer,
                        io_buflen,
                        i_accessType,
                        i_addr);
    }

    //----------------------------------------------
    //---  Below here is the indirect scom logic ---

    // Bits 0:3 of the address hold the indirect and form bits
    // We shift out 60 bits to read the form bit here
    uint8_t form = (i_addr >> 60) & 1;

    // If the form is 0, we are using the "old" indirect scom method
    if (form == 0) {
        return doForm0IndirectScom(i_opType,
                                   i_target,
                                   io_buffer,
                                   io_buflen,
                                   i_accessType,
                                   i_addr);
        if (l_err) break;

    // If form is equal to 1, we are using new FBC method
    } else {
        return doForm1IndirectScom(i_opType,
                                   i_target,
                                   io_buffer,
                                   io_buflen,
                                   i_accessType,
                                   i_addr);
    }
}

errlHndl_t doScomOp(DeviceFW::OperationType i_opType,
                    TARGETING::Target* i_target,
                    void* io_buffer,
                    size_t& io_buflen,
                    int64_t i_accessType,
                    uint64_t i_addr);

errlHndl_t doForm0IndirectScom(DeviceFW::OperationType i_opType,
                               TARGETING::Target* i_target,
                               void* io_buffer,
                               size_t& io_buflen,
                               int64_t i_accessType,
                               uint64_t i_addr);

/*
 * data |= (addr(20:31) << 20);
 * addr = addr(32:63);
 */
errlHndl_t doForm1IndirectScom(DeviceFW::OperationType i_opType,
                               TARGETING::Target* i_target,
                               void* io_buffer,
                               size_t& io_buflen,
                               int64_t i_accessType,
                               uint64_t i_addr)
{
    uint64_t l_io_buffer = 0;
    uint64_t temp_scomAddr = 0;
    uint64_t l_data_from_addr = 0;

    memcpy(&l_io_buffer, io_buffer, 8);
    memcpy(&temp_scomAddr, &i_addr, 8);

    if (i_opType == DeviceFW::READ)
        die("Indirect Scom Form 1 does not support read op");

    // We want to make sure the user inputted data bits 0:11 are zero
    // so we can push addr(20:31) in it.
    if ((l_io_buffer & 0xFFF0000000000000) != 0)
        die("Data(0:11) is not zero: data out of range!");

    // Set up Address reg
    // cmdreg = addr(32:63)
    temp_scomAddr = i_addr & 0x00000000FFFFFFFF;

    // Set up data regs
    // data(0:11) = addr(20:31)
    l_data_from_addr = i_addr & 0x00000FFF00000000;
    // Do some bit shifting so things line up nicely
    l_data_from_addr <<= 20;

    // data(12:63) = data(12:63)
    // Set Data reg
    l_io_buffer |= l_data_from_addr;

    // Now perform the op requested using the
    // local io_buffer with the indirect addr imbedded.
    return doScomOp(i_opType,
                    i_target,
                    &l_io_buffer,
                    io_buflen,
                    i_accessType,
                    temp_scomAddr);
}
```
