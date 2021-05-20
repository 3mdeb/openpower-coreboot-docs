Payload loading and starting
----------------------------

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

SPIRA-S fields
==============

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
