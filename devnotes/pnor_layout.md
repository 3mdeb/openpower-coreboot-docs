# About this document
This document gathers known informations about partition layout in the original
PNOR image from the Talos II platform.

# Partition table

PNOR contains partition table at offset 0. It is constructed out of header and multiple entries.
Refer to the [ffs source code](https://github.com/open-power/ffs/blob/3ec70fbc458e32eef0d0b1de79688b4dc48cbd57/ffs/ffs.h#L108) for more detailed informations.
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

| Name                        | ID | Offset:End         | Size    | Type | Flags | Actual size  |
| --------------------------- | -- | ------------------ | -------:| ---- | ----- | ------------:|
| [part](#part)               | 01 | 00000000:00001FFF  | 2000    | p    | p     | same as size |
| [HBEL](#HBEL)               | 02 | 00008000:0002BFFF  | 24000   | d    | -     | same as size |
| [GUARD](#GUARD)             | 03 | 0002C000:00030FFF  | 5000    | d    | -     | same as size |
| NVRAM                       | 04 | 00031000:000C0FFF  | 90000   | d    | -     | same as size |
| [SECBOOT](#SECBOOT)         | 05 | 000C1000:000E4FFF  | 24000   | d    | -     | same as size |
| DJVPD                       | 06 | 000E5000:0012CFFF  | 48000   | d    | -     | same as size |
| MVPD                        | 07 | 0012D000:001BCFFF  | 90000   | d    | -     | same as size |
| CVPD                        | 08 | 001BD000:00204FFF  | 48000   | d    | -     | same as size |
| [HBB](#HBB)                 | 09 | 00205000:00304FFF  | 100000  | d    | -     | same as size |
| HBD                         | 0A | 00305000:00424FFF  | 120000  | d    | -     | same as size |
| [HBI](#HBI)                 | 0B | 00425000:019C4FFF  | 15A0000 | d    | -     | same as size |
| SBE                         | 0C | 019C5000:01A80FFF  | BC000   | d    | -     | same as size |
| [HCODE](#HCODE)             | 0D | 01A81000:01BA0FFF  | 120000  | d    | -     | same as size |
| HBRT                        | 0E | 01BA1000:021A0FFF  | 600000  | d    | -     | same as size |
| [PAYLOAD](#PAYLOAD)         | 0F | 021A1000:022A0FFF  | 100000  | d    | -     | same as size |
| [BOOTKERNEL](#BOOTKERNEL)   | 10 | 022A1000:03820FFF  | 1580000 | d    | -     | same as size |
| OCC                         | 11 | 03821000:03940FFF  | 120000  | d    | -     | same as size |
| FIRDATA                     | 12 | 03941000:03943FFF  | 3000    | d    | -     | same as size |
| [VERSION](#VERSION)         | 13 | 03944000:03945FFF  | 2000    | d    | -     | same as size |
| BMC_INV                     | 14 | 03968000:03970FFF  | 9000    | d    | -     | same as size |
| [HBBL](#HBBL)               | 15 | 03971000:03977FFF  | 7000    | d    | -     | same as size |
| ATTR_TMP                    | 16 | 03978000:0397FFFF  | 8000    | d    | -     | same as size |
| ATTR_PERM                   | 17 | 03980000:03987FFF  | 8000    | d    | -     | same as size |
| [IMA_CATALOG](#IMA_CATALOG) | 18 | 03989000:039C8FFF  | 40000   | d    | -     | same as size |
| RINGOVD                     | 19 | 039C9000:039E8FFF  | 20000   | d    | -     | same as size |
| WOFDATA                     | 1A | 039E9000:03CE8FFF  | 300000  | d    | -     | same as size |
| HB_VOLATILE                 | 1B | 03CE9000:03CEDFFF  | 5000    | d    | -     | same as size |
| MEMD                        | 1C | 03cee000:03CFBFFF  | E000    | d    | -     | same as size |
| SBKT                        | 1D | 03d02000:03D05FFF  | 4000    | d    | -     | same as size |
| HDAT                        | 1E | 03d06000:03D0DFFF  | 8000    | d    | -     | same as size |
| UVISOR                      | 1F | 03d10000:03E0FFFF  | 100000  | d    | -     | same as size |
| [BOOTKERNFW](#BOOTKERNFW)   | 20 | 03e10000:03FEFFFF  | 1E0000  | d    | -     | same as size |
| [BACKUP_PART](#BACKUP_PART) | 21 | 03ff7000:03FFEFFF  | 8000    | d    | -     | 0            |

Type:
* l = FFS_TYPE_LOGICAL
* d = FFS_TYPE_DATA
* p = FFS_TYPE_PARTITION

Flags:
* b = FFS_FLAGS_U_BOOT_ENV
* p = FFS_FLAGS_PROTECTED

# Reproducibility
Reproducibility of partitions can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Firmware/Reproducible_Builds/Status).

# Partition description
## part
This partition contains partition table.

## HBEL
This partition is used by `hostboot` to to save crash information for later
debug.
More informations can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Hostboot_Debug_Howto).

## GUARD
This partition is responsible for holding information about components that
are detected as broken.
More information about it can be found at the
[wiki page](https://wiki.raptorcs.com/wiki/Troubleshooting/Guard_Partition).

## SECBOOT
This partition is used to hold secure variables across renppts in
tamper-resistant manner. Because writes to PNOR can't be prevented, hash of
this partition is stored in the `TPM NV`, and verified at the boot time.
More informations can be found at the
[wiki page](https://open-power.github.io/skiboot/doc/secvar/secboot_tpm.html).

## HBB
This partition is overwritten with `coreboot` image containing `bootblock`.

## HBI
This partition is overwritten with `coreboot` image containing `romstage`,
`ramstage` and `payload` containing `hostboot`.

## HCODE
FW for PPE

## PAYLOAD
This partition contains `skiboot`.

## BOOTKERNEL
This partition contains `skiroot`

## VERSION
Probably just an information about versions of a binaries in the PNOR image.

## HBBL
nie jest wykonywany z PNOR, to tylko obraz który podczas aktualizacji przeprowadzanej przez hostboota był zapisywany do SEEPROM. Nie wiem czy tak samo jest z SBE, dużo większa ta partycja niż SEEPROM

## IMA_CATALOG
This partition holds a list of events supported by OCC microcode.

## BOOTKERNFW
Probably `petitboot`, `initramfs`.
Proprietary AMD GPU FW was added here at Talos II System Package 2.00 release.

## BACKUP_PART
Because `Actual Size` of this partition is 0, it doesn't take any space in PNOR
image.
