# About this document
This document gathers known informations about partition layout in the original
PNOR image from the Talos II platform.

# Partition table

PNOR contains partition table at offset 0. It is constructed out of header and multiple entries.
Refer to the [ffs source code](https://github.com/open-power/ffs/blob/3ec70fbc458e32eef0d0b1de79688b4dc48cbd57/ffs/ffs.h#L108) for more detailed informations.
```cpp
 struct ffs_hdr {
	uint32_t         magic;         // @magic:          Eye catcher/corruption detector
                                    //                  should always contain `0x50415254` or text `PART`
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

| Name                       | ID | Offset:End         | Size    | Type | Flags | Actual  |
| -------------------------- | -- | ------------------ | ------- | ---- | ----- | ------- |
| [part](#part)               | 01 | 00000000:00001FFF  | 2000    | p    | p     | 2000    |
| [HBEL](#HBEL)               | 02 | 00008000:0002BFFF  | 24000   | d    | -     | 24000   |
| [GUARD](#GUARD)             | 03 | 0002C000:00030FFF  | 5000    | d    | -     | 5000    |
| [NVRAM](#NVRAM)             | 04 | 00031000:000C0FFF  | 90000   | d    | -     | 90000   |
| [SECBOOT](#SECBOOT)         | 05 | 000C1000:000E4FFF  | 24000   | d    | -     | 24000   |
| [DJVPD](#DJVPD)             | 06 | 000E5000:0012CFFF  | 48000   | d    | -     | 48000   |
| [MVPD](#MVPD)               | 07 | 0012D000:001BCFFF  | 90000   | d    | -     | 90000   |
| [CVPD](#CVPD)               | 08 | 001BD000:00204FFF  | 48000   | d    | -     | 48000   |
| [HBB](#HBB)                 | 09 | 00205000:00304FFF  | 100000  | d    | -     | 100000  |
| [HBD](#HBD)                 | 0A | 00305000:00424FFF  | 120000  | d    | -     | 120000  |
| [HBI](#HBI)                 | 0B | 00425000:019C4FFF  | 15A0000 | d    | -     | 15A0000 |
| [SBE](#SBE)                 | 0C | 019C5000:01A80FFF  | BC000   | d    | -     | BC000   |
| [HCODE](#HCODE)             | 0D | 01A81000:01BA0FFF  | 120000  | d    | -     | 120000  |
| [HBRT](#HBRT)               | 0E | 01BA1000:021A0FFF  | 600000  | d    | -     | 600000  |
| [PAYLOAD](#PAYLOAD)         | 0F | 021A1000:022A0FFF  | 100000  | d    | -     | 100000  |
| [BOOTKERNEL](#BOOTKERNEL)   | 10 | 022A1000:03820FFF  | 1580000 | d    | -     | 1580000 |
| [OCC](#OCC)                 | 11 | 03821000:03940FFF  | 120000  | d    | -     | 120000  |
| [FIRDATA](#FIRDATA)         | 12 | 03941000:03943FFF  | 3000    | d    | -     | 3000    |
| [VERSION](#VERSION)         | 13 | 03944000:03945FFF  | 2000    | d    | -     | 2000    |
| [BMC_INV](#BMC_INV)         | 14 | 03968000:03970FFF  | 9000    | d    | -     | 9000    |
| [HBBL](#HBBL)               | 15 | 03971000:03977FFF  | 7000    | d    | -     | 7000    |
| [ATTR_TMP](#ATTR_TMP)       | 16 | 03978000:0397FFFF  | 8000    | d    | -     | 8000    |
| [ATTR_PERM](#ATTR_PERM)     | 17 | 03980000:03987FFF  | 8000    | d    | -     | 8000    |
| [IMA_CATALOG](#IMA_CATALOG) | 18 | 03989000:039C8FFF  | 40000   | d    | -     | 40000   |
| [RINGOVD](#RINGOVD)         | 19 | 039C9000:039E8FFF  | 20000   | d    | -     | 20000   |
| [WOFDATA](#WOFDATA)         | 1A | 039E9000:03CE8FFF  | 300000  | d    | -     | 300000  |
| [HB_VOLATILE](#HB_VOLATILE) | 1B | 03CE9000:03CEDFFF  | 5000    | d    | -     | 5000    |
| [MEMD](#MEMD)               | 1C | 03cee000:03CFBFFF  | E000    | d    | -     | E000    |
| [SBKT](#SBKT)               | 1D | 03d02000:03D05FFF  | 4000    | d    | -     | 4000    |
| [HDAT](#HDAT)               | 1E | 03d06000:03D0DFFF  | 8000    | d    | -     | 8000    |
| [UVISOR](#UVISOR)           | 1F | 03d10000:03E0FFFF  | 100000  | d    | -     | 100000  |
| [BOOTKERNFW](#BOOTKERNFW)   | 20 | 03e10000:03FEFFFF  | 1E0000  | d    | -     | 1E0000  |
| [BACKUP_PART](#BACKUP_PART) | 21 | 03ff7000:03FFEFFF  | 8000    | d    | -     | 0       |

Type:
* l = FFS_TYPE_LOGICAL
* d = FFS_TYPE_DATA
* p = FFS_TYPE_PARTITION

Flags:
* b = FFS_FLAGS_U_BOOT_ENV
* p = FFS_FLAGS_PROTECTED

# Partition description
## part
TBD
## HBEL
TBD
## GUARD
TBD
## NVRAM
TBD
## SECBOOT
TBD
## DJVPD
TBD
## MVPD
TBD
## CVPD
TBD
## HBB
TBD
## HBD
TBD
## HBI
TBD
## SBE
TBD
## HCODE
FW dla PPE
## HBRT
TBD
## PAYLOAD
skiboot
## BOOTKERNEL
skiroot
## OCC
TBD
## FIRDATA
TBD
## VERSION
TBD
## BMC_INV
TBD
## HBBL
przeprowadzanej przez hostboota był zapisywany do SEEPROM. Nie wiem czy tak
samo jest z SBE, dużo większa ta partycja niż SEEPROM
## ATTR_TMP
TBD
## ATTR_PERM
TBD
## IMA_CATALOG
TBD
## RINGOVD
TBD
## WOFDATA
TBD
## HB_VOLATILE
TBD
## MEMD
VPD dla RAM
## SBKT
TBD
## HDAT
TBD
## UVISOR
TBD
## BOOTKERNFW
petitboot, initramfs?
## BACKUP_PART
TBD
