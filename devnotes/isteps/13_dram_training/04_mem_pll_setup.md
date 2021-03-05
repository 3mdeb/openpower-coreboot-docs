## mem_pll_setup: Setup PLL for MBAs (13.4)

> a) p9_mem_pll_setup.C (proc chip)
>     - This step is a no-op on cumulus
>     - This step is a no-op if memory is running in synchronous mode since the MCAs are using the nest PLL, HWP detect
>       and exits
>     - MCA PLL setup
>       - Moved PLL out of bypass (just DDR)
>     - Performs PLL checking

```
For each functional Proc:
> if ( !ATTR_MC_SYNC_MODE )
  For each functional MC(BIST?):
    // Drop PLDY bypass of Progdelay logic
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL1 (WAND)                  // 0x070F0045
      [all] 1
      [2]   CLK_PDLY_BYPASS_EN =  0

    // Drop DCC bypass of DCC logic
    TP.TPCHIP.NET.PCBSLMC01.NET_CTRL1 (WAND)                  // 0x070F0045
      [all] 1
      [1]   CLK_DCC_BYPASS_EN =   0

    // Attribute description: "Skip the locking sequence and check for lock of NEST/MEM/XBUS/OBUS/PCI PLLs".
    // Can't find where it is set, assuming 0.
    > if (ATTR_NEST_MEM_X_O_PCI_BYPASS == 0)
      // Drop PLL test enable
      TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WAND)                  // 0x070F0041
        [all] 1
        [3]   PLL_TEST_EN = 0

      // Drop PLL reset
      TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WAND)                  // 0x070F0041
        [all] 1
        [4]   PLL_RESET =   0

      delay(5ms)

      // Check PLL lock
      TP.TPCHIP.NET.PCBSLMC01.PLL_LOCK_REG                      // 0x070F0019
        assert([0] (reserved) == 1)

      // Drop PLL Bypass
      TP.TPCHIP.NET.PCBSLMC01.NET_CTRL0 (WAND)                  // 0x070F0041
        [all] 1
        [5]   PLL_BYPASS =  0

      // Set scan ratio to 4:1
      TP.TCMC01.MCSLOW.OPCG_ALIGN                               // 0x07030001
        [47-51] SCAN_RATIO =  3           // 4:1

    > end if

    // Reset PCB Slave error register
    TP.TPCHIP.NET.PCBSLMC01.ERROR_REG                           // 0x070F001F
      [all] 1                 // Write 1 to clear

    // Unmask PLL unlock error in PCB slave
    TP.TPCHIP.NET.PCBSLMC01.SLAVE_CONFIG_REG                    // 0x070F001E
      [12]  (part of) ERROR_MASK =  0
```
