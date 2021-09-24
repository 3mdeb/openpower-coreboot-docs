# POWER9 core starting and power management

POWER9 supports different levels of sleep, called stop states. Different levels
turn off different parts of processor: clocks, register values, timing
facilities, voltage, both for core and cache. The most power saving states can
turn off whole quad, that is 4 cores along with their L2 and L3 caches.
Components that are higher in the hierarchy can be turned off only if all of the
lower level components are also turned off. This is similar to the C-states on
x86.

There are 16 possible stop states, but not all of them are implemented, and stop
level 15 is special. Power consumption is bigger for numerically lower stop
states. All POWER9 cores are powered off after platform starts. SBE then turns
on one core (always the first functional one), after initializing its cache
chiplet (cache is "closer" to the SBE so it has to be enabled first). The same
is done for other cores in isteps 16.1-16.2, or at runtime when requested
through OPAL call.

POWER9 does not support per-core frequency control. Frequency is always managed
at quad level. Voltage depends on the frequency so per-core voltage control is
possible only if other cores entered stop state high enough to be clocked off.

## Chips responsible for power management

Many different chips, with different architectures, are used to control power
operations. Each type of processor runs its own firmware that has to be loaded
(from possibly modified reference images) by another chip higher in hierarchy.
Exception to that is the first core started after reset, this is orchestrated
by the SBE directly. These chips are, starting from the top:

1. OCC - PPC405 core, part of OCC complex. It is tasked with loading, starting
   and control of 4 PPEs (Programmable PPC-lite Engines). Its code is loaded by
   hostboot, along with code for underlying chips, to main memory at HOMER
   (Hardware Offload Microcode Engine Region). Apart from power management, it
   also reads sensor values and passes them to BMC.
2. PGPE - PPE core, one of four included in OCC complex. This chip controls
   P-states - frequency and voltage of the POWER9 cores, but not sleep states.
   Code is loaded and execution ordered to be started by OCC.
3. SGPE - PPE core, one of four included in OCC complex. Controls deeper sleep
   states - those in which cache is turned off. Loaded and started by OCC.
4. CME - PPE core, located in L3 cache for each pair of POWER9 cores, that gives
   12 CMEs in whole CPU. It is loaded and started by SPGE when waking from
   deeper sleep states. Tasked with (re-)enabling the core itself.

Refer to figure 23-3 on page 303 of [POWER9 Processor's User Manual](https://wiki.raptorcs.com/w/images/c/ce/POWER9_um_OpenPOWER_v21_10OCT2019_pub.pdf).

#### Cache initialization

* Power on cache PFET controller
* Reset quad chiplet logic (quad chiplet is the same as cache chiplet), set up
  clocks
* Set up DCC (duty cycle control) and disable DCC bypass
* Initialize GPTR (general purpose test register) and TIME (?)
* Initialize quad DPLL
* Set up quad DCC skew adjusts
* Initialize (flush) quad chiplet logic except for chiplet rings done above
* Zero out all arrays
* Start quad clocks
* Apply SCOM initialization (the values may be different for IPL and runtime)

#### Core initialization

* Runtime only: determine which core to start, request PCB mux and wait for
  grant (IPL doesn't have to worry about concurrent access)
* Power on cache PFET controller
* Reset core chiplet logic, set up clocks
* Initialize GPTR (general purpose test register) and TIME (?)
* Initialize (flush) quad chiplet logic except for chiplet rings done above
* Zero out all arrays
* Start core clocks
* Apply SCOM initialization (the values may be different for IPL and runtime)

These steps are done only for runtime:
* CME (core management engine) blocks interrupts to the core
* CME loads core's HRMOR with address pointing to HOMER
* CME issues SRESET to all threads of the core
* Threads reload SPR values
* Threads load "real" HRMOR value (this requires synchronization of threads) and
  some SPRs that couldn't be loaded before and enter stop level 15 (special stop
  level)
* After all threads enter stop 15 the CME re-enables interrupts

## HOMER

Each CPU has its own HOMER. Hostboot reserves memory for 8 processors, 4M each,
regardless of how many are actually present (hotplug maybe?). It is allocated
always at the top of physical memory (OPAL specification). HOMER consists of 4
regions: OPMR, QPMR, CPMR and PPMR, i.e. OCC, Quad, Core, and Pstate PM region.
Each region has size of 1M, however except for the first one only a few dozens
kilobytes are actually used. More information about in-memory layout can be
found in `import/chips/p9/procedures/hwp/lib/p9_hcode_image_defines.H`,
`import/chips/p9/procedures/hwp/lib/p9_hcd_memmap_base.H` and [HOMER](homer.md).

The contents of HOMER are customized, based on the reference image found in
HCODE PNOR partition. That partition is a nested XIP Image which structure is
defined in `import/chips/p9/xip/p9_xip_image.h`.

## OCC

Structurally OCC also includes SGPE (GPE3) and PGPE (GPE2), although its
firmware (in OCC partition) includes code only for GPE0 and GPE1 with the rest
being provided by HCODE partition.

OCC and all GPEs have access to the same SRAM, part of which is used to
implement IPC via shared memory.  Other communication mechanisms include at
least doorbells, interrupts and SCOM (both data and OCB channels).

Parts of the complex operate independently in several threads and monitor each
other, so that if one component halts for whatever reason others can disable
themselves automatically.

Initialization of OCC is quite complicated due to interactions with PGPE, SGPE
and CMEs. Success of the initialization depends on all of its dependencies
working in sync, that is all of them need to be started in close succession one
after another or initialization will fail. If any of the components were
initialized earlier, they must be reset before proceeding.

Approximate startup procedure:
* Prepare code image for OCC
* Start SGPE
* Start PGPE
* Prepare bootcode for OCC in its SRAM
* Reset OCC to start its execution, it will enter Standby state
* Provide OCC configuration data (order in which data is sent is important!)
* Move OCC into Active state (mind that internally OCC goes first to Observation
  and then to Active)

See also:
* [Official OCC overview](https://github.com/open-power/docs/blob/master/occ/OCC_overview.md)
* [OCC Firmware Interface Specification for POWER9](https://raw.githubusercontent.com/open-power/docs/master/occ/OCC_P9_FW_Interfaces.pdf)

## Debugging

Debugging by verbose prints performed by coeboot is mostly useless when it comes
to low-level power management - when a core is stopped, it doesn't execute the
code, obviously. Most of the steps listed above are performed by different
chips, which don't have direct access to serial port.

`pdbg` can be used to read SCOM registers through PIB, but anything that has to
be done by a core (reading SPR, GPR, memory, or even `threadstatus`) uses a
special wake-up sequence, which, being a wake-up, messes up current sleep state.

### SGPE log

SGPE saves its log in a condensed format in OCC complex's SRAM. It uses 256
bytes long (by default) circular buffer.

#### Log format

```C
//This is the data that is updated (in the buffer header) every time we add
//a new entry to the buffer.
typedef union
{
    struct
    {
        uint32_t  tbu32;
        uint32_t  offset;
    };
    uint64_t word64;
}PkTraceState; //pk_trace_state_t;

#define PK_TRACE_IMG_STR_SZ 16

//Header data for the trace buffer that is used for parsing the data.
//Note: pk_trace_state_t contains a uint64_t which is required to be
//placed on an 8-byte boundary according to the EABI Spec.  This also
//causes cb to start on an 8-byte boundary.
typedef struct
{
    //these values are needed by the parser
    uint16_t            version;
    uint16_t            rsvd;
    char                image_str[PK_TRACE_IMG_STR_SZ];
    uint16_t            instance_id;
    uint16_t            partial_trace_hash;
    uint16_t            hash_prefix;
    uint16_t            size;
    uint32_t            max_time_change;
    uint32_t            hz;
    uint32_t            pad;
    uint64_t            time_adj64;

    //updated with each new trace entry
    PkTraceState        state;

    //circular trace buffer
    uint8_t             cb[PK_TRACE_SZ];
}PkTraceBuffer; //pk_trace_buffer_t;

extern PkTraceBuffer g_pk_trace_buf __attribute__((section (".sdata")));
```

Note that `state.offset` may be bigger than `PK_TRACE_SZ` - it indicates that
the buffer roller over and some entries were lost. `size` specifies the length
of the circular buffer, not including the size of header (in other words, `size`
is set to `PK_TRACE_SZ`). Entries in the buffer come in three different formats:
tiny, big or binary.

Tiny format always occupies 8 bytes. It consists of 16b string hash (more about
it later), 16b parameter and 32b timestamp (30 most significant bits) + format
type (2 bits, `1` defines tiny format). This format is used for strings that
take no argument or exactly one argument that fits in 2 bytes or less.

Big format is used when trace requires a parameter bigger than 2 bytes, or when
it has multiple parameters (up to 4). Every parameter is converted to `uint32_t`
and written in pairs to the buffer, in the order they appear in the trace
string. Writes are always aligned to 8B, so for odd number of parameters there
is an empty slot at the end. Entry footer, consisting of 16b hash, 8b `complete`
flag, 8b number of parameters and 32b of timestamp and type (`2` for big
format), is written **after** the parameters. This implies that the log has to
be parsed from end (`state.offset`) to start, with a possible roll over.

Binary format is almost identical to big format, except instead of number of
parameters, a number of bytes is specified in the footer. Type of binary format
is `3`.

#### String hashes

Strings are hashed at the compilation time and saved to `trexStringFile`,
located in `hcode-<revision_hash>/output/obj/stop_gpe_p9n<dd>`. Lines in that
file have the following format:

```
<hash>||<string>||<file>
```

Hashes are written as decimal numbers, for easier handling convert them to
hexadecimal with `awk -F'|' -vOFS='|' '{printf("%8.8x", $1); $1 = ""; print $0}' trexStringFile`:

```
...
d7a3bafa||Initializing External Interrupt Routing Registers||../../import/chips/p9/procedures/ppe/pk/gpe/gpe_init.c
d7a3c0a1||ERROR: L2 Clock Start Failed. HALT SGPE!||../../import/chips/p9/procedures/ppe_closed/sgpe/stop_gpe//p9_sgpe_stop_exit.c
d7a3c30b||ERROR: Failed to Release Cache %d PCB Slave Atomic Lock. Register Content: %x||../../import/chips/p9/procedures/ppe_closed/sgpe/stop_gpe//p9_sgpe_stop_entry.c
...
```

As you can see, every line starts with `d7a3` - this value is saved as
`hash_prefix` in the log header, and only the last 16 bits are saved in each
entry. The string itself has a format ready to use with `printf`-like functions.

#### Reading OCC SRAM

OCC memory can be read with OCC Control Bridge (OCB), accessible through SCOM.
Linear stream mode can be enabled, that way consecutive bytes can be accessed
without having to manually change the address before every read. To get the
address of the log buffer, find `g_pk_trace_buf` in
`hcode/output/images/stop_gpe_p9n<dd>/stop_gpe_p9n<dd>.map` - in this example it
is `0x00000000fff37e28`. That value has to be shifted left 32 bits.

```
        # enable stream mode
root@talos:~# pdbg -P pib putscom 0x0006D013 0x0800000000000000
        # disable circular mode = enable linear mode
root@talos:~# pdbg -P pib putscom 0x0006D012 0x0400000000000000
        # set OCB address - must be 8B aligned
root@talos:~# pdbg -P pib putscom 0x0006D010 0xfff37e2800000000
```

From now on, every read from `0x6D015` will read data from address specified
above and increase that address by 8:

```
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0x0002000073746f70 (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0x5f6770655f70396e (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0x3233000000036341 (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0xd7a30100fe2329af (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0x01bce39a00000000 (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0xffffffffeb1fc78e (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0x00000000000000c8 (/kernelfsi@0/pib@1000)
root@talos:~# pdbg -P pib getscom 0x0006D015
p0: 0x000000000006d015 = 0xbafa000014e03675 (/kernelfsi@0/pib@1000)
```

Reading from `0x6D010` will return address of *next* 8 bytes to read.

[Script automatizing reading from OCC SRAM](./scripts/dump_occ_sram.sh)

### PGPE log

Format matches that of SGPE with the size of 0x400 bytes.  Location and string
hashes are different obviously.

### CME log

CME uses the same format as SGPE. Of course, different `trexStringFile` and
`*.map` has to be used. Contrary to SGPE, CMEs can be powered off during deeper
STOP states. This destroys the contents of the log and all of CME SRAM.

#### Reading CME SRAM

Each CME has its own SRAM, which can be powered down independently of each other
CME and OCC complex. Because of that, it has to be read while CME is running,
which makes debugging less useful. Also, different set of registers has to be
used for each CME - in the following commands, `q` is quad number (0-5) and `m`
is either `0` for CME0 or `4` for CME1 for a given quad.

```
        # enable auto-incrementation of addresses
root@talos:~# pdbg -P pib putscom 0x1q012m0C 0x8000000000000000
        # need to subtract 0xffff8000 (SRAM base) from address and shift by 32
root@talos:~# pdbg -P pib putscom 0x1q012m0D 0x000066d800000000
```

After that read from `0x1q012m0E` and parse as previously. Contrary to accessing
from OCC SRAM, `0x1q012m0D` doesn't change its value - it holds the original
address.

[Script automatizing reading from CME SRAM](./scripts/dump_cme_sram.sh). Note
that it is hardcoded for CME0 of first quad.

### OCC log

OCC log has different format than the previous two - at least it can be parsed
from start to end, because arguments are saved after the header.

#### Log format

OCC logs are split into 3 parts: `g_trac_err_buffer`, `g_trac_inf_buffer` and
`g_trac_imp_buffer`. This makes dumping all entries harder, but it helps with
preserving important entries while informative ones can cause their buffers to
wrap. Each of those buffers has the size of 0x2000 bytes. They can be read in
the same way as described in [Reading OCC SRAM](#reading-occ-sram) section.

```
/*
 * Structure is put at beginning of all trace buffers
 */
typedef struct trace_buf_head {
    UCHAR ver;         /* version of this struct (1)                      */
    UCHAR hdr_len;     /* size of this struct in bytes                    */
    UCHAR time_flg;    /* meaning of timestamp entry field                */
    UCHAR endian_flg;  /* flag for big ('B') or little ('L') endian       */
    CHAR comp[16];     /* the buffer name as specified in init call       */
    UINT32 size;       /* size of buffer, including this struct           */
    UINT32 times_wrap; /* how often the buffer wrapped                    */
    UINT32 next_free;  /* offset of the byte behind the latest entry      */
    UINT32 te_count;   /* Updated each time a trace is done               */
    UINT32 extracted;  /* Not currently used                              */
}trace_buf_head_t;

/*
 * Timestamp and thread id for each trace entry.
 */
typedef struct trace_entry_stamp {
    UINT32 tbh;        /* timestamp upper part                            */
    UINT32 tbl;        /* timestamp lower part                            */
    UINT32 tid;        /* process/thread id                               */
}trace_entry_stamp_t;

/*
 * Structure is used by adal app. layer to fill in trace info.
 */
typedef struct trace_entry_head {
    UINT16 length;     /* size of trace entry                             */
    UINT16 tag;        /* type of entry: xTRACE xDUMP, (un)packed         */
    UINT32 hash;       /* a value for the (format) string                 */
    UINT32 line;       /* source file line number of trace call           */
}trace_entry_head_t;

/*
 * Parameter traces can be all contained in one write.
 */
typedef struct trace_entire_entry {
    trace_entry_stamp_t stamp;
    trace_entry_head_t head;
    UINT32 args[TRACE_MAX_ARGS + 1];
} trace_entire_entry_t;
```

`TRACE_MAX_ARGS` equals 5, `+ 1` is used for saving size of whole entry as the
last parameter. If there are less parameters, the entry takes appropriately less
space, but it is always a multiply of 8B. `head.length` is total size in bytes
of parameters, including implicit entry size "parameter".

#### OCC string hashes

Those are located in `occ-<revision>/obj/occStringFile` after building. They
have exactly the same format as previous components, except whole 32 bits are
used for hash - there is no constant prefix.

#### OCC errors

As part of an error log, pointer to which is included in poll response, OCC can
provide FFDC (helpful on panics because it includes address from which it was
called), full or partial logs of PGPE and all three logs of OCC. Size of error
buffers is fixed and the logs can be truncated.
