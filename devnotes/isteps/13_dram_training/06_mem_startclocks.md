## mem_startclocks: Start clocks on MBA/MCAs (13.6)

> a) p9_mem_startclocks.C (proc chip)
>     - This step is a no-op on cumulus
>     - This step is a no-op if memory is running in synchronous mode since the MCAs are using the nest PLL, HWP detect
>       and exits
>     - Drop fences and tholds on MBA/MCAs to start the functional clocks

```
For each functional Proc:
pg_vector = bitmap of ATTR_CHIP_UNIT_POS of functional Perv children of Proc, bit numbers come from Perl + XML
 - /src/import/chips/p9/procedures/hwp/perv/p9_sbe_common.C:692
> if(!ATTR_MC_SYNC_MODE)
  For each functional MC(BIST?):
    // Call p9_mem_startclocks_cplt_ctrl_action_function for Mc chiplets
    p9_mem_startclocks_cplt_ctrl_action_function():
      // Drop partial good fences
      TP.TCMC01.MCSLOW.CPLT_CTRL1 (WO_CLEAR)                // 0x07000021
        [all]   0
        [3]     TC_VITL_REGION_FENCE =                  ~ATTR_PG[3]         // TODO: where does ATTR_PG value come from?
        [4-14]  TC_REGION{1-3}_FENCE, UNUSED_{8-14}B =  ~ATTR_PG[4-14]
      // Reset abistclk_muxsel and syncclk_muxsel
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_CLEAR)                // 0x07000020
        [all]   0
        [0]     CTRL_CC_ABSTCLK_MUXSEL_DC = 1
        [1]     TC_UNIT_SYNCCLK_MUXSEL_DC = 1

    // Call module align chiplets for Mc chiplets
    p9_sbe_common_align_chiplets():
      // Exit flush
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_OR)                   // 0x07000010
        [all]   0
        [2]     CTRL_CC_FLUSHMODE_INH_DC =  1

      // Enable alignement
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_OR)                   // 0x07000010
        [all]   0
        [3]     CTRL_CC_FORCE_ALIGN_DC =    1

      // Clear chiplet is aligned
      TP.TCMC01.MCSLOW.SYNC_CONFIG                          // 0x07030000
        [7]     CLEAR_CHIPLET_IS_ALIGNED =  1

      // Unset Clear chiplet is aligned
      // No delay, but Hostboot does a second read, doesn't reuse previously written value
      TP.TCMC01.MCSLOW.SYNC_CONFIG                          // 0x07030000
        [7]     CLEAR_CHIPLET_IS_ALIGNED =  0

      delay(100us)

      // Line below copied from Hostboot, but it mentions wrong bit
      // Poll OPCG done bit to check for run-N completeness
      timeout(10*100us):
        TP.TCMC01.MCSLOW.CPLT_STAT0                         // 0x07000100
        if (([9] CC_CTRL_CHIPLET_IS_ALIGNED_DC) == 1) break
        delay(100us)

      // Disable alignment
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_CLEAR)                // 0x07000020
        [all]   0
        [3]     CTRL_CC_FORCE_ALIGN_DC =  1

    // Call module clock start stop for MC01, MC23
    p9_sbe_common_clock_start_stop(l_trgt_chplt, CLOCK_CMD = 1,
                                    DONT_STARTSLAVE = 0, DONT_STARTMASTER = 0, l_clock_regions,
                                    CLOCK_TYPES = 0x7):
      // Chiplet exit flush
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_OR)                   // 0x07000010
        [all]   0
        [2]     CTRL_CC_FLUSHMODE_INH_DC =  1

      // Clear Scan region type register
      TP.TCMC01.MCSLOW.SCAN_REGION_TYPE                     // 0x07030005
        [all]   0

      // Setup all Clock Domains and Clock Types
      TP.TCMC01.MCSLOW.CLK_REGION                           // 0x07030006
        [0-1]   CLOCK_CMD =       1     // start
        [2]     SLAVE_MODE =      0
        [3]     MASTER_MODE =     0
        [4-14]  CLOCK_REGION_* =  ATTR_PG[4-14]
        [48]    SEL_THOLD_SL =    1
        [49]    SEL_THOLD_NSL =   1
        [50]    SEL_THOLD_ARY =   1

      // Poll OPCG done bit to check for completeness
      timeout(10*100us):
        TP.TCMC01.MCSLOW.CPLT_STAT0                         // 0x07000100
        if (([8] CC_CTRL_OPCG_DONE_DC) == 1) break
        delay(100us)

      // Here Hostboot calculates what is expected clock status, based on previous values and requested command. It is done
      // by generic functions, but because we know exactly which clocks were to be started, we can test just for those.
      TP.TCMC01.MCSLOW.CLOCK_STAT_SL                        // 0x07030008
      TP.TCMC01.MCSLOW.CLOCK_STAT_NSL                       // 0x07030009
      TP.TCMC01.MCSLOW.CLOCK_STAT_ARY                       // 0x0703000A
        assert(([4-14] & ATTR_PG[4-14]) == ATTR_PG[4-14])

    // Call p9_mem_startclocks_fence_setup_function for Mc chiplets
    p9_mem_startclocks_fence_setup_function():
      // Hostboot does it based on pg_vector. I have no idea what exactly pg_vector represents. I also don't know if it is
      // possible to have a functional MCBIST for which we don't want to drop the fence (functional MCBIST with nonfunctional
      // PERV?). In any case, further code tries to configure all functional MCBISTs, so perhaps it is better to drop more
      // fences than necessary than to forget about one.
      > if ((MC.ATTR_CHIP_UNIT_POS == 0x07 && pg_vector[5]) ||
      >     (MC.ATTR_CHIP_UNIT_POS == 0x08 && pg_vector[3]))
      > {
        // Drop chiplet fence
        TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WAND)            // 0x070F0041
          [all] 1
          [18]  FENCE_EN =  0
      > }

    // Call p9_mem_startclocks_flushmode for Mc chiplets
    p9_mem_startclocks_flushmode():
      // Clear flush_inhibit to go in to flush mode
      TP.TCMC01.MCSLOW.CPLT_CTRL0 (WO_CLEAR)                // 0x07000020
        [all]   0
        [2]     CTRL_CC_FLUSHMODE_INH_DC =  1

    // Call p9_sbe_common_configure_chiplet_FIR for MC chiplets
    p9_sbe_common_configure_chiplet_FIR():
      // reset pervasive FIR
      TP.TCMC01.MCSLOW.LOCAL_FIR                            // 0x0704000A
        [all]   0

      // configure pervasive FIR action/mask
      TP.TCMC01.MCSLOW.LOCAL_FIR_ACTION0                    // 0x07040010
        [all]   0
      TP.TCMC01.MCSLOW.LOCAL_FIR_ACTION1                    // 0x07040011
        [all]   0
        [0-3]   0xF
      TP.TCMC01.MCSLOW.LOCAL_FIR_MASK                       // 0x0704000D
        // 0x0FFFFFFFFFC00000
        [all]   0
        [4-41]  0x3FFFFFFFFF

      // reset XFIR
      TP.TCMC01.MCSLOW.XFIR                                 // 0x07040000
        [all]   0

      // configure XFIR mask
      TP.TCMC01.MCSLOW.FIR_MASK                             // 0x07040002
        [all]   0

    // Reset FBC chiplet configuration
    TP.TCMC01.MCSLOW.CPLT_CONF0                             // 0x07000008
      [48-51] TC_UNIT_GROUP_ID_DC = ATTR_PROC_FABRIC_GROUP_ID     // Where do these come from?
      [52-54] TC_UNIT_CHIP_ID_DC =  ATTR_PROC_FABRIC_CHIP_ID
      [56-60] TC_UNIT_SYS_ID_DC =   ATTR_PROC_FABRIC_SYSTEM_ID

    // Add to Multicast Group
      // avoid setting if register is already set, i.e. != p9SbeChipletReset::MCGR_CNFG_SETTING_EMPTY
      TP.TPCHIP.NET.PCBSLMC01.MULTICAST_GROUP_1             // 0x070F0001
        [3-5]   MULTICAST1_GROUP: if 7 then set to 0
        [16-23] (not described):  if [3-5] == 7 then set to 0x1C    // No clue why Hostboot modifies these bits
      TP.TPCHIP.NET.PCBSLMC01.MULTICAST_GROUP_2             // 0x070F0002
        [3-5]   MULTICAST1_GROUP: if 7 then set to 2
        [16-23] (not described):  if [3-5] == 7 then set to 0x1C
```
