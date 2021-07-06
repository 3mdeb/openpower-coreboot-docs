# About this document
This document gathers known information about partition layout in the original
PNOR image from the Talos II platform.

# Partition table

PNOR contains partition table at offset 0. It is constructed out of header and multiple entries.
Refer to the
[ffs source code](https://github.com/open-power/ffs/blob/3ec70fbc458e32eef0d0b1de79688b4dc48cbd57/ffs/ffs.h#L108)
for more detailed information.
```cpp
 struct ffs_hdr {
    uint32_t         magic;         // @magic:          Eye catcher/corruption detector
                                    //                  should always contain 0x50415254 or text PART
    uint32_t         version;       // @version:        Version of the structure
    uint32_t         size;          // @size:           Size of partition table (in block_size)
    uint32_t         entry_size;    // @entry_size:     Size of struct ffs_entry element (in bytes)
    uint32_t         entry_count;   // @entry_count:    Number of struct ffs_entry elements in @entries array
    uint32_t         block_size;    // @block_size:     Size of block on device (in bytes)
    uint32_t         block_count;   // @block_count:    Number of blocks on device
    uint32_t         resvd[4];      // @resvd:          Reserved words for future use
    uint32_t         checksum;      // @checksum:       Header checksum
    struct ffs_entry entries[];     // @entries:        Pointer to array of partition entries
} __attribute__ ((packed));

struct ffs_entry {
    char     name[16];      // @name:       Opaque null terminated string
    uint32_t base;          // @base:       Starting offset of partition in flash (in hdr.block_size)
    uint32_t size;          // @size:       Partition size (in hdr.block_size)
    uint32_t pid;           // @pid:        Parent partition entry (0xFFFFFFFF for toplevel)
    uint32_t id;            // @id:         Partition entry ID [1..65536]
    uint32_t type;          // @type:       Describe type of partition
    uint32_t flags;         // @flags:      Partition attributes (optional)
    uint32_t actual;        // @actual:     Actual partition size (in bytes)
    uint32_t resvd[4];      // @resvd:      Reserved words for future use
    struct {
        uint32_t data[16];  // @user:       User data (optional)
    } user;
    uint32_t checksum;      // @checksum:   Partition entry checksum (includes all above)
} __attribute__ ((packed));
```

Each partiton can contain image of following types:
- FFS_TYPE_DATA
- FFS_TYPE_LOGICAL
- FFS_TYPE_PARTITION

# Partition layout
Data presented below was extracted using `./fcp -L file:flash.pnor -o 0` and
enchanced with additional data extracted directly from `flash.pnor`.
More on how to use this tool can be found in [porting.md#ffs-tools](porting.md#ffs-tools).

Partition Table
Version: 1
Size: 2 * 0x1000 (block size)
Block Count: 0x4000
Entry Size: 0x80
Number of entries: 0x21

| Name                        | ID   | Offset:End             | Size (bytes) | Type | Flags | Actual size  | coreboot usage                        |
| --------------------------- | ---- | ---------------------- | ------------:| ---- | ----- | ------------:| ------------------------------------- |
| [part](#part)               | 0x01 | 0x00000000:0x00001FFF  | 0x2000       | p    | p     | same as size | Partition table                       |
| [HBEL](#HBEL)               | 0x02 | 0x00008000:0x0002BFFF  | 0x24000      | d    | -     | same as size | None                                  |
| [GUARD](#GUARD)             | 0x03 | 0x0002C000:0x00030FFF  | 0x5000       | d    | -     | same as size | None                                  |
| [NVRAM](#NVRAM)             | 0x04 | 0x00031000:0x000C0FFF  | 0x90000      | d    | -     | same as size | Required                              |
| [SECBOOT](#SECBOOT)         | 0x05 | 0x000C1000:0x000E4FFF  | 0x24000      | d    | -     | same as size | None                                  |
| DJVPD                       | 0x06 | 0x000E5000:0x0012CFFF  | 0x48000      | d    | -     | same as size | None                                  |
| MVPD                        | 0x07 | 0x0012D000:0x001BCFFF  | 0x90000      | d    | -     | same as size | None                                  |
| CVPD                        | 0x08 | 0x001BD000:0x00204FFF  | 0x48000      | d    | -     | same as size | None                                  |
| [HBB](#HBB)                 | 0x09 | 0x00205000:0x00304FFF  | 0x100000     | d    | -     | same as size | bootblock                             |
| [HBD](#HBD)                 | 0x0A | 0x00305000:0x00424FFF  | 0x120000     | d    | -     | same as size | None                                  |
| [HBI](#HBI)                 | 0x0B | 0x00425000:0x019C4FFF  | 0x15A0000    | d    | -     | same as size | romstage, ramstage, payload (skiboot) |
| [SBE](#SBE)                 | 0x0C | 0x019C5000:0x01A80FFF  | 0xBC000      | d    | -     | same as size | None                                  |
| [HCODE](#HCODE)             | 0x0D | 0x01A81000:0x01BA0FFF  | 0x120000     | d    | -     | same as size | None                                  |
| HBRT                        | 0x0E | 0x01BA1000:0x021A0FFF  | 0x600000     | d    | -     | same as size | None                                  |
| [PAYLOAD](#PAYLOAD)         | 0x0F | 0x021A1000:0x022A0FFF  | 0x100000     | d    | -     | same as size | None                                  |
| [BOOTKERNEL](#BOOTKERNEL)   | 0x10 | 0x022A1000:0x03820FFF  | 0x1580000    | d    | -     | same as size | skiroot                               |
| [OCC](#OCC)                 | 0x11 | 0x03821000:0x03940FFF  | 0x120000     | d    | -     | same as size | None                                  |
| FIRDATA                     | 0x12 | 0x03941000:0x03943FFF  | 0x3000       | d    | -     | same as size | None                                  |
| [VERSION](#VERSION)         | 0x13 | 0x03944000:0x03945FFF  | 0x2000       | d    | -     | same as size | skiboot verifies this, but just warns about mismatch |
| BMC_INV                     | 0x14 | 0x03968000:0x03970FFF  | 0x9000       | d    | -     | same as size | None                                  |
| [HBBL](#HBBL)               | 0x15 | 0x03971000:0x03977FFF  | 0x7000       | d    | -     | same as size | None                                  |
| ATTR_TMP                    | 0x16 | 0x03978000:0x0397FFFF  | 0x8000       | d    | -     | same as size | None                                  |
| ATTR_PERM                   | 0x17 | 0x03980000:0x03987FFF  | 0x8000       | d    | -     | same as size | None                                  |
| [IMA_CATALOG](#IMA_CATALOG) | 0x18 | 0x03989000:0x039C8FFF  | 0x40000      | d    | -     | same as size | None                                  |
| RINGOVD                     | 0x19 | 0x039C9000:0x039E8FFF  | 0x20000      | d    | -     | same as size | None                                  |
| WOFDATA                     | 0x1A | 0x039E9000:0x03CE8FFF  | 0x300000     | d    | -     | same as size | None                                  |
| HB_VOLATILE                 | 0x1B | 0x03CE9000:0x03CEDFFF  | 0x5000       | d    | -     | same as size | None                                  |
| [MEMD](#MEMD)               | 0x1C | 0x03cee000:0x03CFBFFF  | 0xE000       | d    | -     | same as size | Memory configuration data             |
| SBKT                        | 0x1D | 0x03d02000:0x03D05FFF  | 0x4000       | d    | -     | same as size | None                                  |
| [HDAT](#HDAT)               | 0x1E | 0x03d06000:0x03D0DFFF  | 0x8000       | d    | -     | same as size | None                                  |
| UVISOR                      | 0x1F | 0x03d10000:0x03E0FFFF  | 0x100000     | d    | -     | same as size | None                                  |
| [BOOTKERNFW](#BOOTKERNFW)   | 0x20 | 0x03e10000:0x03FEFFFF  | 0x1E0000     | d    | -     | same as size | None                                  |
| [BACKUP_PART](#BACKUP_PART) | 0x21 | 0x03ff7000:0x03FFEFFF  | 0x8000       | d    | -     | 0            | Required                              |

> Note: If partition has no coreboot usage means that erasing this partition
> didn't prevent the platform from starting skiroot.
> Partition may still be used in some case
> or in the later stages of the booting process.

Type:
* l = FFS_TYPE_LOGICAL
* d = FFS_TYPE_DATA
* p = FFS_TYPE_PARTITION

Flags:
* b = FFS_FLAGS_U_BOOT_ENV
* p = FFS_FLAGS_PROTECTED

# Reproducibility
Reproducibility of partition content can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Firmware/Reproducible_Builds/Status).

# Partition original usage
## part
This partition contains partition table.

## HBEL
This partition is used by `hostboot` to to save crash information for later
debug.
More information can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Hostboot_Debug_Howto).

## GUARD
This partition is responsible for holding information about components that
are detected as broken.
More information about it can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Troubleshooting/Guard_Partition).

## NVRAM
Non volatile memory required for OPAL systems.
More information can be fouind on the
[wiki page](https://open-power.github.io/skiboot/doc/opal-api/opal-nvram-read-write-7-8.html).

## SECBOOT
This partition is used to hold secure variables across renppts in
tamper-resistant manner. Because writes to PNOR can't be prevented, hash of
this partition is stored in the `TPM NV`, and verified at the boot time.
More information can be found at the
[wiki page](https://open-power.github.io/skiboot/doc/secvar/secboot_tpm.html).

## HBB
Bootloader code for `hostboot`

## HBD
Hostboot data

## HBI
Main `hostboot` application

## SBE
Self Boot Engine, stored in module EEPROM

## HCODE
FW for PPE

## PAYLOAD
This partition contains `skiboot`.

## BOOTKERNEL
This partition contains `skiroot`: Linux kernel bundled
with `initrd` (Linux userspace and  `petitboot` binaries).

## OCC
Most likely contains FW for OCC (On Chip Controller).
Source code for this FW is available [here](https://git.raptorcs.com/git/talos-occ/).

## VERSION
Probably just an information about versions of a binaries in the PNOR image.

## HBBL
This partition is used by hostboot during update operation.

## IMA_CATALOG
This partition holds a list of events supported by OCC microcode.

## MEMD
Holds memory configuration data.

## HDAT
Holds inforamtions from hostboot similar to device-tree,
but in hostboot-data format.

## BOOTKERNFW
Proprietary AMD GPU FW was added here at Talos II System Package 2.00 release.

## BACKUP_PART
Because `Actual Size` of this partition is 0, it doesn't take any space in PNOR
image.
