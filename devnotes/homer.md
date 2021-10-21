# HOMER (Hardware Offload Microcode Engine Region)

High level description of HOMER can be found at [OpenPOWER docs repo](https://github.com/open-power/docs/blob/master/occ/p9_pmcd_homer.pdf).

This document tries to describe all structures and point to the source of data
created in istep 15.1. Offsets are relative to the base of relevant region
(Quad, Core or Pstate), unless noted otherwise.

### OCC PM region (OPMR)

This region is not touched by 15.1.

### Quad PM region (QPMR)

#### SGPE

* offset: 0
* size: <80K, with padding to 128K after

##### QPMR header

* offset: 0
* size: 512 bytes

Comes from HCODE->SGPE->QPMR. Holds flags, offsets and sizes of further
structures. It is modified later by multiple functions (layoutRingsForSgpe(),
updateQpmrHeader() and functions called by those two), based on contents of
other structures and attributes.

##### L1 bootloader, aka bootcopier

* offset: 512B
* size: <=1K

Comes from HCODE->SGPE->L1_BOOTLOADER. Actual code is less than the full size.

##### L2 bootloader, aka bootloader

* offset: 1.5K
* size: <=1K

Comes from HCODE->SGPE->L2_BOOTLOADER. Actual code is less than the full size.

##### SRAM image

* offset: 2.5K
* size: <=74K, later written to QPMRHdr->sram_img_size

Comes from HCODE->SGPE->HCODE which holds interrupt vectors, header and code.
Actual code size is less than the full size. The rest is filled by the code with
ring data; that data is included in SRAM image size.

###### Interrupt vectors

* offset: 0 (relative to SRAM image), QPMRHdr->img_offset (relative to QPMR)
* size: 0x180 = 384 bytes

###### SGPE header

* offset: 0x180 (relative to SRAM image)
* size: depends on version

This also holds offsets and sizes of various structures, in a format that is
easy to use by GPE that reads it. Written to in various places.

###### Code

* offset: immediately after header and alignment
* size: <50K (ends at QPMRHdr->img_len, relative to SRAM image)

###### Common ring

* offset: immediately after code and alignment, written later to
  QPMRHdr->cmn_ring_offset
* size: depends on the size of rings, written later to QPMRHdr->cmn_ring_len

Filled by layoutRingsForSGPE() based on data from getPpeScanRings(), which reads
rings from MVPD partition.

###### Specialized ring

* offset: immediately after common ring and alignment, written later to
  QPMRHdr->spec_ring_offset
* size: depends on the size of rings, written later to QPMRHdr->spec_ring_len

Filled by layoutRingsForSGPE() based on data from getPpeScanRings(), which reads
rings from MVPD partition.

#### Cache SCOM region

* offset: 128K
* size: 6*4K

Filled by populateEpsilonL2ScomReg(), populateEpsilonL3ScomReg(),
populateL3RefreshScomReg() and populateNcuRngBarScomReg() by calling STOP API
(p9_stop_save_scom()).

#### Auxiliary (24x7)

* offset: 512K
* size: 64K

Not filled by 15.1.

### Core PM region (CPMR)

#### Self restore region

* offset: 0
* size: 9K

##### SR header, aka CPMR header

* offset: 0
* size: 256B

Comes from HCODE->RESTORE->CPMR. Contains various version numbers, flags,
offsets and sizes. Modified in multiple places, mostly in updateCpmrCmeRegion().
WARNING: this XIP image must be written after the next one.

##### SR code

* offset: 256B
* size: 8.75K

Comes from HCODE->RESTORE->SELF. Image in XIP actually has a padding for the
header above, filled with zeros. Reading it after the header would overwrite it.

#### Core self restore

* offset: 9K
* size: 96K (24 cores * 4K)

This region holds 24 (max number of cores) sets of the areas listed below. This
whole range is filled with `attn` instructions, except for first instructions in
restore areas by initSelfRestoreRegion(). Save areas for functional cores are
then updated by p9_stop_init_cpureg() and p9_stop_init_self_save().

##### Thread restore area

* offset: 0 (relative to core self restore)
* size: 2K (4 threads * 512B)

##### Thread save area

* offset: 2K (relative to core self restore)
* size: 1K (4 threads * 256B)

##### Core restore area

* offset: 3K (relative to core self restore)
* size: 512B

##### Core save area

* offset: 3.5K (relative to core self restore)
* size: 512B

#### Core SCOM restore

* offset: 256K
* size: 6K (12*512B, actually there are 24*256B entries, but Hostboot groups the
  cores in pairs)

Not filled by 15.1.

#### CME SRAM region

* offset: 262K
* size: <=64K

Comes from HCODE->CME->HCODE which holds interrupt vectors, header and code.
Actual code size is less than the full size. The rest is filled by the code with
ring data; that data is included in SRAM image size.

##### Interrupt vectors

* offset: 0 (relative to SRAM image), CPMRHdr->img_offset (relative to CPMR)
* size: 0x180 = 384 bytes

##### CME header

* offset: 0x180 (relative to SRAM image)
* size: depends on version

This also holds offsets and sizes of various structures, in a format that is
easy to use by GPE that reads it (e.g. offsets and sizes are in 32B blocks,
because that is the size CME block copy engine uses). Written to in various
places: updateCpmrCmeRegion(), layoutRingsForCme(), buildParameterblock().

##### Code

* offset: immediately after header and alignment
* size: ~30K (ends at CPMRHdr->img_len, relative to SRAM image)

##### Common ring

* offset: immediately after code and alignment, written later to
  CPMRHdr->cme_common_ring_offset **in bytes**
* size: depends on the size of rings, written later to
  CPMRHdr->cme_common_ring_len **in bytes**

Filled by layoutRingsForCME() based on data from getPpeScanRings(), which reads
rings from MVPD partition.

##### Specialized ring

* offset: immediately after common ring and alignment, written later to
  CPMRHdr->core_spec_ring_offset **in 32B blocks**
* size: depends on the size of rings, written later to CMEHdr->max_spec_ring_len
  and CPMRHdr->core_spec_ring_len **in 32B blocks**

Filled by layoutRingsForCME() based on data from getPpeScanRings(), which reads
rings from MVPD partition.

##### CME Pstates Parameter Block

* offset: immediately after specialized ring and alignment, written later to
  CMEHdr->pstate_offset **as 32B blocks** relative to SRAM image
* size: sizeof(LocalPstateParmBlock)

Created by buildParameterBlock().

Pairs of specialized ring and CME PPB are repeated for every functional core.
Those structures don't have fixed offsets, they are written one after the other,
without gaps in between (other than alignment to CME copy engine block size).
Sum of specialized ring and PPB sizes is written to CMEHdr->custom_len as a
** number of 32B blocks**.

### Pstate PM region (PPMR)

#### PPMR header

* offset: 0
* size: 1K

Comes from HCODE->PGPE_>PPMR. Contains various offsets and sizes. Modified by
buildParameterBlock().

#### L1 bootloader, aka bootcopier

* offset: 1K
* size: <=1K

Comes from HCODE->PGPE->L1_BOOTLOADER. Actual code is less than the full size.

#### L2 bootloader, aka bootloader

* offset: 2K
* size: <=1K

Comes from HCODE->PGPE->L2_BOOTLOADER. Actual code is less than the full size.

#### SRAM image

* offset: 3K
* size: <=50K, later written to PPMRHdr->sram_img_size

Comes from HCODE->PGPE->HCODE which holds interrupt vectors, header and code.
Actual code size is less than the full size. The rest is filled by the code with
ring data; that data is included in SRAM image size.

##### Interrupt vectors

* offset: 0 (relative to SRAM image), PPMRHdr->hcode_offset (relative to PPMR)
* size: 0x180 = 384 bytes

##### PGPE header

* offset: 0x180 (relative to SRAM image)
* size: depends on version

This also holds offsets and sizes of various structures, in a format that is
easy to use by GPE that reads it. Written to in updatePpmrHeader().

##### Code

* offset: immediately after header and alignment
* size: <50K (ends at PPMRHdr->hcode_len, relative to SRAM image)

##### Global PPB

* offset: immediately after code and alignment, written to PPMRHdr->gppb_offset
  relative to PPMR
* size: sizeof(GlobalPstateParmBlock), written to PPMRHdr->gppb_len (aligned to
  8B)

Created by buildParameterBlock() (gppb_init()) from attributes and data from
MVPD.

#### OCC PPB

* offset: 128K
* size: sizeof(OCCPstateParmBlock)

Created by buildParameterBlock() (oppb_init()) from attributes and data from
MVPD.

#### OCC P-State Table

* offset: 144K
* size: 16K

Not filled by 15.1.

#### WOF tables

* offset: 768K
* size: 256K

Copied by buildParameterBlock() (oppb_init()) from WOFDATA PNOR partition, based
on data from MVPD (different tables are used based on core count, frequency,
power etc).

# Dumps

## Homer filled by Hostboot

Homer dump after Hostboot fills the the structure with the data (after Petitboot
starts) is [available here](https://cloud.3mdeb.com/index.php/s/cNZJYE9ysgSaebJ).

## Dumping Homer image

To create a memdump of Homer:

1.
    Log in to BMC.

2.
    Check if you have at least 4MB of space available in `\tmp`.
    If not, remove unimportant files from `/tmp` to free up space.

    ```
    $ df
    ```
3.
    (Optional) Check if Homer is located it the correct address.
    ```
    $ pdbg -p0 -c1 -t0 getmem 0x11fd800000 0x100 2>/dev/null | hexdump -C
    ```
    > Note: If OCC Image is not filled yet, the memory will still be zeroed.

4.
    Dump Homer image. It should take approximately 40-50 minutes.

    ```
    $ pdbg -p0 -c1 -t0 getmem 0x11fd800000 0x400000 2>/dev/null >/tmp/homer.bin
    ```

## Homer dumped at istep 15.2

[Dumped image](https://cloud.3mdeb.com/index.php/s/HDNikYe7Jmfc7Pg)

Homer was dumped after executing
istep 15.2 `proc_set_pba_homer_bar` in Hostboot.
Code was stopped after 15.2 in `proc_set_pba_homer_bar` function.

```
            TRACFCOMP(ISTEPS_TRACE::g_trac_isteps_trace,
                      "proc_set_pba_homer_bar: unsecure HOMER addr = 0x%.16llX",
                      l_unsecureHomerAddr);
        }

    }
    for(;;);

    TRACFCOMP( ISTEPS_TRACE::g_trac_isteps_trace, "call_proc_set_pba_homer_bar exit" );
    // end task, returning any errorlogs to IStepDisp
    return l_StepError.getErrorHandle();
}
```

### Building modified Hostboot image

Modified Hostboot was build in the following way:

1.
    Build the official Hostboot image:
    ```
    $ git clone -b raptor-v2.00 --recursive https://scm.raptorcs.com/scm/git/talos-op-build
    $ cd talos-op-build
    $ git checkout raptor-v2.00
    $ git submodule update
    $ . op-build-env
    $ export KERNEL_BITS=64
    $ op-build talos_defconfig
    $ op-build
    ```

2.
    Modify `Hostboot` inside the `output/build/hostboot-<hash>` directory.

3.
    Rebuild `Hostboot`
    ```
    $ rm -rf output/build/machine-xml-*
    $ rm -rf output/build/hostboot-*
    $ op-build hostboot-rebuild openpower-pnor-rebuild
    ```
