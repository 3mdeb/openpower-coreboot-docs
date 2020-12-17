# Access to SCOM registers

According to information given on the OpenPower-firmware mailing list,
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

## Failed SCOM read attempts

### XSCOM base: **0x800603FC000000000** register: **0xF000F**

**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr 0xF0008, size 8, region '(null)', reason: rejected
XSCOM read failed at @0xf0008 pcba=0x0001e001
Invalid access at addr 0xF0010, size 8, region '(null)', reason: rejected
XSCOM read failed at @0xf0010 pcba=0x0001e002
```

### XSCOM base: **0x800003FC00000000**, **0x800023FC00000000**
### or **0x800623FC00000000** register: **0xF000F**
**Talos II**:\
`resets`\
**qemu**:
```
Invalid access at addr 0x3FC000F0008, size 8, region '(null)', reason: rejected
Invalid access at addr 0x3FC000F0010, size 8, region '(null)', reason: rejected
```


### XSCOM base: **0x800603FC000000000** register: **0xF0010**, **0xF001F**
### or **0xF0020**
**Talos II**:\
`Not tested`\
**qemu**:
```
Invalid access at addr <address>, size 8, region '(null)', reason: rejected
XSCOM read failed at @<address> pcba=0x0001e001
Invalid access at addr <address>, size 8, region '(null)', reason: rejected
XSCOM read failed at @<address> pcba=0x0001e002
```

### Assembly instruction **`ldcix`**
Following code was tested:
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

### Adding **`volatile`** keyword to variable type and **`ull`** suffix
### to register address
Code behavior was unchanged\
**Talos II**: `resets`\
**qemu**: Outputs information about invalid access to stdout

# Analysis of SCOM in hostboot code

To access `SCOM` `putScom()` or `getScom()` is used.
Early analysis of call chain resulted with following:

```
   putScom(target, address, data)
-> platPutScom(target, address, data)
-> deviceWrite(target, data, size, DEVICE_SCOM_ADDRESS(address, opMode))
-> Singleton<Associator>::instance().performOp(WRITE, target, buffer, buflen, accessType, args)
-> findDeviceRoute(opType = WRITE, devType, accessType)(opType = WRITE, target, buffer, buflen, accessType, addr)
```

before findDeviceRoute(), procedure has to be registerd using Associator.registerRoute()

/src/usr/fsiscom/fsiscom.C:178 fsiScomPerformOp() is probably a function responsible for reading/writing
