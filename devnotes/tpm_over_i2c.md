This document is about TPM chips with I2C interface, their availability,
documentation, drivers and other relevant sources that could help in supporting
I2C TPM on Talos.

## Known I2C chips

 * Nuvoton (problematic to buy, if possible at all)
   * `NPCT650`
     * [Linux driver](https://elixir.bootlin.com/linux/latest/source/drivers/char/tpm/tpm_i2c_nuvoton.c)
   * `NPCT750`
     * [Linux driver patches](https://github.com/Nuvoton-Israel/tpm_i2c_ptp)
   * [in Hostboot](https://github.com/open-power/hostboot/blob/master/src/usr/i2c/tpmdd.C#L1667)
   * [in skiboot](https://github.com/open-power/skiboot/blob/master/libstb/drivers/tpm_i2c_nuvoton.c) (650 only?)

 * ST (problematic to buy)
   * `ST33TPHF20I2C` (TPM 2.0)
     * [product](https://www.st.com/en/secure-mcus/st33tphf20i2c.html)
     * [datasheet](https://www.alldatasheet.com/datasheet-pdf/pdf/1179026/STMICROELECTRONICS/ST33TPHF20I2C.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33tphf20i2c.pdf)
   * `ST33TPHF2XI2C` (TPM 2.0)
     * [product](https://www.st.com/en/secure-mcus/st33tphf2xi2c.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33tphf2xi2c.pdf)
   * `ST33TPHF2EI2C` (TPM 1.2 & TPM 2.0)
     * [product](https://www.st.com/en/secure-mcus/st33tphf2ei2c.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33tphf2ei2c.pdf)
   * `ST33GTPMAI2C` (TPM 2.0)
     * [product](https://www.st.com/en/secure-mcus/st33gtpmai2c.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33gtpmai2c.pdf)
   * `ST33GTPMII2C` (TPM 2.0)
     * [product](https://www.st.com/en/secure-mcus/st33gtpmii2c.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33gtpmii2c.pdf)
   * `ST33TPM12I2C` (TPM 1.2)
     * [product](https://www.st.com/en/secure-mcus/st33tpm12i2c.html)
     * [data brief](https://www.st.com/resource/en/data_brief/st33tpm12i2c.pdf)
   * [Linux driver patches](https://github.com/STMicroelectronics/TCG-TPM-I2C-DRV)
   * [some other Linux driver upstream](https://elixir.bootlin.com/linux/latest/source/drivers/char/tpm/st33zp24)

 * Atmel (available in multiple stores)
   * `AT97SC3205T` (TPM 1.2)
     * [datasheet](https://datasheet.datasheetarchive.com/originals/dk/DKDS-15/281773.pdf)
     * [store search](https://www.findchips.com/search/AT97SC3205T-H3M4C-00)
     * [store](https://eu.mouser.com/c/?q=AT97SC3205T&instock=y)
     * [Linux driver](https://elixir.bootlin.com/linux/latest/source/drivers/char/tpm/tpm_i2c_atmel.c)

 * Infineon (available in one store)
   * SLB 9645TT1.2 (TPM 1.2)
     * [product](https://www.infineon.com/cms/en/product/security-smart-card-solutions/optiga-embedded-security-solutions/optiga-tpm/slb-9645tt1.2/)
     * [datasheet](https://www.infineon.com/dgdl/Infineon-Data-sheet-SLB9645_1.2_Rev1.2-DS-v01_02-EN.pdf?fileId=5546d462689a790c016929d1c4074fdf)
     * [store](https://www.arrow.com/en/products/slb9645vq12fw13332xuma2/infineon-technologies-ag)
     * [Linux driver](https://elixir.bootlin.com/linux/latest/source/drivers/char/tpm/tpm_i2c_infineon.c)

## Implementation efforts

### coreboot driver

coreboot already has drivers for two kinds of chips from above:
 * [Atmel I2C chips](https://github.com/coreboot/coreboot/blob/master/src/drivers/i2c/tpm/tis_atmel.c)
 * [Infineon I2C chips](https://github.com/coreboot/coreboot/blob/master/src/drivers/i2c/tpm/tpm.c)

Drivers for Nuvoton or ST33 could be derived from Linux sources if needed.

### Linux driver

Drivers for Atmel, Infineon and NPCT650 are in upstream Linux.  NPCT750 driver
is available as a set of patches for v5.6

Driver for ST33 is available as a patch for v5.4.83

### coreboot DT

TPM should be added to device tree in a way compatible with skiboot and Linux.

### skiboot driver

Looks like only `NPCT650` is supported.

Drivers for other TPMs can be derived from coreboot or Linux drivers.

### Existing drivers

The smallest drivers are for Atmel: coreboot ~100 lines, Linux ~200 lines.

Drivers for Infineon are ~600 lines in coreboot and ~750 in Linux (coreboot
driver derived from the Linux driver).

Linux driver for Nuvoton is ~700 lines.

Linux driver for ST is ~300 lines.

Drivers like these are copied between projects and adapted, sometimes one device
driver is derived from the other explicitly.  This likely means that it won't be
a major task to make a missing driver.

Implementations seem to be centered around transfer functions, which implement
send and receive operations on registers.

### Other potentially relevant sources

[wolfTPM](https://github.com/wolfSSL/wolfTPM) seems to implement drivers in
userspace using I2C devices exposed through `devfs`.

There are also QEMU patches for
[Atmel I2C TPM AT97SC3204T](https://lists.gnu.org/archive/html/qemu-devel/2016-11/msg04484.html).

"If you have a TPM security chip which is connected to a regular, non-tcg I2C
master (i.e. most embedded platforms)" (not sure if applicable):
* [old attempt](https://sourceforge.net/p/tpmdd/mailman/tpmdd-devel/thread/1458502483-16887-13-git-send-email-christophe-h.ricard%40st.com/#msg34951208)
* [recent try](https://lkml.org/lkml/2022/4/7/485)
