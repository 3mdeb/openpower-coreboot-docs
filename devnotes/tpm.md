# TPM for IBM POWER9 - feasibility study

This document is an attempt to gather information about the issues faced with
TPMs on POWER9 and what are the potential solutions to them.

Mind that thanks to POWER9 being more secure than any x86 by design, it is more
important to have TPM support than to have a particularly secure or feature-full
one.  See [platform comparison] for more details.

## Some definitions

* TPM - Trusted Platform Module
* dTPM - Discreet TPM module (a dedicated pluggable hardware module)
* fTPM - Firmware-based TPM implementation
* LPC - Low Pin Count (hardware interface)
* I2C - Inter-Integrated Circuit (hardware interface)
* SBE - Self-Boot Engine (part of POWER9 CPU and/or its firmware)
* MCU - MicroController Unit (like a board connected via USB)
* FPGA - Field-Programmable Gate Array (programmable integrated circuit)
* BMC - Baseboard Management Controller ("service processor")

## Problems

### Talos II cannot generate LPC TPM cycles

A short overview of relationships between LPC and TPM on LPC can be found in
this [TPM publication]:

    The format of these TPM cycles is virtually identical to I/O LPC cycles
    discussed earlier. The only visible difference between TPM and I/O cycles
    is the value sent during the START phase of LPC transactions. Existing I/O
    cycles are “target” cycles in LPC bus terminology and use 0x0 as their
    4-bit START value. The new TPM cycles use the previously reserved value 0x5
    as the start value.

To repeat: TPM cycles are implemented as I/O LPC cycles but use a different
START nibble (`5` instead of `0`).  The value of `5` is reserved in
[LPC specification] and is claimed by [TPM specification] \(Table 43). The same
TPM specification mentions in an informative comment that:

    For system Software, the TPM has a 64-bit address of 0x0000_0000_FED4_xxxx.
    On LPC, the chipset passes the least significant 16 bits to the TPM.

Which allows generation of LPC cycles with arbitrary START nibble.

The problem is that LPC accesses on Talos II don't have this property and fix
START nibble at `0`, thus precluding a direct LPC TPM connection. See [lpcdd.C]
in Hostboot, LPC cycles are abstracted away via more high-level memory-mapped
I/O, where clients don't write START field themselves and have no control over
its value.

## Quick overview

| Aspect\Approach | [Direct I2C] | [BMC fTPM] | [LPC adapter] | [SPI adapter] | [MCU dTPM]    | [LibreBMC]    | [SBE]    |
| --------------- | ------------ | ---------- | ------------- | ------------- | ----------    | ----------    | -----    |
| [TPM kind]      | dTPM         | fTPM       | dTPM          | dTPM          | dTPM          | dTPM          | fTPM     |
| Placement       | MCU          | BMC        | MCU           | MCU           | MCU           | BMC           | CPU      |
| Fully open      | No           | Yes        | No            | No            | Yes           | Yes           | Yes      |
| HW protected    | Yes          | No         | Yes           | Yes           | Yes           | Yes           | Yes      |
| In stock        | Hardly (-1)  | -          | Yes (+1)      | Yes (+1)      | Hardly (-1)   | -             | -        |
| Design HW       | Maybe (+/-1) | No (+1)    | Yes (-1)      | Yes (-1)      | Partial (-.5) | Partial (-.5) | No (+1)  |
| Manufacture HW  | Maybe (+/-1) | No (+1)    | Yes (-1)      | Yes (-1)      | Yes (-1)      | Yes (-1)      | No (+1)  |
| Needs OS driver | No (+1)      | Yes (-1)   | Yes (-1)      | No (+1)       | Maybe (+/-1)  | Yes (-1)      | Yes (-1) |
| Score           | +2 / -2      | +1         | -2            | 0             | -1.5 / -3.5   | -2.5          | +1       |

### Score counting

Some rows in the table are informational and no score is assigned to them, the
rest get points according to the following set of rules:

 * if aspect isn't applicable, then 0
 * if aspect is satisfied, then +1 or +0.5 if partially true
 * if aspect is lacking, then -1 or -0.5 if partially true
 * if aspects is marked as inverted, negate the value

| Aspect          | Scored   | Description                                              |
| ------          | ------   | -----------                                              |
| TPM kind        | No       | Whether it's fTPM or dTPM                                |
| Placement       | No       | Where TPM functionality is located physically            |
| Fully open      | No       | Whether involved hardware and software are fully open    |
| HW protected    | No       | Protection against tampering is by design                |
| In stock        | Yes      | If modules compatible with the approach are easy to find |
| Design HW       | Inverted | Designing hardware is necessary                          |
| Manufacture HW  | Inverted | Producing hardware is necessary                          |
| Needs OS driver | Inverted | Producing OS driver is necessary                         |

## Approaches

### I2C TPM module

Hardware module with an uncommon interface (TPMs for PC mostly use LPC or SPI).
Known chips of this kind are/were produced by Nuvoton, ST, Infineon,
Atmel (AT97SC3205T).

* **Pros**
  * Talos II has I2C header exposed on the board.
  * Best option if can be bought.

* **Cons**
  * No datasheets for Nuvoton chips.
  * Limited or no availability of ready to use modules.
  * Might require design and production of new TPM module with I2C interface.

* **Risks**
  * Hard to find and this might not improve in the future.

* **Implementation effort**
  * See [TPM over I2C](tpm_over_i2c.md).

* **Other notes**
  * ST33 has Linux driver for TPM 2.0
  * Bought modules won't have OSS firmware.

******
### Software TPM on BMC

TPM functionality exposed to the POWER9 CPU via LPC gadget device from BMC.
That is OpenBMC is extended to provide fTPM though LPC bus, but do this with
START field equal to `0`.
As secure as BMC is.

* **Pros**
  * No additional hardware needed.
  * Open implementation.

* **Cons**
  * Need to secure the BMC and manage that properly.

* **Risks**
  * BMC might not have been designed to fulfill security requirements.
  * Still won't address LPC cycles issue?

* **Implementation effort**
  * Implement fTPM in BMC.
  * LPC gadget BMC -> POWER.
  * No existing driver.
    Maybe need to hack up existing LPC driver to change the way LPC is accessed
    and translate START value.

* **Other notes**
  * We could go full RO with coreboot if desired (by sacrificing some
    performance for MVPD).

******
### IO LPC translator (IO TPM cycle - LPC TPM cycle)

New piece of hardware with FPGA that addresses LPC cycles issues.

Although might not actually need FPGA:

    LAD[0]' = LAD[0] + ~LFRAME#
    LAD[2]' = LAD[2] + ~LFRAME#

* **Pros**
  * Conventional TPM modules.

* **Cons**
  * Need to design and manufacture this hardware.

* **Risks**

* **Implementation effort**
  * Slightly easier than the fTPM on BMC - but not much.

* **Other notes**

******
### FPGA translator (SPI - LPC TPM cycle)

New piece of hardware that addresses LPC cycles issues by creating an SPI - LPC
bridge, where LPC side is connected to the board and SPI TPM module is used.

* **Pros**
  * Conventional TPM modules.
  * No need for a new driver.

* **Cons**
  * Need to design and manufacture this hardware.

* **Risks**

* **Implementation effort**
  * Slightly easier than the fTPM on BMC - but not much.

* **Other notes**

******
### Open TPM on MCU (e.g. [lpnTPM])

Find an existing open hardware TPM project based on an MCU and possibly extend
it for I2C interface.

* **Pros**
  * Open hardware.

* **Cons**
  * Need to implement and manufacture TPM module.

* **Risks**
  * I2C is not in the scope of [lpnTPM] as of now.

* **Implementation effort**
  * Slightly easier than the fTPM on BMC - but not much.

* **Other notes**

******
### TPM on [LibreBMC][LibreBMC-site]

Embed TPM into an alternative BMC hardware that's still in development?
The BMC itself is implemented as a custom extension card defined by [DC-SCM]?

* **Pros**
  * Open hardware.

* **Cons**
  * Project is in an early phase of development.

* **Risks**
  * Current design does not have TPM connector (because it's builtin?).
  * Not possible without redesign Talos board?

* **Implementation effort**

* **Other notes**
  * Maybe we can implement fTPM in FPGA BMC.

******
### fTPM in [SBE][SBE-site]

Implement TPM on a chip that's used primarily for booting POWER9 processor.

* **Pros**
  * SBE chip is an independent piece of hardware that's part of secure boot
    sequence for POWER9.

* **Cons**

* **Risks**
  * SBE SEEPROM may not have enough free space to include fTPM implementation.

* **Implementation effort**
  * Will need a driver for the OS.
    If there is one for SBE communication from IBM, it still needs an update to
    support custom SBE firmware and extensions made to its protocol.

* **Other notes**
  * We have not dealt with SBE much so far, but coreboot uses it less than
    Hostboot does, so we might be able to remove unused parts of sources.
  * Looks like `sbe_seeprom_DD2` is compressed from 86 to 65 KiB, but compressed
    `sbe_seeprom_AXONE` is 150 KiB.
  * There are 96 KiB of RAM and 256 KiB of SEEPROM (227 KiB effectively due to
    ECC).
  * Stripped uncompressed x86-64 `libtpm.a` from [MS][fTPM] compiled with
    `-Os -g0` gives 257 KiB as a sum of text and data columns printed by `size`.
    There might be a chance it will fit when compressed.  It's a bit larger for
    x86.
  * But there is also TPM NV memory that needs to be stored somewhere...

## See also

* [TPM over LPC](tpm_over_lpc.md)
* [TPM over I2C](tpm_over_i2c.md)

[fTPM]: https://github.com/microsoft/ms-tpm-20-ref
[LibreBMC-site]: https://openpower.foundation/groups/librebmc/
[SBE-site]: https://wiki.raptorcs.com/wiki/Self-Boot_Engine
[lpnTPM]: https://lpntpm.lpnplant.io/
[DC-SCM]: https://www.opencompute.org/documents/ocp-dc-scm-spec-rev-1-0-pdf
[platform comparison]: https://wiki.raptorcs.com/wiki/Platform_Comparison
[TPM kind]: https://blog.3mdeb.com/2021/2021-10-08-ftpm-vs-dtpm/
[LPC specification]: https://www.intel.com/content/dam/www/program/design/us/en/documents/low-pin-count-interface-specification.pdf
[TPM specification]: https://trustedcomputinggroup.org/wp-content/uploads/PC-Client-Specific-Platform-TPM-Profile-for-TPM-2p0-v1p05p_r14_pub.pdf
[TPM publication]: https://www.sciencedirect.com/science/article/pii/S0898122112004634
[lpcdd.C]: https://github.com/open-power/hostboot/blob/master/src/usr/lpc/lpcdd.C

[Direct I2C]: #i2c-tpm-module
[BMC fTPM]: #software-tpm-on-bmc
[LPC adapter]: #io-lpc-translator-io-tpm-cycle---lpc-tpm-cycle
[SPI adapter]: #fpga-translator-spi---lpc-tpm-cycle
[MCU dTPM]: #open-tpm-on-mcu-eg-lpntpm
[LibreBMC]: #tpm-on-librebmc
[SBE]: #ftpm-in-sbe
