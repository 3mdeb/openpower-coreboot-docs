# TPM connection over LPC interface

At this point in time, using TPM module over LPC interface was unsuccessful.

## TPM support in Hostboot

Hostboot mentions in one of the comments in code, that two families of
TPM from Nuvoton vendor are supported.

```
Hostboot code only supports Nuvoton 65x and 75x Models at this time
```

This support was not verified yet.

## Talos II TPM Connector

Talos II TPM connector has `LPC`and `I2C` connections.\
The description is available in the [user guide](https://wiki.raptorcs.com/w/images/e/e3/T2P9D01_users_guide_version_1_0.pdf).
![](../images/TPM_connector_schematic.png)

## TPM over LPC interface

`OPTIGA™ TPM SLB 9665TT2.0 TPM2.0` was tested over an LPC interface.

To test the chip, original Hostboot image with [Heads](https://github.com/3mdeb/openpower-coreboot-docs/blob/main/releases/0.3.0.heads.md)
as a payload was flashed into the system.

Talos II properly booted, however no TPM module was detected by Heads.

```
$ dmesg | grep -i tpm
[    4.552516] ima: No TPM chip found, activating TPM-bypass!
```

### START nibble

The LPC TPM uses the same cycles as I/O cycles which we implement e.g. for
serial port. The only difference is the START nibble. There may exist a register
thast allows to alter a START nibble that is sent on each LPC cycle.

> TODO: Check if this type of register exists.

## Supported TPM connections

TPM connection is hardware-supperted via LPC and I2C interface.
[Source](https://wiki.raptorcs.com/wiki/User:HLandau/Block_Diagram_Discussion#Minor_CPU_Interfaces)

* CPU0 LPC [to FlexVer] to BMC, LPC TPM

    ```
    The LPC interface of CPU0 is connected to the BMC. The BMC serves the PNOR flash chip connected to it to CPU0, and CPU0 loads boot firmware from it. A TPM connector is also provided on the board which exposes this bus, and allows a standard TPM to be attached to it.

    A FlexVer module, if fitted, can intermediate this bus and proxy all communications between the CPU and other devices on the LPC bus. This switching is done automatically via analogue components on the mainboard when a FlexVer device is connected.
    ```
* BMC I2C TO I2C TPM
    ```
    Runs to the TPM connector. Allows connection of a TPM via I2C instead of LPC. In this case, the connection is via the BMC.
    ```
