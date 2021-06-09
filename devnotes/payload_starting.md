# Payload loading and starting

Note that we abandoned HDAT idea and decided to use Flattened Device Tree.
You can skip next section and jump to [FDT entry](#fdt-entry). We have decided
to stick with FDT after some helpful discussion with IBM folks:
* in [this githiub issue](https://github.com/open-power/skiboot/issues/264)
* in [openpower-firmware mailing list thread](https://lists.ozlabs.org/pipermail/openpower-firmware/2021-May/000641.html)

## HDAT analysis

All of the following comes from analyzing skiboot's code only.
- every thread jumps into skiboot (at offset 0x180)
  - needs new HRMOR
  - skiboot is self-relocating, shouldn't matter where it is loaded (alignment?)
- skiboot is compressed with XZ
  - ~1.5MB total, <400kB compressed size; PNOR partition is 1MB
  - https://www.7-zip.org/sdk.html - recent LZMA SDK versions have support for
    XZ, but coreboot currently uses older version (4.42)
  - https://review.coreboot.org/c/coreboot/+/41963 - newer LZMA code, but no
    LZMA2 or XZ
    - change may be rejected due to performance issues: https://ticket.coreboot.org/issues/194
    - on PPC performance is increased, likely because compressed image is in
      RAM and not in flash, where reading one byte at a time is slow
  - another idea is to put skiboot in CBFS
    - this would be redundant
    - must have skiboot binary for building coreboot
    - there are multiple versions of binary file:
      - ELF
      - LID
        - raw or compressed into XZ file
        - STB or no STB
      - none of these will fit in 512kB CBFS, even with compression
        - skiboot.elf as a payload (just size check, wouldn't work): 393kB LZMA,
          604kB LZ4, ~318kB available before ramstage implementation and with
          customizable debug options disabled (CBFS, RAM init)
      - we may try using other partition than HBB for CBFS, this implies one of:
        - bootblock in SEEPROM
        - bootblock in HBB, rest of CBFS on other partition
        - changes to PNOR layout
- coreboot is somewhat x86-centric (functions in device.h expect TOLUM etc.)
  - needed mostly to prepare memory map
  - would be nice to pass that map from romstage where it was created and
    written to hardware
- HDAT
  - /include/mem-map.h:
    - `SKIBOOT_BASE` = 0x30000000
    - `SPIRA_HEAP_BASE` = `SKIBOOT_BASE` + 0x01200000 -- SPIRA-S is located here
      - S stands for Support processor (FSP or BMC)
      - there is also SPIRA-H (Hypervisor), but it is written in skiboot's code
        and constant (unless it is patched by hostboot after loading skiboot?)
      - main SPIRA is also present in LID, and is not empty (`proc_init`,
        `cpu_ctrl`, `mdump_src`, `heap`)
    - `SPIRA_HEAP_SIZE` = 0x00800000
  - /hdata/spira.h:
    - all (?) data is in BE
    - HDIF (Hypervisor Data Interface Format) common header - version, ID, sizes
      and count of elements
    - `HDIF_idata_ptr` - offset and size of internal data block
    - HDIF array header - size and count of array elements
    - array of N-tuples (address, allocated and used space, TCE (?) offset)
      - various platform information
      - 18 N-tuples in `struct spiras_ntuples`, but `SPIRAS_NTUPLES_COUNT` = 0x10
      - each N-tuple may point to more than one element (array?)
    - SPIRA-S and SPIRA-H are copied to SPIRA, which has almost all SPIRA-S
      and SPIRA-H fields
      - SPIRA-S has `hbrt_data` but SPIRA doesn't
      - SPIRA-H has `hs_data_area` and `proc_dump_area` but SPIRA doesn't
      - SPIRA also has `chip_tod`, `ext_cache_fru_vpd`, `heap` and `paca` fields

#### SPIRA-S fields

Most `*_vpd` fields are parsed by `_vpd_parse` which "just adds" the data to DT.
Unless otherwise noted, hostboot creates one entry for each field for current
platform setup. Note that even if HB does not create any entry it still has to
write an N-tuple with count=0.

- `sp_subsys` - BMC and/or FSP information, LPC, UART
- `ipl_params` - temporary or permanent settings used, hot or cold boot etc.
- `nt_enclosure_vpd` - VPD, HB does not create any
- `slca` - Slot Location Code Array, a tree representing topology of the system
- `backplane_vpd` - VPD
- `system_vpd` - "System VPD uses the VSYS record, so its special", probably it
  uses OSYS for OpenPOWER instead
- `clock_vpd` - VPD, HB does not create any
- `anchor_vpd` - VPD, HB does not create any
- `op_panel_vpd` - VPD, HB does not create any
- `misc_cec_fru_vpd` - VPD, HB does not create any
- `ms_vpd` - Memory Description Tree (MS = mainstore?), this is also special
- `cec_iohub_fru` - an array of CEC Hub FRU HDIF structures, cross referenced
  with SLCA for actual ports
- `pcia` - (Processor Core ...?) SPIRA contain an array of these, one per
  processor core; holds IDs, core frequency, cache sizes etc, HB creates 4
  entries for currently used CPU
- `proc_chip` - Processor Chip Related Data (?), one per chip, lists NX, PORE,
  OCC, CAPP state, OpenCAPI, NVLink and I2C devices attached to chip
- `hs_data` - Host Services data, this field in SPIRA-H built into skiboot LID
  points to `SPIRA_HEAP_BASE`, HB creates 1 entry (or maybe just updates
  actually used size?)
- `hbrt_data` - not even copied to SPIRA, but HB creates 1 entry
- `ipmi_sensor` - should be self-explanatory
- `node_stb_data` - Secure/Trusted Boot (TPMs)

---

Additional findings based on hostboot analysis:
- Node Address Communication Area (NACA) is used
  - structure in LID at offset 0x4000
  - notable fields:
    - entry points for different protocols (PHYP, OPAL)
    - pointer to LID table (?)
    - pointer to Service Processor Interace Root Array (SPIRA)
    - various version and capability fields
  - pointers are probably offsets, otherwise it wouldn't be relocatable
    - or whole payload is aligned to HRMOR?

## FDT entry

coreboot has support for FIT payloads, which is similar to what skiboot expects
when started through FDT entry point. It is not enabled for PPC64 as of now, but
simple changes to Kconfig files are enough to make it build. There are however
some subtle but important differences that have to be worked out:

- in Kconfigs for `PAYLOAD_FIT` and `PAYLOAD_FIT_SUPPORT` `ARCH_PPC64` is
  missing in `depends on`
- previous stages were started through function descriptors in OPD section, for
  FDT entry point we have to jump at offset 0x10 from the start of the image.
  This is raw entry point but plain C function call would try to use OPD logic
  so it has to be done in assembly
- CBFS with embedded skiboot and DT will no longer fit in HBB partition, which
  slightly complicates deployment (see [flashing instructions](#flashing) below)
- coreboot removes existing memory nodes from the device tree and writes its
  own. This removes `ibm,chip-id` props which are expected by skiboot to create
  memory associativity mapping (NUMA). The final solution should recreate those
  props, but for testing it is enough to pass the memory information in FDT blob
  and comment out a call to `fit_update_memory()` in `fit_payload()`

All changes mentioned above are on [power_fit_loading branch](https://github.com/3mdeb/coreboot/tree/power_fit_loading).
Some of them are temporary, just to start the payload to discover how much of
hardware initialization is still missing.

### Preparing a Device Tree

Unfortunately, we can't just use Device Tree [read from running system](../logs/28.05.2021_talos_ii_device-tree.dts)
because it contains information that was filled by skiboot, and should not be
present in the tree passed to it, otherwise skiboot will complain and abort. By
comparing it with unit test for [Witherspoon](https://git.raptorcs.com/git/talos-skiboot/tree/hdata/test/op920.wsp.dts)
we can deduce which information is redundant. These include:

- reserved memory ranges - this includes `/memreserve/` at the beginning of the
  file, `reserved-names` and `reserved-ranges` in the root and `reserved-memory`
  node
- `chosen` node
- for each CPU: `ibm,associativity`, `ibm,dec-bits`, `interrupts` and
  `interrupt-parent`
- whole `ibm,powerpc-cpu-features`
- `ibm,firmware-versions`
- most of the `ibm,opal` node, except for `phandle`, `leds`, `phandle` and
  `ibm,enabled-stop-levels` in `power-mgt`
- `imc-counters`
- `interrupt-controller` - 2 nodes (?)
- interrupt fields for `lpc` and `ipmi-bt` node
  - `status = "reserved"` for serial
- `ibm,associativity` for each `memory`
- `pciex` - all entries
- `psi`
- `vas`
- `ibm,opal-id` and `ibm,port-name` for each I2C bus
- `ibm,842-high-fifo`, `ibm,842-normal-fifo`, `ibm,gzip-high-fifo` and
  `ibm,gzip-normal-fifo`

All of the above reduced DTS file from ~7000 lines to just over 1500. Stripped
file can be found [here](../logs/28.05.2021_talos_ii_device-tree_stripped.dts).

### Obtaining skiboot.lid
`skiboot.lid` can be obtained in a few different ways:

* First one is to [build it](#building-skibootlid) - it is very quick and easy.

* Second one is to copy it from
`talos-op-build/output/build/skiboot-<hash>/skiboot.lid`,
if you already have built full PNOR image, however it is very time-consuming if
you haven't.

* The third way is to read it from PNOR. Its partition doesn't have ECC, but you
have to remove STB header.

1. From the BMC:

```shell
pflash -P PAYLOAD -r /tmp/skiboot.bin
```

2. Copy this file to your computer (tools included in BMC's BusyBox are lacking
   required options), strip the header and decompress XZ file:

```shell
tail -c +$((0x1001)) skiboot.bin | unxz > skiboot.lid
```

### Building skiboot.lid

   1. Clone the skiboot repository
   ```
   git clone https://git.raptorcs.com/git/talos-skiboot
   ```

   2. Checkout correct revision
   ```
   cd talos-skiboot
   git checkout 9858186353f2203fe477f316964e03609d12fd1d
   ```

   3. Start docker container
   ```
   docker run --rm -it -v $PWD:/home/skiboot/skiboot -w /home/skiboot/skiboot 3mdeb/coreboot-sdk:mkimage /bin/bash
   ```

   4. Build the skiboot image
   ```
   make -j`nproc` CROSS=powerpc64-linux-gnu-
   ```

   5. `skiboot.lid` is located in root of your skiboot repository.
---

### Preparing FIT payload

This is loosely based on documentation for [qemu-aarch64 mainboard](https://doc.coreboot.org/mainboard/emulation/qemu-aarch64.html#building-coreboot-with-an-arbitrary-fit-payload)
and [generic FIT information](https://doc.coreboot.org/lib/payloads/fit.html).

1. Create `config.its` file with the following contents:

```
/dts-v1/;
/ {
	description = "Simple image with skiboot and FDT blob";
	#address-cells = <1>;

	images {
		kernel {
			description = "skiboot";
			data = /incbin/("skiboot.lid");
			type = "kernel";
			arch = "powerpc";
			compression = "none";
			load = <0x00000>;
			entry = <0x10>;
			hash-1 {
				algo = "crc32";
			};
		};
		fdt-1 {
			description = "Flattened Device Tree blob";
			data = /incbin/("fdt.bin");
			type = "flat_dt";
			arch = "powerpc";
			compression = "none";
			load = <0x1000000>;
			hash-1 {
				algo = "crc32";
			};
		};
	};

	configurations {
		default = "conf-1";
		conf-1 {
			description = "Boot skiboot with FDT blob";
			kernel = "kernel";
			fdt = "fdt-1";
		};
	};
};
```

2. From the directory that contains `config.its`, `skiboot.lid` and `fdt.bin`
   run:

```shell
mkimage -f config.its uImage
```

3. Copy newly created `uImage` file somewhere coreboot can see it and point to
   it in coreboot's config menu as a FIT payload. Remember to use at least 2MB
   as ROM chip size.

### Flashing

Because HBB is no longer big enough to hold whole CBFS we have to flash
bootblock and main CBFS separately. Build system already creates signed
bootblock with ECC. First copy both files to BMC:

```shell
scp build/coreboot.rom.signed.ecc root@<BMC IP>:/tmp/coreboot.rom.signed.ecc
scp build/bootblock.signed.ecc root@<BMC IP>:/tmp/bootblock.signed.ecc
```

Then log in to BMC and flash bootblock to HBB and the rest to HBI:

```shell
pflash -e -P HBB -p /tmp/bootblock.signed.ecc
pflash -e -P HBI -p /tmp/coreboot.rom.signed.ecc
```

Start the platform as usual. It should boot up to a point where skiboot tries
to start execution on other cores.
