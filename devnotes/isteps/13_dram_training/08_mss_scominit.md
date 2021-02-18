## mss_scominit: Perform scom inits to MC and PHY (13.8)

> - HW units included are MCBIST, MCA/PHY (Nimbus) or membuf, L4, MBAs (Cumulus)
> - Does not use initfiles, coded into HWP
> - Uses attributes from previous step
> - Pushes memory extent configuration into the MBA/MCAs
>   - Addresses are pulled from attributes, set previously by mss_eff_config
>   - MBA/MCAs always start at address 0, address map controlled by
>     proc_setup_bars below

```
for each functional MCBIST:
  if (count_dimms(MCBIST) == 0) continue
  for each functional or magic MCA:
    // no DIMM = no SPD = no parameters to init MCA
    if (count_dimms(MCA) > 0)
      p9n_mca_scom(MCA, MCBIST, MCA.getParent<MCS>, FAPI_SYSTEM, MCBIST.getParent<PROC_CHIP>)     // generated from initfile
        // Regs for ID=5 below are not documented
        *0x05010823 =       // P9N2_MCS_PORT02_MCPERF0
            [22-27] = 0x20                              // AMO_LIMIT

        *0x05010824 =       // P9N2_MCS_PORT02_MCPERF2
            [0-2]   = 1                                 // PF_DROP_VALUE0
            [3-5]   = 3                                 // PF_DROP_VALUE1
            [6-8]   = 5                                 // PF_DROP_VALUE2
            [9-11]  = 7                                 // PF_DROP_VALUE3
            [13-15] =                                   // REFRESH_BLOCK_CONFIG
                if has only one DIMM in MCA:
                  0b000 : if master ranks = 1
                  0b001 : if master ranks = 2
                  0b100 : if master ranks = 4
                // Per allowable DIMM mixing rules, we cannot mix different number of ranks on any single port
                if has both DIMMs in MCA:
                  0b010 : if master ranks = 1
                  0b011 : if master ranks = 2
                  0b100 : if master ranks = 4       // 4 mranks is the same for one and two DIMMs in MCA
            [16] =                                      // ENABLE_REFRESH_BLOCK_SQ
            [17] =                                      // ENABLE_REFRESH_BLOCK_NSQ, always the same value as [16]
                1 : if (1 < (DIMM0 + DIMM1 logical ranks) <= 8 && not (one DIMM, 4 mranks, 2H 3DS)
                0 : otherwise
            [18]    = 0                                 // ENABLE_REFRESH_BLOCK_DISP
            [28-31] = 0b0100                            // SQ_LFSR_CNTL
            [50-54] = 0b11100                           // NUM_RMW_BUF
            [61] = ATTR_ENABLE_MEM_EARLY_DATA_SCOM      // EN_ALT_ECR_ERR, 0?

        *0x05010825 =       // P9N2_MCS_PORT02_MCAMOC
            [1]     = 0                                 // FORCE_PF_DROP0
            [4-28]  = 0x19fffff                         // WRTO_AMO_COLLISION_RULES
            [29-31] = 1                                 // AMO_SIZE_SELECT, 128B_RW_64B_DATA

        *0x05010826 =       // P9N2_MCS_PORT02_MCEPSQ
            [0-7]   = 1                                 // JITTER_EPSILON
            // ATTR_PROC_EPS_READ_CYCLES_T* are calculated in 8.6
            [8-15]  = (ATTR_PROC_EPS_READ_CYCLES_T0 + 6) / 4        // LOCAL_NODE_EPSILON
            [16-23] = (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4        // NEAR_NODAL_EPSILON
            [24-31] = (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4        // GROUP_EPSILON
            [32-39] = (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4        // REMOTE_NODAL_EPSILON
            [40-47] = (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4        // VECTOR_GROUP_EPSILON

        *0x05010827 =       // P9N2_MCS_PORT02_MCBUSYQ
            [0]     = 1                                 // ENABLE_BUSY_COUNTERS
            [1-3]   = 1                                 // BUSY_COUNTER_WINDOW_SELECT, 1024 cycles
            [4-13]  = 38                                // BUSY_COUNTER_THRESHOLD0
            [14-23] = 51                                // BUSY_COUNTER_THRESHOLD1
            [24-33] = 64                                // BUSY_COUNTER_THRESHOLD2

        *0x0501082b =       // P9N2_MCS_PORT02_MCPERF3
            [31] = 1                                    // ENABLE_CL0
            [41] = 1                                    // ENABLE_AMO_MSI_RMW_ONLY
            [43] = !ATTR_ENABLE_MEM_EARLY_DATA_SCOM     // ENABLE_CP_M_MDI0_LOCAL_ONLY, !0 = 1?
            [44] = 1                                    // DISABLE_WRTO_IG
            [45] = 1                                    // AMO_LIMIT_SEL

        MC01.PORT0.SRQ.MBA_DSM0Q =       // 0x701090a
            // These are set per port so all latencies should be calculated from both DIMMs (if present)
            [0-5]   MBA_DSM0Q_CFG_RODT_START_DLY =  ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL
            [6-11]  MBA_DSM0Q_CFG_RODT_END_DLY =    ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL + 5
            [12-17] MBA_DSM0Q_CFG_WODT_START_DLY =  0
            [18-23] MBA_DSM0Q_CFG_WODT_END_DLY =    5
            [24-29] MBA_DSM0Q_CFG_WRDONE_DLY =      24
            [30-35] MBA_DSM0Q_CFG_WRDATA_DLY =      ATTR_EFF_DRAM_CWL + ATTR_MSS_EFF_DPHY_WLO - 8
            // Assume RDIMM, non-NVDIMM only
            [36-41] MBA_DSM0Q_CFG_RDTAG_DLY =
                MSS_FREQ_EQ_1866:                   ATTR_EFF_DRAM_CL[l_def_PORT_INDEX] + 7
                MSS_FREQ_EQ_2133:                   ATTR_EFF_DRAM_CL[l_def_PORT_INDEX] + 7
                MSS_FREQ_EQ_2400:                   ATTR_EFF_DRAM_CL[l_def_PORT_INDEX] + 8
                MSS_FREQ_EQ_2666:                   ATTR_EFF_DRAM_CL[l_def_PORT_INDEX] + 9

        MC01.PORT0.SRQ.MBA_TMR0Q =      // 0x701090b
            [0-3]   MBA_TMR0Q_RRDM_DLY =
                MSS_FREQ_EQ_1866:             8
                MSS_FREQ_EQ_2133:             9
                MSS_FREQ_EQ_2400:             10
                MSS_FREQ_EQ_2666:             11
            [4-7]   MBA_TMR0Q_RRSMSR_DLY =  4
            [8-11]  MBA_TMR0Q_RRSMDR_DLY =  4
            [12-15] MBA_TMR0Q_RROP_DLY =    ATTR_EFF_DRAM_TCCD_L
            [16-19] MBA_TMR0Q_WWDM_DLY =
                MSS_FREQ_EQ_1866:             8
                MSS_FREQ_EQ_2133:             9
                MSS_FREQ_EQ_2400:             10
                MSS_FREQ_EQ_2666:             11
            [20-23] MBA_TMR0Q_WWSMSR_DLY =  4
            [24-27] MBA_TMR0Q_WWSMDR_DLY =  4
            [28-31] MBA_TMR0Q_WWOP_DLY =    ATTR_EFF_DRAM_TCCD_L
            [32-36] MBA_TMR0Q_RWDM_DLY =        // same as below
            [37-41] MBA_TMR0Q_RWSMSR_DLY =      // same as below
            [42-46] MBA_TMR0Q_RWSMDR_DLY =
                MSS_FREQ_EQ_1866:             ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL + 8
                MSS_FREQ_EQ_2133:             ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL + 9
                MSS_FREQ_EQ_2400:             ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL + 10
                MSS_FREQ_EQ_2666:             ATTR_EFF_DRAM_CL - ATTR_EFF_DRAM_CWL + 11
            [47-50] MBA_TMR0Q_WRDM_DLY =
                MSS_FREQ_EQ_1866:             ATTR_EFF_DRAM_CWL - ATTR_EFF_DRAM_CL + 8
                MSS_FREQ_EQ_2133:             ATTR_EFF_DRAM_CWL - ATTR_EFF_DRAM_CL + 9
                MSS_FREQ_EQ_2400:             ATTR_EFF_DRAM_CWL - ATTR_EFF_DRAM_CL + 10
                MSS_FREQ_EQ_2666:             ATTR_EFF_DRAM_CWL - ATTR_EFF_DRAM_CL + 11
            [51-56] MBA_TMR0Q_WRSMSR_DLY =      // same as below
            [57-62] MBA_TMR0Q_WRSMDR_DLY =    ATTR_EFF_DRAM_CWL + ATTR_EFF_DRAM_TWTR_S + 4

        MC01.PORT0.SRQ.MBA_TMR1Q =      // 0x701090c
            [0-3]   MBA_TMR1Q_RRSBG_DLY =   ATTR_EFF_DRAM_TCCD_L
            [4-9]   MBA_TMR1Q_WRSBG_DLY =   ATTR_EFF_DRAM_CWL + ATTR_EFF_DRAM_TWTR_L + 4
            [10-15] MBA_TMR1Q_CFG_TFAW =    ATTR_EFF_DRAM_TFAW
            [16-20] MBA_TMR1Q_CFG_TRCD =    ATTR_EFF_DRAM_TRCD
            [21-25] MBA_TMR1Q_CFG_TRP =     ATTR_EFF_DRAM_TRP
            [26-31] MBA_TMR1Q_CFG_TRAS =    ATTR_EFF_DRAM_TRAS
            [41-47] MBA_TMR1Q_CFG_WR2PRE =  ATTR_EFF_DRAM_CWL + ATTR_EFF_DRAM_TWR + 4
            [48-51] MBA_TMR1Q_CFG_RD2PRE =  ATTR_EFF_DRAM_TRTP
            [52-55] MBA_TMR1Q_TRRD =        ATTR_EFF_DRAM_TRRD_S
            [56-59] MBA_TMR1Q_TRRD_SBG =    ATTR_EFF_DRAM_TRRD_L
            [60-63] MBA_TMR1Q_CFG_ACT_TO_DIFF_RANK_DLY =
                MSS_FREQ_EQ_1866:           8
                MSS_FREQ_EQ_2133:           9
                MSS_FREQ_EQ_2400:           10
                MSS_FREQ_EQ_2666:           11

        MC01.PORT0.SRQ.MBA_WRQ0Q =      // 0x701090d
            [5]     MBA_WRQ0Q_CFG_WRQ_FIFO_MODE =               0   // ATTR_MSS_REORDER_QUEUE_SETTING, 0 = reorder
            [6]     MBA_WRQ0Q_CFG_DISABLE_WR_PG_MODE =          1
            [55-58] MBA_WRQ0Q_CFG_WRQ_ACT_NUM_WRITES_PENDING =  8

        MC01.PORT0.SRQ.MBA_RRQ0Q =      // 0x701090e
            [6]     MBA_RRQ0Q_CFG_RRQ_FIFO_MODE =               0   // ATTR_MSS_REORDER_QUEUE_SETTING
            [57-60] MBA_RRQ0Q_CFG_RRQ_ACT_NUM_READS_PENDING =   8

        MC01.PORT0.SRQ.MBA_FARB0Q =     // 0x7010913
            if (l_TGT3_ATTR_MSS_MRW_DRAM_2N_MODE == 0x02 || (l_TGT3_ATTR_MSS_MRW_DRAM_2N_MODE == 0x00 && l_TGT2_ATTR_MSS_VPD_MR_MC_2N_MODE_AUTOSET == 0x02))
              [17] MBA_FARB0Q_CFG_2N_ADDR =         1     // Default is auto for mode, 1N from VPD, so [17] = 0
            [38] MBA_FARB0Q_CFG_PARITY_AFTER_CMD =  1
            [61-63] MBA_FARB0Q_CFG_OPT_RD_SIZE =    3

        MC01.PORT0.SRQ.MBA_FARB1Q =     // 0x7010914
            [0-2]   MBA_FARB1Q_CFG_SLOT0_S0_CID = 0
            [3-5]   MBA_FARB1Q_CFG_SLOT0_S1_CID = 4
            [6-8]   MBA_FARB1Q_CFG_SLOT0_S2_CID = 2
            [9-11]  MBA_FARB1Q_CFG_SLOT0_S3_CID = 6
            if (DIMM0 is 8H 3DS)
              [12-14] MBA_FARB1Q_CFG_SLOT0_S4_CID =   1
              [15-17] MBA_FARB1Q_CFG_SLOT0_S5_CID =   5
              [18-20] MBA_FARB1Q_CFG_SLOT0_S6_CID =   3
              [21-23] MBA_FARB1Q_CFG_SLOT0_S7_CID =   7
            else
              [12-14] MBA_FARB1Q_CFG_SLOT0_S4_CID =   0
              [15-17] MBA_FARB1Q_CFG_SLOT0_S5_CID =   4
              [18-20] MBA_FARB1Q_CFG_SLOT0_S6_CID =   2
              [21-23] MBA_FARB1Q_CFG_SLOT0_S7_CID =   6
            if (DIMM0 has 4 master ranks)
              [12-14] MBA_FARB1Q_CFG_SLOT0_S4_CID =   4     // TODO: test if all slots with 1x4R DIMMs works with that
            [24-26] MBA_FARB1Q_CFG_SLOT1_S0_CID = 0
            [27-29] MBA_FARB1Q_CFG_SLOT1_S1_CID = 4
            [30-32] MBA_FARB1Q_CFG_SLOT1_S2_CID = 2
            [33-35] MBA_FARB1Q_CFG_SLOT1_S3_CID = 6
            if (DIMM1 is 8H 3DS)
              [36-38] MBA_FARB1Q_CFG_SLOT1_S4_CID =   1
              [39-41] MBA_FARB1Q_CFG_SLOT1_S5_CID =   5
              [42-44] MBA_FARB1Q_CFG_SLOT1_S6_CID =   3
              [45-47] MBA_FARB1Q_CFG_SLOT1_S7_CID =   7
            else
              [36-38] MBA_FARB1Q_CFG_SLOT1_S4_CID =   0
              [39-41] MBA_FARB1Q_CFG_SLOT1_S5_CID =   4
              [42-44] MBA_FARB1Q_CFG_SLOT1_S6_CID =   2
              [45-47] MBA_FARB1Q_CFG_SLOT1_S7_CID =   6
            if (DIMM1 has 4 master ranks)
              [36-38] MBA_FARB1Q_CFG_SLOT1_S4_CID =   4     // TODO: test if all slots with 1x4R DIMMs works with that

        MC01.PORT0.SRQ.MBA_FARB2Q =       // 0x7010915
            F(X) = (((X >> 4) & 0xc) | ((X >> 2) & 0x3))    // Bits 0,1,4,5 of X
            [0-3]   MBA_FARB2Q_CFG_RANK0_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][0][0])
            [4-7]   MBA_FARB2Q_CFG_RANK1_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][0][1])
            [8-11]  MBA_FARB2Q_CFG_RANK2_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][0][2])
            [12-15] MBA_FARB2Q_CFG_RANK3_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][0][3])
            [16-19] MBA_FARB2Q_CFG_RANK4_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][1][0])
            [20-23] MBA_FARB2Q_CFG_RANK5_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][1][1])
            [24-27] MBA_FARB2Q_CFG_RANK6_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][1][2])
            [28-31] MBA_FARB2Q_CFG_RANK7_RD_ODT = F(ATTR_MSS_VPD_MT_ODT_RD[l_def_PORT_INDEX][1][3])
            [32-35] MBA_FARB2Q_CFG_RANK0_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][0][0])
            [36-39] MBA_FARB2Q_CFG_RANK1_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][0][1])
            [40-43] MBA_FARB2Q_CFG_RANK2_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][0][2])
            [44-47] MBA_FARB2Q_CFG_RANK3_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][0][3])
            [48-51] MBA_FARB2Q_CFG_RANK4_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][1][0])
            [52-55] MBA_FARB2Q_CFG_RANK5_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][1][1])
            [56-59] MBA_FARB2Q_CFG_RANK6_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][1][2])
            [60-63] MBA_FARB2Q_CFG_RANK7_WR_ODT = F(ATTR_MSS_VPD_MT_ODT_WR[l_def_PORT_INDEX][1][3])

        MC01.PORT0.SRQ.PC.MBAREF0Q =      // 0x7010932
            [5-7]   MBAREF0Q_CFG_REFRESH_PRIORITY_THRESHOLD = 3
            [8-18]  MBAREF0Q_CFG_REFRESH_INTERVAL =           ATTR_EFF_DRAM_TREFI / (8 * (DIMM0 + DIMM1 logical ranks))
            [30-39] MBAREF0Q_CFG_TRFC =                       ATTR_EFF_DRAM_TRFC
            [40-49] MBAREF0Q_CFG_REFR_TSV_STACK =             ATTR_EFF_DRAM_TRFC_DLR
            [50-60] MBAREF0Q_CFG_REFR_CHECK_INTERVAL =        ((ATTR_EFF_DRAM_TREFI / 8) * 6) / 5

        MC01.PORT0.SRQ.PC.MBARPC0Q =      // 0x7010934
            [6-10]  MBARPC0Q_CFG_PUP_AVAIL =
              MSS_FREQ_EQ_1866: 6
              MSS_FREQ_EQ_2133: 7
              MSS_FREQ_EQ_2400: 8
              MSS_FREQ_EQ_2666: 9
            [11-15] MBARPC0Q_CFG_PDN_PUP =
              MSS_FREQ_EQ_1866: 5
              MSS_FREQ_EQ_2133: 6
              MSS_FREQ_EQ_2400: 6
              MSS_FREQ_EQ_2666: 7
            [16-20] MBARPC0Q_CFG_PUP_PDN =
              MSS_FREQ_EQ_1866: 5
              MSS_FREQ_EQ_2133: 6
              MSS_FREQ_EQ_2400: 6
              MSS_FREQ_EQ_2666: 7
            [21] MBARPC0Q_RESERVED_21 =         // MCP_PORT0_SRQ_PC_MBARPC0Q_CFG_QUAD_RANK_ENC
              (l_def_MASTER_RANKS_DIMM0 == 4): 1
              (l_def_MASTER_RANKS_DIMM0 != 4): 0

        MC01.PORT0.SRQ.PC.MBASTR0Q =      // 0x7010935
            [12-16] MBASTR0Q_CFG_TCKESR = 5
            [17-21] MBASTR0Q_CFG_TCKSRE =
              MSS_FREQ_EQ_1866: 10
              MSS_FREQ_EQ_2133: 11
              MSS_FREQ_EQ_2400: 12
              MSS_FREQ_EQ_2666: 14
            [22-26] MBASTR0Q_CFG_TCKSRX =
              MSS_FREQ_EQ_1866: 10
              MSS_FREQ_EQ_2133: 11
              MSS_FREQ_EQ_2400: 12
              MSS_FREQ_EQ_2666: 14
            [27-37] MBASTR0Q_CFG_TXSDLL =
              MSS_FREQ_EQ_1866: 597
              MSS_FREQ_EQ_2133: 768
              MSS_FREQ_EQ_2400: 768
              MSS_FREQ_EQ_2666: 939
            [46-56] MBASTR0Q_CFG_SAFE_REFRESH_INTERVAL = ATTR_EFF_DRAM_TREFI / (8 * (DIMM0 + DIMM1 logical ranks))

        MC01.PORT0.ECC64.SCOM.RECR =      // 0x7010a0a
            [16-18] MBSECCQ_VAL_TO_DATA_DELAY =
              l_TGT4_ATTR_MC_SYNC_MODE == 1:  5
              l_def_mn_freq_ratio < 915:      3
              l_def_mn_freq_ratio < 1150:     4
              l_def_mn_freq_ratio < 1300:     5
              l_def_mn_freq_ratio >= 1300:    6
            [19]    MBSECCQ_DELAY_VALID_1X =  0
            [20-21] MBSECCQ_NEST_VAL_TO_DATA_DELAY =
              l_TGT4_ATTR_MC_SYNC_MODE == 1:  1
              l_def_mn_freq_ratio < 1040:     1
              l_def_mn_freq_ratio < 1150:     0
              l_def_mn_freq_ratio < 1215:     1
              l_def_mn_freq_ratio < 1300:     0
              l_def_mn_freq_ratio < 1400:     1
              l_def_mn_freq_ratio >= 1400:    0
            [22]    MBSECCQ_DELAY_NONBYPASS =
              l_TGT4_ATTR_MC_SYNC_MODE == 1:  0
              l_def_mn_freq_ratio < 1215:     0
              l_def_mn_freq_ratio >= 1215:    1
            [40]    MBSECCQ_RESERVED_36_43 =        // MCP_PORT0_ECC64_ECC_SCOM_MBSECCQ_BYPASS_TENURE_3
              l_TGT4_ATTR_MC_SYNC_MODE == 1:  0
              l_TGT4_ATTR_MC_SYNC_MODE == 0:  1

        MC01.PORT0.ECC64.SCOM.DBGR =      // 0x7010a0b
            [9]     DBGR_ECC_WAT_ACTION_SELECT =  0
            [10-11] DBGR_ECC_WAT_SOURCE =         0

        MC01.PORT0.WRITE.WRTCFG =         // 0x7010a38
            [9] = 1     // MCP_PORT0_WRITE_NEW_WRITE_64B_MODE   this is marked as RO const 0 for bits 8-63 in docs!

      mss::mc::thermal_throttle_scominit()"
        set_pwr_cntrl_reg():
          MC01.PORT0.SRQ.PC.MBARPC0Q =    // 0x7010934
            [3-5]   MBARPC0Q_CFG_MIN_MAX_DOMAINS =                          0
            [22]    MBARPC0Q_CFG_MIN_DOMAIN_REDUCTION_ENABLE =
              if ATTR_MSS_MRW_POWER_CONTROL_REQUESTED == PD_AND_STR_OFF:    0     // default
              else:                                                         1
            [23-32] MBARPC0Q_CFG_MIN_DOMAIN_REDUCTION_TIME =                959
        set_str_reg()
          MC01.PORT0.SRQ.PC.MBASTR0Q =      // 0x7010935
            [0]     MBASTR0Q_CFG_STR_ENABLE =
              ATTR_MSS_MRW_POWER_CONTROL_REQUESTED == PD_AND_STR:           1
              ATTR_MSS_MRW_POWER_CONTROL_REQUESTED == PD_AND_STR_CLK_STOP:  1
              ATTR_MSS_MRW_POWER_CONTROL_REQUESTED == POWER_DOWN:           0
              ATTR_MSS_MRW_POWER_CONTROL_REQUESTED == PD_AND_STR_OFF:       0     // default
            [2-11]  MBASTR0Q_CFG_ENTER_STR_TIME =                           1023
        set_nm_support()
          MC01.PORT1.SRQ.MBA_FARB3Q =       // 0x7010956
            [0-14]  MBA_FARB3Q_CFG_NM_N_PER_SLOT = ATTR_MSS_RUNTIME_MEM_THROTTLED_N_COMMANDS_PER_SLOT[mss::index(MCA)]
            [15-30] MBA_FARB3Q_CFG_NM_N_PER_PORT = ATTR_MSS_RUNTIME_MEM_THROTTLED_N_COMMANDS_PER_PORT[mss::index(MCA)]
            [31-44] MBA_FARB3Q_CFG_NM_M =          ATTR_MSS_MRW_MEM_M_DRAM_CLOCKS     // default 0x200
            [45-47] MBA_FARB3Q_CFG_NM_RAS_WEIGHT = 0
            [48-50] MBA_FARB3Q_CFG_NM_CAS_WEIGHT = 1
            // Set to disable permanently due to hardware design bug (HW403028) that won't be changed
            [53]    MBA_FARB3Q_CFG_NM_CHANGE_AFTER_SYNC = 0
        set_safemode_throttles(MCA)
          MC01.PORT1.SRQ.MBA_FARB4Q =       // 0x7010957
            [27-41] MBA_FARB4Q_EMERGENCY_N = ATTR_MSS_RUNTIME_MEM_THROTTLED_N_COMMANDS_PER_PORT[mss::index(MCA)]  // BUG? var name says per_slot...
            [42-55] MBA_FARB4Q_EMERGENCY_M = ATTR_MSS_MRW_MEM_M_DRAM_CLOCKS

    // end "if (count_dimms(MCA) > 0)"

    p9n_ddrphy_scom():
      --------------------------------------------------------
      IOM0.DDRPHY_DP16_DLL_VREG_CONTROL0_P0_{0,1,2,3,4} =     // 0x8000002a0701103f, +0x0400_0000_0000
          [48-50] RXREG_VREG_COMPCON_DC = 3
          [52-59] = 0x74:
                  [53-55] RXREG_VREG_DRVCON_DC =  0x7
                  [56-58] RXREG_VREG_REF_SEL_DC = 0x2
          [62-63] = 0:
                  [62] DLL_DRVREN_MODE =      POWER8 mode (thermometer style, enabling all drivers up to the one that is used)
                  [63] DLL_CAL_CKTS_ACTIVE =  After VREG calibration, some analog circuits are powered down

      IOM0.DDRPHY_DP16_DLL_VREG_CONTROL1_P0_{0,1,2,3,4} =     // 0x8000002b0701103f, +0x0400_0000_0000
          [48-50] RXREG_VREG_COMPCON_DC = 3
          [52-59] = 0x74:
                  [53-55] RXREG_VREG_DRVCON_DC =  0x7
                  [56-58] RXREG_VREG_REF_SEL_DC = 0x2
          [62-63] = 0:
                  [62] DLL_DRVREN_MODE =      POWER8 mode (thermometer style, enabling all drivers up to the one that is used)
                  [63] DLL_CAL_CKTS_ACTIVE =  After VREG calibration, some analog circuits are powered down

      IOM0.DDRPHY_DP16_WRCLK_PR_P0_{0,1,2,3,4} =              // 0x800000740701103f, +0x0400_0000_0000
          // For zero delay simulations, or simulations where the delay of the SysClk tree and the WrClk tree are equal,
          // set this field to 60h
          [49-55] TSYS_WRCLK = 0x60

      IOM0.DDRPHY_DP16_IO_TX_CONFIG0_P0_{0,1,2,3,4} =         // 0x800000750701103f, +0x0400_0000_0000
          [48-51] STRENGTH =                    0x4 // 2400 MT/s
          [52]    DD2_RESET_READ_FIX_DISABLE =  0   // Enable the DD2 function to remove the register reset on read feature
                                                    // on status registers

      IOM0.DDRPHY_DP16_DLL_CONFIG1_P0_{0,1,2,3,4} =           // 0x800000770701103f, +0x0400_0000_0000
          [48-63] = 0x0006:
                  [48-51] HS_DLLMUX_SEL_0_0_3 = 0
                  [53-56] HS_DLLMUX_SEL_1_0_3 = 0
                  [61]    S0INSDLYTAP =         1 // For proper functional operation, this bit must be 0b
                  [62]    S1INSDLYTAP =         1 // For proper functional operation, this bit must be 0b

      IOM0.DDRPHY_DP16_IO_TX_FET_SLICE_P0_{0,1,2,3,4} =       // 0x800000780701103f, +0x0400_0000_0000
          [48-63] = 0x7f7f:
                  [59-55] EN_SLICE_N_WR = 0x7f
                  [57-63] EN_SLICE_P_WR = 0x7f
      ----------------------------------------------------------------

a)    IOM0.DDRPHY_ADR_BIT_ENABLE_P0_ADR0 =        // 0x800040000701103f     // can all 'a)' be reordered and merged into loop?
          [48-63] = 0xffff

a)    IOM0.DDRPHY_ADR_BIT_ENABLE_P0_ADR1 =        // 0x800044000701103f
          [48-63] = 0xffff

      IOM0.DDRPHY_ADR_DIFFPAIR_ENABLE_P0_ADR1 =   // 0x800044010701103f
          [48-63] = 0x5000:
                  [49] DI_ADR2_ADR3: 1 = Lanes 2 and 3 are a differential clock pair
                  [51] DI_ADR6_ADR7: 1 = Lanes 6 and 7 are a differential clock pair

      IOM0.DDRPHY_ADR_DELAY1_P0_ADR1 =            // 0x800044050701103f
          [48-63] = 0x4040:
                  [49-55] ADR_DELAY2 = 0x40
                  [57-63] ADR_DELAY3 = 0x40

      IOM0.DDRPHY_ADR_DELAY3_P0_ADR1 =            // 0x800044070701103f
          [48-63] = 0x4040:
                  [49-55] ADR_DELAY6 = 0x40
                  [57-63] ADR_DELAY7 = 0x40

a)    IOM0.DDRPHY_ADR_BIT_ENABLE_P0_ADR2 =        // 0x800048000701103f
          [48-63] = 0xffff

a)    IOM0.DDRPHY_ADR_BIT_ENABLE_P0_ADR3 =        // 0x80004c000701103f
          [48-63] = 0xffff

      ------------------------------------------------
      IOM0.DDRPHY_ADR_DLL_VREG_CONFIG_1_P0_ADR32S{0,1} =    // 0x800080310701103f, +0x0400_0000_0000
          [48-63] = 0x0008:
                  [48-51] HS_DLLMUX_SEL_0_3 = 0
                  [59-62] STRENGTH =          4 // 2400 MT/s

      IOM0.DDRPHY_ADR_MCCLK_WRCLK_PR_STATIC_OFFSET_P0_ADR32S{0,1} =     // 0x800080330701103f, +0x0400_0000_0000
          [48-63] = 0x6000
              // For zero delay simulations, or simulations where the delay of the SysClk tree and the WrClk tree are equal,
              // set this field to 60h
              [49-55] TSYS_WRCLK = 0x60

      IOM0.DDRPHY_ADR_DLL_VREG_CONTROL_P0_ADR32S{0,1} =     // 0x8000803d0701103f, +0x0400_0000_0000
          [48-50] RXREG_VREG_COMPCON_DC =         3
          [52-59] = 0x74:
                  [53-55] RXREG_VREG_DRVCON_DC =  0x7
                  [56-58] RXREG_VREG_REF_SEL_DC = 0x2
          [63] DLL_CAL_CKTS_ACTIVE =  0   // After VREG calibration, some analog circuits are powered down
      ------------------------------------------------

      IOM0.DDRPHY_PC_CONFIG0_P0 =             // 0x8000c00c0701103f
          [48-63] = 0x0202:
                  [48-51] PDA_ENABLE_OVERRIDE =     0
                  [52]    2TCK_PREAMBLE_ENABLE =    0
                  [53]    PBA_ENABLE =              0
                  [54]    DDR4_CMD_SIG_REDUCTION =  1
                  [55]    SYSCLK_2X_MEMINTCLKO =    0
                  [56]    RANK_OVERRIDE =           0
                  [57-59] RANK_OVERRIDE_VALUE =     0
                  [60]    LOW_LATENCY =             0
                  [61]    DDR4_IPW_LOOP_DIS =       0
                  [62]    DDR4_VLEVEL_BANK_GROUP =  1
                  [63]    VPROTH_PSEL_MODE =        0

  // end "for each MCA"

  p9n_mcbist_scom():
    MC01.MCBIST.MBA_SCOMFIR.WATCFG0AQ =         // 0x7012380
        [0-47]  WATCFG0AQ_CFG_WAT_EVENT_SEL =  0x400000000000

    MC01.MCBIST.MBA_SCOMFIR.WATCFG0BQ =         // 0x7012381
        [0-43]  WATCFG0BQ_CFG_WAT_MSKA =  0x3fbfff
        [44-60] WATCFG0BQ_CFG_WAT_CNTL =  0x10000

    MC01.MCBIST.MBA_SCOMFIR.WATCFG0DQ =         // 0x7012383
        [0-43]  WATCFG0DQ_CFG_WAT_PATA =  0x80200004000

    MC01.MCBIST.MBA_SCOMFIR.WATCFG3AQ =         // 0x701238f
        [0-47]  WATCFG3AQ_CFG_WAT_EVENT_SEL = 0x800000000000

    MC01.MCBIST.MBA_SCOMFIR.WATCFG3BQ =         // 0x7012390
        [0-43]  WATCFG3BQ_CFG_WAT_MSKA =  0xfffffffffff
        [44-60] WATCFG3BQ_CFG_WAT_CNTL =  0x10400

    MC01.MCBIST.MBA_SCOMFIR.MCBCFGQ =           // 0x70123e0
        [36]    MCBCFGQ_CFG_LOG_COUNTS_IN_TRACE = 0

    MC01.MCBIST.MBA_SCOMFIR.DBGCFG0Q =          // 0x70123e8
        [0]     DBGCFG0Q_CFG_DBG_ENABLE =         1
        [23-33] DBGCFG0Q_CFG_DBG_PICK_MCBIST01 =  0x780

    MC01.MCBIST.MBA_SCOMFIR.DBGCFG1Q =          // 0x70123e9
        [0]     DBGCFG1Q_CFG_WAT_ENABLE = 1

    MC01.MCBIST.MBA_SCOMFIR.DBGCFG2Q =          // 0x70123ea
        [0-19]  DBGCFG2Q_CFG_WAT_LOC_EVENT0_SEL = 0x10000
        [20-39] DBGCFG2Q_CFG_WAT_LOC_EVENT1_SEL = 0x08000

    MC01.MCBIST.MBA_SCOMFIR.DBGCFG3Q =          // 0x70123eb
        [20-22] DBGCFG3Q_CFG_WAT_GLOB_EVENT0_SEL =      0x4
        [23-25] DBGCFG3Q_CFG_WAT_GLOB_EVENT1_SEL =      0x4
        [37-40] DBGCFG3Q_CFG_WAT_ACT_SET_SPATTN_PULSE = 0x4

  mss::phy_scominit():
    reset_io_tx_config0():
      // These registers were already modified by p9n_ddrphy_scom. Can we set proper strength already there?
      IOM0.DDRPHY_DP16_IO_TX_CONFIG0_P0_{0,1,2,3,4} =     // 0x800000750701103f, +0x0400_0000_0000
          [48-51] STRENGTH =
              MSS_FREQ_EQ_1866: 1
              MSS_FREQ_EQ_2133: 2
              MSS_FREQ_EQ_2400: 4
              MSS_FREQ_EQ_2666: 8

    reset_dll_vreg_config1():
      // These registers were already modified by p9n_ddrphy_scom. Can we set proper strength already there?
      IOM0.DDRPHY_ADR_DLL_VREG_CONFIG_1_P0_ADR32S{0,1} =  // 0x800080310701103f, +0x0400_0000_0000
          [59-62] STRENGTH =
              MSS_FREQ_EQ_1866: 1
              MSS_FREQ_EQ_2133: 2
              MSS_FREQ_EQ_2400: 4
              MSS_FREQ_EQ_2666: 8

    for each functional MCA:
      if (count_dimm(MCA) == 0) continue      // can we have functional MCA with no DIMMs?
      set_rank_pairs():
        // TODO: assumes non-LR DIMMs (platform wiki) and no ATTR_EFF_RANK_GROUP_OVERRIDE (default in
        // hb_temp_defaults.xml), add if needed (get_rank_pair_assignments)
        IOM0.DDRPHY_PC_RANK_PAIR0_P0 =      // 0x8000C0020701103F
            // TODO: re-check whether these numbers are correct. Some assumptions and simplifications were made here.
            // rank_countX is the number of master ranks on DIMM X.
            [48-63] = 0x1537 & F[rank_count0]:      // F = {0, 0xf000, 0xf0f0, 0xfff0, 0xffff}
                [48-50] RANK_PAIR0_PRI = 0
                [51]    RANK_PAIR0_PRI_V = 1: if (rank_count0 >= 1)
                [52-54] RANK_PAIR0_SEC = 2
                [55]    RANK_PAIR0_SEC_V = 1: if (rank_count0 >= 3)
                [56-58] RANK_PAIR1_PRI = 1
                [59]    RANK_PAIR1_PRI_V = 1: if (rank_count0 >= 2)
                [60-62] RANK_PAIR1_SEC = 3
                [63]    RANK_PAIR1_SEC_V = 1: if (rank_count0 == 4)
        IOM0.DDRPHY_PC_RANK_PAIR1_P0 =      // 0x8000C0030701103F
            [48-63] = 0x1537 & F[rank_count1]:     // F = {0, 0xf000, 0xf0f0, 0xfff0, 0xffff}
                [48-50] RANK_PAIR2_PRI = 0
                [51]    RANK_PAIR2_PRI_V = 1: if (rank_count1 >= 1)
                [52-54] RANK_PAIR2_SEC = 2
                [55]    RANK_PAIR2_SEC_V = 1: if (rank_count1 >= 3)
                [56-58] RANK_PAIR3_PRI = 1
                [59]    RANK_PAIR3_PRI_V = 1: if (rank_count1 >= 2)
                [60-62] RANK_PAIR3_SEC = 3
                [63]    RANK_PAIR3_SEC_V = 1: if (rank_count1 == 4)
        IOM0.DDRPHY_PC_RANK_PAIR2_P0 =      // 0x8000C0300701103F
            [48-63] = 0
        IOM0.DDRPHY_PC_RANK_PAIR3_P0 =      // 0x8000C0310701103F
            [48-63] = 0
        IOM0.DDRPHY_PC_CSID_CFG_P0 =        // 0x8000C0330701103F
                [0-63]  0xf000:
                    [48]  CS0_INIT_CAL_VALUE = 1
                    [49]  CS1_INIT_CAL_VALUE = 1
                    [50]  CS2_INIT_CAL_VALUE = 1
                    [51]  CS3_INIT_CAL_VALUE = 1
        IOM0.DDRPHY_PC_MIRROR_CONFIG_P0 =   // 0x8000C0110701103F
                [all] = 0
                // A rank is mirrored if all are true:
                //  - the rank is valid (RANK_PAIRn_XXX_V   ==  1)
                //  - the rank is odd   (RANK_PAIRn_XXX % 2 ==  1)
                //  - the mirror mode attribute is set for the rank's DIMM (set in
                //    src/import/chips/p9/procedures/hwp/memory/lib/dimm/eff_dimm.C from SPD[136])
                //  - We are not in quad encoded mode (so master ranks <= 2)
                [48]    ADDR_MIRROR_RP0_PRI
                        ...
                [55]    ADDR_MIRROR_RP3_SEC
                [58]    ADDR_MIRROR_A3_A4 = 1
                [59]    ADDR_MIRROR_A5_A6 = 1
                [60]    ADDR_MIRROR_A7_A8 = 1
                [61]    ADDR_MIRROR_A11_A13 = 1
                [62]    ADDR_MIRROR_BA0_BA1 = 1
                [63]    ADDR_MIRROR_BG0_BG1 = 1
        IOM0.DDRPHY_PC_RANK_GROUP_EXT_P0 =  // 0x8000C0350701103F
                [all] = 0
                // Same rules as above
                [48]    ADDR_MIRROR_RP0_TER
                        ...
                [55]    ADDR_MIRROR_RP3_QUA

      reset_data_bit_enable():
        IOM0.DDRPHY_DP16_DQ_BIT_ENABLE0_P0_{0,1,2,3} =    // 0x800000000701103F, +0x0400_0000_0000
                [all] = 0
                [48-63] DATA_BIT_ENABLE_0_15 = 0xffff
        IOM0.DDRPHY_DP16_DQ_BIT_ENABLE0_P0_4 =            // 0x800010000701103F
                [all] = 0
                [48-63] DATA_BIT_ENABLE_0_15 = 0xff00
        IOM0.DDRPHY_DP16_DFT_PDA_CONTROL_P0_{0,1,2,3,4} = // 0x800000010701103F, +0x0400_0000_0000
                // This reg is named MCA_DDRPHY_DP16_DATA_BIT_ENABLE1_P0_n in the code.
                // BUG? Spec says bits [48-55] must remain at reset value, but code disagrees. Follow the code for now...
                [all] = 0

      reset_bad_bits():
        // TODO: this disables bad DQ (and DQS if all DQs are dead) bits, based on data saved in NVRAM during earlier
        // trainings. For now assume there are no bad DQ bits.
        // Regs touched: IOM0.DDRPHY_DP16_DQ_BIT_DISABLE_RP{0-3}_P0_{0-4}, IOM0.DDRPHY_DP16_DQS_BIT_DISABLE_RP{0-3}_P0_{0-4}
        // - Is it enough to leave reset values or do we have to set them to 0 explicitly?
        //   - Those bits are set to 1 during training for failed pins in calibration steps
        // - Can DQ(S) bits be "resurrected" later in the training or is the NVRAM data final?

      get_rank_pairs(MCA, l_pairs)
        // l_pairs holds 4 bits of useful information (rank 0,1,2,3 exists or not). It uses up to 4*sizeof(uint64) bytes
        // for this (plus vector overhead) when passed as an argument; a vector of 5 vectors of 5 such 4-element vectors
        // is used in const data section for this. There is also a function which main task is to convert 4/5 to 2/3 and
        // skip empty elements before returning final vector, so static const data cannot be reused.
        // TL;DR: this function and its data is a waste of space
        // See set_rank_pairs above, this function uses different path to get almost identical result. Why this overcomplication?
        l_pairs = vector of {0, 1, 2, 3}, skip entry(-ies) if rank count is <2, e.g.
                {0,2}   - 1 ranks DIMM0, 1 ranks DIMM1
                {0,2,3} - 1 ranks DIMM0, 2(3,4) ranks DIMM1
                {0,1}   - 2(3,4) ranks DIMM0, 0 ranks DIMM1 etc.

      // The following two functions specify which clock/strobes pins (16-23) of DP16 are used to capture outgoing/incoming
      // data on which data pins (0-16). Those will eventually arrive to DIMM as DQS and DQ, respectively. The mapping must
      // be the same for write and read. It will be used after DRAM training to discover which nibbles failed.
      // TODO: are those mappings determined by hardware or can we change them to uniform mapping for all DIMMs/ranks?
      // Schematics don't show what happens inside CPU, i.e. whether these DP16 pins are multiplexed (and configurable by
      // these registers) or hardwired between DP16 and external CPU pins.
      reset_write_clock_enable(MCA, l_pairs)    // Merge with next function
        for each rp in l_pairs:
          wrckl_enable =        // src/import/chips/p9/procedures/hwp/memory/lib/phy/dp16.C
            wrclk_enable_no_spare_x4[0]: if DRAM width = x4
            wrclk_enable_no_spare_x8[MCA.pos]: if DRAM width = x8
          for each reg in wrclk_enable:
            reg.first + (rp << 40) =    // IOM0.DDRPHY_DP16_WRCLK_EN_RP<rp>_P0_{0-4}            0x80000<rp>050701103F, +0x0400_0000_0000
                  [48-63] QUADx_CLKyy = reg.second  // different for each reg/port, but the same for read and write

      reset_read_clock_enable(MCA, l_pairs):
        for each rp in l_pairs:
          rdckl_enable =        // src/import/chips/p9/procedures/hwp/memory/lib/phy/dp16.C
            rdclk_enable_no_spare_x4[0]: if DRAM width = x4
            rdclk_enable_no_spare_x8[MCA.pos]: if DRAM width = x8
          for each reg in rdclk_enable:
            reg.first + (rp << 40) =    // IOM0.DDRPHY_DP16_READ_CLOCK_RANK_PAIR<rp>_P0_{0-4}   0x80000<rp>040701103F, +0x0400_0000_0000
                  [48-63] QUADx_CLKyy = reg.second  // different for each reg/port, but the same for read and write

      reset_rd_vref():
        //       RD_VREF_DVDD * (100000 - ATTR_MSS_VPD_MT_VREF_MC_RD) / RD_VREF_DAC_STEP
        vref_bf =     12      * (100000 - ATTR_MSS_VPD_MT_VREF_MC_RD) / 6500
        IOM0.DDRPHY_DP16_RD_VREF_DAC_{0-7}_P0_{0-3},            // addresses are not regular for DAC
        IOM0.DDRPHY_DP16_RD_VREF_DAC_{0-3}_P0_4 =               // only half of last DP16 is used
              [49-55] BIT0_VREF_DAC = vref_bf
              [57-63] BIT1_VREF_DAC = vref_bf
        IOM0.DDRPHY_DP16_RD_VREF_CAL_EN_P0_{0-4}                // 0x800000760701103F, +0x0400_0000_0000
              [48-63] VREF_CAL_EN = 0xffff          // enable = 0xffff, disable = 0x0000

      pc::reset():
        // IOM0.DDRPHY_PC_CONFIG0_P0 (0x8000c00c0701103f) has been reset in p9n_ddrphy_scom()
        IOM0.DDRPHY_PC_CONFIG1_P0 =             // 0x8000c00d0701103f
              [48-51] WRITE_LATENCY_OFFSET =  ATTR_MSS_EFF_DPHY_WLO
              [52-55] READ_LATENCY_OFFSET =   ATTR_MSS_EFF_DPHY_RLO
                        // If the MRW states 'auto' we use what's in VPD, otherwise we use what's in the MRW
                        +1: if 2N mode (ATTR_MSS_VPD_MR_MC_2N_MODE_AUTOSET, ATTR_MSS_MRW_DRAM_2N_MODE)  // Gear-down mode in JEDEC
              // Assume no LRDIMM
              [59-61] MEMORY_TYPE =           0x5     // 0x7 for LRDIMM
              [62]    DDR4_LATENCY_SW =       1

        IOM0.DDRPHY_PC_ERROR_STATUS0_P0 =       // 0x8000C0120701103F
              [all]   0

        IOM0.DDRPHY_PC_INIT_CAL_ERROR_P0 =      // 0x8000C0180701103F
              [all]   0

      wc::reset():
        IOM0.DDRPHY_WC_CONFIG0_P0 =             // 0x8000CC000701103F
              [all]   0
              // BUG? Mismatch between comment (-,-), code (+,+) and docs (-,+) for operations inside 'max'
              [48-55] TWLO_TWLOE =        12 + max((twldqsen - tmod), (twlo + twlow))
                                   + longest DQS delay in clocks (rounded up) + longest DQ delay in clocks (rounded up)
              [56]    WL_ONE_DQS_PULSE =  1
              [57-62] FW_WR_RD =          0x20      // "# dd0 = 17 clocks, now 32 from SWyatt"
              [63]    CUSTOM_INIT_WRITE = 1         // set to a 1 to get proper values for RD VREF

        IOM0.DDRPHY_WC_CONFIG1_P0 =             // 0x8000CC010701103F
              [all]   0
              [48-51] BIG_STEP =          7
              [52-54] SMALL_STEP =        0
              [55-60] WR_PRE_DLY =        0x2a (42)

        IOM0.DDRPHY_WC_CONFIG2_P0 =             // 0x8000CC020701103F
              [all]   0
              [48-51] NUM_VALID_SAMPLES = 5
              [52-57] FW_RD_WR =          max(tWTR + 11, AL + tRTP + 3)
              [58-61] IPW_WR_WR =         5     // results in 24 clock cycles

        IOM0.DDRPHY_WC_CONFIG3_P0 =             // 0x8000CC050701103F
              [all]   0
              [55-60] MRS_CMD_DQ_OFF =    0x3f

        IOM0.DDRPHY_WC_RTT_WR_SWAP_ENABLE_P0    // 0x8000CC060701103F
              [48]    WL_ENABLE_RTT_SWAP =            0
              [49]    WR_CTR_ENABLE_RTT_SWAP =        0
              [48]    WR_CTR_VREF_COUNTER_RESET_VAL = 150ns converted to clock cycles (depends on the frequency)  // JESD79-4C Table 67

      rc::reset():
        IOM0.DDRPHY_RC_CONFIG0_P0               // 0x8000C8000701103F
              [all]   0
              [48-51] GLOBAL_PHY_OFFSET =
                          MSS_FREQ_EQ_1866: 12
                          MSS_FREQ_EQ_2133: 12
                          MSS_FREQ_EQ_2400: 13
                          MSS_FREQ_EQ_2666: 13
              [62]    PERFORM_RDCLK_ALIGN = 1

        IOM0.DDRPHY_RC_CONFIG1_P0               // 0x8000C8010701103F
              [all]   0

        IOM0.DDRPHY_RC_CONFIG2_P0               // 0x8000C8020701103F
              [all]   0
              [48-52] CONSEC_PASS = 8
              [57-58] 3                   // not documented, BURST_WINDOW?

        IOM0.DDRPHY_RC_CONFIG3_P0               // 0x8000C8070701103F
              [all]   0
              [51-54] COARSE_CAL_STEP_SIZE = 4  // 5/128

        IOM0.DDRPHY_RC_RDVREF_CONFIG0_P0 =      // 0x8000C8090701103F
              [all]   0
              [48-63] WAIT_TIME =
                          0xffff          // as slow as possible, or use calculation from vref_guess_time(), or:
                          MSS_FREQ_EQ_1866: 0x0804
                          MSS_FREQ_EQ_2133: 0x092a
                          MSS_FREQ_EQ_2400: 0x0a50
                          MSS_FREQ_EQ_2666: 0x0b74    // use this value for all freqs maybe?

        IOM0.DDRPHY_RC_RDVREF_CONFIG1_P0 =      // 0x8000C80A0701103F
              [all]   0
              [48-55] CMD_PRECEDE_TIME =  (AL + CL + 15)
              [56-59] MPR_LOCATION =      4     // "From R. King."

      seq::reset():
        IOM0.DDRPHY_SEQ_CONFIG0_P0 =            // 0x8000C4020701103F
              [all]   0
              [49]    TWO_CYCLE_ADDR_EN =
                          2N mode:                1
                          else:                   0
              [54]    DELAYED_PAR = 1
              [62]    PAR_A17_MASK =
                          16Gb x4 configuration:  0
                          else:                   1

        // All log2 values in timing registers are rounded up (I think...)
        IOM0.DDRPHY_SEQ_MEM_TIMING_PARAM0_P0 =  // 0x8000C4120701103F
              [all]   0
              [48-51] TMOD_CYCLES = 5           // log2(max(tMRD, tMOD)) = log2(24), JEDEC tables 169 and 170 and section 13.5
              [52-55] TRCD_CYCLES = log2(tRCD)  // 12.5-15ns, depending on freq either 4 or 5, JEDEC tables 144-150 and s. 13.5
              [56-59] TRP_CYCLES =  log2(tRP)   // 12.5-15ns, depending on freq either 4 or 5, JEDEC tables 144-150 and s. 13.5
              [52-55] TRFC_CYCLES = log2(tRFC)  // different DIMMs on one port may have different tRFCs, use max?

        IOM0.DDRPHY_SEQ_MEM_TIMING_PARAM1_P0 =  // 0x8000C4130701103F
              [all]   0
              [48-51] TZQINIT_CYCLES =  10      // log2(1024), JEDEC tables 169 and 170
              [52-55] TZQCS_CYCLES =    7       // log2(128), JEDEC tables 169 and 170
              [56-59] TWLDQSEN_CYCLES = 5       // log2(25) rounded up, JEDEC tables 169 and 170
              [60-63] TWRMRD_CYCLES =   6       // log2(40) rounded up, JEDEC tables 169 and 170

        IOM0.DDRPHY_SEQ_MEM_TIMING_PARAM2_P0 =  // 0x8000C4140701103F
              [all]   0
              [48-51] TODTLON_OFF_CYCLES =  log2(CWL + AL + PL - 2)
              [52-63] =                     0x777     // "Reset value of SEQ_TIMING2 is lucky 7's"

        IOM0.DDRPHY_SEQ_RD_WR_DATA0_P0 =        // 0x8000C4000701103F
              [all]   0
              [48-63] RD_RW_DATA_REG0 = 0xaa00

        IOM0.DDRPHY_SEQ_RD_WR_DATA1_P0 =        // 0x8000C4010701103F
              [all]   0
              [48-63] RD_RW_DATA_REG1 = 0x00aa

        IOM0.DDRPHY_SEQ_ODT_RD_CONFIG0_P0 =     // 0x8000C40E0701103F
              F(X) = (((X >> 4) & 0xc) | ((X >> 2) & 0x3))    // Bits 0,1,4,5 of X, see also MC01.PORT0.SRQ.MBA_FARB2Q
              [all]   0
              [48-51] ODT_RD_VALUES0 = F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][0][0])
              [56-59] ODT_RD_VALUES1 = F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][0][1])

        IOM0.DDRPHY_SEQ_ODT_RD_CONFIG1_P0 =     // 0x8000C40F0701103F
              F(X) = (((X >> 4) & 0xc) | ((X >> 2) & 0x3))    // Bits 0,1,4,5 of X, see also MC01.PORT0.SRQ.MBA_FARB2Q
              [all]   0
              [48-51] ODT_RD_VALUES0 =    // TODO: where those VPD values come from? Aren't they the same by any chance?
                        count_dimm(MCA) == 2: F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][1][0])
                        count_dimm(MCA) != 2: F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][0][2])
              [56-59] ODT_RD_VALUES1 =
                        count_dimm(MCA) == 2: F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][1][1])
                        count_dimm(MCA) != 2: F(ATTR_MSS_VPD_MT_ODT_RD[index(MCA)][0][3])


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

      reset_ac_boost_cntl():
        IOM0.DDRPHY_DP16_ACBOOST_CTL_BYTE{0,1}_P0_{0,1,2,3,4} =     // 0x8000002{2,3}0701103F, +0x0400_0000_0000
            // For all of the AC Boost attributes, they're laid out in the uint32_t as such:
            // Bit 0-2   = DP16 Block 0 (DQ Bits 0-7)       BYTE0_P0_0
            // Bit 3-5   = DP16 Block 0 (DQ Bits 8-15)      BYTE1_P0_0
            // Bit 6-8   = DP16 Block 1 (DQ Bits 0-7)       BYTE0_P0_1
            // Bit 9-11  = DP16 Block 1 (DQ Bits 8-15)      BYTE1_P0_1
            // Bit 12-14 = DP16 Block 2 (DQ Bits 0-7)       BYTE0_P0_2
            // Bit 15-17 = DP16 Block 2 (DQ Bits 8-15)      BYTE1_P0_2
            // Bit 18-20 = DP16 Block 3 (DQ Bits 0-7)       BYTE0_P0_3
            // Bit 21-23 = DP16 Block 3 (DQ Bits 8-15)      BYTE1_P0_3
            // Bit 24-26 = DP16 Block 4 (DQ Bits 0-7)       BYTE0_P0_4
            // Bit 27-29 = DP16 Block 4 (DQ Bits 8-15)      BYTE1_P0_4
            [all]   0?    // function does read prev values from SCOM but then overwrites all non-const-0 fields. Why bother?
            [48-50] S{0,1}ACENSLICENDRV_DC = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_ACBOOST_WR_DOWN
            [51-53] S{0,1}ACENSLICENDRV_DC = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_ACBOOST_WR_UP
            [54-56] S{0,1}ACENSLICENDRV_DC = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_ACBOOST_RD_UP

      reset_ctle_cntl():
        IOM0.DDRPHY_DP16_CTLE_CTL_BYTE{0,1}_P0_{0,1,2,3,4} =        // 0x8000002{0,1}0701103F, +0x0400_0000_0000
            // For the capacitance CTLE attributes, they're laid out in the uint64_t as such. The resitance
            // attributes are the same, but 3 bits long. Notice that DP Block X Nibble 0 is DQ0:3,
            // Nibble 1 is DQ4:7, Nibble 2 is DQ8:11 and 3 is DQ12:15.
            // Bit 0-1   = DP16 Block 0 Nibble 0     Bit 16-17 = DP16 Block 2 Nibble 0     Bit 32-33 = DP16 Block 4 Nibble 0
            // Bit 2-3   = DP16 Block 0 Nibble 1     Bit 18-19 = DP16 Block 2 Nibble 1     Bit 34-35 = DP16 Block 4 Nibble 1
            // Bit 4-5   = DP16 Block 0 Nibble 2     Bit 20-21 = DP16 Block 2 Nibble 2     Bit 36-37 = DP16 Block 4 Nibble 2
            // Bit 6-7   = DP16 Block 0 Nibble 3     Bit 22-23 = DP16 Block 2 Nibble 3     Bit 38-39 = DP16 Block 4 Nibble 3
            // Bit 8-9   = DP16 Block 1 Nibble 0     Bit 24-25 = DP16 Block 3 Nibble 0
            // Bit 10-11 = DP16 Block 1 Nibble 1     Bit 26-27 = DP16 Block 3 Nibble 1
            // Bit 12-13 = DP16 Block 1 Nibble 2     Bit 28-29 = DP16 Block 3 Nibble 2
            // Bit 14-15 = DP16 Block 1 Nibble 3     Bit 30-31 = DP16 Block 3 Nibble 3
            [48-49] NIB_{0,2}_DQSEL_CAP = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_CTLE_CAP
            [53-55] NIB_{0,2}_DQSEL_RES = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_CTLE_RES
            [56-57] NIB_{1,3}_DQSEL_CAP = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_CTLE_CAP
            [61-63] NIB_{1,3}_DQSEL_RES = appropriate bits from ATTR_MSS_VPD_MT_MC_DQ_CTLE_RES

      reset_delay():
        // "If the reset value is not sufficient for the given system, these registers must be set via the programming interface."
        IOM0.DDRPHY_ADR_DELAY0_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY0 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_CSN0
            [57-63] ADR_DELAY1 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CMD_ADDR_WEN_A14
        IOM0.DDRPHY_ADR_DELAY1_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY2 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_ODT1
            [57-63] ADR_DELAY3 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_C0
        IOM0.DDRPHY_ADR_DELAY2_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY4 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_BA1
            [57-63] ADR_DELAY5 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A10
        IOM0.DDRPHY_ADR_DELAY3_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY6 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_ODT1
            [57-63] ADR_DELAY7 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_BA0
        IOM0.DDRPHY_ADR_DELAY4_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY8 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A00
            [57-63] ADR_DELAY9 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_ODT0
        IOM0.DDRPHY_ADR_DELAY5_P0_ADR0 =
            [all]   0
            [49-55] ADR_DELAY10 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_ODT0
            [57-63] ADR_DELAY11 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CMD_ADDR_CASN_A15

        IOM0.DDRPHY_ADR_DELAY0_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY0 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A13
            [57-63] ADR_DELAY1 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_CSN1
        IOM0.DDRPHY_ADR_DELAY1_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY2 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_D0_CLKN
            [57-63] ADR_DELAY3 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_D0_CLKP
        IOM0.DDRPHY_ADR_DELAY2_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY4 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A17
            [57-63] ADR_DELAY5 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_C1
        IOM0.DDRPHY_ADR_DELAY3_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY6 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_D1_CLKN
            [57-63] ADR_DELAY7 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_D1_CLKP
        IOM0.DDRPHY_ADR_DELAY4_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY8 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_C2
            [57-63] ADR_DELAY9 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_CSN1
        IOM0.DDRPHY_ADR_DELAY5_P0_ADR1 =
            [all]   0
            [49-55] ADR_DELAY10 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A02
            [57-63] ADR_DELAY11 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CMD_PAR

        IOM0.DDRPHY_ADR_DELAY0_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY0 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_CSN0
            [57-63] ADR_DELAY1 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CMD_ADDR_RASN_A16
        IOM0.DDRPHY_ADR_DELAY1_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY2 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A08
            [57-63] ADR_DELAY3 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A05
        IOM0.DDRPHY_ADR_DELAY2_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY4 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A03
            [57-63] ADR_DELAY5 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A01
        IOM0.DDRPHY_ADR_DELAY3_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY6 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A04
            [57-63] ADR_DELAY7 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A07
        IOM0.DDRPHY_ADR_DELAY4_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY8 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A09
            [57-63] ADR_DELAY9 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A06
        IOM0.DDRPHY_ADR_DELAY5_P0_ADR2 =
            [all]   0
            [49-55] ADR_DELAY10 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_CKE1
            [57-63] ADR_DELAY11 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A12

        IOM0.DDRPHY_ADR_DELAY0_P0_ADR3 =
            [all]   0
            [49-55] ADR_DELAY0 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CMD_ACTN
            [57-63] ADR_DELAY1 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_A11
        IOM0.DDRPHY_ADR_DELAY1_P0_ADR3 =
            [all]   0
            [49-55] ADR_DELAY2 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_BG0
            [57-63] ADR_DELAY3 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D0_CKE0
        IOM0.DDRPHY_ADR_DELAY2_P0_ADR3 =
            [all]   0
            [49-55] ADR_DELAY4 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_CKE1
            [57-63] ADR_DELAY5 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_ADDR_BG1
        IOM0.DDRPHY_ADR_DELAY3_P0_ADR3 =
            [all]   0
            [49-55] ADR_DELAY6 = ATTR_MSS_VPD_MR_MC_PHASE_ROT_CNTL_D1_CKE0

      reset_tsys_adr():
        IOM0.DDRPHY_ADR_MCCLK_WRCLK_PR_STATIC_OFFSET_P0_ADR32S{0,1} =   // 0x800080330701103F, +0x0400_0000_0000
            [all]   0
            [49-55] TSYS_WRCLK = ATTR_MSS_VPD_MR_TSYS_ADR
                  // From regs spec:
                  // Set to ‘19’h for 2666 MT/s.
                  // Set to ‘17’h for 2400 MT/s.
                  // Set to ‘14’h for 2133 MT/s.
                  // Set to ‘12’h for 1866 MT/s.

      reset_tsys_data():
        IOM0.DDRPHY_DP16_WRCLK_PR_P0_{0,1,2,3,4} =                      // 0x800000740701103F, +0x0400_0000_0000
            [all]   0
            [49-55] TSYS_WRCLK = ATTR_MSS_VPD_MR_TSYS_DATA
                  // From regs spec:
                  // Set to ‘12’h for 2666 MT/s.
                  // Set to ‘10’h for 2400 MT/s.
                  // Set to ‘0F’h for 2133 MT/s.
                  // Set to ‘0D’h for 1866 MT/s.

      reset_io_impedances():
        IOM0.DDRPHY_DP16_IO_TX_FET_SLICE_P0_{0,1,2,3,4} =               // 0x800000780701103F, +0x0400_0000_0000
            [all]   0
            // 0 - Hi-Z, otherwise impedance = 240/<num of set bits> Ohms
            [49-55] EN_SLICE_N_WR = ATTR_MSS_VPD_MT_MC_DRV_IMP_DQ_DQS[{0,1,2,3,4}]
            [57-63] EN_SLICE_P_WR = ATTR_MSS_VPD_MT_MC_DRV_IMP_DQ_DQS[{0,1,2,3,4}]

        IOM0.DDRPHY_DP16_IO_TX_PFET_TERM_P0_{0,1,2,3,4} =               // 0x8000007B0701103F, +0x0400_0000_0000
            [all]   0
            // 0 - Hi-Z, otherwise impedance = 240/<num of set bits> Ohms
            [49-55] EN_SLICE_N_WR = ATTR_MSS_VPD_MT_MC_RCV_IMP_DQ_DQS[{0,1,2,3,4}]

        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR1 =    // yes, ADR1  // 0x800044200701103F
            // These are RMW one at a time. I don't see why not all at once, or at least in pairs (P and N of the same clocks)
            if (ATTR_MSS_VPD_MT_MC_DRV_IMP_CLK == ENUM_ATTR_MSS_VPD_MT_MC_DRV_IMP_CLK_OHM30):
              [54,52,62,60] SLICE_SELn = 1    // CLK00 P, CLK00 N, CLK01 P, CLK01 N
            else
              [54,52,62,60] = 0

        // Following are reordered to minimalize number of register reads/writes
        ------------------------------------------------------------------------
        val = (ATTR_MSS_VPD_MT_MC_DRV_IMP_CMD_ADDR == ENUM_ATTR_MSS_VPD_MT_MC_DRV_IMP_CMD_ADDR_OHM30) ? 1 : 0
        // val = 30 for all VPD sets
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR0 =
            [50,56,58,62] =           val       // ADDR14/WEN, BA1, ADDR10, BA0
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR0 =
            [48,54] =                 val       // ADDR0, ADDR15/CAS
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR1 =        // same as CLK, however it uses different VPD
            [48,56] =                 val       // ADDR13, ADDR17/RAS
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR1 =
            [52]    =                 val       // ADDR2
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR2 =
            [50,52,54,56,58,60,62] =  val       // ADDR16/RAS, ADDR8, ADDR5, ADDR3, ADDR1, ADDR4, ADDR7
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR2 =
            [48,50,54] =              val       // ADDR9, ADDR6, ADDR12
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR3 =
            [48,50,52,58] =           val       // ACT_N, ADDR11, BG0, BG1

        // Following are reordered to minimalize number of register reads/writes
        ------------------------------------------------------------------------
        val = (ATTR_MSS_VPD_MT_MC_DRV_IMP_CNTL == ENUM_ATTR_MSS_VPD_MT_MC_DRV_IMP_CNTL_OHM30) ? 1 : 0
        // val = 30 for all VPD sets
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR0 =        // same as CMD/ADDR, however it uses different VPD
            [52,60] =                 val       // ODT3, ODT1
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR0 =        // same as CMD/ADDR, however it uses different VPD
            [50,52] =                 val       // ODT2, ODT0
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR1 =        // same as CMD/ADDR, however it uses different VPD
            [54] =                    val       // PARITY
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR2 =        // same as CMD/ADDR, however it uses different VPD
            [52] =                    val       // CKE1
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR3 =        // same as CMD/ADDR, however it uses different VPD
            [54,56,60,62] =           val       // CKE0, CKE3, CKE2, RESET_N

        // Following are reordered to minimalize number of register reads/writes
        ------------------------------------------------------------------------
        val = (ATTR_MSS_VPD_MT_MC_DRV_IMP_CSCID == ENUM_ATTR_MSS_VPD_MT_MC_DRV_IMP_CSCID_OHM30) ? 1 : 0
        // val = 34 for all VPD sets
        // FIXME: this is wrong... DQ(S) can have 34 Ohms, but not CSCID. Maybe X# keywords were not updated after layout
        // was changed? MT definitely says that layout version 0x01 is used. Why does it not assert in reset_imp_cscid?
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR0 =        // same as CMD/ADDR and CNTL, however it uses different VPD
            [48,54] =                 val       // CS0, CID0
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR1 =        // same as CLK and CMD/ADDR, however it uses different VPD
            [50,58] =                 val       // CS1, CID1
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP1_P0_ADR1 =        // same as CMD/ADDR and CNTL, however it uses different VPD
            [48,50] =                 val       // CS3, CID2
        IOM0.DDRPHY_ADR_IO_FET_SLICE_EN_MAP0_P0_ADR2 =        // same as CMD/ADDR, however it uses different VPD
            [48] =                    val       // CS2

        // IO impedances regs summary:            lanes 9-15 have different possible settings (results in 15/30 vs 40/30 Ohm)
        // MAP0_ADR0: all set                       MAP1_ADR0: lanes 12-15 not set
        // MAP0_ADR1: all set                       MAP1_ADR1: lanes 12-15 not set
        // MAP0_ADR2: all set                       MAP1_ADR2: lanes 12-15 not set
        // MAP0_ADR3: all set                       MAP1_ADR3: not used
        // This mapping is consistent with ADR_DELAYx_P0_ADRy settings

      reset_wr_vref_registers():
        IOM0.DDRPHY_DP16_WR_VREF_CONFIG0_P0_{0,1,2,3,4} =       // 0x8000006C0701103F, +0x0400_0000_0000
            // This may be a good place for tweaking training times, if needed
            [all]   0
            [48]    WR_CTR_1D_MODE_SWITCH =       0     // 1 for <DD2
            [49]    WR_CTR_RUN_FULL_1D =          1
            [50-52] WR_CTR_2D_SMALL_STEP_VAL =    0     // implicit +1
            [53-56] WR_CTR_2D_BIG_STEP_VAL =      1     // implicit +1
            [57-59] WR_CTR_NUM_BITS_TO_SKIP =     0     // skip nothing
            [60-62] WR_CTR_NUM_NO_INC_VREF_COMP = 7

        IOM0.DDRPHY_DP16_WR_VREF_CONFIG1_P0_{0,1,2,3,4} =       // 0x800000EC0701103F, +0x0400_0000_0000
            [all]   0
            [48]    WR_CTR_VREF_RANGE_SELECT =      0       // range 1 by default (60-92.5%)
            [49-55] WR_CTR_VREF_RANGE_CROSSOVER =   0x18    // JEDEC table 34
            [56-62] WR_CTR_VREF_SINGLE_RANGE_MAX =  0x32    // JEDEC table 34

        IOM0.DDRPHY_DP16_WR_VREF_STATUS0_P0_{0,1,2,3,4} =       // 0x8000002E0701103F, +0x0400_0000_0000
            [all]   0

        IOM0.DDRPHY_DP16_WR_VREF_STATUS1_P0_{0,1,2,3,4} =       // 0x8000002F0701103F, +0x0400_0000_0000
            [all]   0

        IOM0.DDRPHY_DP16_WR_VREF_ERROR_MASK{0,1}_P0_{0,1,2,3,4} =   // 0x800000F{B,A}0701103F, +0x0400_0000_0000
            [all]   0
            [48-63] 0xffff

        IOM0.DDRPHY_DP16_WR_VREF_ERROR{0,1}_P0_{0,1,2,3,4} =    // 0x800000A{E,F}0701103F, +0x0400_0000_0000
            [all]   0

        // Assume RDIMM
        // Assume unpopulated DIMMs/ranks are not calibrated so their settings doesn't matter (more reg accesses, much simpler code)
        IOM0.DDRPHY_DP16_WR_VREF_VALUE{0,1}_RANK_PAIR0_P0_{0,1,2,3,4} =   // 0x8000005{E,F}0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_VALUE{0,1}_RANK_PAIR1_P0_{0,1,2,3,4} =   // 0x8000015{E,F}0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_VALUE{0,1}_RANK_PAIR2_P0_{0,1,2,3,4} =   // 0x8000025{E,F}0701103F, +0x0400_0000_0000
        IOM0.DDRPHY_DP16_WR_VREF_VALUE{0,1}_RANK_PAIR3_P0_{0,1,2,3,4} =   // 0x8000035{E,F}0701103F, +0x0400_0000_0000
            [all]   0
            [49]    WR_VREF_RANGE_DRAM{0,2} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
            [50-55] WR_VREF_VALUE_DRAM{0,2} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f
            [57]    WR_VREF_RANGE_DRAM{1,3} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x40
            [58-63] WR_VREF_VALUE_DRAM{1,3} = ATTR_MSS_VPD_MT_VREF_DRAM_WR & 0x3f

      reset_drift_limits():
        IOM0.DDRPHY_DP16_DRIFT_LIMITS_P0_{0,1,2,3,4} =          // 0x8000000A0701103F, +0x0400_0000_0000
            [48-49] DD2_BLUE_EXTEND_RANGE = 1         // always ONE_TO_FOUR due to red waterfall workaround

      // Workarounds
      dqs_polarity()        // Does not apply to DD2

      rd_dia_config5():
        IOM0.DDRPHY_DP16_RD_DIA_CONFIG5_P0_{0,1,2,3,4} =        // 0x800000120701103F, +0x0400_0000_0000
            // "this isn't an EC feature workaround, it's a incorrect documentation workaround"
            [all]   0
            [49]    DYN_MCTERM_CNTL_EN =      1
            [52]    PER_CAL_UPDATE_DISABLE =  1     // "This bit must be set to 0 for normal operation"
            [59]    PERCAL_PWR_DIS =          1

      dqsclk_offset():
        IOM0.DDRPHY_DP16_DQSCLK_OFFSET_P0_{0,1,2,3,4} =         // 0x800000370701103F, +0x0400_0000_0000
            // "this isn't an EC feature workaround, it's a incorrect documentation workaround"
            [all]   0
            [49-55] DQS_OFFSET = 0x08       // Config provided by S. Wyatt 9/13

      odt_config():        // Does not apply to DD2

  // Do FIRry things
  mss::unmask::after_scominit():
    for each functional or magic MCA
      IOM0.IOM_PHY0_DDRPHY_FIR_REG =      // 0x07011000         // maybe use SCOM1 (AND) 0x07011001
          [56]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_2 = 0   // calibration errors
          [58]  IOM_PHY0_DDRPHY_FIR_REG_DDR_FIR_ERROR_4 = 0   // DLL errors
      MC01.PORT0.SRQ.MBACALFIRQ =         // 0x07010900         // maybe use SCOM1 (AND) 0x07010901
          [4]   MBACALFIRQ_RCD_PARITY_ERROR = 0
          [8]   MBACALFIRQ_DDR_MBA_EVENT_N =  0
```
