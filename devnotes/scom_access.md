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

Tests were done to access SCOM in bootblock, however when accessing mmio
address `0x000603FC00000000` or `0x000603FC0000F00F`, coreboot seemd to freeze
before writing it over the serial. When writing any other value, coreboot
behaved as expected, writing it and continuing the execution.

Setting bit 0 in address makes it baypass HRMOR translation, but the result is the same as without it set.

Adding `volatile` keyword, trying ot do the same from `bootblock`,
adding `ull` to the register address, trying `0x000003FC00000000`
as XCOM base wich is correct according to the documentation
or using following assembly didn't allow us to access the register.
```
uint64_t buffer;
asm volatile("ldcix %0, %1, %2" : "=r"(buffer) : "b"(0x800603FC00000000ull), "r"(0xF000F));
eieio();
```

Access to this address causes an exception.

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
