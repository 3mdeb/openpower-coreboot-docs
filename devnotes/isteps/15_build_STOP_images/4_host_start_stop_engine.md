NOTE: 15.3 and 15.4 are swapped in [IPL flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)

## host_start_stop_engine: Initialize the STOPGPE engine (15.4)

> a) p9_pm_stopgpe_init(chip_target, ENUM:PM_INIT)
>    - Parameters: PM_INIT (to perform initialization vs PM_RESET that is used
>      during the OCC reset flow)
>    - Starts the Stop GPE engine
>      - Bootloader runs from HOMER OCC offset + 1MB (2MB from HOMER base)
>        - Copies STOP image from HOMER to OCC SRAM
>        - Restarts from OCC SRAM
>      - PK initialization -> STOP Thread(s) started
>      - Sets flag in OCC Flag reg that initialization is complete for HWP to
>        poll on
>    - Loop over all functional cache chiplets
>      - p9_pfet_init.C (cache target, PM_INIT) (called as a subroutine)
>        - Initialize PFET controller parameters (delays)
>        - Note: this the default of the PFETs is OFF and this action will have
>          them remain off.
>    - Loop over all functional core chiplets
>      - p9_pfet_init.C (core target, PM_INIT) (called as a subroutine)
>        - Initialize PFET controller parameters (delays)
>        - Note: this the default of the PFETs is OFF and this action will have
>          them remain off.
>      - NOTE: CME initialization is performed upon STOP exit of the cache
>        chiplet by the STOPGPE so as to allow the wake up of any core within a
>        Quad. This is NOT done via HWPs.

Assumptions made for analysis:
- SMT4 mode (aka not fused)
- flow modes other than PM_INIT were skipped
- ATTR_SYSTEM_CORE_PERIODIC_QUIESCE_DISABLE is set -> Core Periodic Quiesce Hang
  Buster function is disabled
- ATTR_FREQ_PB_MHZ = 1866 MHz

```
for each functional proc:
  // Initialization:  perform order or dynamic operations to initialize
  // the STOP funciton using necessary Platform or Feature attributes.

  // periodic core quiesce workaround settings
  for each functional core:
    TP.TPCHIP.NET.PCBSLEC14.PPMC.PPM_CORE_REGS.CPPM_CPMMR (WOR)     // 0x200F0108
      [all] 0
      [2]   CPPM_CPMMR_RESERVED_2 = 1

  // Initialize the PFET controllers
  pfet_init():
    for each functional core:
      TP.TPCHIP.NET.PCBSLEC14.PPMC.PPM_COMMON_REGS.PPM_PFDLY        // 0x200F011B
        [all] 0
        [0-3] PPM_PFDLY_POWDN_DLY = 0x9     // 250ns, converted and encoded
        [4-7] PPM_PFDLY_POWUP_DLY = 0x9
      TP.TPCHIP.NET.PCBSLEC14.PPMC.PPM_COMMON_REGS.PPM_PFOF         // 0x200F011D
        [all] 0
        [0-3] PPM_PFOFF_VDD_VOFF_SEL =  0x8
        [4-7] PPM_PFOFF_VCS_VOFF_SEL =  0x8
    for each functional cache:
      TP.TPCHIP.NET.PCBSLEP03.PPMQ.PPM_COMMON_REGS.PPM_PFDLY        // 0x100F011B
        [all] 0
        [0-3] PPM_PFDLY_POWDN_DLY = 0x9     // 250ns, converted and encoded
        [4-7] PPM_PFDLY_POWUP_DLY = 0x9
      TP.TPCHIP.NET.PCBSLEP03.PPMQ.PPM_COMMON_REGS.PPM_PFOF         // 0x100F011D
        [all] 0
        [0-3] PPM_PFOFF_VDD_VOFF_SEL =  0x8
        [4-7] PPM_PFOFF_VCS_VOFF_SEL =  0x8

  // Condition the PBA back to the base boot configuration
  pba_reset():
    pba_bc_stop():
      // Stopping Block Copy Download Engine
      *0x00068010                   // undocumented, PU_BCDE_CTL_SCOM
        [all] 0
        [0]   1

      // Stopping Block Copy Upload Engine
      *0x00068015                   // undocumented, PU_BCUE_CTL_SCOM
        [all] 0
        [0]   1

      // Polling on, to verify that BCDE & BCUE are indeed stopped.
      timeout(256*256us):
        *0x00068012                   // undocumented, PU_BCDE_STAT_SCOM
          [0] PBA_BC_STAT_RUNNING?
        *0x00068017                   // undocumented, PU_BCUE_STAT_SCOM
          [0] PBA_BC_STAT_RUNNING?
        if both bits are clear: break
        delay(256us)

      // Clear the BCDE and BCUE stop bits
      *0x00068010                     // undocumented, PU_BCDE_CTL_SCOM
        [all] 0
      *0x00068015                     // undocumented, PU_BCUE_CTL_SCOM
        [all] 0

    pba_slave_reset():
      for sl in {0, 1, 2}:        // slave 3 is owned by SBE, do not reset it
        timeout(16*1us):
          // This write is inside the timeout loop. I don't know if this will cause slaves to reset
          // on each iteration or not, but this is how it is done in hostboot.
          *0x00068001                 // undocumented, PU_PBASLVRST_SCOM
            [all] 0
            [0]   1     // reset?
            [1-2] sl
          if *0x00068001[4 + sl] == 0: break      // 4 + sl: reset in progress?
          delay(1us)

        if *0x00068001[8 + sl]: die()             // 8 + sl: busy?

    // Reset PBA regs
    *0x00068013                       // undocumented, PU_BCDE_PBADR_SCOM
    *0x00068014                       // undocumented, PU_BCDE_OCIBAR_SCOM
    *0x00068015                       // undocumented, PU_BCUE_CTL_SCOM
    *0x00068016                       // undocumented, PU_BCUE_SET_SCOM
    *0x00068018                       // undocumented, PU_BCUE_PBADR_SCOM
    *0x00068019                       // undocumented, PU_BCUE_OCIBAR_SCOM
    *0x00068026                       // undocumented, PU_PBAXSHBR0_SCOM
    *0x0006802A                       // undocumented, PU_PBAXSHBR1_SCOM
    *0x00068027                       // undocumented, PU_PBAXSHCS0_SCOM
    *0x0006802B                       // undocumented, PU_PBAXSHCS1_SCOM
    *0x00068004                       // undocumented, PU_PBASLVCTL0_SCOM
    *0x00068005                       // undocumented, PU_PBASLVCTL1_SCOM
    *0x00068006                       // undocumented, PU_PBASLVCTL2_SCOM
    BRIDGE.PBA.PBAFIR                 // 0x05012840
    BRIDGE.PBA.PBAERRRPT0             // 0x0501284C
      [all] 0

    // Perform non-zero reset operations
    BRIDGE.PBA.PBACFG                 // 0x0501284B
      [all] 0
      [38]  PBACFG_CHSW_DIS_GROUP_SCOPE = 1

    *0x00068021                       // undocumented, PU_PBAXCFG_SCOM
      [all] 0
      [2]   1   // PBAXCFG_SND_RESET?
      [3]   1   // PBAXCFG_RCV_RESET?

    pba_slave_setup_boot_phase():
      // Layout of these registers can be decoded from hostboot, but the values
      // are always the same, so why bother...
      // Set the PBA_MODECTL register
      *0x00068000 = 0x00A0BA9000000000
      // Slave 0 (SGPE and OCC boot)
      *0x00068004 = 0xB7005E0000000000
      // Slave 1 (405 ICU/DCU)
      *0x00068005 = 0xD5005E4000000000
      // Slave 2 (PGPE Boot)
      *0x00068006 = 0xA7005E4000000000

  if (ATTR_VDM_ENABLED || ATTR_IVRM_ENABLED):       // set (or not) in 15.1 - p9_pstate_parameter_block()
    TP.TPCHIP.TPC.ITR.FMU.KVREF_AND_VMEAS_MODE_STATUS_REG     // 0x01020007
      if ([16] == 0): die()     // VDMs/IVRM are enabled but necessary VREF calibration failed

  //First mask bit 7 in OIMR and then clear bit 7 in OISR
  TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OIMR0  (OR)               // 0x0006C006
    [all] 0
    [7]   OCB_OCI_OISR0_GPE2_ERROR =  1
  TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OISR0  (CLEAR)            // 0x0006C001
    [all] 0
    [7]   OCB_OCI_OISR0_GPE2_ERROR =  1

  // Setup the SGPE Timer Selects
  // These hardcoded values are assumed by the SGPE Hcode for setting up
  // the FIT and Watchdog values.
  TP.TPCHIP.OCC.OCI.GPE3.GPETSEL                          // 0x00066000
    [all] 0
    [0-3] GPETSEL_FIT_SEL =       0x1     // FIT - fixed interval timer
    [4-7] GPETSEL_WATCHDOG_SEL =  0xA

  // Clear error injection bits
  *0x0006C18B                         // undocumented, PU_OCB_OCI_OCCFLG2_CLEAR
    [all] 0
    [30]  1       // OCCFLG2_SGPE_HCODE_STOP_REQ_ERR_INJ

  // Boot the STOP GPE
  stop_gpe_init():
    // Debug and FFDC code was skipped
    // First check if SGPE_ACTIVE is not set in OCCFLAG register
    if (TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OCCFLG[8] == 1):        // 0x0006C08A
      // Print a warning maybe?
      TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OCCFLG (CLEAR)            // 0x0006C08B
        [all] 0
        [8]   1       // SGPE_ACTIVE, bits in this register are defined by OCC firmware

    // Program SGPE IVPR
    // ATTR_STOPGPE_BOOT_COPIER_IVPR_OFFSET is set in updateGpeAttributes() in 15.1
    // in hostboot, it is (homer->qpmr.sgpe.header.l1_offset | (0x80100000))
    TP.TPCHIP.OCC.OCI.GPE3.GPEIVPR                            // 0x00066001
      [all]   0
      [0-31]  GPEIVPR_IVPR = ATTR_STOPGPE_BOOT_COPIER_IVPR_OFFSET
              // Only bits [0-22] are actually defined, meaning IVPR must be aligned to 512B

    // Program XCR to ACTIVATE SGPE
    TP.TPCHIP.OCC.OCI.GPE3.GPENXIXCR                          // 0x00066010
      [all] 0
      [1-3] PPE_XIXCR_XCR = 6     // hard reset
    TP.TPCHIP.OCC.OCI.GPE3.GPENXIXCR                          // 0x00066010
      [all] 0
      [1-3] PPE_XIXCR_XCR = 4     // toggle XSR[TRH]
    TP.TPCHIP.OCC.OCI.GPE3.GPENXIXCR                          // 0x00066010
      [all] 0
      [1-3] PPE_XIXCR_XCR = 2     // resume

    // Now wait for SGPE to not be halted and for the HCode to indicate to be active.
    // Warning: consts names in hostboot say timeouts are in ms, but code treats it as us.
    // With debug output it takes much more than 20us between reads (~150us) and passes
    // on 5th pass, which gives ~600us, +/- 150us on 4-core CPU (4 active CMEs).
    timeout(125*20us):
      if ((TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OCCFLG[8] == 1) &&     // 0x0006C08A
          (TP.TPCHIP.OCC.OCI.GPE3.GPEXIXSR[0] == 0)): break     // 0x00066021
      delay(20us)
```
