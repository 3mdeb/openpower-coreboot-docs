## mss_draminit_training: Dram training (13.11)

> a) p9_mss_draminit_training.C (mcbist) -- Nimbus
> b) p9c_mss_draminit_training.C (mba) -- Cumulus
>    - Prior to running this procedure will apply known DQ bad bits to prevent them from participating in training.
>      This information is extracted from the bad DQ attribute and applied to Hardware
>      - Marks the calibration fail array
>    - External ZQ Calibration
>    - Execute initial dram calibration (7 step - handled by HW)
>    - This procedure will update the bad DQ attribute for each dimm based on its findings

```
for each functional MCBIST
  if (count_dimms(MCBIST) == 0) continue

  // Configure the CCS engine
  // Deja vu, see 13.10. Maybe first 4 bits in this list are repeated only for simulation?
  MC01.MCBIST.MBA_SCOMFIR.CCS_MODEQ               // 0x070123A7
        // "It's unclear if we want to run with this true or false. Right now (10/15) this
        // has to be false. Shelton was unclear if this should be on or off in general BRS"
        [0]     CCS_MODEQ_CCS_STOP_ON_ERR =           0
        [1]     CCS_MODEQ_CCS_UE_DISABLE =            0
        [24]    CCS_MODEQ_CFG_CCS_PARITY_AFTER_CMD =  1
        [26]    CCS_MODEQ_COPY_CKE_TO_SPARE_CKE =     1 // Docs: "Does not apply for POWER9. No spare chips to copy to."
        // "Hm. Centaur sets this up for the longest duration possible. Can we do better?"
        // This is timeout so we should only hit it in the case of error. What is the unit of this field? Memclocks?
        [8-23]  CCS_MODEQ_DDR_CAL_TIMEOUT_CNT =       0xffff
        [30-31] CCS_MODEQ_DDR_CAL_TIMEOUT_CNT_MULT =  3

  // Clean out any previous calibration results, set bad-bits and configure the ranks
  for each functional MCA
    setup_and_execute_zqcal()
      for each DIMM, for each rank
        // Max instructions: 4 (ports per MCBIST) * 2 (DIMMs per port) * 2 (master ranks per DIMM) * 2 (sides) * 1 (ZQCL)
        // + 1 (final DES) = 33 instructions
        // TODO: do we have to do this for side A and side B? Probably yes, ZQ is for DRAM, not for RCD.
        // TODO: maybe "double buffering" CCS instruction array is an option? We can fill one half of the array, let it run,
        // fill the second half and only then poll for completion. When first half completes, we start the other half and
        // can begin filling next instructions in the half that is not used at the moment.
        //
        // JEDEC: "All banks must be precharged and tRP met before ZQCL or ZQCS commands are issued by the controller" -
        // not sure if this is met. A refresh during the calibration probably would impact the results. Also, "No other
        // activities should be performed on the DRAM channel by the controller for the duration of tZQinit, tZQoper,
        // or tZQCS" - this means we have to insert a delay after every ZQCL, not only after the last one. As a possible
        // improvement, we could reorder this step a bit and send ZQCL on all ports "simultaneously" (without delays)
        // and add a delay just between different DIMMs/ranks.
        MC01.MCBIST.CCS.CCS_INST_ARR0_{00, 01, .., 31}    // 0x07012315 + N
              [all]   0
              [10]    CCS_INST_ARR0_00_CCS_DDR_ADDRESS_10 =   1
              [20]    CCS_INST_ARR0_00_CCS_DDR_ACTN =         1
              [21]    CCS_INST_ARR0_00_CCS_DDR_ADDRESS_16 =   1
              [22]    CCS_INST_ARR0_00_CCS_DDR_ADDRESS_15 =   1
              [23]    CCS_INST_ARR0_00_CCS_DDR_ADDRESS_14 =   0
              [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =          0xf
              [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =      see comment in 13.10
              [34-35] CCS_INST_ARR0_00_CCS_DDR_CID_0_1 =      see comment in 13.10
              [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =      see comment in 13.10
        MC01.MCBIST.CCS.CCS_INST_ARR1_{00, 01, .., 31}    // 0x07012335 + N
              [all]   0
              // Timeout. This should be tZQinit, but Hostboot uses a bigger margin, and also includes tDLLK for the
              // second time here (first one was after MR0 was written, see also a comment in there). Total timeout in
              // Hostboot is 2*(tDLLK + tZQinit) which is 3-4x more than required, depending on frequency.
              [0-15]  CCS_INST_ARR1_00_IDLES =    1024    // tZQinit
              [59-63] CCS_INST_ARR1_00_GOTO_CMD = (index of next command)

        --------------- do CCS finalization and execution, see draminit_cke_helper ----------------------

    // ZQ calibration done, since now we no longer have to keep CKE high at all times

    IOM0.DDRPHY_PC_INIT_CAL_CONFIG0_P0 = 0      // 0x8000C0160701103F

    // > Disable port fails as it doesn't appear the MC handles initial cal timeouts
    // > correctly (cal_length.) BRS, see conversation with Brad Michael
    MC01.PORT0.SRQ.MBA_FARB0Q =                 // 0x07010913
          [57]  MBA_FARB0Q_CFG_PORT_FAIL_DISABLE = 1

    // > The following registers must be configured to the correct operating environment:
    // > These are reset in phy_scominit
    // > Section 5.2.5.10 SEQ ODT Write Configuration {0-3} on page 422
    // > Section 5.2.6.1 WC Configuration 0 Register on page 434
    // > Section 5.2.6.2 WC Configuration 1 Register on page 436
    // > Section 5.2.6.3 WC Configuration 2 Register on page 438

    clear_initial_cal_errors()
      IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_0,      // 0x8000007A0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_1,
      IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_2,
      IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_3,
      IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_4,
      IOM0.DDRPHY_DP16_WR_ERROR0_P0_0,              // 0x8000001B0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_WR_ERROR0_P0_1,
      IOM0.DDRPHY_DP16_WR_ERROR0_P0_2,
      IOM0.DDRPHY_DP16_WR_ERROR0_P0_3,
      IOM0.DDRPHY_DP16_WR_ERROR0_P0_4,
      IOM0.DDRPHY_DP16_RD_STATUS0_P0_0,             // 0x800000140701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_RD_STATUS0_P0_1,
      IOM0.DDRPHY_DP16_RD_STATUS0_P0_2,
      IOM0.DDRPHY_DP16_RD_STATUS0_P0_3,
      IOM0.DDRPHY_DP16_RD_STATUS0_P0_4,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS2_P0_0,         // 0x800000100701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_RD_LVL_STATUS2_P0_1,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS2_P0_2,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS2_P0_3,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS2_P0_4,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS0_P0_0,         // 0x8000000E0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_RD_LVL_STATUS0_P0_1,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS0_P0_2,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS0_P0_3,
      IOM0.DDRPHY_DP16_RD_LVL_STATUS0_P0_4,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_0,         // 0x800000AE0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_1,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_2,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_3,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_4,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_0,         // 0x800000AF0701103F, +0x0400_0000_0000
      IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_1,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_2,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_3,
      IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_4,
            [all] 0

      IOM0.DDRPHY_APB_CONFIG0_P0 =                  // 0x8000D0000701103F
            [49]  RESET_ERR_RPT = 1, then 0     // toggle. It's ok to do just one read and two writes
      IOM0.DDRPHY_APB_ERROR_STATUS0_P0 =            // 0x8000D0010701103F
            [all] 0

      IOM0.DDRPHY_RC_ERROR_STATUS0_P0 =             // 0x8000C8050701103F
            [all] 0

      IOM0.DDRPHY_SEQ_ERROR_STATUS0_P0 =            // 0x8000C4080701103F
            [all] 0

      IOM0.DDRPHY_WC_ERROR_STATUS0_P0 =             // 0x8000CC030701103F
            [all] 0

      IOM0.DDRPHY_PC_ERROR_STATUS0_P0 =             // 0x8000C0120701103F
            [all] 0

      IOM0.DDRPHY_PC_INIT_CAL_ERROR_P0 =            // 0x8000C0180701103F
            [all] 0

      IOM0.IOM_PHY0_DDRPHY_FIR_REG SCOM1 (AND) =    // 0x07011001
            [all] 1
            [56]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 = 0

    get_rank_pairs(MCA, l_pairs)        // described in 13.8

    dp16::reset_delay_values()    // configurable (?) based on ATTR_MSS_MRW_RESET_DELAY_BEFORE_CAL, by default do it (?)
      for each rp in l_pairs
        IOM0.DDRPHY_DP16_DQS_GATE_DELAY_RP<rp>_P0_0,      // 0x0x80000<rp>130701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_DQS_GATE_DELAY_RP<rp>_P0_1,
        IOM0.DDRPHY_DP16_DQS_GATE_DELAY_RP<rp>_P0_2,
        IOM0.DDRPHY_DP16_DQS_GATE_DELAY_RP<rp>_P0_3,
        IOM0.DDRPHY_DP16_DQS_GATE_DELAY_RP<rp>_P0_4 =
              [all] 0

    dqs_align::turn_on_refresh()      // workaround always enabled
      IOM0.DDRPHY_SEQ_MEM_TIMING_PARAM0_P0          // 0x8000C4120701103F
        // > May need to add freq/tRFI attr dependency later but for now use this value
        // > Provided by Ryan King
        [60-63] TRFC_CYCLES = 9             // tRFC = 2^9 = 512 memcycles = 384-640 ns (2666-1866MT/s, respectively)

      IOM0.DDRPHY_PC_INIT_CAL_CONFIG1_P0                  // 0x8000C0170701103F
        // > Hard coded settings provided by Ryan King for this workaround
        [48-51] REFRESH_COUNT =     0xf
        // TODO: see "Read clock align - pre-workaround" below. Why not 1 until calibration finishes? Does it pull in
        // refresh commands?
        [52-53] REFRESH_CONTROL =   3       // refresh commands may interrupt calibration routines
        [54]    REFRESH_ALL_RANKS = 1
        [55]    CMD_SNOOP_DIS =     0
        [57-63] REFRESH_INTERVAL =  0x13    // Worst case: 6.08us for 1866 (max tCK). Must be not more than 7.8us
        // When we want to preserve data in RAM the controller must send a refresh command every tREFI (7.8us) on average.
        // Up to 8 such commands may be postponed/pulled in in advance, which gives max of 9*tREFI between commands where
        // calibration may happen (minus the time required for refresh command to do its job - tRFC, depends on memory
        // density). With these in mind, max time for any calibration step should be no longer than 70us if we want to
        // preserve the contents of memory. However, due to conservative settings of TRFC_CYCLES and REFRESH_INTERVAL,
        // and the fact that an error is reported by the controller when internal counter overflows after 15 postponed
        // commands (see description of REFRESH_INTERVAL in the documentation) for 2666MT/s DIMMs this time falls down
        // below 50us. Max value for REFRESH_INTERVAL for 1866MT/s DIMMs that meets the 7.8us requirement is 24 (0x18).

    // List of calibration steps for RDIMM, in execution order:
    // - ZQ calibration - calibrates DRAM output driver and on-die termination values (already done)
    // - Write leveling - compensates for skew caused by a fly-by topology
    // - Initial pattern write - not exactly a calibration, but prepares patterns for next steps
    // - DQS align
    // - RDCLK align
    // - Read centering
    // - Write Vref latching - not exactly a calibration, but required for next steps
    // - Write centering
    // - Coarse write/read
    // - Custom read and/or write centering - performed in istep 13.12
    // Some of these steps have pre- or post-workarounds, or both.
    //
    // All of those steps (except ZQ calibration) are executed for each rank pair before going to the next pair. Some of
    // them require that there is no other activity on the controller so parallelization may not be possible.

    for each rp in l_pairs
      ===============================================================
      // Write leveling - pre-workaround
      // JEDEC spec requires disabling RTT_WR during WR_LEVEL, and enabling equivalent terminations
      //
      // All tests here are done for primary rank in a "pair" (which may have up to 4 ranks, because why not), however
      // they should be the same/similar, otherwise we can't group multiple ranks anyway. All settings must be saved in
      // registers for primary rank in group because DDR PHY applies settings from this rank to all ranks in a group
      // ("pair"). In the following, the physical number of primary rank is pri_rank.
      if (ATTR_MSS_VPD_MT_DRAM_RTT_WR == 0): skip this workaround       // goto skip_here
      MR2 =               // redo the rest of the bits
          [A11-A9]  0
      MR1 =               // redo the rest of the bits
          [A8-A10]  240/ATTR_MSS_VPD_MT_DRAM_RTT_WR       // Write properly encoded RTT_WR value as RTT_NOM
      // Assumption: RDIMM has at most 2 primary ranks. This means that possible rank numbers are 0, 1, 4, 5, where
      // 4 and 5 are ranks 0 and 1 on DIMM1. Even if rank numbers 2/3 are used when there is no DIMM1 we will never
      // train them, so it should be safe to ignore them here.
      pri_rank == 0:
        IOM0.DDRPHY_SEQ_ODT_WR_CONFIG0_P0 =     // 0x8000C40A0701103F
              [48] = 1
      pri_rank == 1:
        IOM0.DDRPHY_SEQ_ODT_WR_CONFIG0_P0 =     // 0x8000C40A0701103F
              [57] = 1
      pri_rank == 4:
        IOM0.DDRPHY_SEQ_ODT_WR_CONFIG1_P0 =     // 0x8000C40B0701103F
              [50] = 1
      pri_rank == 5:
        IOM0.DDRPHY_SEQ_ODT_WR_CONFIG1_P0 =     // 0x8000C40B0701103F
              [59] = 1

      mss::workarounds::seq::odt_config - not needed on DD2

      skip_here:

      // Different workaround, executed even if RTT_WR == 0
      workarounds::wr_lvl::configure_non_calibrating_ranks()
        for each rank on MCA except current primary rank:
          MR1 =               // redo the rest of the bits
              [A7]  = 1       // Write Leveling Enable
              [A12] = 1       // Outputs disabled (DQ, DQS)

      ---------------------------------------------------------------
      // Write leveling - main
      // This is a PHY hardware accelerated calibration step

      ################ PHY steps common part - begin ##########################
      // Common part of all PHY steps (w/o debug and error checking for clarity):
      //
      // >  const auto& l_mcbist = mss::find_target<TARGET_TYPE_MCBIST>(i_target);
      // >  auto l_cal_inst = mss::ccs::initial_cal_command(i_rp);
      // >  ccs::program l_program;
      // >
      // >  // Delays in the CCS instruction ARR1 for training are supposed to be 0xFFFF,
      // >  // and we're supposed to poll for the done or timeout bit. But we don't want
      // >  // to wait 0xFFFF cycles before we start polling - that's too long. So we put
      // >  // in a best-guess of how long to wait. This, in a perfect world, would be the
      // >  // time it takes one rank to train one training algorithm times the number of
      // >  // ranks we're going to train. We fail-safe as worst-case we simply poll the
      // >  // register too much - so we can tune this as we learn more.
      // >  l_program.iv_poll.iv_initial_sim_delay = 200;
      // >  l_program.iv_poll.iv_poll_count = 0xFFFF;
      // >  l_program.iv_instructions.push_back(l_cal_inst);
      // >
      // >  // We need to figure out how long to wait before we start polling. Each cal step has an expected
      // >  // duration, so for each cal step which was enabled, we update the CCS program.
      // >  mss::cal_timer_setup(i_target, i_total_cycles, l_program.iv_poll);
      // >  mss::setup_cal_config( i_target, i_rp, i_cal_config, i_abort_on_error);
      // >
      // >  // In the event of an init cal hang, CCS_STATQ(2) will assert and CCS_STATQ(3:5) = "001" to indicate a
      // >  // timeout. Otherwise, if calibration completes, FW should inspect DDRPHY_FIR_REG bits (50) and (58)
      // >  // for signs of a calibration error. If either bit is on, then the DDRPHY_PC_INIT_CAL_ERROR register
      // >  // should be polled to determine which calibration step failed.
      // >  mss::ccs::execute(l_mcbist, l_program, i_target);
      //
      // i_cal_config is one of IOM0.DDRPHY_PC_INIT_CAL_CONFIG0_P0[48-56] bits set.
      // i_total_cycles is a result of calculate_cycles() for given calibration step. cal_timer_setup() then sets
      // polling parameters: initial delay is i_total_cycles/8, delay between polls is 10us, poll count is whatever
      // it takes to get to i_total_cycles (rounded up), multiplied by 4 (although this may be required just for
      // simulation, but wouldn't hurt unless calibration fails anyway).
      // l_cal_inst is almost the same as DES instruction except for ACTN, DDR_CAL_TYPE and DDR_CALIBRATION_ENABLE:
      MC01.MCBIST.CCS.CCS_INST_ARR0_xx              // 0x07012316
            [all]   0
            // "CKE is high Note: P8 set all 4 of these high - not sure if that's correct. BRS"
            [24-27] CCS_INST_ARR0_00_CCS_DDR_CKE =      0xf
            [32-33] CCS_INST_ARR0_00_CCS_DDR_CSN_0_1 =  3     // Not used by the engine for calibration?
            [36-37] CCS_INST_ARR0_00_CCS_DDR_CSN_2_3 =  3     // Not used by the engine for calibration?
            [56-59] CCS_INST_ARR0_00_CCS_DDR_CAL_TYPE = 0xc
      MC01.MCBIST.CCS.CCS_INST_ARR1_xx              // 0x07012336
            [all]   0
            [53-56] CCS_INST_ARR1_00_DDR_CAL_RANK = rp
            [57]    CCS_INST_ARR1_00_DDR_CALIBRATION_ENABLE = 1

      // setup_cal_config()
      IOM0.DDRPHY_PC_INIT_CAL_CONFIG0_P0            // 0x8000C0160701103F
            [48-57] i_cal_config            // ### depends on step, bit 48 set for WR leveling ###
            [58]    ABORT_ON_CAL_ERROR =  0   // Maybe setting to 1 will help in debugging? ATTR_MSS_CAL_ABORT_ON_ERROR
            [60+rp] ENA_RANK_PAIR =       1   // So, rp must be [0-3]

      ### For WR leveling:
      ### > // Note: the following equation is taken from the PHY workbook - leaving the naked numbers in for parity to
      ### > // the workbook
      ### > // This step runs for approximately (80 + TWLO_TWLOE) x NUM_VALID_SAMPLES x (384/(BIG_STEP + 1) +
      ### > // (2 x (BIG_STEP + 1))/(SMALL_STEP + 1)) + 20 memory clock cycles per rank.
      ### TWLO_TWLOE for every defined speed bin is 9.5 + 2 = 11.5 ns, this needs to be converted to clock cycles, it is
      ### the only non-constant component of the equation.
      ### WR_LVL_BIG_STEP = 7
      ### WR_LVL_SMALL_STEP = 0
      ### WR_LVL_NUM_VALID_SAMPLES = 5
      ### We should leave full equation in the code and leave it for the compiler to simplify, but for rough estimations:
      ### i_total_cycles = (80 + to_cycles(11.5ns)) * 320 + 20
      ### i_total_cycles = (80 + 11.5/0.75) * 320 + 20 = 30740    // Min tCK for 2666, ~23us
      ### i_total_cycles = (80 + 11.5/1.25) * 320 + 20 = 28820    // Max tCK for 1866, ~36us

      // Important: delays and timeouts are different than defaults! Depends on i_total_cycles
      // TODO: are there any drawbacks for polling more often than every 10us?
      // TODO: do we have to do this for side A and side B?
      --------------- do CCS finalization and execution, see draminit_cke_helper ----------------------

      ################ PHY steps common part - end ############################

      ---------------------------------------------------------------
      // Write leveling - post-workaround
      //
      // Basically reverting the pre-workaround here
      if (ATTR_MSS_VPD_MT_DRAM_RTT_WR == 0): skip this workaround       // goto skip_here
      MR2 =               // redo the rest of the bits
          [A11-A9]  f(ATTR_MSS_VPD_MT_DRAM_RTT_WR)      // see mrs_load() above
      MR1 =               // redo the rest of the bits
          [A8-A10]  240/ATTR_MSS_VPD_MT_DRAM_RTT_NOM      // integer division rounds properly for 7/34 (watch out for 0)
      // Originally set in 13.8
      IOM0.DDRPHY_SEQ_ODT_WR_CONFIG0_P0 =     // 0x8000C40A0701103F
            F(X) = (((X >> 4) & 0xc) | ((X >> 2) & 0x3))    // Bits 0,1,4,5 of X, see also MC01.PORT0.SRQ.MBA_FARB2Q
            [all]   0
            [48-51] ODT_WR_VALUES0 = F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][0][0])
            [56-59] ODT_WR_VALUES1 = F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][0][1])

      IOM0.DDRPHY_SEQ_ODT_WR_CONFIG1_P0 =     // 0x8000C40B0701103F
            F(X) = (((X >> 4) & 0xc) | ((X >> 2) & 0x3))    // Bits 0,1,4,5 of X, see also MC01.PORT0.SRQ.MBA_FARB2Q
            [all]   0
            [48-51] ODT_WR_VALUES2 =
                      // RDIMM can't have more than 2 master ranks, maybe don't use ranks 2/3? On the other hand,
                      // maybe ranks 2/3 are used as "DIMM1 not present". TODO: check VPD for clues
                      count_dimm(MCA) == 2: F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][1][0])
                      count_dimm(MCA) != 2: F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][0][2])
            [56-59] ODT_WR_VALUES3 =
                      count_dimm(MCA) == 2: F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][1][1])
                      count_dimm(MCA) != 2: F(ATTR_MSS_VPD_MT_ODT_WR[index(MCA)][0][3])
              [59] = 1

      mss::workarounds::seq::odt_config - not needed on DD2

      skip_here:

      // Different workaround, executed even if RTT_WR == 0
      workarounds::wr_lvl::configure_non_calibrating_ranks()
        for each rank on MCA except current primary rank:
          MR1 =               // redo the rest of the bits
              [A7]  = 0       // Write Leveling Enable: 0 = disabled
              [A12] = 0       // Outputs disabled (DQ, DQS): 0 = not disabled

      ===============================================================
      // Initial Pattern Write
      // No pre-/post-workaround required. This is PHY hardware accelerated step.
      //
      // This isn't exactly a calibration algorithm, but it prepares data for further steps. It writes to MPRs (Multi
      // Purpose Registers), using values specified in IOM0.DDRPHY_SEQ_RD_WR_DATA{0,1}_P0 in 13.8.
      PHY steps common part:
        i_cal_config[49] ENA_INITIAL_PAT_WR = 1
        // > Not sure how long this should take, so we're gonna use 1 to make sure we get at least one polling loop
        i_total_cycles = 1    // Note that delay between polls is 10us, so it basically becomes our timeout

      ===============================================================
      // DQS (read) alignment
      // No pre-workaround required, post-workaround does not apply to DD2.*. This is PHY hardware accelerated step.
      PHY steps common part:
        i_cal_config[50] ENA_DQS_ALIGN = 1
        // > Note: the following equation is taken from the PHY workbook - leaving the naked numbers in for parity to the
        // > workbook. This step runs for approximately 6 x 600 x 4 DRAM clocks per rank pair.
        i_total_cycles = 6 * 600 * 4 (= 14400)

      ===============================================================
      // Alignment of the internal SysClk to the Read clock
      // This is PHY hardware accelerated step.
      //
      // Read clock align - pre-workaround
      // Turn off refresh, we don't want it to interfere here
      IOM0.DDRPHY_PC_INIT_CAL_CONFIG1_P0                  // 0x8000C0170701103F
        // TODO: we just set it before starting calibration steps. As we don't have any precious data in RAM yet (no S3?),
        // maybe we can use 0 there and just change it to 3 in the post-workaround?
        [52-53] REFRESH_CONTROL =   0       // refresh commands are only sent at start of initial calibration

      ---------------------------------------------------------------
      // Read clock align - main
      PHY steps common part:
        i_cal_config[51] ENA_RDCLK_ALIGN = 1
        // > Note: the following equation is taken from the PHY workbook - leaving the naked numbers in for parity to the
        // > workbook. This step runs for approximately 24 x ((1024/COARSE_CAL_STEP_SIZE + 4 x COARSE_CAL_STEP_SIZE) x 4 + 32)
        // > DRAM clocks per rank pair
        // COARSE_CAL_STEP_SIZE = 4
        i_total_cycles = 24 * ((256 + 16) * 4 + 32) = 26880

      ---------------------------------------------------------------
      // Read clock align - post-workaround
      // > In DD2.*, We adjust the red waterfall to account for low VDN settings. We move the waterfall forward by one
      IOM0.DDRPHY_DP16_DQS_RD_PHASE_SELECT_RANK_PAIR{0-3}_P0_{0-3}      // 0x80000{0-3}090701103F, +0x0400_0000_0000
        [48-49] DQSCLK_SELECT0 = (++DQSCLK_SELECT0 % 4)
        [52-53] DQSCLK_SELECT1 = (++DQSCLK_SELECT1 % 4)
        [56-57] DQSCLK_SELECT2 = (++DQSCLK_SELECT2 % 4)
        [60-61] DQSCLK_SELECT3 = (++DQSCLK_SELECT3 % 4)
      IOM0.DDRPHY_DP16_DQS_RD_PHASE_SELECT_RANK_PAIR{0-3}_P0_4          // 0x80001{0-3}090701103F
        [48-49] DQSCLK_SELECT0 = (++DQSCLK_SELECT0 % 4)
        [52-53] DQSCLK_SELECT1 = (++DQSCLK_SELECT1 % 4)
        // Can't change non-existing quads

      // Turn on refresh
      // This is exactly the same as was called just before iterating over rank pairs...
      IOM0.DDRPHY_SEQ_MEM_TIMING_PARAM0_P0                              // 0x8000C4120701103F
        [60-63] TRFC_CYCLES = 9
      IOM0.DDRPHY_PC_INIT_CAL_CONFIG1_P0                                // 0x8000C0170701103F
        [48-51] REFRESH_COUNT =     0xf
        [52-53] REFRESH_CONTROL =   3       // refresh commands may interrupt calibration routines
        [54]    REFRESH_ALL_RANKS = 1
        [55]    CMD_SNOOP_DIS =     0
        [57-63] REFRESH_INTERVAL =  0x13

      ===============================================================
      // Read centering
      // This is PHY hardware accelerated step. Assuming both Vref and centering is enabled.
      //
      // Read centering - pre-workaround
      turn off refreshing - see "Read clock align - pre-workaround"

      IOM0.DDRPHY_DP16_CONFIG0_P0_{0-4}                                 // 0x800000030701103F, +0x0400_0000_0000
        [62]  1         // part of ATESTSEL_0_4 field

      ---------------------------------------------------------------
      // Read centering - main
      IOM0.DDRPHY_DP16_RD_VREF_CAL_EN_P0_{0-4}                          // 0x800000760701103F, +0x0400_0000_0000
        [all] 0
        [48-63] VREF_CAL_EN = 0xffff    // But we already did this in reset_rd_vref() in 13.8...

      IOM0.DDRPHY_RC_RDVREF_CONFIG1_P0                                  // 0x8000C80A0701103F
        [60]  CALIBRATION_ENABLE =  1
        [61]  SKIP_RDCENTERING =    0

      PHY steps common part:
        i_cal_config[52] ENA_READ_CTR = 1
        // > Note: the following equation is taken from the PHY workbook - leaving the naked numbers in for parity to the
        // > workbook
        // > This step runs for approximately 6 x (512/COARSE_CAL_STEP_SIZE + 4 x (COARSE_CAL_STEP_SIZE +
        // > 4 x CONSEQ_PASS)) x 24 DRAM clocks per rank pair.
        // COARSE_CAL_STEP_SIZE = 4
        // CONSEQ_PASS = 8
        i_total_cycles = 6 * (128 + 4 * (4 + 32)) * 24 = 39168

      ---------------------------------------------------------------
      // Read centering - post-workaround
      workarounds::dp16::rd_dq::fix_delay_values() - does not apply to DD2.*

      turn on refreshing - see "Read clock align - post-workaround"

      IOM0.DDRPHY_DP16_CONFIG0_P0_{0-4}                                 // 0x800000030701103F, +0x0400_0000_0000
        [62]  0         // part of ATESTSEL_0_4 field

      ===============================================================
      // Write VREF Latching
      // Pre-workaround does not apply to DD2.*, there is no post-workaround.
      //
      // > JEDEC has a 3 step latching process for WR VREF
      // > 1) enter into VREFDQ training mode, with the desired range value is XXXXXX
      // > 2) set the VREFDQ value while in training mode - this actually latches the value
      // > 3) exit VREFDQ training mode and go into normal operation mode
      // Each step is followed by a 150ns (tVREFDQE or tVREFDQX) stream of DES commands before next one.
      for each present rank in rp:
        // Step 1
        MR6 =
          [all] 0
          [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f       // This is "don't care" in step 1
          [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
          [A7]      1
          [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

        // Step 2 - exactly the same as step 1
        MR6 =
          [all] 0
          [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f
          [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
          [A7]      1
          [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

        // Step 3
        MR6 =
          [all] 0
          [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f       // This is "don't care" when A7 is not set
          [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40       // This is "don't care" when A7 is not set
          [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

      ===============================================================
      // Write centering
      // This is PHY hardware accelerated step. Assuming both Vref and centering is enabled.
      //
      // Write centering - pre-workaround
      // Following the assumption from reset_bad_bits() - assume there are no bad DQ bits
      // DRAM is one IC on the DIMM module, there are 9 DRAMs for x8 and 18 for x4 devices (DQ bits/width) per master rank
      l_dram_delays[number of DRAMs]        // will be used in post-workaround
      for each DRAM:
        // This assumes that the delays are the same across the rank pair. Does it mean that grouping is useful only for
        // slave ranks and one master rank always is exactly one rank pair? This would explain a lot...
        // Before centering the delays should be the same for each DQ of a given DRAM
        // Hostboot extracted just the delay from that SCOM, but the rest is 0s so why bother
        // DP = (DRAM*width) / 16
        // val = (DRAM*width) % 16
        l_dram_delays[DRAM] = IOM0.DDRPHY_DP16_WR_DELAY_VALUE_<val>_RP<rp>_REG_P0_<DP>
                    // 0x800000380701103F and others

      ---------------------------------------------------------------
      // Write centering - main
      // Hostboot sets bit 48 of IOM0.DDRPHY_DP16_WR_VREF_CONFIG0_P0_{0-4} to 0, but it was already set that way
      // in reset_wr_vref_registers() in 13.8. It is also used by a workaround, where it may be set as 1.
      PHY steps common part:
        i_cal_config[53] ENA_WRITE_CTR = 1
        // > Note: the following equation is taken from the PHY workbook - leaving the naked numbers in for parity to the
        // > workbook
        // > 1000 + (NUM_VALID_SAMPLES * (FW_WR_RD + FW_RD_WR + 16) *
        // > (1024/(SMALL_STEP +1) + 128/(BIG_STEP +1)) + 2 * (BIG_STEP+1)/(SMALL_STEP+1)) x 24 DRAM
        // > clocks per rank pair.
        //
        // Yes, write leveling values are used for write centering, this is not an error (or is it? CONFIG0 says BIG_STEP = 1)
        // WR_LVL_BIG_STEP = 7
        // WR_LVL_SMALL_STEP = 0
        // WR_LVL_NUM_VALID_SAMPLES = 5
        //
        // > Per PHY spec, defaults to 0. Would need an attribute to drive differently
        // FW_WR_RD = 0
        //
        // > From the PHY spec. Also confirmed with S. Wyatt as this is different than the calculation used in Centaur.
        // > This field must be set to the larger of the two values in number of memory clock cycles.
        // > FW_RD_WR = max(tWTR + 11, AL + tRTP + 3)
        // > Note from J. Bialas: The difference between tWTR_S and tWTR_L is that _S is write to read
        // > time to different bank groups, while _L is to the same. The algorithm should be switching
        // > bank groups so tWTR_S can be used
        // tWTR_S is shorter than tWTR_L
        // tWTR_S can be read from SPD[0x29] and SPD[0x2c], there are also minimal values in DDR4 spec, max of those should
        // be used (tWTR_L is in SPD[0x29] and SPD[0x2d], if it were to be used)
        // tRTP = 7.5ns, but it has to be converted to nCK (this comes from DDR4 spec)
        // AL = 0
        i_total_cycles = 1000 + (5 * (0 + max(tWTR_S + 11, 0 + to_clocks(7.5ns) + 3) + 16) *
                          (1024/(0 + 1) + 128/(7 + 1)) + 2 * (7+1)/(0+1)) * 24
        // to_clocks(7.5ns) is between 6 and 10 clocks for all supported frequencies (1866-2666MT/s). Minimal tWTR_S per JEDEC
        // is max(2nCK, 2.5ns), which means that even for minimal possible tWTR_S it is at least as big as the second part, so
        // max(tWTR_S + 11, to_clocks(7.5ns) + 3) = tWTR_S + 11
        // Reducing as much as possible:
        // i_total_cycles = 1000 + ((tWTR_S + 27) * 5200 + 16) * 24 = 1000 + (5200*tWTR_S + 140400 + 16) * 24 =
        //                  1000 + 124800*tWTR_S + 3369984 = 124800*tWTR_S + 3370984
        // Taking tWTR_S = 2.5ns (as seen in some SPDs) this gives 3620584 - 3870184 cycles = ~2.9 - 4.5ms (rough estimations).

      ---------------------------------------------------------------
      // Write centering - post-workaround
      // Assume no NVDIMMs.
      // Write centering may fail for DQ bits that have significantly different RD VREF values than the rest of DQ bits under
      // the same DRAM. In that case disable all but one, known to be good, DQ bits and rerun WR centering.
      //
      // This workaround uses per DRAM addressing (PDA) mode. In general, PDA is used to set values that may not be the same
      // for all DRAMs on a given rank, such as ODT or Vref. Only MRS commands are available in PDA mode. How to use it:
      // 0. Write leveling must be performed before entering PDA mode.
      // 1. Enable PDA by writing MR3 with A4 = 1 (this MRS is sent to ALL DRAMs).
      // 2. In PDA mode DQ0 of each DRAM serves as a "chip select" - when DQ0 is 1, that DRAM ignores the command. Note that
      //    DQ bits on the edge connector do not have to be mapped 1:1 into consecutive DRAM DQ pins (see SPD[60-77]). For
      //    this reason the controller may choose to drive all the DQ bits of a nibble (or byte for x8 devices).
      // 3. After appropriate DQ bits are set, MRS command may be sent as usual. Because only MRS commands are allowed, there
      //    is no separate tMOD, but tMRD_PDA = max(16nCK, 10ns) is used instead of tMRD = 8nCK. In addition to tMRD_PDA more
      //    delay is needed because the DQ bits must propagate to DRAM (AL + CWL + PL + BL/2), we can subtract half clock
      //    cycle because tMRD_PDA is counted since the arrival of the last DQ bit, not including the hold time. The total
      //    minimum time between MRS commands in PDA mode is thus (AL + (PL) + CWL + BL/2 - 0.5tCK + tMRD_PDA), where:
      //    - AL - Additive Latency, set in MR1
      //    - CWL - CAS Write Latency, set in MR2
      //    - BL - Burst Length, set in MR0, divided by 2 because DDR
      //    - PL - C/A Parity Latency, set in MR5, optional
      // 4. To exit PDA mode write MR3 with A4 = 0. As this command is sent while still in PDA mode, it is send ONLY to the
      //    selected DRAMs. DQ0 bits for all DRAMs should be driven to 0 so the command is sent to all DRAM devices at once.
      //    Bits in mode registers are assigned in such a way that no per-DRAM values are configured by MR3. Next command
      //    (other than MRS and DES) may be send after (AL + (PL) + CWL + BL/2 - 0.5tCK + tMOD).
      //
      // That was the theory. On POWER9 we have some help from the hardware. We still have the CCS to send MRS commands. DP16s
      // have registers that can set appropriate DQ bits for a whole nibble, we just have to specify which one. Delay and hold
      // time of DQ bits is configurable, although Hostboot uses 0 for delay and max possible value for hold (0x3f memory
      // clock cycles) because it is "safer and easier than figuring out how long to hold the values". PHY logic can only
      // track one MRS PDA command at a time. This introduces another timing constraint: spacing between MRS commands in PDA
      // mode must be at least (delay + hold + 8) memory clock cycles.
      //
      // Unfortunately, those DQ bits cannot be changed by CCS_INSTR_ARR0/1 registers, which means that switching the targets
      // must be performed outside of CCS, manually. We still can and should, if possible, select multiple DRAMs at once.
      // Configuring CCS for each command takes time, we also have to poll the status to know when it is safe to modify
      // registers specifying DQs. We cannot change them in the middle of an instruction, but it should be safe to do so
      // during the last tMRD_PDA, however the time saved this way would be negligible.
      //
      // On the controller, PDA mode is enabled by multiple bits. There is one global (per MCA) bit to set up the driving of
      // DQ/DQS bits in write control block, and MR3 mirrors (per rank pair) in PHY control block. The latter are changed
      // automatically, but unfortunately they do not account for RDIMMs A- and B-sides. This is not an issue for entering
      // PDA mode (side B is written with all DQs driven to 0, except for possibly longer timeout they are ignored by DRAMs
      // that are not in PDA mode yet), but for exiting we need another workaround to tell the controller that it is still
      // using PDA and should drive DQ bits as ordered. This is done by manually setting MR3[A4] mirror between issuing
      // commands to A- and B-side. Note that the workaround is needed just for the command that clears MR3[A4], and not all
      // of the MRS commands.

      // Check if there are any (new) bad DQ bits, there is no reason to run this workaround if calibration passed.
      for each DRAM:
        // DP = (DRAM*width) / 16
        IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_<DP>:       // 0x8000007C0701103F and others
            if ALL 'width' bits for this DRAM == 1: add DRAM to l_bad_drams

      if l_bad_drams is empty: skip workaround        // go to next calibration step

      // We are about to disable DQ bits. One of those bits may be DQ0, which is used to select DRAM in PDA mode, which
      // means we have to do PDA first to redo WR VREF latch.
      //
      // Attribute ATTR_MSS_VPD_MT_VREF_DRAM_WR that is written to most of the registers below is defined for MCS, so it
      // will be the same for all ranks.
      //
      // Deselect all DRAMs by setting appropriate DQ bits. Hostboot does it later (PDA enter) but doing it now will
      // simplify the rest of the code. In the code these registers were named MCA_DDRPHY_DP16_DATA_BIT_ENABLE1_P0_*.
      IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_{0-3}                     // 800000010701103F, +0x0400_0000_0000
          [60-63] MRS_CMD_DATA_Nx = 1
      IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_4
          [60-61] MRS_CMD_DATA_Nx = 1             // only two bits for last DP

      for each DRAM in l_bad_drams:
        mss::ddr4::pda::commands::add_command():
          // In Hostboot this function is generalised and uses C++ containers magic to minimise the number of rank switching.
          // For this particular case (one command to a group of ranks, no switching required other than exit from PDA) it
          // is enough to just add bad DRAM to the current set, provided that this set was cleared before the loop. PDA mode
          // will be enabled regardless of DQ bits, even though the controller will set those just for bad DRAMs on the PDA
          // entry for side B. In non-PDA mode those bits are ignored by DRAMs.
          // reg = (DRAM*width) / 16
          // bit = (DRAM*width/4) % 4             // so 1 bit per DRAM for x4, 2 bits for x8
          IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_<reg>
              [60+bit] MRS_CMD_DATA_Nx = 0
              if x8:                  // for x8 set second nibble too, DQ0 may be in any of them
                [60+bit+1] = 0

        configure_wr_vref_to_nominal():
          // DP = (DRAM*width) / 16
          // reg = ((DRAM*width) % 16) / 2
          IOM0.DDRPHY_DP16_WR_VREF_VALUE<reg>_RANK_PAIR<rp>_P0_<DP> =   // 0x8000005E0701103F and others
              // Modify just the bits for this DRAM, leave the rest as it was set by the calibration
              if DRAM % 2 == 0:
                [49]    WR_VREF_RANGE_DRAM{0,2} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
                [50-55] WR_VREF_VALUE_DRAM{0,2} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f
              if DRAM % 2 == 1:
                [57]    WR_VREF_RANGE_DRAM{1,3} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
                [58-63] WR_VREF_VALUE_DRAM{1,3} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f

        reset_wr_dq_delay():
          // Saved in pre-workaround
          // DP = (DRAM*width) / 16
          // val = (DRAM*width) % 16
          IOM0.DDRPHY_DP16_WR_DELAY_VALUE_<val>_RP<rp>_REG_P0_<DP> = l_dram_delays[DRAM]
                      // 0x800000380701103F and others

        // Must temporally re-enable DQ bits for PDA
        clear_dram_disable_bits():
          // DP = (DRAM*width) / 16
          // pos = 48 + (DRAM*width) % 16
          IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_<DP>:       // 0x8000007C0701103F and others
              [pos - pos+width-1]   0     // might as well do this in the first loop of this workaround

      // MR6 is sent to bad DRAMs only through PDA, but we are sending one command to multiple DRAMs, so this is outside
      // "for each DRAM" loop. But first we have to set up and enter PDA mode. For MRS use CCS as before, remember to
      // send the commands to side A and side B. No workaround needed for entry.
      //
      // JEDEC says to wait for tMOD = max(24nCK, 15ns) even if the next command is another MRS, but we may have different
      // constraints:
      // > Note: the delay is taken from the PDA timing register's maximum value 2^6
      // > 10 cycles for safety
      // Hostboot waits for 64 + 10 = 74 cycles.
      MR3:
        [A4]      1

      // PDA mode is enabled for DRAMs, but the controller isn't set to drive DQ bits yet, do it now.
      IOM0.DDRPHY_WC_CONFIG3_P0                                 // 0x8000CC050701103F
        [all] 0       // all non-0 bits are changed, no need to read the register
        [48]    PDA_RANKDELAY_ENABLE =  1
        [49-54] MRS_CMD_DQ_ON =         0
        [55-60] MRS_CMD_DQ_OFF =        0x3f

      // Only now Hostboot configures DQ bits in DDRPHY_DP16_DFT_PDA_CONTROL registers, until now all that data was hold
      // in the PDA command structures. It also disables DRAMs (one by one) after the command stream is sent, preparing for
      // the next set of commands, even though there are none.
      //
      // By JEDEC, we should wait for (AL + (PL) + CWL + BL/2 - 0.5tCK + tMRD_PDA) = ~30-40 clocks, depending on settings,
      // but because of delay in DDRPHY_WC_CONFIG3_P0 we have to wait at least (MRS_CMD_DQ_ON + MRS_CMD_DQ_OFF + 8) = 71
      // clock cycles. On top of that, because we are changing VrefDQ, tVREFDQ{E,X} = 150ns is used instead of tMRD_PDA.
      // However, Hostboot uses just tVREFDQE which probably is OK, as it is longer than other constraints and DRAM still
      // receives it in the same intervals.
      //
      // This is exactly the same as in Write VREF latching, except we do not do this for all ranks.
      // Step 1
      MR6 =
        [all] 0
        [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f       // This is "don't care" in step 1
        [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
        [A7]      1
        [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

      // Step 2 - exactly the same as step 1
      MR6 =
        [all] 0
        [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f
        [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
        [A7]      1
        [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

      // Step 3
      MR6 =
        [all] 0
        [A5-A0]   ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f       // This is "don't care" when A7 is not set
        [A6]      ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40       // This is "don't care" when A7 is not set
        [A12-A10] (conv_to_nCK(SPD[40] + SPD[117])) - 4

      // Vref on DRAMs has been restored, prepare to exit PDA.
      // Select all DRAMs, not just the disabled ones. Hostboot does it too, overwriting what it just wrote during PDA
      // command set finalisation where it deselected bad DRAMs.
      IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_{0-3}                     // 800000010701103F, +0x0400_0000_0000
          [60-63] MRS_CMD_DATA_Nx = 0
      IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_4
          [60-61] MRS_CMD_DATA_Nx = 0             // only two bits for last DP

      // Disable PDA on DRAMs. This needs a workaround, so the command is send to side A only (for now).
      MR3:
        [A4] 0

      // Right now the controller sees that PDA is being disabled, so it traps this information in MR3 mirror register.
      // It does it before sending the command to side B, so side B would receive this command as if it were in non-PDA
      // mode. We must manually set A4 mirror bit to tell the controller that it should still work in PDA mode.
      IOM0.DDRPHY_PC_MR3_RP<rp>_P0                                  // 0x8000C<rp>1F0701103F
        // This bit corresponds to A4. Perhaps bit numbering is reversed (A0->63, A1->62...), or bits may be swizzled,
        // there is no explicit information about the order of bits, but 59 comes from the Hostboot code.
        [59]  1

      // Now send the same command to side B (with inverting and mirroring as usual).
      MR3:
        [A4] 0

      // Disable PDA mode in WC register
      IOM0.DDRPHY_WC_CONFIG3_P0                                 // 0x8000CC050701103F
        // Any reason for RMW and not zeroing the whole register?
        [48]    PDA_RANKDELAY_ENABLE =  0

      // Everything up to this point was done just to revert the state to that from before first try of write centering,
      // for bad DRAMs only. Now we have to disable all but one DQ bits for bad DRAMs. We must select a bit that is good,
      // that means its RD Vref is close to the median value of DRAM's Vrefs (not true median actually, for even number
      // of elements, as is always the case for DQ bits, we choose element n/2 instead of average of this and the next
      // element - integer math and such value always exists).
      for each DRAM in l_bad_drams:
        disable_bits():
          // > There are two RD VREF values stored per register, one RD VREF value corresponds to one bit
          // > For a x4 DRAM, there are 4 bits per DRAM, meaning two registers need to be read
          // > For a x8 DRAM, there are 8 bits per DRAM, meaning four registers need to be read
          // DP = (DRAM*width) / 16
          // DAC_l = (DRAM * width/2) % 8
          // DAC_h = ((DRAM+1) * width/2) % 8 - 1
          // DRAMs do not cross DPs and both values in a register describe exactly one DRAM. We need the median of
          // values from registers in range DAC_l-DAC_h from:
          IOM0.DDRPHY_DP16_RD_VREF_DAC_<DAC>_P0_<DP>
            [49-55] BIT(2*DAC)_VREF_DAC
            [57-63] BIT(2*DAC+1)_VREF_DAC

          // Hostboot searches for the first good bit but any one would do probably. It uses a threshold of 3 units to
          // minimise lookup time. Take care to not overflow.
          // TODO: can we find median without sorting, so we can keep the index of element? If yes, is this faster?
          // good_bit = first bit for which RD VREF value is median +/- 3
          // pos = 48 + (DRAM*width) % 16
          IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_<DP>:       // 0x8000007C0701103F and others
              [pos - pos+width-1]   1         // disable all...
              [pos+good_bit]        0         // ...but one, known to be good

      configure_skip_bits():
        IOM0.DDRPHY_DP16_WR_VREF_CONFIG0_P0_{0,1,2,3,4} =       // 0x8000006C0701103F, +0x0400_0000_0000
            [57-59] WR_CTR_NUM_BITS_TO_SKIP = 7     // skip all but one

      // Rerun main calibration
      // TODO: what about i_total_cycles? Hostboot uses one value, regardless of number of skip bits and whether 2D or 1D
      // calibration is performed.
      run()

      // Clear training FIRs
      IOM0.IOM_PHY0_DDRPHY_FIR_REG SCOM1 (AND) =    // 0x07011001
          [all] 1
          [56]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 = 0

      // Hostboot here reads IOM0.IOM_PHY0_DDRPHY_FIR_REG (0x07011001) and discards the value. This was not done in
      // clear_initial_cal_errors().

      // Check if DRAMs are still bad.
      for each DRAM in l_bad_drams:
        // DP = (DRAM*width) / 16
        IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_<DP>:       // 0x8000007C0701103F and others
            if ALL 'width' bits for this DRAM == 1: l_is_bad = true

        if l_is_bad == false:     // if DRAM was recovered
          clear_dram_disable_bits():
            // DP = (DRAM*width) / 16
            // pos = 48 + (DRAM*width) % 16
            IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_<DP>:       // 0x8000007C0701103F and others
                [pos - pos+width-1]   0

      // Now that the disable bits for recovered DRAMs were cleared, we can rerun 1D WR centering calibration.
      IOM0.DDRPHY_DP16_WR_VREF_CONFIG0_P0_{0,1,2,3,4} =       // 0x8000006C0701103F, +0x0400_0000_0000
          [48]  WR_CTR_1D_MODE_SWITCH = 1

      // Rerun main calibration, again
      run()

      ===============================================================
      // Coarse WR/RD calibration
      // No pre-/post-workaround required. This is PHY hardware accelerated step.
      PHY steps common part:
        i_cal_config
            [54] ENA_INITIAL_COARSE_WR =  1
            [55] ENA_COARSE_RD =          1
        // 40 cycles for WR, 32 for RD
        i_total_cycles = 72

      ===============================================================

      // That was the last calibration step performed in draminit_training, there are two more in draminit_trainadv.
      // We are still in "for each rp" loop.
      //
      // Assuming ABORT_ON_CAL_ERROR==0 in DDRPHY_PC_INIT_CAL_CONFIG0_Px, we want to continue training other DIMMs/ranks
      // regardless of whether calibration passed or failed. We may or may not have bad DQ bits now. A number of bad bits
      // can be handled.
      //
      // Check if initial calibration failed.
      l_rc = process_initial_cal_errors(DIMM):
        IOM0.DDRPHY_DP16_RD_VREF_CAL_ERROR_P0_{0-4}               // 0x8000007A0701103F, +0x0400_0000_0000
          // We can test all bits of the last DP16 - unused bits are always 0 because they were not trained.
          if any register != 0: return error

        // Note ERROR_MASK1 is before ERROR_MASK0 in SCOM space
        IOM0.DDRPHY_DP16_WR_VREF_ERROR0_P0_{0-4}                  // 0x800000AE0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_ERROR1_P0_{0-4}                  // 0x800000AF0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_ERROR_MASK0_P0_{0-4}             // 0x800000FB0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_ERROR_MASK1_P0_{0-4}             // 0x800000FA0701103F, +0x0400_0000_0000
          if any (ERRORn & ~ERROR_MASKn) != 0: return error

        IOM0.DDRPHY_PC_INIT_CAL_ERROR_P0                          // 0x8000C0180701103F
          if [60-63] == 0 or      // no rank pairs reported
           [48-59] == 0:          // no errors reported
            // This is either true success or an error in the calibration engine itself. Check for latter.
            IOM0.IOM_PHY0_DDRPHY_FIR_REG        // 0x07011000
              if IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 (52) == 1:
                // > Clear the PHY FIR ERROR 2 bit so we don't keep failing training and training advance on this port
                [52] IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 = 0
                // FIXME: comments say that we return success if there are FIR errors, even when bit 52 was 1. Code
                // seems to negate that and return error caused by PLL or error caused by FIR, which is treated the same
                // by functions higher in the call stack (different debug output maybe?). It is possible that I totally
                // misunderstood the code, double check later with fresh mind.
                // Or, maybe it should return "success" as an indication of failure that isn't related to bad DQ bits,
                // so there is no reason to even try fixing it?
                return error

            return success        // still in the "if no rp or no errors reported" branch

        // At this point we have no RD/WR VREF errors, but at least one bit in PC_INIT_CAL_ERROR is set. Print it and:
        return error        // This also checks PLL vs FIR errors in Hostboot

      if l_rc != success:
        // Hostboot assumes that when we got here, we have some bad DQ bits. It doesn't seem to differentiate between
        // bad bits and other errors, like calibration engine failure (unless FFDC does something about that).
        //
        // We can recover from 1 nibble + 1 bit (or less) bad lines. Anything more and DIMM is beyond repair. A bad
        // nibble is a nibble with any number of bad bits. If a DQS is bad (either true or complementary signal, or
        // both), a whole nibble (for x4 DRAMs) or byte (x8) is considered bad.
        //
        // Function used in Hostboot checks again if there are any bad bits. It also has support for checking more than
        // one rank pair (maybe it is called from other places, too). These check are omitted here.
        //
        // Check both DQS and DQ registers in one loop, iterating over DP16s - easier to sum bad bits/nibbles.
        //
        // See reset_write_clock_enable() and reset_write_clock_enable() in 13.8 or an array in process_bad_bits() in
        // phy/dp16.C for mapping of DQS bits in x8 and mask bits from this register accordingly.
        // TODO: maybe unused bits are not trained and cannot fail? This would simplify things, we could use the same
        // logic for x8 as for x4.
        total_bad_nibbles = 0
        total_bad_bits = 0
        IOM0.DDRPHY_DP16_DQS_BIT_DISABLE_RP<rp>_P0_{0-4}:         // 0x80000<rp>7D0701103F, +0x0400_0000_0000
          // This calculates how many (DQS_t | DQS_c) failed - if _t and _c failed for the same DQS, we count it as one.
          bad_dqs = bit_count((reg & 0x5500) | ((reg & 0xaa00) >> 1))
          if x8 && bad_dqs > 0: DIMM is FUBAR, return error?
          total_bad_nibbles += bad_dqs
          // If we are already past max possible number, we might as well return now
          if total_bad_nibbles > 1: DIMM is FUBAR, return error?

        IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP<rp>_P0_{0-4}:          // 0x80000<rp>7C0701103F, +0x0400_0000_0000
          nibble = {[48-51], [52-55], [56-59], [60-63]}   // exclude nibble corresponding to a bad DQS, it won't get worse
          for each nibble:
            if bit_count(nibble) >  1: total_bad_nibbles += 1
            if bit_count(nibble) == 1: total_bad_bits += 1
            // We can't have two bad bits, one of them must be treated as bad nibble
            if total_bad_bits    >  1: total_bad_nibbles += 1, total_bad_bits -= 1
            if total_bad_nibbles >  1: DIMM is FUBAR, return error?

        // Now, if total_bad_nibbles is less than 2 we know that total_bad_bits is also less than 2, and DIMM is good
        // enough for recovery.

    // End of "for each rp" loop

    // Hostboot writes bad DQ maps into an attribute. Attribute name suggests this is saved in VPD, but comments mention
    // SPD. Long time ago we assumed no bad DQ bits from VPD, all of them were to be trained on each boot, so no reason
    // to write it back (at least for now).

    workarounds::dp16::modify_calibration_results() - does not apply to DD2.*

  // End of "for each functional MCA" loop

  // Hostboot just logs the errors reported earlier (i.e. more than 1 nibble + 1 bit of bad DQ lines) "and lets PRD
  // deconfigure based off of ATTR_BAD_DQ_BITMAP".
  // TODO: what is PRD? How does it "deconfigure" and what? Quick glance at the code: it may have something to do with
  // undocumented 0x0501082X SCOM registers, there are usr/diag/prdf/*/*.rule files with yacc/flex files to compile them.
  // It also may be using `attn` instruction.

  mss::unmask::after_dram_training():
    // > All mcbist attentions are already special attentions
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT0             // 0x07012306
          [1] MCBISTFIRQ_COMMAND_ADDRESS_TIMEOUT =  0
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT1             // 0x07012307
          [1] MCBISTFIRQ_COMMAND_ADDRESS_TIMEOUT =  1
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRMASK             // 0x07012303
          [1] MCBISTFIRQ_COMMAND_ADDRESS_TIMEOUT =  0     //recoverable_error (0,1,0)

    for each functional MCA
      MC01.PORT0.SRQ.MBACALFIR_ACTION0              // 0x07010906
            [2]   MBACALFIR_MASK_REFRESH_OVERRUN =        0
            [5]   MBACALFIR_MASK_DDR_CAL_TIMEOUT_ERR =    0
            [7]   MBACALFIR_MASK_DDR_CAL_RESET_TIMEOUT =  0
            [9]   MBACALFIR_MASK_WRQ_RRQ_HANG_ERR =       0
            [11]  MBACALFIR_MASK_ASYNC_IF_ERROR =         0
            [12]  MBACALFIR_MASK_CMD_PARITY_ERROR =       0
            [14]  MBACALFIR_MASK_RCD_CAL_PARITY_ERROR =   0
      MC01.PORT0.SRQ.MBACALFIR_ACTION1              // 0x07010907
            [2]   MBACALFIR_MASK_REFRESH_OVERRUN =        1
            [5]   MBACALFIR_MASK_DDR_CAL_TIMEOUT_ERR =    1
            [7]   MBACALFIR_MASK_DDR_CAL_RESET_TIMEOUT =  1
            [9]   MBACALFIR_MASK_WRQ_RRQ_HANG_ERR =       1
            [11]  MBACALFIR_MASK_ASYNC_IF_ERROR =         0
            [12]  MBACALFIR_MASK_CMD_PARITY_ERROR =       0
            [14]  MBACALFIR_MASK_RCD_CAL_PARITY_ERROR =   1
      MC01.PORT0.SRQ.MBACALFIR_MASK                 // 0x07010903
            [2]   MBACALFIR_MASK_REFRESH_OVERRUN =        0   // recoverable_error (0,1,0)
            [5]   MBACALFIR_MASK_DDR_CAL_TIMEOUT_ERR =    0   // recoverable_error (0,1,0)
            [7]   MBACALFIR_MASK_DDR_CAL_RESET_TIMEOUT =  0   // recoverable_error (0,1,0)
            [9]   MBACALFIR_MASK_WRQ_RRQ_HANG_ERR =       0   // recoverable_error (0,1,0)
            [11]  MBACALFIR_MASK_ASYNC_IF_ERROR =         0   // checkstop (0,0,0)
            [12]  MBACALFIR_MASK_CMD_PARITY_ERROR =       0   // checkstop (0,0,0)
            [14]  MBACALFIR_MASK_RCD_CAL_PARITY_ERROR =   0   // recoverable_error (0,1,0)
```
