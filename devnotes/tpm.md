This document is an attempt to gather information about the issues faced with
TPMs on POWER9 and what are the potential solutions to them.

## Some definitions

* fTPM - Firmware-based TPM implementation

## Problems

### POWER9 cannot generate LPC TPM cycles

This is a hardware limitation and cannot be worked around in firmware.

The consequence is that regular LPC TPM cannot be used on POWER9 by directly
connecting it to the TPM LPC header (one might wonder why have the LPC TPM
header in the first place...).

## Quick overview

| Aspect\Approach | [I2C]   |[On BMC]|[FPGA LPC]|[FPGA SPI]|[On MCU] |[LibreBMC]| [SBE] |
| --------------- | -----   | ------ | -------- | -------- | ------  | -------- | ----- |
| Interface       | I2C     | -      | LPC      | SPI      | I2C     | ?        | -     |
| Placement       | Board   | BMC    | Board    | Board    | USB?    | BMC      | CPU   |
| In stock        | Hardly  | -      | Yes      | Yes      | Hardly  | -        | -     |
| Software only   | No      | Yes    | No       | No       | No      | No       | Yes   |
| Design HW       | Maybe   | No     | Yes      | Yes      | Partial | Partial  | No    |
| Manufacture HW  | Maybe   | No     | Yes      | Yes      | Yes     | Yes      | No    |
| Lacks OS driver | Maybe   | Yes    | No       | No       | Maybe   | No?      | Maybe |
| Openness        | Partial | Full   | Partial  | Partial  | Full    | Full     | Full  |

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

******
### Software TPM on BMC

TPM functionality exposed to the POWER9 CPU via LPC gadget device from BMC.
As secure as BMC is.

* **Pros**
  * No additional hardware needed.
  * Open implementation.

* **Cons**
  * Need to secure the BMC and manage that properly.

* **Risks**
  * BMC might not have been designed to fulfill security requirements.

* **Implementation effort**
  * Implement fTPM in BMC.
  * LPC gadget BMC -> POWER.
  * No existing driver. Maybe need to hack up existing LPC driver.

* **Other notes**
  * We could go full RO with coreboot if desired (by sacrificing some
    performance for MVPD).

******
### FPGA translator (LPC TPM cycle - LPC TPM cycle)

New piece of hardware that addresses LPC cycles issues.

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
### FPGA translator (SPI - LPC TPM cycle)

New piece of hardware that addresses LPC cycles issues.

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

Find an existing open hardware TPM and possibly extend it for I2C interface.

* **Pros**
  * Open hardware.

* **Cons**
  * Need to implement and manufacture TPM module.

* **Risks**
  * I2C is not in the scope as of now.

* **Implementation effort**
  * Slightly easier than the fTPM on BMC - but not much.

* **Other notes**

******
### TPM on [LibreBMC][LibreBMC-site]

Embed TPM into an alternative BMC hardware that's still in development?
The BMC itself is implemented as a custom extension card defined by DS-CMS?

* **Pros**
  * Open hardware.

* **Cons**
  * Project is in an early phase of development.

* **Risks**
  * Current design does not have TPM connector.

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
  * There are 96 KiB of RAM and 256 KiB of SEEPROM.
  * Stripped uncompressed x86-64 `libtpm.a` from [MS][fTPM] compiled with
    `-Os -g0` gives 257 KiB as a sum of text and data columns printed by `size`.
    There might be a chance it will fit when compressed.  It's a bit larger for
    x86.

## See also

* [TPM over LPC](tpm_over_lpc.md)
* [TPM over I2C](tpm_over_i2c.md)

[fTPM]: https://github.com/microsoft/ms-tpm-20-ref
[LibreBMC-site]: https://openpower.foundation/groups/librebmc/
[SBE-site]: https://wiki.raptorcs.com/wiki/Self-Boot_Engine
[lpnTPM]: https://lpntpm.lpnplant.io/

[I2C]: #i2c-tpm-module
[On BMC]: #software-tpm-on-bmc
[FPGA LPC]: #fpga-translator-lpc-tpm-cycle---lpc-tpm-cycle
[FPGA SPI]: #fpga-translator-spi---lpc-tpm-cycle
[On MCU]: #open-tpm-on-mcu-eg-lpntpm
[LibreBMC]: #tpm-on-librebmc
[SBE]: #ftpm-in-sbe-
