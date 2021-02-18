## mss_draminit_trainadv: Advanced dram training (13.12)

> a) p9_mss_draminit_training_advanced.C  (mcbist target) - Nimbus
> b) p9c_mss_draminit_training_advanced.C  (mba target)   - Cumulus
>    - Prior to running this procedure will apply known DQ bad bits to prevent them from participating in training.
>      This information is extracted from the bad DQ attribute and applied to Hardware
>      - Marks the MCBist mask
>    - This step will contain any algorithms to improve data eye post training
>      - At the moment this is a no-op for P9 Nimbus
>      - For P9 Cumulus the VREF calibration will be done here
>    - Also will contain some characterization (mfg only) tests
>      - There will be a FAPI interface for dumping characterization data, platform implementation is TBD (dump to console,
>        memory, PNOR)
>    - This procedure will update the bad DQ attribute for each dimm based on its finding

Code flow is very similar to that from 13.11. Key differences:
- CCS is already configured, there is no need to do this again,
- register resets, ZQ calibration and workarounds (DQS refreshing) are not repeated,
- no workarounds (for RDIMM) after all training steps,
- no "FIRry things" after the training,
- bad DQ bits are not updated, despite what IPL flow says.

It jumps straight into calibration steps.

```
for each functional MCBIST
  if (count_dimms(MCBIST) == 0) go to next MCBIST

  for each functional MCA
    get_rank_pairs(MCA, l_pairs)        // described in 13.8
    for each rp in l_pairs
      // Custom read/write centering
      // Those are two separate PHY hardware accelerated steps, but WR centering may fail because of bad RD centering,
      // in which case both RD and WR must be repeated with a different pattern. Initial Pattern Write described earlier
      // is also used in this step to load custom patterns into appropriate DRAM registers.
      //
      // There are 3 custom read patterns and 1 write pattern. Code iterates over read patterns until it succeeds for both
      // read and write centering, or runs out of patterns. In any case, it returns success - default centering was performed
      // earlier and its results are restored if custom centering fails.
      save original settings:               // good values from default pattern centering, copy to variables
        IOM0.DDRPHY_DP16_READ_DELAY{0-7}_RANK_PAIR<rp>_P0_{0-3}         // 0x80000<rp>5{0-7}0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_READ_DELAY{0-3}_RANK_PAIR<rp>_P0_4
        IOM0.DDRPHY_DP16_DQS_RD_PHASE_SELECT_RANK_PAIR<rp>_P0_{0-4}     // 0x80000<rp>090701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_{0-4}                 // 0x80000<rp>7C0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_DQS_BIT_DISABLE_RP<rp>_P0_{0-4}                // 0x80000<rp>7D0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_READ_EYE_SIZE{0-11}_RANK_PAIR<rp>_P0_{0-4}     // 0x80000<rp>6{0-B}0701103F, +0x0400_0000_0000
        // 111 registers in total
        // TODO: maybe save write centering results here, too? But that is 100 additional registers...

      // Read and write patterns are originally stored in a form that needs swizzling (reversing bit order for each byte
      // separately). Unless we want to read the data from Hostboot's structures, we can store it already swizzled. Whenever
      // a pattern is read in the notes below, it should operate on already swizzled form.
      // Note that it is not the same as endianness, as it changes order of bits, not bytes.
      read_patterns = {0x8E942BC6, 0xEA0CA6C9, 0x13EC02FD}      // before swizzling
      read_patterns = {0x7129D463, 0x57306593, 0xC83740BF}      // after swizzling
      write_pattern = 0x69                                      // before swizzling
      write_pattern = 0x96969696                                // after swizzling and copying to fill the whole register
      ret = success       // we may need to differentiate between "pattern failed, try next" and "abort whole calibration"
      for each pattern
        ===============================================================
        // Custom read centering - pre-workaround
        turn off refreshing - see "Read clock align - pre-workaround"

        ---------------------------------------------------------------
        // Custom read centering - main
        //
        // Inner loop starts at the outer loop's current pattern and updates it, something like:
        //
        // for (int cur_pat = 0; cur_pat < NUM_PATTERNS; cur_pat++) {
        //   pre_workaround();
        //   for ( ; cur_pat < NUM_PATTERNS; cur_pat++) {
        //      ret = custom_read_calib_main(cur_pat);
        //      if (ret == success) break;
        //   }
        //   post_workaround();
        //   (...)
        //   ret = custom_write_calib();
        //   if (ret == success) break;
        // }
        //
        // Code above is simplified, there will be more tests and functions take more arguments, like rank pair and MCA.
        // This is done to try all patterns in one iteration of outer loop if read centering fails for a given pattern,
        // without the need for multiple invocations of pre- and post-workarounds, and without trying to do write calibration
        // if read calibration fails. In Hostboot, the inner loop is inside "main()" and some steps are done before that loop.
        //
        // Hostboot saves another copy of calibration registers - this would be used if there were a custom read calibration
        // without a write calibration, but it is redundant when both of those steps are performed. Likewise, this second
        // copy is restored before "main()" returns, but when both steps are performed together, it always has the same,
        // original values, regardless of when and where the failure happens. It is enough to restore it only once, after
        // the outer loop exits, only if it didn't report success.
        // TODO: move outside of loop?
        IOM0.DDRPHY_RC_CONFIG0_P0                           // 0x8000C8000701103F
            [63] STAGGERED_PATTERN = 1

        // TODO: move outside of loop?
        IOM0.DDRPHY_DP16_RX_CONFIG0_P0_{0-4}                // 0x800000060701103F, +0x0400_0000_0000
            [62-63] READ_CENTERING_MODE = 3

        for each not yet checked pattern
          if ret != success:
            restore original settings

          IOM0.DDRPHY_SEQ_RD_WR_DATA0_P0                    // 0x8000C4000701103F
            [all] 0?      // Hostboot does RMW, but everything else is RO, const 0
            [48-63] RD_WR_DATA_REG0 = (read_patterns[cur_pat] & 0xFFFF0000) >> 16

          IOM0.DDRPHY_SEQ_RD_WR_DATA1_P0                    // 0x8000C4010701103F
            [all] 0?      // Hostboot does RMW, but everything else is RO, const 0
            [48-63] RD_WR_DATA_REG1 = (read_patterns[cur_pat] & 0x0000FFFF)

          // Two registers set above specify:
          // a) data that is sent to MPRs in Initial Pattern Write,
          // b) data that is sent to reserved memory in Custom Write Centering,
          // c) expected data for Custom Read and Custom Write Centering.
          //
          // It isn't automatically sent to MPRs before Custom Read Calibration, so we must do another Initial Pattern Write.
          PHY steps common part:
            i_cal_config[49] ENA_INITIAL_PAT_WR = 1
            i_total_cycles = 1

          // Initial Pattern Write shouldn't fail, so this probably clears previous Custom Read Centering errors.
          clear_initial_cal_errors()          // described in 13.11.

          // TODO: move outside of loop?
          IOM0.DDRPHY_RC_RDVREF_CONFIG1_P0                  // 0x8000C80A0701103F
            [60]  CALIBRATION_ENABLE =  0
            [61]  SKIP_RDCENTERING =    0

          // Everything is configured, start PHY assisted calibration now.
          PHY steps common part:
            i_cal_config[56] ENA_CUSTOM_RD = 1
            // > This step runs for approximately 6 x (512/COARSE_CAL_STEP_SIZE + 4 x (COARSE_CAL_STEP_SIZE +
            // > 4 x CONSEQ_PASS)) x 24 DRAM clocks per rank pair.
            // This is exactly the same as in default read centering - algorithm is the same.
            i_total_cycles = 6 * (128 + 4 * (4 + 32)) * 24 = 39168

          ret = process_initial_cal_errors(DIMM)    // see end of 13.11
          if ret == success: break      // from the inner loop

        ---------------------------------------------------------------
        // Custom read centering - post-workaround
        turn on refreshing - see "Read clock align - post-workaround"

        ===============================================================

        // Break out of outer loop if read centering failed for all custom patterns. We will restore later.
        if ret != success: break

        ===============================================================
        // Custom write centering
        //
        // Skipping what was already done (is repeated for a case where only custom write centering is performed):
        // - saving BIT_DISABLE registers
        // - setting staggered mode
        save original write settings
          IOM0.DDRPHY_DP16_WR_DELAY_VALUE_{0-15,16,18,20,22}_RP<rp>_REG_P0_{0-4} // 0x80000<rp>380701103F, +0x0400_0000_0000
          // DP16_DQS_BIT_DISABLE and DP16_DQ_BIT_DISABLE are already saved

        IOM0.DDRPHY_DP16_WR_VREF_CONFIG0_P0_{0-4}         // 0x8000006C0701103F, +0x0400_0000_0000
          [48] WR_CTR_1D_MODE_SWITCH = 1

        IOM0.DDRPHY_SEQ_RD_WR_DATA0_P0                    // 0x8000C4000701103F
          [all] 0?      // Hostboot does RMW, but everything else is RO, const 0
          [48-63] RD_WR_DATA_REG0 = (write_patterns & 0xFFFF0000) >> 16

        IOM0.DDRPHY_SEQ_RD_WR_DATA1_P0                    // 0x8000C4010701103F
          [all] 0?      // Hostboot does RMW, but everything else is RO, const 0
          [48-63] RD_WR_DATA_REG1 = (write_patterns & 0x0000FFFF)

        clear_initial_cal_errors()          // described in 13.11.

        PHY steps common part:
          i_cal_config[57] ENA_CUSTOM_WR = 1
          // > 1000 + (NUM_VALID_SAMPLES * (FW_WR_RD + FW_RD_WR + 16) *
          // > (1024/(SMALL_STEP +1) + 128/(BIG_STEP +1)) + 2 * (BIG_STEP+1)/(SMALL_STEP+1)) x 24 DRAM
          // > clocks per rank pair.
          // This is exactly the same as in default write centering - algorithm is the same.
          i_total_cycles = 1000 + (5 * (0 + max(tWTR_S + 11, 0 + to_clocks(7.5ns) + 3) + 16) *
                            (1024/(0 + 1) + 128/(7 + 1)) + 2 * (7+1)/(0+1)) * 24

        ret = process_initial_cal_errors(DIMM)    // see end of 13.11
        if ret == success: break      // from the outer loop

        // else
        restore original write settings

      // End of outer loop. We either have found a better calibration values (ret == success) or calibration failed, in
      // which case we have to restore original, working values. As this step is not required for proper DRAM operation,
      // we do not report a failure in any case.
      if ret != success:
        restore original settings
```
