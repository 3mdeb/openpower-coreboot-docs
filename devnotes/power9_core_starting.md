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
found in `import/chips/p9/procedures/hwp/lib/p9_hcode_image_defines.H` and
`import/chips/p9/procedures/hwp/lib/p9_hcd_memmap_base.H`.

The contents of HOMER are customized, based on the reference image found in
HCODE PNOR partition. That partition is a nested XIP Image which structure is
defined in `import/chips/p9/xip/p9_xip_image.h`.
