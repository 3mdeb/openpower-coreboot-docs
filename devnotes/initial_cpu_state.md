# CPU state on stage entries

CPU is expected to work in big endian at all times during coreboot execution.
This decision was made because:

* this is the endianness in which coreboot execution starts
* `coreboot-sdk` includes libgcc compiled for BE only
* there are CPU load/store instructions with endianness conversion
* some instructions are only available in BE
* it will help to assure that all of coreboot components are designed in
  endian-agnostic way - CBFS, FMAP, (de)compression algorithms etc.

When an OS or a payload starts, it usually executes a [sequence of instructions](https://github.com/torvalds/linux/blob/v5.8/arch/powerpc/boot/ppc_asm.h#L65)
to switch to the target endianness of its choosing.

## Bootblock

The entry state depends on whether bootblock is loaded instead of HBBL (Hostboot
bootloader) or HBB (Hostboot core).

#### HBBL

Binary size is limited to 20 kB (ECC not included). HRMOR is set to 130 MB, that
is 2 MB into the L3 cache region (10 MB total). HBBL is loaded to HRMOR + 12 kB,
so the exception vectors (which are very sparse) do not have to be included into
already limited size of HBBL. SBE jumps to HRMOR + 12 kB, no other registers are
set.

#### HBB

HRMOR is set to 128 MB (beginning of L3 cache). In current HBBL implementation,
HBB image size is limited to 1 MB with ECC (space in range 128-130 MB is split
in half between ECC and non-ECC image of HBB). HBB can later overwrite memory
used previously by HBBL and ECC image. HBB begins execution at HRMOR, no space
is implicitly reserved for exception vectors.

### Common parts

Except for those 12 kB reserved for exception vectors and consequent change to
the linking address, the state is almost identical for both paths.

Different HRMOR doesn't impact the linking address, we are using only hypervisor
state with real mode addressing so HRMOR is always added during calculation of
effective addresses. Whenever access to physical address is required (e.g. IO
operations), bit 0 (MSB) is set so HRMOR is ignored.

There are no guaranteed values in any of the registers, some of them must be set
before we can begin execution of C code.

###### Stack

`r1` is stack register. It must be loaded with a proper value before any C
function is called.

###### TOC

PPC64 reserves one register (`r2`) for a pointer to the table of contents. It is
used to access GOT (global offset data) and SDA (small data area). This value is
set separately for every externally visible function, and is saved in [function descriptor](https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html#FUNC-DES).
All descriptors from a file are saved in a separate section called OPD (official
procedure descriptors). When a function name's symbol is loaded into a register
in assembly, it conveniently holds the address of descriptor; when it is used in
a branch instruction, the entry point to the function is used instead.

###### BSS

Bootblock is loaded as a binary (not ELF), which includes BSS. It is already
zeroed so it doesn't require any additional work. If the size of bootblock
becomes a problem, we can exclude this section from the binary and use cache
memory, in that case we would have to zero it in the code.

### Hand-off to romstage

TBD:
* how to properly obtain new `r2`?
