## mss_ddr_phy_reset: Soft reset of DDR PHY macros (13.9)

> - Lock DDR DLLs
>   - Already configured DDR DLL in scaninit
> - Sends Soft DDR Phy reset
> - Kick off internal ZQ Cal
> - Perform any config that wasn't scanned in (TBD)
>   - Nothing known here

```
for each functional MCBIST:
  p9_mss_ddr_phy_reset
    if (count_dimms(MCBIST) == 0) return

    for each functional or magic MCA
      MC01.PORT0.SRQ.MBA_FARB5Q =
            [8]     MBA_FARB5Q_CFG_FORCE_MCLK_LOW_N = 0

    // Drive all control signals to their inactive/idle state, or inactive value
    for each functional or magic MCA
      IOM0.DDRPHY_DP16_SYSCLK_PR0_P0_{0,1,2,3,4} =              // 0x800000070701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_SYSCLK_PR1_P0_{0,1,2,3,4} =              // 0x8000007F0701103F, +0x0400_0000_0000
            [all]   0
            [48]    reserved = 1            // MCA_DDRPHY_DP16_SYSCLK_PR0_P0_0_01_ENABLE

    // Assert reset to PHY for 32 memory clocks
    for each functional or magic MCA
      MC01.PORT0.SRQ.MBA_CAL0Q =                                // 0x0701090F
            [57]    MBA_CAL0Q_RESET_RECOVER = 1

    delay(32 memclocks)     // These delays probably are the only reason why everything has separate "for each MCA" loops

    // Deassert reset_n
    for each functional or magic MCA
      MC01.PORT0.SRQ.MBA_CAL0Q =                                // 0x0701090F
            [57]    MBA_CAL0Q_RESET_RECOVER = 0

    // Flush output drivers
    for each functional or magic MCA
      IOM0.DDRPHY_ADR_OUTPUT_FORCE_ATEST_CNTL_P0_ADR32S{0,1} =    // 0x800080350701103F, 0x800084350701103F
            [all]   0
            [48]    FLUSH =   1
            [50]    INIT_IO = 1

      IOM0.DDRPHY_DP16_CONFIG0_P0_{0,1,2,3,4} =                   // 0x800000030701103F, +0x0400_0000_0000
            [all]   0
            [51]    FLUSH =                 1
            [54]    INIT_IO =               1
            [55]    ADVANCE_PING_PONG =     1
            [58]    DELAY_PING_PONG_HALF =  1

    delay(32 memclocks)     // These delays probably are the only reason why everything has separate "for each MCA" loops

    for each functional or magic MCA
      IOM0.DDRPHY_ADR_OUTPUT_FORCE_ATEST_CNTL_P0_ADR32S{0,1} =    // 0x800080350701103F, 0x800084350701103F
            [all]   0
            [48]    FLUSH =   0
            [50]    INIT_IO = 0

      IOM0.DDRPHY_DP16_CONFIG0_P0_{0,1,2,3,4} =                   // 0x800000030701103F, +0x0400_0000_0000
            [all]   0
            [51]    FLUSH =                 0
            [54]    INIT_IO =               0
            [55]    ADVANCE_PING_PONG =     1
            [58]    DELAY_PING_PONG_HALF =  1

    // ZCTL Enable
    for each magic MCA            // note we are already in "for each MCBIST" loop
      IOM0.DDRPHY_PC_RESETS_P0 =                                  // 0x8000C00E0701103F
            // Yet another documentation error: all bits in this register are marked as read-only
            [51]    ENABLE_ZCAL = 1

    // Is this really necessary? We are polling ZCAL_DONE, if it were to fail we're screwed anyway
    delay(1024 memclocks)
    // Comment says ENABLE_ZCAL has to be deasserted here, neither code nor spec agrees
    for each magic MCA            // note we are already in "for each MCBIST" loop
      timeout(50*10ns):
            // Maybe this should be reordered for consistent timeouts across MCAs: timeout->for each->check bit.
            // Difference between RMW (assert reset bit) and polling should be much smaller that this timeout,
            // which would otherwise be added per each magic MCA in the worst case scenario. On the other hand,
            // number of magic MCAs is const and relatively small, we may end up dropping 'for each' altogether.
            if (IOM0.DDRPHY_PC_DLL_ZCAL_CAL_STATUS_P0 [63] /*ZCAL_DONE*/) == 1) break    // 0x8000C0000701103F
            delay(10ns)

    // DLL calibration
    // Here was an early return if no functional MCAs were found. Wouldn't that make whole MCBIST non-functional?
    for each functional MCA
      IOM0.DDRPHY_ADR_DLL_CNTL_P0_ADR32S{0,1} =           // 0x8000803A0701103F, 0x8000843A0701103F
            [48]    INIT_RXDLL_CAL_RESET = 0
      IOM0.DDRPHY_DP16_DLL_CNTL{0,1}_P0_{0,1,2,3} =       // 0x8000002{4,5}0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_DLL_CNTL0_P0_4
            [48]    INIT_RXDLL_CAL_RESET = 0
      IOM0.DDRPHY_DP16_DLL_CNTL1_P0_4
            [48]    INIT_RXDLL_CAL_RESET = 1      // no-op?

    // 32,772 dphy_nclk cycles from Reset=0 to VREG Calibration to exhaust all values
    // 37,382 dphy_nclk cycles for full calibration to start and fail ("worst case")
    delay(37,382 memclocks)

    // The comment before poll says:
    // > To keep things simple, we'll poll for the change in one of the ports. Once that's completed, we'll
    // > check the others. If any one has failed, or isn't notifying complete, we'll pop out an error
    // The issue is that it only tests the first of the functional ports. Other ports may or may not have failed.
    // Even if this times out, the rest of the function continues normally, without throwing any error...
    // A case in which the first MCA finishes calib before the rest does is caught by "worst case" delay above.
    // If the tests below were done properly (poll every MCA), we could do better than "worst case", statistically
    // we should hit proper VREG in half of that period, which would give ~8ms wasted here (average for DDR4-2133).
    // For now I'll leave it described as it was done in Hostboot, but there is room for improvement here.
    timeout(50*10ns):
      if (IOM0.DDRPHY_PC_DLL_ZCAL_CAL_STATUS_P0
                [48]  DP_DLL_CAL_GOOD ==        1
                [49]  DP_DLL_CAL_ERROR ==       0
                [50]  DP_DLL_CAL_ERROR_FINE ==  0
                [51]  ADR_DLL_CAL_GOOD ==       1
                [52]  ADR_DLL_CAL_ERROR ==      0
                [53]  ADR_DLL_CAL_ERROR_FINE == 0) break    // success
      if (IOM0.DDRPHY_PC_DLL_ZCAL_CAL_STATUS_P0
                [49]  DP_DLL_CAL_ERROR ==       1 |
                [50]  DP_DLL_CAL_ERROR_FINE ==  1 |
                [52]  ADR_DLL_CAL_ERROR ==      1 |
                [53]  ADR_DLL_CAL_ERROR_FINE == 1) break and do the workaround
      // either 48 or 51 is 0
      delay(10ns)

    // Workaround is also required if any of coarse VREG has value 1 after calibration
    // Test from poll above is repeated here - this time for every MCA, but it doesn't wait until DLL gets
    // calibrated if that is still in progress. The registers below (also used in the workaround) __must not__
    // be written to while hardware calibration is in progress.
    for each functional MCA     // this loop may be skipped if we're doing workaround anyway
      if (IOM0.DDRPHY_ADR_DLL_VREG_COARSE_P0_ADR32S0        |       // 0x8000803E0701103F
          IOM0.DDRPHY_DP16_DLL_VREG_COARSE0_P0_{0,1,2,3,4}  |       // 0x8000002C0701103F, +0x0400_0000_0000
          IOM0.DDRPHY_DP16_DLL_VREG_COARSE1_P0_{0,1,2,3}    |       // 0x8000002D0701103F, +0x0400_0000_0000
                [56-62] REGS_RXDLL_VREG_DAC_COARSE = 1)    // The same offset for ADR and DP16, convenient
              do the workaround

    // Proper workaround - skip if not needed
    -----------------------------------------
    fix_bad_voltage_settings
      for each functional MCA
        // Each MCA has 10 DLLs: ADR DLL0, DP0-4 DLL0, DP0-3 DLL1. Each of those can fail. For each DLL there are 5 registers
        // used in this workaround, those are (see src/import/chips/p9/procedures/hwp/memory/lib/workarounds/dll_workaround.C):
        // - l_CNTRL:         DP16 or ADR CNTRL register
        // - l_COARSE_SAME:   VREG_COARSE register for same DLL as CNTRL reg
        // - l_COARSE_NEIGH:  VREG_COARSE register for DLL neighbor for this workaround
        // - l_DAC_LOWER:     DLL DAC Lower register
        // - l_DAC_UPPER:     DLL DAC Upper register
        // Warning: the last two have their descriptions swapped in dll_workaround.H
        // It seems that the code excepts that DLL neighbor is always good, what if it isn't?
        //
        // General flow, stripped from C++ bloating and repeated loops:
        for each DLL          // list in workarounds/dll_workaround.C
          1. check if this DLL failed, if not - skip to the next one
                (l_CNTRL[62 | 63] | l_COARSE_SAME[56-62] == 1) -> failed
          2. set reset bit, set skip VREG bit, clear the error bits
                l_CNTRL[48] =     1
                l_CNTRL[50-51] =  2     // REGS_RXDLL_CAL_SKIP, 2 - skip VREG calib., do coarse delay calib. only
                l_CNTRL[62-63] =  0
          3. clear DLL FIR (see "Do FIRry things" at the end of 13.8)  // this was actually done for non-failed DLLs too, why?
                IOM0.IOM_PHY0_DDRPHY_FIR_REG =      // 0x07011000         // maybe use SCOM1 (AND) 0x07011001
                      [56]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 = 0   // calibration errors
                      [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4 = 0   // DLL errors
          4. write the VREG DAC value found in neighbor (good) to the failing DLL VREG DAC
                l_COARSE_SAME[56-62] = l_COARSE_NEIGH[56-62]
          5. reset the upper and lower fine calibration bits back to defaults
                l_DAC_LOWER[56-63] =  0x0x8000    // Hard coded default values per Steve Wyatt for this workaround
                l_DAC_UPPER[56-63] =  0x0xFFE0
          6. run DLL Calibration again on failed DLLs
                l_CNTRL[48] = 0
        // Wait for calibration to finish
        delay(37,382 memclocks)     // again, we could do better than this

        // Check if calibration succeeded (same tests as in 1 above, for all DLLs)
        for each DLL
          if (l_CNTRL[62 | 63] | l_COARSE_SAME[56-62] == 1): failed, assert and die?
    -----------------------------------------
    // End of DLL workaround

    // Start bang-bang-lock
    // Take dphy_nclk/SysClk alignment circuits out of reset and put into continuous update mode
    for each functional MCA
      IOM0.DDRPHY_ADR_SYSCLK_CNTL_PR_P0_ADR32S{0,1} =           // 0x800080320701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_SYSCLK_PR0_P0_{0,1,2,3,4} =              // 0x800000070701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_SYSCLK_PR1_P0_{0,1,2,3} =                // 0x8000007F0701103F, +0x0400_0000_0000
            [all]   0
            [48-63] 0x8024      // From the DDR PHY workbook

    // Wait at least 5932 dphy_nclk clock cycles to allow the dphy_nclk/SysClk alignment circuit to perform initial alignment
    delay(5932 memclocks)

    // Check for LOCK in DDRPHY_DP16_SYSCLK_PR_VALUE registers and DDRPHY_ADR_SYSCLK_PR_VALUE
    for each functional MCA
      timeout(50*10ns):
        IOM0.DDRPHY_ADR_SYSCLK_PR_VALUE_RO_P0_ADR32S{0,1}       // 0x800080340701103F, +0x0400_0000_0000
              [56]  BB_LOCK     &
        IOM0.DDRPHY_DP16_SYSCLK_PR_VALUE_P0_{0,1,2,3}           // 0x800000730701103F, +0x0400_0000_0000
              [48]  BB_LOCK0    &
              [56]  BB_LOCK1    &
        IOM0.DDRPHY_DP16_SYSCLK_PR_VALUE_P0_4
              [48]  BB_LOCK0          // last DP16 uses only first half
        if all bits listed above are set: break
        delay(10ns)

    // Write 0b0 into the DDRPHY_PC_RESETS register bit 1. This write de-asserts the SYSCLK_RESET
    for each functional MCA
      IOM0.DDRPHY_PC_RESETS_P0 =                                  // 0x8000C00E0701103F
              [49]  SYSCLK_RESET = 0

    // Reset the windage registers
    // According to the PHY team, resetting the read delay offset must be done after SYSCLK_RESET
    for each functional MCA
      // This was using floating point math, so it has to be changed.
      // ATTR_MSS_VPD_MT_WINDAGE_RD_CTR holds (signed) value of offset in picoseconds. It must be converted to
      // phase rotator ticks. There are 128 ticks per clock, and clock period depends on memory frequency.
      // See FREQ_TO_CLOCK_PERIOD in /src/import/generic/memory/lib/utils/conversion.H for values.
      // Result is rounded away from zero, so we have to add *or subtract* half of tick.
      //
      // Maybe we can skip this (40 register writes per port), from documentation:
      // "This register must not be set to a nonzero value unless detailed timing analysis shows that, for
      // a particular configuration, the read-centering algorithm places the sampling point off from the eye center."
      IOM0.DDRPHY_DP16_READ_DELAY_OFFSET0_RANK_PAIR{0,1,2,3}_P0_{0,1,2,3,4} =   // 0x80000{0,1,2,3}0C0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_READ_DELAY_OFFSET1_RANK_PAIR{0,1,2,3}_P0_{0,1,2,3,4} =   // 0x80000{0,1,2,3}0D0701103F, +0x0400_0000_0000
              [all]   0
              [49-55] OFFSET0 = offset_in_ticks_rounded
              [57-63] OFFSET1 = offset_in_ticks_rounded

    // Take the dphy_nclk/SysClk alignment circuit out of the Continuous Update mode
    for each functional MCA
      IOM0.DDRPHY_ADR_SYSCLK_CNTL_PR_P0_ADR32S{0,1} =           // 0x800080320701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_SYSCLK_PR0_P0_{0,1,2,3,4} =              // 0x800000070701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_SYSCLK_PR1_P0_{0,1,2,3} =                // 0x8000007F0701103F, +0x0400_0000_0000
            [all]   0
            [48-63] 0x8020      // From the DDR PHY workbook

    // Wait at least 32 dphy_nclk clock cycles
    delay(32 memclocks)
    // Done bang-bang-lock

    // Per J. Bialas, force_mclk_low can be dasserted
    for each functional MCA
      MC01.PORT0.SRQ.MBA_FARB5Q =
            [8]     MBA_FARB5Q_CFG_FORCE_MCLK_LOW_N = 1

    // Workarounds
    // It reads and writes back DDRPHY_DP16_RD_VREF_DAC_n_P0_p. Docs don't say it has side effects, but who knows...
    mss::workarounds::dp16::after_phy_reset - not needed on DD2

    // New for Nimbus - perform duty cycle clock distortion calibration (DCD cal)
    // Per PHY team's characterization, the DCD cal needs to be run after DLL calibration
    // It can be skipped based on ATTR_MSS_RUN_DCD_CALIBRATION
    for each functional MCA
      // DCD hardware calibration is a three step process:
      // 1) kick off cal on all the registers
      // 2) poll for done on all of the registers
      // 3) loop through the list of failing DCD regs and do the software calibration
      // Step 2) in hostboot also calculates average value of adjust for all successfully calibrated regs. This value
      // is later discarded in step 3), so instead of doing these steps separately we can modify and merge them into
      // one loop. Note that software calibration takes time, it may impact the timeout calculation for further regs.
      //
      // 1) kick off cal on all the registers
      IOM0.DDRPHY_ADR_DCD_CONTROL_P0_ADR32S0                        // 0x800080380701103F
      IOM0.DDRPHY_DP16_DCD_CONTROL{0,1}_P0_{0,1,2,3}                // 0x800000A{4,5}0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_DCD_CONTROL0_P0_4
              [all]   0
              [48-63] 0x80a0  // for DD2 first field (DLL_DCD_ADJUST) is 8b long, for DD1 it was 7b

      // 2) poll for done on all of the registers
      IOM0.DDRPHY_ADR_DCD_CONTROL_P0_ADR32S0                        // 0x800080380701103F
      IOM0.DDRPHY_DP16_DCD_CONTROL{0,1}_P0_{0,1,2,3}                // 0x800000A{4,5}0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_DCD_CONTROL0_P0_4
        // Timeout is separate for each reg, but perhaps it should use one global number of iterations for all regs per MCA
        timeout((128 + 256) * 100ns):         // starting from the middle, max 128 steps down and 256 steps up
          if [61] DLL_DCD_CAL_DONE == 1: break
          delay(100ns)

        // 3) do the software calibration for failing DCD regs
        if [62] DLL_DCD_CAL_ERROR == 1:
          seed = 0x80
          A_val = B_val = 0
          // This is almost identical for both sides, might be a function
          // Can we use bisect instead of linear search? Would delays be different?
          // Hostboot returned RC which tells if given side failed, but it can fail only if SCOM access or delay
          // fails (in that case we have bigger problems...), otherwise it asserts.
          ------------------------------
          tick =      +1
          expected =  1
          overflow =  0xff
          [all]   0
          [48-55] DLL_DCD_ADJUST =      seed
          [56]    DLL_DCD_CORRECT_EN =  1
          [57]    DLL_DCD_ITER_A =      1   // side A
          [58]    DLL_DCD_CAL_ENABLE =  0
          [63]    DLL_DCD_COMPARE_OUT = 0
          // BUG? No delay between write and read here, but there is 100ns delay later
          write and read back register    // DLL_DCD_COMPARE_OUT will be set based on whether target val is higher/lower than seed
          if ([63] == 1):   // target is below seed value
            tick =      -1
            expected =  0
            overflow =  0x00
          // overflow is the last valid value (poor variable naming), so use do..while
          do:
            if ([63] == expected): break
            seed += tick
            [48-55] seed
            [63]    0     // must be cleared before each write
            write
            delay(100ns)
            read
            // Here was another 100ns delay. Docs does not specify any. Why delay *after* read?
            // Here current value was obtained from read value, but it should be always seed (unless HW modifies it?)
          while (seed != overflow)
          // BUG? Hostboot asserts on seed != overflow, what if it is the last good value? Let's check expected instead
          assert([63] == expected)  // maybe not assert? We didn't get 50% duty cycle, but it still might be close enough,
                                    // RDIMM is more forgiving than DIMM (40% instead of 48% min)
          A_val = seed
          ------------------------------
          // Note we use A value as a seed - in ideal world A_val == B_val, real world should be close to that
          // Hostboot used (A_val - 1) to cover corner case when A_val == B_val, but it introduced another corner case
          // for (A_val - 1) == B_val.
          do the same for side B
          // [57] = 0
          // B_val = seed
          ------------------------------
          // The final value is the average of the a-side and b-side values
          // Hostboot uses convoluted way of calculating target value based on RCs from functions above
          [48-55] (A_val + B_val) / 2
          [63]    0

    // FIR
    mss::check::during_phy_reset
      // Mostly FFDC, which to my current knowledge is just the error logging. If it does anything else,
      // this needs rechecking
      for each functional or magic MCA
        // If any of these bits is set, return error. Clear them unconditionally (maybe they are not cleared on platform reset?)
        MC01.PORT0.SRQ.MBACALFIRQ =           // 0x07010900         // use SCOM1 (AND) 0x07010901 for clearing
              [0]   MBACALFIRQ_MBA_RECOVERABLE_ERROR
              [1]   MBACALFIRQ_MBA_NONRECOVERABLE_ERROR
              [10]  MBACALFIRQ_SM_1HOT_ERR
        IOM0.IOM_PHY0_DDRPHY_FIR_REG          // 0x07011000         // use SCOM1 (AND) 0x07011001 for clearing
              [54]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_0
              [55]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_1
              [56]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2
              [57]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_3
              [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4
              [59]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_5
              [60]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_6
              [61]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_7

    mss::unmask::after_phy_reset
      // *MASK must be always written as a last one, otherwise we may get unintended actions
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT0             // 0x07012306
            [2]   MCBISTFIRQ_INTERNAL_FSM_ERROR =       0
            [13]  MCBISTFIRQ_SCOM_RECOVERABLE_REG_PE =  0
            [14]  MCBISTFIRQ_SCOM_FATAL_REG_PE =        0
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT1             // 0x07012307
            [2]   MCBISTFIRQ_INTERNAL_FSM_ERROR =       0
            [13]  MCBISTFIRQ_SCOM_RECOVERABLE_REG_PE =  1
            [14]  MCBISTFIRQ_SCOM_FATAL_REG_PE =        0
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRMASK             // 0x07012303
            [2]   MCBISTFIRQ_INTERNAL_FSM_ERROR =       0   // checkstop (0,0,0)
            [13]  MCBISTFIRQ_SCOM_RECOVERABLE_REG_PE =  0   // recoverable_error (0,1,0)
            [14]  MCBISTFIRQ_SCOM_FATAL_REG_PE =        0   // checkstop (0,0,0)

      // No magic, so cannot be merged with previous function
      for each functional MCA
        MC01.PORT0.SRQ.MBACALFIR_ACTION0              // 0x07010906
              [0]   MBACALFIR_MASK_MBA_RECOVERABLE_ERROR =    0
              [1]   MBACALFIR_MASK_MBA_NONRECOVERABLE_ERROR = 0
              [4]   MBACALFIR_MASK_RCD_PARITY_ERROR =         0
              [10]  MBACALFIR_MASK_SM_1HOT_ERR =              0
        MC01.PORT0.SRQ.MBACALFIR_ACTION1              // 0x07010907
              [0]   MBACALFIR_MASK_MBA_RECOVERABLE_ERROR =    1
              [1]   MBACALFIR_MASK_MBA_NONRECOVERABLE_ERROR = 0
              [4]   MBACALFIR_MASK_RCD_PARITY_ERROR =         1
              [10]  MBACALFIR_MASK_SM_1HOT_ERR =              0
        MC01.PORT0.SRQ.MBACALFIR_MASK                 // 0x07010903
              [0]   MBACALFIR_MASK_MBA_RECOVERABLE_ERROR =    0   // recoverable_error (0,1,0)
              [1]   MBACALFIR_MASK_MBA_NONRECOVERABLE_ERROR = 0   // checkstop (0,0,0)
              [4]   MBACALFIR_MASK_RCD_PARITY_ERROR =         0   // recoverable_error (0,1,0)
              [10]  MBACALFIR_MASK_SM_1HOT_ERR =              0   // checkstop (0,0,0)
        IOM0.IOM_PHY0_DDRPHY_FIR_ACTION0_REG          // 0x07011006
              [54]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_0 = 0
              [55]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_1 = 0
              [57]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_3 = 0   // no ERROR_2
              [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4 = 0
              [59]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_5 = 0
              [60]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_6 = 0
              [61]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_7 = 0
        IOM0.IOM_PHY0_DDRPHY_FIR_ACTION1_REG          // 0x07011007
              [54]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_0 = 1
              [55]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_1 = 1
              [57]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_3 = 1
              [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4 = 1
              [59]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_5 = 1
              [60]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_6 = 1
              [61]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_7 = 1
        IOM0.IOM_PHY0_DDRPHY_FIR_MASK_REG             // 0x07011003
              [54]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_0 = 0   // recoverable_error (0,1,0)
              [55]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_1 = 0   // recoverable_error (0,1,0)
              [57]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_3 = 0   // recoverable_error (0,1,0)
              [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4 = 0   // recoverable_error (0,1,0)
              [59]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_5 = 0   // recoverable_error (0,1,0)
              [60]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_6 = 0   // recoverable_error (0,1,0)
              [61]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_7 = 0   // recoverable_error (0,1,0)
```
