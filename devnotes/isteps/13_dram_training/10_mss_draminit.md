## mss_draminit: Dram initialize (13.10)

> a) p9_mss_draminit.C (mcbist) -- Nimbus
> b) p9c_mss_draminit.C (mba) -- Cumulus
>    - RCD parity errors are checked before logging other errors - HWP will exit with RC
>    - De-assert dram reset
>    - De-assert bit (Scom) that forces mem clock low - dram clocks start
>    - Raise CKE
>    - Load RCD Control Words
>    - Load MRS - for each dimm pair/ports/rank
>      - ODT Values
>      - MR0-MR6
> c) Check for attentions (even if HWP has error)
>    - FW
>      - Call PRD
>        - If finds and error, commit HWP RC as informational
>        - Else commit HWP RC as normal
>      - Trigger reconfig loop is anything was deconfigured

```
for each functional MCBIST
  MC01.MCBIST.MBA_SCOMFIR.CCS_MODEQ               // 0x070123A7
        // "It's unclear if we want to run with this true or false. Right now (10/15) this
        // has to be false. Shelton was unclear if this should be on or off in general BRS"
        [0]   CCS_MODEQ_CCS_STOP_ON_ERR =           0
        [1]   CCS_MODEQ_CCS_UE_DISABLE =            0
        [24]  CCS_MODEQ_CFG_CCS_PARITY_AFTER_CMD =  1
        [26]  CCS_MODEQ_COPY_CKE_TO_SPARE_CKE =     1   // Docs: "Does not apply for POWER9. No spare chips to copy to."

  for each functional MCA
    MC01.PORT0.SRQ.MBA_FARB5Q                     // 0x07010918
          // RESET_N should stay low for at least 200us (JEDEC fig 7) for cold boot. Who and when sets it low?
          // "Up, down P down, up N. Somewhat magic numbers - came from Centaur and proven to be the
          // same on Nimbus. Why these are what they are might be lost to time ..."
          [0-1] MBA_FARB5Q_CFG_DDR_DPHY_NCLK =          0x1     // 0b01     // 2nd RMW
          [2-3] MBA_FARB5Q_CFG_DDR_DPHY_PCLK =          0x2     // 0b10     // 2nd RMW
          [4]   MBA_FARB5Q_CFG_DDR_RESETN =             1                   // 3rd RMW (optional (?), only if changes)
          [5]   MBA_FARB5Q_CFG_CCS_ADDR_MUX_SEL =       1                   // 1st RMW (optional, only if changes)
          [6]   MBA_FARB5Q_CFG_CCS_INST_RESET_ENABLE =  0                   // 1st RMW (optional, only if changes)
    delay(500us)  // part of 3rd RMW, but delay is unconditional

  // JEDEC, fig 7,8: delays above and below end at the same point, they are not consecutive.
  // RDIMM spec says that clocks must be stable for 16nCK before RESET_n = 1. This is not explicitly ensured.
  // Below seems unnecessary, we are starting clocks at the same time as deasserting reset (are we?)
  delay(10ns)   // max(10ns, 5tCK), but for all DDR4 Speed Bins 10ns is bigger - JEDEC

  // draminit_cke_helper - this is done only for the first functional MCA because CCS_ADDR_MUX_SEL is set
  for first functional MCA
    // All of this may be used later, maybe creating a function is in order
    // Hostboot stops CCS before sending new programs. I'm not sure it is wise to do, unless there are infinite loops
    // MC01.MCBIST.MBA_SCOMFIR.CCS_CNTLQ          // 0x070123A5
    //        [all] 0
    //        [1]   CCS_CNTLQ_CCS_STOP = 1
    // timeout(50*10ns):
    //    if MC01.MCBIST.MBA_SCOMFIR.CCS_STATQ[0] (CCS_STATQ_CCS_IP) != 1: break          // 0x070123A6
    //    delay(10ns)

    MC01.MCBIST.CCS.CCS_INST_ARR0_00              // 0x07012315
          [all]   0
          // "ACT is high. It's a no-care in the spec but it seems to raise questions when
          // people look at the trace, so lets set it high."
          [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =     1
          // "CKE is high Note: P8 set all 4 of these high - not sure if that's correct. BRS"
          [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =      0xf
          [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =  3
          [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =  3
    MC01.MCBIST.CCS.CCS_INST_ARR1_00              // 0x07012335
          [all]   0
          // According to comments, 400 comes from JEDEC, but I have not seen this value anywhere.
          // What I have seen is tXPR = max(5nCK, tRFC(min) + 10ns). We can discard 5nCK, in any sane configuration
          // it is always smaller than the rest (this is probably defined to cover DLL off mode). tRFC(min) depends
          // on the memory density, it is 160, 260 or 350ns for 2, 4, 8Gb, respectively (16Gb has default and two
          // optional values, but it isn't supported by platform anyway). Unless we want to check memory density
          // here for each DIMM under MCBIST, we should use worst case of 350+10=360ns. This needs to be converted
          // to memory cycles, which (obviously) depends on the memory frequency. Average clock period tCK(avg)
          // varies from <1.5ns (max period for DDR4-1600) to 0.625ns (min period for DDR4-3200), using these numbers
          // we get from 240 to 576 cycles. Calculating backwards, 400 cycles would meet tXPR criteria for tCK = 0.9ns,
          // which is just below the middle of DDR4-2400 Speed Bin for 8Gb (meaning 2400MT/s will work, but 2666MT/s
          // may not, depending on the safety margin used by vendor). For 4Gb density (tXPR = 270ns) with 400 cycles
          // we get tCK = 0.675ns, which lands in DDR4-3200 bin, 2Gb density easily covers all defined DDR4 bins.
          // On the other hand, we wouldn't want to wait for almost 600 cycles on lower speeds...
          //
          // tl;dr version: for 400 cycles
          // - all 2Gb and 4Gb DIMMs should work
          // - 8Gb DIMMs <= 2400MT/s should work
          // - these are guaranteed by spec, rest depends on margins used by vendors
          //
          // -1 is there because CCS does not wait for IDLES for the last command before clearing IP (in progress) bit,
          // so we must use one separate DES instruction at the end
          [0-15]  CCS_INST_ARR1_00_IDLES =    400 - 1
          [59-63] CCS_INST_ARR1_00_GOTO_CMD = 1

    --------------- begin of CCS finalization and execution ----------------
    // Final DES
    MC01.MCBIST.CCS.CCS_INST_ARR0_01              // 0x07012316
          [all]   0
          // "ACT is high. It's a no-care in the spec but it seems to raise questions when
          // people look at the trace, so lets set it high."
          [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =     1
          // "CKE is high Note: P8 set all 4 of these high - not sure if that's correct. BRS"
          [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =      0xf
          [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =  3
          [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =  3
    MC01.MCBIST.CCS.CCS_INST_ARR1_01              // 0x07012336
          [all]   0
          [58]    CCS_INST_ARR1_00_CCS_END = 1
    // Select ports
    MC01.MCBIST.MBA_SCOMFIR.MCB_CNTLQ             // 0x070123DB
          // Broadcast mode is not supported, set only one bit at a time
          [2-5]   MCB_CNTLQ_MCBCNTL_PORT_SEL = bitmap with MCA index  // not always 0x8 (port 0), it may not be functional

    // Lets go
    MC01.MCBIST.MBA_SCOMFIR.CCS_CNTLQ          // 0x070123A5
          [all] 0
          [0]   CCS_CNTLQ_CCS_START = 1
    // Note that these timeouts may be different for different CCS invocations
    delay(400 memclocks)  // initial delay of poll method, maybe it should be slightly lower? Code execution takes time
    timeout(50*10ns):
      if MC01.MCBIST.MBA_SCOMFIR.CCS_STATQ[0] (CCS_STATQ_CCS_IP) != 1: break          // 0x070123A6
      delay(10ns)
    if MC01.MCBIST.MBA_SCOMFIR.CCS_STATQ != 0x40..00: report failure  // only [1] set, others 0
    --------------- end of CCS finalization and execution ----------------


    cleanup_from_execute()  - no-op if not LRDIMM_CAPABLE

  // "Per conversation with Shelton and Steve, turn off addr_mux_sel after the CKE CCS but before the RCD/MRS CCSs"
  // TODO: is the last DES being repeated still after this? We need CKE high later so RCD stays on, and also for DRAM
  // per specification CKE must stay high until the initialization sequence is finished, including tZQinit and tDLLK.
  // Is there any way for us to test it? Or is there someone who knows?
  for each functional MCA
    MC01.PORT0.SRQ.MBA_FARB5Q                     // 0x07010918
          [5]   MBA_FARB5Q_CFG_CCS_ADDR_MUX_SEL = 0

  // Load RCD control words
  for each functional MCA
    rcd_load(MCA)
      // Hostboot supposedly is data-driven, however, most of the times it is the code that prepares that data. See e.g.
      // src/import/chips/p9/procedures/hwp/memory/lib/dimm/eff_dimm.C. It is full of (get attr, attr[port][dimm] = const,
      // set attr) sequences. Sometimes the value depends on e.g. memory frequency or timings, usually read from SPD data.
      // Sometimes values are unnecessarily converted multiple times between micro-/nano-/picoseconds and memory cycles.
      // This is also true for Register Control Words.
      //
      // There are two ways of accessing RCWs: in-band on the memory channel as an MRS command ("MR7") or through I2C.
      //
      // From JESD82-31: "For changes to the control word setting, (...) the controller needs to wait tMRD after
      // _the last control word access_, before further access _to the DRAM_ can take place".
      // MRS is passed to rank 0 of the DRAM, but MR7 is reserved so it is ignored by DRAM. tMRD (8nCK) applies here,
      // unless longer delay is needed for RCWs which control the clock timing (see JESD82-31 for list of such). This
      // makes sense from DRAMs point of view, however we are talking to the Registering Clock Driver (RCD), not DRAM.
      // From parts marked in the sentence above one may assume that only one delay at the end is necessary and RCWs
      // can be written back to back; however, in the same document in table 141 tMRD is defined as "Number of clock
      // cycles between two control word accesses, MRS accesses, or any DRAM commands".
      //
      // I2C access to RCWs is required to support byte writes, and writes in blocks of up to double word (32b) size.
      // Bigger blocks are not required. Reads must always be 32b, 32b-aligned blocks, even when read as bytes. RCD
      // ignores the two lowest bits so unaligned accesses would return shifted values. RCWs are tightly packed in I2C
      // space, so it is not possible to write just one 4b RCW without writing its neighbor. This is especially
      // important for F0RC06 - Command Space Control Word, as it it able to reset the state of RCD. For this reason,
      // the mentioned register has NOP command (all 1's). JESD82-31 does not specify timeouts required for such
      // multi-RCWs writes, or any other writes. These are not MRS accesses, so it would be strange to apply those
      // timeouts. Perhaps only the registers that actually change the clock settings require time to stabilize.
      // On the other hand, I2C is relatively slow, so it is possible that the write itself is long enough.
      // RCD I2C address is 0xBx, it should be located on the same bus as SPD (number 3 in Petitboot). It uses a bit
      // unusual bus command encoding, see section 3.3 in JESD82-31 and/or `rcd_i2c_dump.sh` in this repository for
      // an example of reading and writing register values.
      // TODO: calculate whether we need additional timeouts.
      // TODO2: do we need draminit_cke_helper() when using I2C access, too? Maybe it should be done after RCD is set?
      //
      // From a comment in the code:
      // > Secret sauce : insider knowledge
      // > The JEDEC spec doesn't mention anything about ordering of RCWs
      // > but a supplier informed us that we need to send with CKE low:
      // > 4-bit RCWs first (excluding RC09), followed by 8-bit RCWs.
      // > Then with CKE high (We raise it w/the RCW): 4-bit RC09
      // This may come from an older version of spec, JESD82-31A precises (second paragraph in 2.23) that CKE is
      // "don't care" as long as CKE power down feature is disabled. This is the default setting, it is controlled
      // by a bit in F0RC09, which is why it works when this register is written as the last one. This may also be
      // somehow connected with "BUG?" in MC01.MCBIST.CCS.CCS_INST_ARR0_00 and MC01.MCBIST.CCS.CCS_INST_ARR0_01.
      //
      // > You're probably asking "Why always turn off CKE's? What is this madness?"
      // > Well, due to a vendor sensitivity, we need to have the CKE's off until we run RC09 at the very end
      // > Unfortunately, we need to have the CKE's off on the DIMM we are running second
      // > We also don't want to turn off the CKE's on the DIMM we are running first
      // > Therefore, we want to setup all RCW commands to have CKE's off across both DIMM's
      // > We then manually turn on the CKE's associated with a specific DIMM
      //
      // I2C probably doesn't have to have CKE set to any particular value.
      //
      // Except for F0RC09, EVERY other register is written once, in order. It doesn't make any sense for registers
      // F0RC4x, F0RC5x and F0RC6x. These are used as a windowed access to other Function spaces (F1, F2, ... F15),
      // and the access itself is initiated by a command written to F0RC06. Host is responsible for putting DRAM in
      // the MPR mode before sending read command, and issuing the MPR read operation after that command. Writes are
      // performed by the RCD.
      //
      // Registers in Hostboot are loaded with the help of CCS. Here are just their values, for loading procedure see
      // src/import/chips/p9/procedures/hwp/memory/lib/dimm/ddr4/control_word_ddr4.H or use I2C. Default values are
      // all 0s on reset. Note these registers are little endian, bit 0 is LSB, the same goes for SPD data.
      //
      // Before accessing RCWs Hostboot drives CKE low by sending PDE command (Power Down Entry). Probably not needed
      // for I2C.
      F0RC00  = 0x0   // depends on reference raw card used, sometimes 0x2 (ref. A, B, C and custom?)
                      // Maybe SPD bytes 137 and 138 can tell this?
      F0RC01  = 0x0   // depends on reference raw card used, sometimes 0xC (ref. C?). JESD82-31: "The system must
                      // read the module SPD to determine which clock outputs are used by the module", but which
                      // bytes? We can also download ref cards schematics and see which clock signals are connected.
      F0RC02  =
          [0] = 1 if(!(16Gb density && x4 width))     // disable A17?     // Why not use SPD[5]?
                      // Hostboot waits for tSTAB, however it is not necessary as long as bit 3 is not changed.
      F0RC03  =
          [0-1] SPD[137][4-5]   // Address/Command drive strength
          [2-3] SPD[137][6-7]   // CS drive strength
                      // There is also a workaround for NVDIMM hybrids, not needed for plain RDIMM
      F0RC04  =
          // BUG? Hostboot reverses bitfields order for RC04, 05
          [0-1] SPD[137][2-3]   // ODT drive strength
          [2-3] SPD[137][0-1]   // CKE drive strength
                      // There is also a workaround for NVDIMM hybrids, not needed for plain RDIMM
      F0RC05  =
          [0-1] SPD[138][2-3]   // Clocks drive strength, A side (1,3)
          [2-3] SPD[138][0-1]   // Clocks drive strength, B side (0,2)
                      // There is also a workaround for NVDIMM hybrids, not needed for plain RDIMM
      F0RC06  = 0xf   // This is a command register, either don't touch it or use NOP (F)
      F0RC07  = 0x0   // This is a command register, either don't touch it or use NOP (0)
      F0RC08  =
          [0-1] =
              1 if master ranks == 4 (SPD[12])        // C0 and C1 enabled      // master rank AKA package rank
              3 if not 3DS (check SPD[6] and SPD[10]) // all disabled
              2 if slave ranks <= 2                   // C0 enabled             // slave rank AKA logical rank...
              1 if slave ranks <= 4                   // C0 and C1 enabled      // ...SPD[6] Die Count
              0 otherwise (3DS with 5-8 slave ranks)  // C0, C1 and C2 enabled
          [3] = 1 if(!(16Gb density && x4 width))     // disable A17?     // Why not use SPD[5]?
      F0RC09  =
          [2] =
              // TODO: add test for it. Maybe leave it as 0 for now, this is "just" for power saving.
              0 if this DIMM's ODTs are used for writes or reads that target the other DIMM on the same port
              1 otherwise
          [3] = 1     // Register CKE Power Down. CKE must be high at the moment of writing to this register and stay high.
                      // TODO: For how long? Indefinitely, tMRD, tInDIS, tFixedOutput or anything else?
      F0RC0A  =
          [0-2] =     // There are other valid values not used by Hostboot
              1 if 1866 MT/s
              2 if 2133 MT/s
              3 if 2400 MT/s
              4 if 2666 MT/s
      F0RC0B  = 0xe   // External VrefCA connected to QVrefCA and BVrefCA
      F0RC0C  = 0     // Normal operating mode
      F0RC0D  =
          [0-1] =         // CS mode
              3 if master ranks == 4 (SPD[12])    // encoded QuadCS
              0 otherwise                         // direct DualCS
          [2] = 1         // RDIMM
          [3] = SPD[136]  // Address mirroring for MRS commands
      F0RC0E  = 0xd     // Parity enable, ALERT_n assertion and re-enable
      F0RC0F  = 0       // Normal mode
      F0RC1x  = 0       // Normal mode, VDD/2
      F0RC2x  = 0       // Normal mode, all I2C accesses enabled
      F0RC3x  =
              0x1f if 1866 MT/s
              0x2c if 2133 MT/s
              0x39 if 2400 MT/s
              0x47 if 2666 MT/s
      F0RC4x  = 0       // Should not be touched at all, it is used to access different function spaces
      F0RC5x  = 0       // Should not be touched at all, it is used to access different function spaces
      F0RC6x  = 0       // Should not be touched at all, it is used to access different function spaces
      F0RC7x  = 0       // Value comes from VPD, 0 is default, it doesn't seem to be changed anywhere in the code...
      F0RC8x  = 0       // Default QxODT timing for reads and for writes
      F0RC9x  = 0       // QxODT not asserted during writes, all ranks
      F0RCAx  = 0       // QxODT not asserted during reads, all ranks
      F0RCBx  =
          [0-2] =       // Note that only the first line is different than F0RC08 (C0 vs. C0 & C1)
              6 if master ranks == 4 (SPD[12])        // C0 enabled             // master rank AKA package rank
              7 if not 3DS (check SPD[6] and SPD[10]) // all disabled
              6 if slave ranks <= 2                   // C0 enabled             // slave rank AKA logical rank...
              4 if slave ranks <= 4                   // C0 and C1 enabled      // ...SPD[6] Die Count
              0 otherwise (3DS with 5-8 slave ranks)  // C0, C1 and C2 enabled

      // After all RCWs are set, DRAM gets reset "to ensure it is reset properly". Can we ever avoid it?
      // Comment: "Note: the minimum for a FORC06 soft reset is 32 cycles, but we empirically tested it at 8k cycles"
      // Shouldn't we rather wait (again!) for periods defined in JESD79-4C? (200us low and 500us high)
      F0RC06 = 0x2      // Set QRST_n to active (low)
      delay(8000 memclocks)
      F0RC06 = 0x3      // Set QRST_n to inactive (high)
      delay(8000 memclocks)

      // Dumped values from currently installed DIMM, from Petitboot:
      // 0xc7 0x18 0x42 0x00 0x00 0x00 0x00 0x00    VID[2], DID[2], RID[1], 3x reserved
      // 0x02 0x01 0x00 0x03 0xcb 0xe4 0x40 0x0d    F0RC00-0F (4b each)
      // 0x00 0x00 0x47 0x00 0x00 0x00 0x00 0x00    F0RC1x-8x (8b each)
      // 0x00 0x00 0x07                             F0RC9x-Bx (8b each), then all zeroes (Error Log Registers)

      mss::ccs::workarounds::hold_cke_high()
        // In Hostboot, all of the above RCW instructions are not executed yet at this point. This function iterates
        // over all of them and latches CKE high after it is first set high (which should be RC09) for all instructions
        // that follow. Since I2C access does not depend on CKE, perhaps we may ignore it and leave CKE high at all
        // times. Note that CKE was set high in draminit_cke_helper and should still be in this state, unless unsetting
        // CCS_ADDR_MUX_SEL or CCS completion changes it.
        //
        // My suggestion is to not touch CKE after draminit_cke_helper() and see if it works. If not, try doing DES with
        // CKE high after/instead of delays after toggling QRST_n in F0RC06.

  // Load data buffer control words (BCW)
  bcw_load(i_target) - not LRDIMM -> no data buffers

  // Load MRS
  mrs_load(i_target)
    // Programming the Mode Registers consists of entering special mode using MRS (Mode Register Set) command and sending
    // MR# values, one at a time, in a specific order (3,6,5,4,2,1,0). Those values are sent using address lines,
    // including bank and bank group lines, which select which MR to write to. One of the implications is that these
    // values cannot be read back. PHY controller holds the mirrors of last written values in its registers, but the
    // mapping of bits is not clear. This mirror is RW, so there is a possibility that the values are not the same as the
    // real ones (but this would be a bad idea as these bits are used by a controller). It gets further complicated when
    // PDA mode was used at any point, as there is just one mirror register per rank pair.
    //
    // We have to write a whole register even when changing just one bit, this means that we have to remember what was
    // written, or be able to (re)generate valid data. For this platform we have CCS which can be programmed to push all
    // MRs in one sequence of instructions, including all required timeouts. There are two main timeout parameters: tMRD
    // (minimal amount of time between two MRS commands) and tMOD (time between MRS and non-MRS and non-DES command). For
    // all Speed Bins tMRD = 8nCK, tMOD = max(24nCK, 15ns) = 24nCK. Exceptions to those are:
    // - gear down mode
    // - PDA mode
    // - settings to command & address lines: C/A parity latency, CS to C/A latency (only tMRC doesn't apply)
    // - VrefDQ training
    // - DLL Enable, DLL Reset (only tMOD doesn't apply)
    // - maximum power saving mode (only tMOD doesn't apply)
    //
    // MRS are written per rank usually, although most of them must have the same values across the DIMM or even port.
    // There are some settings that apply to individual DRAMs instead of whole rank (e.g. Vref in MR6). Normally settings
    // written to MR# are passed to each DRAM, if individual DRAM has to have its settings changed independently of others
    // we must use Per DRAM Addressability (PDA) mode. PDA is possible only if write leveling was performed.
    //
    // CCS is per MCBIST, so we need at most 4 (ports) * 2 (DIMMs per port) * 2 (master ranks per DIMM)
    // * 2 (A- and B-side) * (7 (# of MRS) + 1 (final DES)) = 256 instructions. CCS holds space for 32 instructions, so we
    // have to divide it and send a set of instructions per DIMM or even smaller chunks.
    //
    // TODO: is 4 ranks on RDIMM possible/used? PHY supports two ranks per DIMM (see 2.1 in any of the volumes of the
    // registers specification), but Hostboot has configurations even for RDIMMs with 4 master ranks (see xlate_map vector
    // in src/import/chips/p9/procedures/hwp/memory/lib/mc/xlate.C). Maybe those are counted in different places, i.e.
    // before and after RCD, and thanks to Encoded QuadCS 4R DIMMs are visible to the PHY as 2R devices?
    //
    // Just the MR# values, send each one in this order using procedure described below. Remember about address mirroring!
    // These bits are also in little endian order. All multi-bit fields must be swizzled.
    --------------------------------------------------
    MR3 =
      [all] 0
      [A3]      1 if 2N mode      // Geardown Mode. Is this why Hostboot doubles tMRD and tMOD?
      [A10-A9]  1                 // CRC to WR latency. 1 = 5nCK for 1600 < freq <= 2666.
      [A8-A6]   0                 // Fine Granularity Refresh. Default 0, depends on MR4[A3]. Attr in MRW, whatever that is.
                                  // On-the-fly FGR is not supported on RDIMM.
      [A5]      0                 // Temp readout. It is 0, won't it impact `sensors`?
    MR6 =
      [all] 0
      [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f       // This is "don't care" when A7 is not set
      [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40       // This is "don't care" when A7 is not set
      // BUG? Hostboot doesn't take into account minimal values per freq from JESD79-4C
      // Keep in mind that SPD[40] is unsigned MTB and SPD[117] is signed FTB
      [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4
    MR5 =
      [all] 0
      // ATTR_MSS_VPD_MT_DRAM_RTT_PARK is saved as a value in Ohms. JEDEC uses seemingly strange mapping to describe
      // these values in MR5, however there is some logic to it. All possible values are (240/N) Ohms, where N is between
      // 1 and 7. This N is a value saved into MR5, with reversed bit order, so 60 = 240/4 => 0b001, 80 = 240/3 => 0b110
      // etc, so just don't swizzle this field. A value of 0 (both in VPD as well as in MR5) disables RTT_PARK.
      [A6-A8] 240/ATTR_MSS_VPD_MT_DRAM_RTT_PARK     // integer division rounds properly for 7/34 (watch out for 0)
    MR4 =
      [all] 0
      // code style BUG: src/import/chips/p9/procedures/hwp/memory/lib/dimm/ddr4/mrs04.C#65 uses wrong enum, not harmful
      [A2]  0         // ATTR_MSS_MRW_TEMP_REFRESH_RANGE, default 0
      [A3]  0         // ATTR_MSS_MRW_TEMP_REFRESH_MODE, default 0, impacts MR3[A8-A6]: if this == 0 then MR3[A8-A6] = 0
      [A11] (ATTR_MSS_VPD_MT_PREAMBLE & 0xf0) >> 4
      [A12] (ATTR_MSS_VPD_MT_PREAMBLE & 0x0f)
    MR2 =
      [all] 0
      [A5-A3] =
          if (ATTR_MSS_VPD_MT_PREAMBLE & 0x0f) == 1:    // 2nCK write preamble
            4 if 2400 MT/s      // CWL = 14
            5 if 2666 MT/s      // CWL = 16
          if (ATTR_MSS_VPD_MT_PREAMBLE & 0x0f) == 0:    // 1nCK write preamble
            1 if 1866 MT/s      // CWL = 10
            2 if 2133 MT/s      // CWL = 11
            3 if 2400 MT/s      // CWL = 12
            4 if 2666 MT/s      // CWL = 14
      [A7-A6] =   // Low Power Auto Self Refresh, doesn't use Auto, just Manual mode (normal or extended temp range)
          0 if ATTR_MSS_MRW_REFRESH_RATE_REQUEST == ATTR_MSS_MRW_REFRESH_RATE_REQUEST_SINGLE*
          2 if ATTR_MSS_MRW_REFRESH_RATE_REQUEST == ATTR_MSS_MRW_REFRESH_RATE_REQUEST_DOUBLE*
      // ATTR_MSS_VPD_MT_DRAM_RTT_WR is also saven in Ohms, however this time there is no logic when it comes to
      // VPD<->MR2 mapping. It seems as if JEDEC added third bit later.
      // '0' seems to be the safest option, but it is not always the case in VPD.
      // See "Write leveling - pre-workaround" (and post-workaround) in 13.11, maybe write 0 here and don't do pre-?
      // VPD    MR2
      // 0      0b000     Dynamic ODT Off
      // 80     0b100     RZQ/3
      // 120    0b001     RZQ/2
      // 240    0b010     RZQ/1
      // 1      0b011     Hi-Z
      [A11-A9]  f(ATTR_MSS_VPD_MT_DRAM_RTT_WR)
      [A12]     ATTR_MSS_MRW_DRAM_WRITE_CRC
    MR1 =
      [all] 0
      [A0]      1       // DLL Enable. There is a note about reversed states in JESD79-4C, not sure what this is about...
      [A2-A1] =         // Output Driver Impedance Control
          0 if ATTR_MSS_VPD_MT_DRAM_DRV_IMP_DQ_DQS == ENUM_ATTR_MSS_VPD_MT_DRAM_DRV_IMP_DQ_DQS_OHM34  // 34 in VPD
          1 if ATTR_MSS_VPD_MT_DRAM_DRV_IMP_DQ_DQS == ENUM_ATTR_MSS_VPD_MT_DRAM_DRV_IMP_DQ_DQS_OHM48  // 48 in VPD
      // ATTR_MSS_VPD_MT_DRAM_RTT_NOM is saved as a value in Ohms. JEDEC uses seemingly strange mapping to describe
      // these values in MR1, however there is some logic to it. All possible values are (240/N) Ohms, where N is between
      // 1 and 7. This N is a value saved into MR5, with reversed bit order, so 60 = 240/4 => 0b001, 80 = 240/3 => 0b110
      // etc, so just don't swizzle this field. A value of 0 (both in VPD as well as in MR5) disables RTT_PARK.
      // See also ATTR_MSS_VPD_MT_DRAM_RTT_PARK in MR5.
      [A8-A10]  240/ATTR_MSS_VPD_MT_DRAM_RTT_NOM      // integer division rounds properly for 7/34 (watch out for 0)
      [A11] =
          1 if x8     // SPD[12][0-2] == 1
          0 if x4     // SPD[12][0-2] == 0
    // MR0 has two non-contiguous bit fields, numbers in brackets are sorted MSB to LSB.
    MR0 =
      [all] 0
      // Common for all DIMMs in this domain (?), calculated in istep 7.3, encoded! Values are not in order.
      [A12,A6-A4,A2]  CAS Latency
      [A8]            1             // DLL Reset, "Default is to reset DLLs during IPL"
      // JESD79-4C also specifies min of 15ns, we should probably use max of those two values. Field is encoded,
      // not 1:1, and not in order! (values 22 and 24 are swapped)
      [A13,A11-A9]    tWR = conv_to_nCK(SPD[41-42])
    --------------------------------------------------

    for each DIMM
      // Procedure for sending MRS through CCS
      //
      // We need to remember about two things here:
      // - RDIMM has A-side and B-side, some address bits are inverted for B-side; side is selected by DBG1 (*)
      // - odd ranks may or may not have mirrored lines, depending on SPD[136].
      // *) When mirroring is enabled DBG0 is used for odd ranks to select side, instead of DBG1.
      //
      // Because of those two reasons we cannot simply repeat MRS data for all sides and ranks, we have to do some juggling
      // instead. Inverting is easy, we just have to XOR with appropriate mask (special case for A17, it is not inverted if
      // it isn't used). Mirroring will require manual bit manipulations, we cannot use two pairs of shift and mask because
      // A11/A13 and BG0/BG1 are not next to each other in the CCS instruction registers.
      //
      // There are no signals that are mirrored but not inverted, which means that the order of those operations doesn't
      // matter.
      --------------------------------------------------
      // Rank 2n, side A
      MC01.MCBIST.CCS.CCS_INST_ARR0_{00, 01, .., 31}    // 0x07012315 + N
            [all]   0
            [0-13]  CCS_INST_ARR0_00_CCS_DDR_ADDRESS_0_13 =   A0-A13 to be written to MR#
            [17-19] CCS_INST_ARR0_00_CCS_DDR_BANK_0_1, CCS_INST_ARR0_00_CCS_DDR_BANK_GROUP_0 = MR#    // swizzled!
            [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =           1
            // "CKE is high Note: P8 set all 4 of these high - not sure if that's correct. BRS"
            [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =            0xf
            // The encoding of CS signals in Hostboot seems like it would fail if DIMM1 has more ranks than DIMM0
            // Below depends on CS mode and which DIMM are we on:
            // In encoded QuadCS (i.e. when we have 4 master ranks) on DIMM0:
            //    - rank 0: CSN_0_1 = 0b01, CID_0_1 = 0b00, CSN_2_3 = 0b11
            //    - rank 1: CSN_0_1 = 0b01, CID_0_1 = 0b11, CSN_2_3 = 0b11
            //    - rank 2: CSN_0_1 = 0b10, CID_0_1 = 0b00, CSN_2_3 = 0b11
            //    - rank 3: CSN_0_1 = 0b10, CID_0_1 = 0b11, CSN_2_3 = 0b11
            // In direct DualCS on DIMM0:
            //    - rank 0: CSN_0_1 = 0b01, CID_0_1 = 0b00, CSN_2_3 = 0b11
            //    - rank 1: CSN_0_1 = 0b10, CID_0_1 = 0b00, CSN_2_3 = 0b11
            //    - rank 2: CSN_0_1 = 0b11, CID_0_1 = 0b00, CSN_2_3 = 0b01
            //    - rank 3: CSN_0_1 = 0b11, CID_0_1 = 0b00, CSN_2_3 = 0b10
            // On DIMM1 in both cases CSN_0_1 and CSN_2_3 are exchanged
            [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =        see above
            [34-35] CCS_INST_ARR0_00_CCS_DDR_CID_0_1 =        see above
            [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =        see above
      MC01.MCBIST.CCS.CCS_INST_ARR1_{00, 01, .., 31}    // 0x07012335 + N
            [all]   0
            // Timeout. This is tMRD (8nCK), except for the last one (MR0): tMOD (max(24nCK, 15ns)).
            // Hostboot uses doubled tMRD and tMOD values "to increase margin per lab request", it also waits for
            // tDLLK after the last write: "Adding Per Glancy's request, to ensure DLL locking time". The tDLLK is later
            // added again for ZQ calibration, one of these would be more than enough. Per spec, we need to wait for
            // tDLLK between write to MR0 and normal operation, we also need to wait for tZQinit between ZQCL (done in
            // next istep) and normal operation. This means that we need at least max(tDLLK, tMOD + tZQinit) between
            // write to MR0 and normal operation. tDLLK is lower than or equal to tZQinit for all defined DDR4 speed
            // bins, so we may use only tZQinit and forget about tDLLK altogether.
            //
            // Also, it is not clear if the delay is needed between A- and B-side:
            // > Not sure if we can get tricky here and only delay after the b-side MR. The question is whether the delay
            // > is needed/assumed by the register or is purely a DRAM mandated delay. We know we can't go wrong having
            // > both delays but if we can ever confirm that we only need one we can fix this. BRS
            //
            // TODO: test if all of this is needed, we may have 4x longer delays than necessary. Definitely tDLLK doesn't
            // have to be separated between sides (or even components higher in topology), however the worst case would
            // have to be used.
            [0-15]  CCS_INST_ARR1_00_IDLES =    see above
            [59-63] CCS_INST_ARR1_00_GOTO_CMD = (index of next command)
      // Rank 2n, side B
      MC01.MCBIST.CCS.CCS_INST_ARR0_{00, 01, .., 31}    // 0x07012315 + N
            [all]   0
            [0-13]  CCS_INST_ARR0_00_CCS_DDR_ADDRESS_0_13 =   (A0-A13 to be written to MR#) XOR (A3-A9,A11,A13)
            // A17 is low for all MRS commands, but here it is inverted. It is not clear if we can drive it high when it
            // is not used. If we can, we may just write 1 here always, which would simplify the code. Most likely this
            // would impact the electrical characteristics, as it wouldn't be terminated.
            [14]    CCS_INST_ARR0_02_CCS_DDR_ADDRESS_17 =     1 if used     // 16Gb density && x4 width, or check SPD[5]
            [15]    CCS_INST_ARR0_02_CCS_DDR_BANK_GROUP_1 =   1     // B side
            [17-19] CCS_INST_ARR0_00_CCS_DDR_BANK_0_1, CCS_INST_ARR0_00_CCS_DDR_BANK_GROUP_0 = ~(MR#)   // swizzled!
            [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =           1
            [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =            0xf
            [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =        see above
            [34-35] CCS_INST_ARR0_00_CCS_DDR_CID_0_1 =        see above
            [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =        see above
      MC01.MCBIST.CCS.CCS_INST_ARR1_{00, 01, .., 31}    // 0x07012335 + N
            [all]   0
            [0-15]  CCS_INST_ARR1_00_IDLES =    see above
            [59-63] CCS_INST_ARR1_00_GOTO_CMD = (index of next command)

      // Rank 2n+1, side A, only if rank is present
      MC01.MCBIST.CCS.CCS_INST_ARR0_{00, 01, .., 31}    // 0x07012315 + N
            [all]   0
            // Mirroring swaps the following bit pairs:
            //   A3 <->  A4
            //   A5 <->  A6
            //   A7 <->  A8
            //  A12 <-> A13
            //  BA0 <-> BA1
            //  BG0 <-> BG1
            // After mirroring, BG0 is the bit that chooses side B when set.
            [0-13]  CCS_INST_ARR0_00_CCS_DDR_ADDRESS_0_13 =   A0-A13 to be written to MR#, mirrored
            [15]    CCS_INST_ARR0_02_CCS_DDR_BANK_GROUP_1 =   MR#.BG0
            [17-18] CCS_INST_ARR0_00_CCS_DDR_BANK_0_1 =       MR#.BA_0_1, mirrored    // mirrored + swizzled = nop
            [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =           1
            [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =            0xf
            [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =        see above
            [34-35] CCS_INST_ARR0_00_CCS_DDR_CID_0_1 =        see above
            [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =        see above
      MC01.MCBIST.CCS.CCS_INST_ARR1_{00, 01, .., 31}    // 0x07012335 + N
            [all]   0
            [0-15]  CCS_INST_ARR1_00_IDLES =    see above
            [59-63] CCS_INST_ARR1_00_GOTO_CMD = (index of next command)

      // Rank 2n+1, side B, only if rank is present
      MC01.MCBIST.CCS.CCS_INST_ARR0_{00, 01, .., 31}    // 0x07012315 + N
            [all]   0
            [0-13]  CCS_INST_ARR0_00_CCS_DDR_ADDRESS_0_13 =   (A0-A13 to be written to MR#) XOR (A3-A9,A11,A13), mirrored
            [14]    CCS_INST_ARR0_02_CCS_DDR_ADDRESS_17 =     1 if used     // 16Gb density && x4 width, or check SPD[5]
            [15]    CCS_INST_ARR0_02_CCS_DDR_BANK_GROUP_1 =   ~MR#.BG0
            [17-18] CCS_INST_ARR0_00_CCS_DDR_BANK_0_1 =       ~(MR#.BA_0_1), mirrored   // mirrored + swizzled = nop
            [19]    CCS_INST_ARR0_02_CCS_DDR_BANK_GROUP_0 =   1   // B side
            [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =           1
            [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =            0xf
            [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =        see above
            [34-35] CCS_INST_ARR0_00_CCS_DDR_CID_0_1 =        see above
            [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =        see above
      MC01.MCBIST.CCS.CCS_INST_ARR1_{00, 01, .., 31}    // 0x07012335 + N
            [all]   0
            [0-15]  CCS_INST_ARR1_00_IDLES =    see above
            [59-63] CCS_INST_ARR1_00_GOTO_CMD = (index of next command)

      ...
      ... repeat for all ranks, all MRS commands
      ...

      --------------- do CCS finalization and execution, see draminit_cke_helper ----------------------

mss_post_draminit
  // This plays with attributes generated by src/usr/targeting/common/genHwsvMrwXml.pl based on XML files from
  // src/usr/targeting/common/xmltohb/. We need someone fluent in Perl to parse it probably...
  //
  // IIUC this function can change VDDR dynamically on some platforms, depending on OPENPOWER_VOLTMSG config option.
  // This option is nowhere to be found, but there is OPENPOWER_MEM_VOLT in src/usr/isteps/HBconfig, which seems to
  // have description similar to what OPENPOWER_VOLTMSG should be responsible for.
  //
  // There is a possibility that this is no-op on BMC-based platforms. Names of some variables suggest that a message
  // is sent to SP.
```
