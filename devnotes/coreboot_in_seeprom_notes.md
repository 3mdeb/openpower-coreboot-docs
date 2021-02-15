---

Printing through serial from assembly works. This means that LPC works and does
not require setup. Because output is buffered by BMC it is impossible to say
where exactly the checkstop happens. Also, I didn't check if UART transmit
register can accept further bytes, this may also introduce further errors.

---

Dump of MSR and SPRs from HBB (coreboot, infinite loop near the end of romstage)
and HBBL (infinite loop as the first instruction in SEEPROM). No value on HBBL
side means that it is the same as in HBB. No value under HBB means that either
register was not checked or checking put the platform in a state that didn't
allow further reads.

`-` after SPR number means that it is defined in SoC spec only, `+` - ISA only,
otherwise in both.

```
	HBB					HBBL
	===					====

MSR	0x9000000002802000			0x9000000000000000	// 0x2802000 set by coreboot
SPRs:
0-	0x000000000001c400	special SPR 0	0
1	0x0000000020040000	XER		0	// Fixed-Point Exception Register, may change
2-	0x0000000000123450	MTMSR		0	// all write only regs return this, maybe depends on CFAR?
3	0			(U?)DSCR
4-6-				special SPR 4-6	hangs
8	varies			LR		0	// Link register
9	0x00000000000c34f7	CTR		0	// Counter Register, used by e.g. loops
13	0			AMR
17	0			DSCR
18	0			DSISR
19	0			DAR
22	varies			DEC
26	0			SRR0
27	0x9000000000000000	SRR1		0	// First instructions in bl_start.S, MSR with TA cleared
28	0x0000000000123628	CFAR		0x0000000000003000	// Come-From Address Register, updated on ret from int or branch (debug)
29	0			AMR
48	0			PIDR
61	0			IAMR
64-95				BHRB (reserv)
128	0			TFHAR
129	0			TFIAR
130	0			TEXASR
131	0			TEXASRU
136	0			CTRL (mfspr)
144+	0			TIDR
152	write only		CTRL (mtspr)
153	0			FSCR
157	0			UAMOR
158	write only		GSR (mtspr)
159	0			PSPB
176	0			DPDES
180	0			DAWR0
186	0			RPR
187	0			CIABR
188	0			DAWR0
190	0			HFSCR
256	0			VRSAVE
259	0			SPRG3 (mfspr)
268	varies			TB (mfspr)
269	varies			TBU (mfspr)	// Time Base Upper, 32 most significant bits of TB
272-275	0			SPRG[0-3]
276-				SPRC
277-				SPRD
283	0			CIR (mfspr)
284	write only		TBL (mtspr)
285	write only		TBU (mtspr)
286	write only		TBU40 (mtspr)
287	0x00000000004e1202	PVR (mfspr)
304	0			HSPRG0
305	0			HSPRG1
306	0			HDSISR
307	0			HDAR
308	varies (ends with 0)	SPURR
309	varies (ends with 0)	PURR
310	0xfffffffad509a05e	HDEC		0xfffffffe972be50e	// constant when threads are quiesced, but DEC changes...
313	0x00000000f8000000	HRMOR		0x00000000f8200000	// set by HBBL
314	0			HSRR0
315	0			HSRR1
317-	0x0010000000800000	TFMR
318	0			LPCR
319	0			LPIDR
336	0			HMER
337	0			HMEER
338	0			PCR
339	0			HEIR
349	0			AMOR
446	0			TIR (mfspr)
447-463-			res (PC internal)
464	0			PTCR
465-475-			res (PC internal)
476-				res (msgclr)
477-				res (msgclrp)
478-				res (msgsndp)
479-				res (msgclru)
496-				USPRG0
497-				USPRG1
505-				URMOR				// access requires MSR.SMF (bit 40) to be set
506-				USRR0
507-				USRR1
511-				SMFCTRL				// access requires MSR.SMF (bit 40) to be set
768	0			SIER (mfspr)
769	0			MMCR2
770	0			MMCRA
771	0			PMC1
772	0			PMC2
773	0			PMC3
774	0			PMC4
775	0			PMC5
776	0			PMC6
779	0x0000000080000000	MMCR0
780	0			SIAR (mfspr)
781	0			SDAR (mfspr)
782	0			MMCR1 (mfspr)
784	0			SIER
785	0			MMCR2
786	0			MMCRA
787	0			PMC1
788	0			PMC2
789	0			PMC3
790	0			PMC4
791	0			PMC5
792	0			PMC6
795	0x0000000080000000	MMCR0
796	0			SIAR
797	0			SDAR
798	0			MMCR1
799-				IMC		0
800	0			BESCRS
801	0			BESCRSU
802	0			BESCRR
803	0			BESCRRU
804	0			EBBHR
805	0			EBBRR
806	0			BESCR
808-811				res (nop)
815	0			TAR
816	0			ASDR
823	0x00000000007f03ff	PSSCR
825-				res (MTXER)
826-				res (MFNIA)
848	varies			IC
849	varies			VTB
850-	0			LDBAR
851-				MMCRC
853-				PMSR
855	0x00000000007f03ff	PSSCR
861-				L2QOSR
880-				TRIG0
881-				TRIG1
882-				TRIG2
884-				PMCR
885-				RWMR
895-				WORT
896	0x0010000000000000	PPR
898	0x0000000000100000	PPR32
921-				TSCR
922-				TTR
1006-				TRACE
1008-	0x1480000000000000	HID			// initial val is 0x0400000000000000 according to docs
1023	0x0000000000000004	PIR (mfspr)
```

---

Tasks performed by HBBL which may impact the execution:

- set task priority to medium (high in comments)
  - already done by coreboot
  - noop? This should be default priority after boot, maybe warm reboot behaves
    differently
- clear MSR[TA] (bit 1)
  - undocumented bit
  - not actually performed - SRR0 and SRR1 are filled, but there is no `rfid`
    instruction, so MSR isn't changed
  - this bit is already clear on the entry (at least for cold boots)
  - followed by `lwsync`, `isync`, this is normal after some changes to MSR,
    not sure if it is required for TA bit because there is no documentation
  - tried in coreboot in SEEPROM, no difference
- set up initial TOC in r2
  - coreboot already does it
- set up initial stack in r1
  - in HBBL this is 64kB after `_start`
  - coreboot already does it
    - stack area at 1MB - 1.5MB, so even with HRMOR 2MB into L3 cache it is
      still less than 10MB
    - unless unusual address hashing function is used for cache tags
- `dcbz` before and after HBBL
  - security feature, zeroes cache
  - first range: blLoadSize, HRMOR+2MB
    - stack
    - HBB ECC working space
    - blLoadSize taken from structure passed by SBE, 0x8000 (32kB)
  - second range: HRMOR-2MB, HRMOR
    - HBB working space
    - HBB running space
  - not done by coreboot
- `_updates_and_setup`
  - adds HRMOR to `g_blData` and `g_blScratchSpace`
  - sets up TI info - header and `HBBL_TI_OFFSET` with HRMOR included
- `_load_exception_vectors` - see next section
- call `main`
  - written in C
  - no return, but calls back to assembly at the end
  - see below
- enterHBB
  - apply "ignore HRMOR" bit to the instruction addresses
    - done with properly prepared `blr`
  - set new HRMOR (always old HRMOR - 2MB, regardless of what the old HRMOR is)
  - set new URMOR to new HRMOR
    - only if MSR bit 41 (SMF) is enabled, otherwise this SPR is not accessible
    - "Due to bug in P9N, P9C early levels need to subtract op-code"
      - URMOR -= 0x7C797BA6
  - `isync`, `slbia`, `isync`
  - jump to HBB
    - `mtssr0`, `rfid`

---

Interrupts in HBBL (+ implemented, -unimplemented)

- (+) HBBL_system_reset
- (+) HBBL_machine_check
- (+) HBBL_data_storage
- (+) HBBL_data_segment
- (+) HBBL_inst_storage
- (+) HBBL_inst_segment
- (+) HBBL_external
- (+) HBBL_alignment
- (+) HBBL_prog_ex
- (+) HBBL_fp_unavail
- (+) HBBL_decrementer
- (+) HBBL_hype_decrementer
- (+) HBBL_privileged_doorbell
- (-) reserved (0xb00) - why is it defined if it is reserved?
- (+) HBBL_system_call
- (+) HBBL_trace
- (+) HBBL_hype_data_storage
- (+) HBBL_hype_inst_storage
- (+) HBBL_hype_emu_assist
- (+) HBBL_hype_maint
- (+) HBBL_syscall_hype_doorbell
- (+) HBBL_perf_monitor
- (+) HBBL_vector_unavail
- (+) HBBL_vsx_unavail
- (+) HBBL_fac_unavail
- (+) HBBL_hype_fac_unavail
- (+) HBBL_softpatch
- (+) HBBL_debug

All of them call bl_terminate(), some of them save SSR1 or DSISR. In any case,
they just report the exception with no further handling.

---

128MB = HBB running address, HBB HRMOR
129MB = HBB ECC working address
130MB = HBBL HRMOR
131MB = HBB working address

HBB HRMOR may be also at 4GB - 128MB (that is the case for the current code),
but all addresses are the same relative to each other. Comments in multiple
places mention only 128MB HRMOR.

What happens in C:

- `BOOTLOADER_TRACE`
  - just writes a byte to `g_blData->bl_trace`, reporting progress
- write "bootload" to scratch reg 3
  - may be checked by BMC?
- write address and size for dumping memory to scratch reg 1
  - 4MB, starting at HBB HRMOR
- copy LPC BAR passed by SBE to `g_blData`
  - simple data validation
  - not applicable to coreboot (unless we will need different LPC BAR)
- `getHBBSection`
  - `findTOC`
    - coreboot code for finding the top of flash is based on this so it uses
      the same address, but with "ignore HRMOR" bit set
    - in HBBL, the searching loop starts 0x9000 bytes below the top, so it finds
      backup PNOR TOC at the first try
      - we may implement this in coreboot, too
      - right now we check every 0x1000 from the very top of flash
      - but starting from higher addresses shouldn't generate checkstop
    - first does a dummy access (read first 4B of expected TOC)
      - buffer is at `g_blScratchSpace` for this and following reads
      - `handleMMIO`
        - uses cache-inhibited reads, normal writes
        - it also sets "ignore HRMOR" for both source and destination, even
          though `g_blScratchSpace` had this bit set already in assembly
    - check OPB Master Status Reg for error (LPC Addr 0xC0010000)
      - also uses `handleMMIO`
      - `bl_terminate` on error
    - copy whole TOC to a local buffer (0x8000 bytes)
      - also uses `handleMMIO`
  - parse TOC
    - works on copy, no HW touched, no need for `handleMMIO`
    - checksum calculated for all entries, not just HBB
    - all info is copied into another structure
- copy HBB PNOR partition
  - to HBB ECC working space if ECC used, to working space otherwise
  - `handleMMIO`
- remove ECC if used
  - copies from ECC working space to working space
- `setSecureData`
  - copy other data passed by SBE to `g_blData`
    - simple data validation, conversion
    - sets XSCOM and fields related to security
      - includes pointer to Secure ROM (SHA512 functions)
    - LPC BAR was copied earlier because it was used by `getHBBSection`
    - not applicable to coreboot (unless we will need different XSCOM BAR)
- `setKeyAddrMapData`
  - memcpy from SBE-to-HBBL data to `g_blData`
- `copyBlToHbtoHbLocation`
  - memcpy from `g_blData` to a place where HBB will look for it
  - not applicable to coreboot
- `verifyContainer`
  - noop if not using Secure Boot
  - otherwise `bl_terminate` if no valid Secure ROM
  - use Secure ROM to validate container (passes pointer to HBB working copy
    and a HW key hash from the last 64 bytes of HBBL, but no size of HBB?)
- copy from working space to running space
- write "starthbb" to scratch reg 3
- call `enterHBB`
  - does not return
  - see above

---

The most reliable information can be obtained by inserting `b .` instructions
to the coreboot binary in different places and checking whether BMC reports
watchdog timeout (meaning code before this infinite loop executed without an
error) or checkstop (something bad happened in the executed code). Because the
code is ECC protected, a whole ECC word + checksum (9 bytes) must be written,
but this is still much faster than writing whole bootblock for each test.

Before call to console_init: checkstop
Before call to bootblock_main_with_timestamp: checkstop
After loading TOC for main from OPD: timeout
In main, before creating stack frame: timeout
  // code writes to 16(r1), this is above our stack, but unused
In main, after creating stack frame: checkstop

This means that the faulting instruction is one these:

```
	std     r0,16(r1)
	stdu    r1,-112(r1)
```

Both of them write to stack. This is the first time anything is written. Even
when stack is set at the same address as in HBBL (i.e. 64kB above HRMOR) the
behaviour is the same.

None of the following (inserted between move to r1 and store) seems to help:

- `hwsync`
- `isync`
- `slbia`
- `eieio`
- `slbsync`
- `tlbsync`
- `sync`

`tlbia` is not implemented, causes a hypervisor emulation assistance interrupt.

// TODO: try with stack inside bootblock or in the 12kB space for vectors
