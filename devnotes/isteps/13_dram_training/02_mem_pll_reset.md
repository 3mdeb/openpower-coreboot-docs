## mem_pll_reset: Reset PLL for MCAs in async (13.2)

> a) p9_mem_pll_reset.C (proc chip)
>     - This step is a no-op on cumulus as the centaur is already has its PLLs setup in step 11
>     - This step is a no-op if memory is running in synchronous mode since the MCAs are using the nest PLL, HWP detect
>       and exits
>     - If in async mode then this HWP will put the PLL into bypass, reset mode
>     - Disable listen_to_sync for MEM chiplet, whenever MEM is not in sync to NEST

```
For each functional Proc:
> if ( !ATTR_MC_SYNC_MODE )
  For each functional MC(BIST?):
    // Assert endpoint reset
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WOR)                 // 0x070F0042
      [all] 0
      [1]   PCB_EP_RESET =  1

    // Mask PLL unlock error in PCB slave
    TP.TPCHIP.NET.PCBSLMC01.SLAVE_CONFIG_REG                // 0x070F001E
      [12]  (part of) ERROR_MASK =  1

    // Move MC PLL into reset state (3 separate writes, no delays between them)
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WOR)                 // 0x070F0042
      [all] 0
      [5]   PLL_BYPASS =  1
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WOR)                 // 0x070F0042
      [all] 0
      [4]   PLL_RESET =   1
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WOR)                 // 0x070F0042
      [all] 0
      [3]   PLL_TEST_EN = 1

    // Assert MEM PLDY and DCC bypass
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL1 (WOR)                 // 0x070F0046
      [all] 0
      [1]   CLK_DCC_BYPASS_EN =   1
      [2]   CLK_PDLY_BYPASS_EN =  1

    // Drop endpoint reset
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WAND)                // 0x070F0041
      [all] 1
      [1]   PCB_EP_RESET =  0

    // Disable listen to sync pulse to MC chiplet, when MEM is not in sync to nest
    TP.TCMC01.MCSLOW.SYNC_CONFIG                            // 0x07030000
      [4] LISTEN_TO_SYNC_PULSE_DIS = 1

    // Initialize OPCG_ALIGN register
    TP.TCMC01.MCSLOW.OPCG_ALIGN                             // 0x07030001
      [all] 0
      [0-3]   INOP_ALIGN =        5         // 8:1
      [12-19] INOP_WAIT =         0
      [47-51] SCAN_RATIO =        0         // 1:1
      [52-63] OPCG_WAIT_CYCLES =  0x20

    // scan0 flush PLL boundary ring
    TP.TCMC01.MCSLOW.CLK_REGION                             // 0x07030006
      [all]   0
      [14]    CLOCK_REGION_UNIT10 = 1
      [48]    SEL_THOLD_SL =        1
      [49]    SEL_THOLD_NSL =       1
      [50]    SEL_THOLD_ARY =       1
    TP.TCMC01.MCSLOW.SCAN_REGION_TYPE                       // 0x07030005
      [all]   0
      [14]    SCAN_REGION_UNIT10 =  1
      [56]    SCAN_TYPE_BNDY =      1
    TP.TCMC01.MCSLOW.OPCG_REG0                              // 0x07030002
      [0]     RUNN_MODE =           0
    // Separate write, but don't have to read again
    TP.TCMC01.MCSLOW.OPCG_REG0                              // 0x07030002
      [2]     RUN_SCAN0 =           1

    timeout(200 * 16us):
      TP.TCMC01.MCSLOW.CPLT_STAT0                           // 0x07000100
      if (([8] CC_CTRL_OPCG_DONE_DC) == 1) break
      delay(16us)

    // Cleanup
    TP.TCMC01.MCSLOW.CLK_REGION                             // 0x07030006
      [all]   0
    TP.TCMC01.MCSLOW.SCAN_REGION_TYPE                       // 0x07030005
      [all]   0
```
