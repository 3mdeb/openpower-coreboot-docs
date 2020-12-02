## CPU execution modes

* user         - problem state
* supervisor   \
* hypervisor    } privileged states
* ultravisor   /

Higher privilege levels include instructions from lower levels, e.g. hypervisor
instructions are available in ultravisor.

`sc` (System Call) changes execution mode. (Every mode or only user ->
supervisor?)

EA/IP-relative addressing exists.

CPU can work both in big- and little-endian mode, it can be changed dynamically.
There are load/store instructions with endianness conversion.

#### Real-world endianness usage

LE was included in PPC later, first CPUs supported only BE. Because of that,
first OSes were also BE (MacOS, BSD, early Linux). Nowadays, while BE is still
supported by Linux kernel, most [distributions](https://wiki.raptorcs.com/wiki/Operating_System_Compatibility_List)
use LE in order to provide compatibility with [graphics hardware](https://lwn.net/Articles/408848/).

For backward compatibility, OPAL (OpenPOWER abstraction layer, a set of runtime
services provided by firmware) uses BE. Every call has to [switch to BE](https://github.com/torvalds/linux/blob/master/arch/powerpc/boot/opal-calls.S#L42)
and back.

#### Switching from unknown endianness to a proper one

From [Linux kernel](https://github.com/torvalds/linux/blob/master/arch/powerpc/boot/ppc_asm.h#L65):

```
#define FIXUP_ENDIAN						   \
	tdi   0,0,0x48;	  /* Reverse endian of b . + 8		*/ \
	b     $+44;	  /* Skip trampoline if endian is good	*/ \
	.long 0xa600607d; /* mfmsr r11				*/ \
	.long 0x01006b69; /* xori r11,r11,1			*/ \
	.long 0x00004039; /* li r10,0				*/ \
	.long 0x6401417d; /* mtmsrd r10,1			*/ \
	.long 0x05009f42; /* bcl 20,31,$+4			*/ \
	.long 0xa602487d; /* mflr r10				*/ \
	.long 0x14004a39; /* addi r10,r10,20			*/ \
	.long 0xa6035a7d; /* mtsrr0 r10				*/ \
	.long 0xa6037b7d; /* mtsrr1 r11				*/ \
	.long 0x2400004c  /* rfid				*/
```

If run as the first instructions after entry (or return from potentially
different endianness, e.g OPAL), this code checks if the endianness is good and
changes it otherwise. If first operand of `tdi` is 0, it is a no-op. The same
sequence is used for fixing BE and LE - the assembler uses the endianness for
which it is configured both for instructions and `.long`s.

## PPC CPU state after reset (QEMU)

* NIP  =              0x100
* HF   = 0x8000000000000000
* GRP3 =         0x1fef0000
* RES  = 0xffffffffffffffff
* PVR  =         0x004d0200 (640, default CPU)
  PVR  =         0x004e1200 (power9)
* LPCR =         0x0401f00c
* other = 0

[Spec for ePAPR](https://web.archive.org/web/20120419173345/https://www.power.org/resources/downloads/Power_ePAPR_APPROVED_v1.1.pdf)
states what those registers mean. There should be similar documents for other
platforms. Most important parts:

* R3 - effective address of the device tree (8B aligned)
* R6 - ePAPR magic value—to distinguish from non-ePAPR-compliant firmware
* R7 - size of boot IMA

State of non-boot cores on MP system is also defined - R3,6,7 are 0, PC is 0x4.

## ABI

#### Function calling sequence

These are conventions used by ELF. They are not enforced by the architecture,
but GCC most likely uses those, so it will be best to keep this conventions to
ease the development.

Some of the registers have their functions reserved for OS specific data, maybe
those can be used for other purposes (r13, r2 for PPC).

There are actually two slightly different versions: ELFv1 and ELFv2 ABI. While
they are compatible with both endian types, ELFv1 is default for BE, ELFv2 - LE.

###### PPC

[Spec](https://refspecs.linuxfoundation.org/elf/elfspec_ppc.pdf)

| Reg.   | Usage                                                           | Volatile |
|--------|-----------------------------------------------------------------|:--------:|
|r0      | Volatile register which may be modified during function linkage | V        |
|r1      | Stack frame pointer, always valid                               | NV       |
|r2      | System-reserved register                                        | NV (not used by app code) |
|r3-r4   | Volatile registers used for parameter passing and return values | V        |
|r5-r10  | Volatile registers used for parameter passing                   | V        |
|r11-r12 | Volatile registers which may be modified during function linkage| V        |
|r13     | Small data area pointer register                                | NV (process data, const?) |
|r14-r30 | Registers used for local variables                              | NV       |
|r31     | Used for local variables or "environment pointers"              | NV       |
|f0      | Volatile register                                               | V        |
|f1      | Volatile register used for parameter passing and return values  | V        |
|f2-f8   | Volatile registers used for parameter passing                   | V        |
|f9-f13  | Volatile registers                                              | V        |
|f14-f31 | Registers used for local variables                              | NV       |
|CR0-CR7 | Condition Register Fields, each 4 bits wide                     | CR2-4 NV, others V |
|LR      | Link Register                                                   | V        |
|CTR     | Count Register                                                  | V        |
|XER     | Fixed-Point Exception Register                                  | V        |
|FPSCR   | Floating-Point Status and Control Register                      | V (except FP control - exception and rounding) |


###### PPC64

[Spec](https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html)

| Reg.   | Usage                                                           | Volatile |
|--------|-----------------------------------------------------------------|:--------:|
|r0      | Volatile register used in function prologs                      | V        |
|r1      | Stack frame pointer                                             | NV       |
|r2      | TOC pointer                                                     | NV (combines GOT and SDA (small data area) |
|r3      | Volatile parameter and return value register                    | V        |
|r4-r10  | Volatile registers used for function parameters                 | V        |
|r11     | Volatile register used in calls by pointer and as an environment pointer for languages which require one | V |
|r12     | Volatile register used for exception handling and glink code    | V        |
|r13     | Reserved for use as system thread ID                            | NV (const?) |
|r14-r31 | Nonvolatile registers used for local variables                  | NV       |
|f0      | Volatile scratch register                                       | V        |
|f1-f4   | Volatile floating point parameter and return value registers    | V        |
|f5-f13  | Volatile floating point parameter registers                     | V        |
|f14-f31 | Nonvolatile registers                                           | NV       |
|LR      | Link register                                                   | V        |
|CTR     | Loop counter register                                           | V        |
|XER     | Fixed point exception register                                  | V        |
|FPSCR   | Floating point status and control register                      | V (except FP control - exception and rounding) |

On processors with the VMX feature:

| Reg.   | Usage                                  |
|--------|----------------------------------------|
|v0-v1   | Volatile scratch registers             |
|v2-v13  | Volatile vector parameters registers   |
|v14-v19 | Volatile scratch registers             |
|v20-v31 | Non-volatile registers                 |
|vrsave  | Non-volatile 32-bit register           |

`r2` should point to `base of TOC` + 0x8000. This way, it can address 64kB of
memory starting at TOC using just an immediate (signed) offset. The initial `r2`
value should be located in function descriptor, pointed by `e_entry` in ELF
header in ELFv2. In this ABI version, a function can have two entry points: a
local one and a global one. The global entry point loads `r2` and/or other
registers and falls through to the local entry point.

For ELFv1, this is slightly different. The same function symbol, when used in
assembly, may point to `.opd` (official procedure descriptors) section when it
is loaded into a register, or directly to the entry point of the function when
used as a target for branch instruction. In the descriptor, the first doubleword
(64b) is the entry point, and the second one is the `r2` value for given
function.

[Descriptor format](https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html#FUNC-DES)

#### Stack

There is no hardware support for stack in PPC architecture, it is a concept
defined purely in the software.

`r1` is used as a stack pointer, it points to the lowest used address by a given
function (exception from this rule described below). Stack grows downwards and
SP is always quad-word-aligned (16 B). SP points to the back chain - a word that
is a pointer to the previous back chain (or NULL for the first frame). The
format of stack frame is strictly defined, it helps with back-tracing during
debugging, but the function does not need to reserve memory if it doesn't use
all of the fields (e.g. leaf functions do not need to save its LR, CR or other
volatile registers).

In PPC64 the 288 (0x120) bytes below the SP are volatile and can be used by a
function without preparing a new stack frame (if it is a leaf function). This is
similar to the "red zone" in SysV AMD64 ABI, it also needs to be protected from
interrupt handlers etc.

## Support in coreboot

coreboot-sdk has tools for big-endian only (`powerpc64-linux-gnu-`). Most Linux
distributions use little-endian, but still expect OPAL to be big-endian. Linux
kernel is capable of changing its endianness regardless of the state in which it
is started.

Changing the default settings using compiler/linker flags is not enough, there
are libraries that would have to be recompiled (`libgcc.a`) to support
little-endian compilation. The same goes for compiling for ELFv2 ABI, which
_may_ be required for passing control to the payload (Skiboot, Skiroot or
something else). In that case, it may be easier to jump to the payload using
custom assembler stub than to rewrite all of the code specific to given ABI up
to that point.

There is a tree for a QEMU platform for PPC64. It builds, but the first (and
only) instruction is a loop. It lacks CRT0, so it can't start the C code out of
the box. As a bare minimum, it requires a stack and a proper value in `r2`
register. ELFv1 ABI is used, so `r2` has to be read from `opd` section with code
like:

```
	lis		%r12, qemu_power8_main@ha
	addi	%r12, %r12, qemu_power8_main@l
	ld		%r2, 8(%r12)
	ld		%r12, 0(%r12)
	mtctr	%r12
	bctr
```

`@h`, `@l` and `@ha` are assembler operators. The first one specifies the higher
32 bits, second - lower 32 bits, third - high 32 bits accounting for the fact
that instructions like `addi` use signed immediate.

> x@ha = (x+0x8000)@h

With minimal CRT0-like setup described above, followed by a jump to
`qemu_power8_main()` platform hangs somewhere in the code for enabling the
serial output. The QEMU documentation does not describe how the serial port can
be accessed on PPC64. Even with serial output disabled in coreboot menu, the
console is not written to CBMEM. It's initialization code jumps into some random
place, resulting in invalid opcode at some point, or a different exception.

Linker script does not account for exception vectors, other than the reset
vector. Other vectors are filled with code of bootblock, which may account for
strange behavior that changes after modifications to unrelated, not yet executed
code. The region for those vectors can be reserved, but it requires growing the
bootblock area.

Without console output the platform boots up to `run_romstage()` in which it
fails trying to access FMAP. Here, the symptoms depend on whether QEMU emulates
POWER8 or POWER9: the former enters an infinite loop after heavy changes to the
contents of RAM, and for the latter QEMU exits with:

```
qemu: fatal: Trying to execute code outside RAM or ROM at 0x00fffffffffffffc
```

`-d guest_errors` is helpful here, on the error such as above it prints the
content of the registers at the time of error. Unfortunately, it doesn't print
which instruction caused this error or what were the stack contents.

This may (but doesn't have to) be caused by the fact that ROM is loaded into
the beginning of RAM, starting with address 0. This may be a problem on tests
like this:

```
	fmap = rdev_mmap(boot, offset, sizeof(struct fmap));

	if (fmap == NULL)
		return -1;
```

Another issue with FMAP and most likely other structures is that they are
created as little endian, and the code assumes that it uses the platform's
endianess.

After the next stage or payload is loaded, `r2` must be changed accordingly.
Code for loading ELF files may require modifications.

## Components of boot sequence

[More complete list](https://wiki.raptorcs.com/wiki/OpenPOWER_Firmware)

* BMC (sometimes FSP - Flexible Service Processor, different chip with similar
  function)
* SBE - [Self-Boot Engine](https://wiki.raptorcs.com/wiki/Self-Boot_Engine)
  - Is this what sets initial register values: devtree, PVR ePAPR magic?
* Hostboot - first piece of code running on host CPU.  Main task is memory
  initialization.
* Skiboot - performs wider platform initialization, including initialization of
  on-die PCI Express host bus controllers. It also implements OPAL.
* Skiroot - Linux kernel and userspace environment started by Skiboot
  - Petitboot - bootloader in the form of userspace application run
    automatically after Skiroot starts. It starts target kernel using `kexec`.

Raptor's [TODO list for coreboot](https://wiki.raptorcs.com/wiki/Coreboot/ToDo)
says about using coreboot instead of Hostboot, perhaps the non-OPAL part of
Skiboot can be ported as well? This would provide better isolation between the
hardware (initialization) and software (runtime services), but this would
require changes to existing split between repositories. Also, it is possible
that OPAL is heavily connected with hardware initialization code and it can't
exist without it.

It seems that the code (at least for Hostboot) uses ELFv1, even though
specification for ELFv2 is linked in the [documentation](https://wiki.raptorcs.com/wiki/Category:Documentation).

#### Components sizes

PNOR has 64 MB.

* Hostboot ~18 MB
  - hostboot.bin 532 KB
  - hostboot_bootloader.bin 24 KB - this part is copied to L3 cache by SBE (only
    this?)
  - hostboot_extended.bin 12 MB
  - hostboot_runtime.bin 5,4 MB
  - hostboot_securerom.bin 12 KB
* Skiboot ~415kB (XZ LZMA2 compression), 6.8 MB ELF
* zImage (Skiroot) is ~16 MB, rootfs ~65 MB (12 MB XZ compressed)

[PNOR presentation](https://www.slideshare.net/YutakaKawai/5-p9-pnor-and-open-bmc-overview-final)

The sizes above are for Zaius platform, everything below is for Talos.

Partition header may be 4 KB, at least it is where the data begins after the
offset reported in the [output](logs/pflash.log) of `pflash -i`.

The only found documentation about mapping components to partitions is a
[status page for reproducible builds](https://wiki.raptorcs.com/wiki/Firmware/Reproducible_Builds/Status).
There are also some scattered pieces of information on other pages, like HBEL
being used on [page about debugging Hostboot](https://wiki.raptorcs.com/wiki/Hostboot_Debug_Howto).

#### Hostboot analysis

It uses multi-threaded code for initialization, starting from the beginning.

High-level invocations of performed isteps can be found in
`src/usr/isteps/istepXX` directories. They only contain basic logic and
top-level function calls, the functions themselves are scattered in other
places.

Most of the isteps have similar form: get a list of processors and call
appropriate function(s) for all (or all except master) processors. This results
in many unnecessary invocations of exactly the same loop. However, in some cases
this may be better for asynchronous operations, like between the start of CBS
(CFAM Boot Sequencer) and waiting for SBE SEEPROM completion.

## Schematics analysis

* TPM connector uses both LPC and I2C (there are known I2C-specific attacks)
* VPD on I2C (min 256K EEPROM size)
* Lattice ICE40 FPGA
  - uses the same clock as TPM LPC:
    `TPM LPC Clock is only for debug. Double drop loads on 33MHz is not recommended for products`
* Aspeed AST2500
* BCM flash (FW):  MX25L25635FMI-10G
* BIOS flash (BOOT): MT25QL512ABB8ESF-0SIT
* both have DIP switch for WP
* BCM has multiple UARTs (2 connected to COM ports)
* PMC has two UARTs
* NVRAM: MX29GL128FUT2I-11G
* connections between BCM and CPU0:
  - LPC
  - PCIe E2 C (x1)
* between CPU0 and CPU1:
  - X Bus

[DDR4 spec](https://www.jedec.org/standards-documents/docs/jesd79-4a)

#### Notes on remote access

Network communication between host and BMC is blocked.

To access host serial console use `obmc-console-client` run from the BMC - the
Web interface is FUBAR. To exit from this tool use `<Return>~~.`. [Readme](https://github.com/openbmc/obmc-console)
tells to use `~.`, but this shuts down the SSH connection to the BMC.

Debian 10.5 is installed, along with SSH server (user "debian", password
"debian"). Some logs were obtained, see `logs` directory.

[Ultravisor](https://doc.kusakata.com/powerpc/ultravisor.html) privilege level
possibly is unsupported on this CPU, even though documentation suggests
otherwise. PVR for this platform:

```
[   54.170003237,7] CPU: Boot CPU PIR is 0x001c PVR is 0x004e1202
```

Power consumption of idle Debian is significantly lower than that of idle
Petitboot (around 73W and 122W, respectively), as reported by BMC.
