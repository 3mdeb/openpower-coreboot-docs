## mss_draminit_mc: Hand off control to MC (13.13)

> a) p9_mss_draminit_mc.C  (mcbist) - Nimbus
> b) p9c_mss_draminit_mc.C  (membuf) -Cumulus
>    - P9 Cumulus -- Set IML complete bit in centaur
>    - Start main refresh engine
>    - Refresh, periodic calibration, power controls
>    - Turn on ECC checking on memory accesses
>    - Note at this point memory FIRs can be monitored by PRD

```
for each functional MCBIST
  if (count_dimms(MCBIST) == 0) continue

  for each functional MCA
    if (count_dimms(MCA) == 0) continue

    // Set up the MC port <-> DIMM address translation registers.
    //
    // These are not documented in specs, everything described here comes from the code (and comments). Depending on how
    // you count them, there are 2 or 3 base configurations, and the rest is a modification of one of the bases or its
    // derived forms. Each level usually adds one row bit, but sometimes it removes it or modifies rank bits. In most cases
    // when it happens, the rest of bits must be shifted.
    //
    // There are two pairs of identical settings for each master/slave rank configurations: 4Gb x4 is always the same as
    // 8Gb x8, and 8Gb x4 is the same as 16Gb x8.
    //
    // Base configurations are:
    // - 1 rank, non-3DS 4Gb x4 and second DIMM is also 1R (not necessarily 4Gb x4)
    //   - special case when the other DIMM is not an 1R device (or not present)
    // - 2 rank, non-3DS 4Gb x4
    // The special case uses different column, bank and bank group addressing, the other two cases use identical mapping.
    // This is due to the fact that for one 1R DIMM there is no port address bit with index 7, which is used as C9 in other
    // cases. Hostboot divides those cases as listed above, but it might make more sense to separate special case and use
    // uniform logic for the rest. However, for two 1R DIMMs port address 29 is always assigned to D-bit (more about it
    // later), because bit map fields for rows use only 3 bit for encoding, meaning that only port bits 0-7 can be mapped
    // to row bits 15-17.
    // TODO: does this imply that 2 1R DIMMs have to have the same size when installed on the same port? Otherwise we may
    // have stripes of holes for smaller DIMM.
    //
    // According to code, port addresses 0-7 and 22-32 can be configured in the register - 19 possibilities total, encoded,
    // so bit field in register is 5 bits long, except for row bitmaps, which are only 3 bits long (addresses 0-7 only).
    // Column, bank and bank group addressing is always the same (DDR4 always has 10 column, 2 bank, 2 bank group bits),
    // difference is in row bit mapping (we may or may not have bits 15, 16 and 17, those are indexed from 0 so we can have
    // 15-18 row bits in total) and ranks mapping (up to 2 bits for master ranks, up to 3 bits for slave ranks in 3DS
    // devices).
    // TODO: what about 16Gb bank groups? Those should use just 1 bit, but the code doesn't change it.
    //
    // Apart from already mentioned bits there is also D-bit (D is short for DIMM). It is used to tell the controller which
    // DIMM has to be accessed. To avoid holes in the memory map, larger DIMM must be mapped to lower addresses. For example,
    // when we have 4GB and 8GB DIMMs:
    //
    // 0       4       8      12      16 ...      memory space, in GB
    // |   DIMM X      |DIMM Y | hole  | ...             <- this is good
    // |DIMM Y | hole  |   DIMM X      | ...             <- this is bad
    //
    // Whether DIMM X is in DIMM0 or DIMM1 slot doesn't matter. The example is simplified - the addresses do not translate
    // directly to CPU address space. There are multiple MCAs in the system, they are grouped together later in 14.5, based
    // on the mappings calculated in 7.4.
    //
    // There are two pieces to configure for D-bit:
    // - D_BIT_MAP - which address bit is used to decide which DIMM is used (this would correspond to 8GB in the example
    //   above), this is common to both DIMMs,
    // - SLOTn_D_VALUE - what value should the D-bit have to access DIMMn, each DIMM has its own SLOTn_D_VALUE, when one
    //   DIMM has this bit set, the other one should have it cleared; in the (good) example above DIMM Y should have this
    //   bit set. When both DIMMs have the same size then only one D_VALUE must be set, but it doesn't matter which one.
    // TODO: what if only one DIMM is present? Do we have to set these to something sane (0 and 0 should work) or is it
    // enough that VALID bit is clear for the other DIMM?
    //
    // If bits are assigned in a proper order, we can use a constant table with mappings and assign values from that table
    // to registers describing address bits in a sparse manner, depending on a number of rank and row bits used by a given
    // DIMM. The order is based on the cost of changing individual bits on the DIMM side (considering data locality):
    // 1. Bank group, bank and column bits are sent with every read/write command. It takes RL = AL + CL + PL after the read
    //    command until first DQ bits appear on the bus. In practice we usually don't care about write delays, when data is
    //    sent to the controller the CPU can already execute further code, it doesn't have to wait until it is actually
    //    written to DRAM. This is a cheap change.
    //    TODO: this is for DRAM, any additional delay caused by RCD, PHY or MC?
    // 2. Ranks (both master and slave) are selected by CS (and Chip ID for slave ranks) bits, they are also sent with each
    //    command. Depending on MR4 settings, we may need to wait for additional tCAL (CS to Command Address Latency), DRAM
    //    needs some time to "wake up" before it can parse commands. If tCAL is not used (default in Hostboot), the cost is
    //    the same as for BG, BA, column bits. It doesn't matter whether master or slave ranks are assigned first, but
    //    Hostboot starts with slave ranks - it has 5 bits per bit map, so it can encode higher numbers.
    // 3. Row bits - these are expensive. Row must be activated before its column are accessed. Each bank can have one
    //    activated row at a time. If there was an open row (different than the one we want to access), it must be precharged
    //    (it takes tRP before next activation command can be issued), and then the new row can be activated (after which we
    //    have to wait for tRCD before sending the read/write command). A row cannot be opened indefinitely, there is both a
    //    minimal and maximal period between ACT and PRE commands (tRAS), and minimums for read to precharge (tRTP), ACT to
    //    ACT for different banks (tRRD, with differentiation between the same and different bank groups) and Four Activate
    //    Window (tFAW). When row changes don't happen too often, we usually have to wait for tRCD and sometimes also tRP,
    //    on top of the previous delays.
    // 4. D bit. Two DIMMs on a channel share all of its lines except CLK, CS, ODT and CKE bits. Because we don't have to
    //    change CS for a given DIMM, the cost is the same as 1 (assuming hardware holds CS between commands). However, this
    //    bit has to be assigned lastly (i.e. it has to be the most significant port address bit) to not introduce holes in
    //    the memory space for two differently sized DIMMs.
    //    // TODO: can we safely map it closer to LSB (at least before row bits) when we have two DIMMs with the same size?
    //
    // TODO: what about bad DQ bits? Do they impact this in any way? Probably not, until DIMM isn't disabled.
    //
    // Below are registers layouts reconstructed from import/chips/p9/common/include/p9n2_mc_scom_addresses_fld.H:
    0x05010820                      // P9N2_MCS_PORT02_MCP0XLT0, also PORT13 on +0x10 SCOM addresses
        [0]     SLOT0_VALID             // set if DIMM present
        [1]     SLOT0_D_VALUE           // set if both DIMMs present and size of DIMM1 > DIMM0
        [2]     12GB_ENABLE     // unused (maybe for 12Gb/24Gb DRAM?)
        [5]     SLOT0_M0_VALID
        [6]     SLOT0_M1_VALID
        [9]     SLOT0_S0_VALID
        [10]    SLOT0_S1_VALID
        [11]    SLOT0_S2_VALID
        [12]    SLOT0_B2_VALID          // Hmmm...
        [13]    SLOT0_ROW15_VALID
        [14]    SLOT0_ROW16_VALID
        [15]    SLOT0_ROW17_VALID
        [16]    SLOT1_VALID             // set if DIMM present
        [17]    SLOT1_D_VALUE           // set if both DIMMs present and size of DIMM1 <= DIMM0
        [21]    SLOT1_M0_VALID
        [22]    SLOT1_M1_VALID
        [25]    SLOT1_S0_VALID
        [26]    SLOT1_S1_VALID
        [27]    SLOT1_S2_VALID
        [28]    SLOT1_B2_VALID          // Hmmm...
        [29]    SLOT1_ROW15_VALID
        [30]    SLOT1_ROW16_VALID
        [31]    SLOT1_ROW17_VALID
        [35-39] D_BIT_MAP
        [41-43] M0_BIT_MAP              // 3b for M0 but 5b for M1
        [47-51] M1_BIT_MAP
        [53-55] R17_BIT_MAP
        [57-59] R16_BIT_MAP
        [61-63] R15_BIT_MAP

    0x05010821                      // P9N2_MCS_PORT02_MCP0XLT1
        [3-7]   S0_BIT_MAP
        [11-15] S1_BIT_MAP
        [19-23] S2_BIT_MAP
        [35-39] COL4_BIT_MAP
        [43-47] COL5_BIT_MAP
        [51-55] COL6_BIT_MAP
        [59-63] COL7_BIT_MAP

    0x05010822                      // P9N2_MCS_PORT02_MCP0XLT2
        [3-7]   COL8_BIT_MAP
        [11-15] COL9_BIT_MAP
        [19-23] BANK0_BIT_MAP
        [27-31] BANK1_BIT_MAP
        [35-39] BANK2_BIT_MAP           // Hmmm...
        [43-47] BANK_GROUP0_BIT_MAP
        [51-55] BANK_GROUP1_BIT_MAP

    // All *_BIT_MAP fields above are encoded. Note that some of them are 3b long, those can map only PA 0 through 7.
    // When code unmaps any bit, the bit map field is also cleared to 0, in addition to SLOTn_*_VALID bit. This probably
    // isn't necessary, but without documentation it is hard to tell.
    //
    // PA   | field
    // 0    | 0b00000   0x00
    // 1    | 0b00001   0x01
    // 2    | 0b00010   0x02
    // ...
    // 7    | 0b00111   0x07
    // 22   | 0b01000   0x08      // defined but not actually used by Hostboot
    // 23   | 0b01001   0x09
    // ...
    // 32   | 0b10010   0x12
    //
    // Mappings used by Hostboot can be found in `RAM_addressing.ods` in this repo. I have made some comments about settings
    // that look suspicious to me, but I'm not an expert.
    //
    // Registers listed above should be set according to the spreadsheet, I will not repeat these settings here.

    setup_read_pointer_delay():
      MC01.PORT0.ECC64.SCOM.RECR                    // 0x07010A0A
          [6-8] MBSECCQ_READ_POINTER_DELAY = 1    // code sets this to "ON", but this field is numerical value
          // Not sure where this attr comes from or what is its default value. Assume !0 = 1 -> TCE correction enabled
          [27]  MBSECCQ_ENABLE_TCE_CORRECTION = !ATTR_MNFG_FLAGS.MNFG_REPAIRS_DISABLED_ATTR

    // Enable Power management based off of mrw_power_control_requested
    // > Before enabling power controls, run the parity disable workaround
    // TODO: this is a loop over MCAs inside a loop over MCAs. Is this really necessary?
    for each functional MCA
      // > The workaround is needed iff
      // > 1) greater than or equal to DD2
      // > 2) self time refresh is enabled
      // > 3) the DIMM's are not TSV                // TSV == 3DS?
      // > 4) a 4R DIMM is present
      // FIXME: skip for now, we do not have any 4R, non-3DS sticks to test it anyway
      str_non_tsv_parity()

    MC01.PORT0.SRQ.PC.MBARPC0Q                      // 0x07010934
      if ATTR_MSS_MRW_POWER_CONTROL_REQUESTED ==            // default 0 == off
          ENUM_ATTR_MSS_MRW_IDLE_POWER_CONTROL_REQUESTED_POWER_DOWN         ||        // 1
          ENUM_ATTR_MSS_MRW_IDLE_POWER_CONTROL_REQUESTED_PD_AND_STR_CLK     ||        // 2
          ENUM_ATTR_MSS_MRW_IDLE_POWER_CONTROL_REQUESTED_PD_AND_STR_CLK_STOP:         // 3
        [2] MBARPC0Q_CFG_MIN_MAX_DOMAINS_ENABLE = ATTR_MSS_MRW_POWER_CONTROL_REQUESTED

    // This was already done after draminit_cke_helper, search for "Per conversation with Shelton and Steve..." in 13.10,
    // > however that might be a work-around so we set it low here kind of like belt-and-suspenders. BRS
    change_addr_mux_sel()
      MC01.PORT0.SRQ.MBA_FARB5Q                     // 0x07010918
          [5]   MBA_FARB5Q_CFG_CCS_ADDR_MUX_SEL = 0

    change_port_fail_disable()
      MC01.PORT0.SRQ.MBA_FARB0Q                     // 0x07010913
          [57]  MBA_FARB0Q_CFG_PORT_FAIL_DISABLE = 0

    // > MC work around for OE bug (seen in periodics + PHY)
    // > Turn on output-enable always on. Shelton tells me they'll fix for DD2
    // This is also surrounded by #ifndef REMOVE_FOR_DD2, but this name is nowhere else to be found. If this still have to
    // be used, we may as well merge it with the previous write.
    change_oe_always_on()
      MC01.PORT0.SRQ.MBA_FARB0Q                     // 0x07010913
          [55]  MBA_FARB0Q_CFG_OE_ALWAYS_ON = 1

    change_refresh_enable()
      MC01.PORT0.SRQ.PC.MBAREF0Q                    // 0x07010932
          [0] MBAREF0Q_CFG_REFRESH_ENABLE = 1

    enable_periodic_cal()
      // A large chunk of this function is disabled, protected by #ifdef TODO_166433_PERIODICS, which also isn't mentioned
      // anywhere else. This is what is left:
      MC01.PORT0.SRQ.MBA_CAL3Q                      // 0x07010912
          [all]   0
          [0-1]   MBA_CAL3Q_CFG_INTERNAL_ZQ_TB =        0x3
          [2-9]   MBA_CAL3Q_CFG_INTERNAL_ZQ_LENGTH =    0xff
          [10-11] MBA_CAL3Q_CFG_EXTERNAL_ZQ_TB =        0x3
          [12-19] MBA_CAL3Q_CFG_EXTERNAL_ZQ_LENGTH =    0xff
          [20-21] MBA_CAL3Q_CFG_RDCLK_SYSCLK_TB =       0x3
          [22-29] MBA_CAL3Q_CFG_RDCLK_SYSCLK_LENGTH =   0xff
          [30-31] MBA_CAL3Q_CFG_DQS_ALIGNMENT_TB =      0x3
          [32-39] MBA_CAL3Q_CFG_DQS_ALIGNMENT_LENGTH =  0xff
          [40-41] MBA_CAL3Q_CFG_MPR_READEYE_TB =        0x3
          [42-49] MBA_CAL3Q_CFG_MPR_READEYE_LENGTH =    0xff
          [50-51] MBA_CAL3Q_CFG_ALL_PERIODIC_TB =       0x3
          [52-59] MBA_CAL3Q_CFG_ALL_PERIODIC_LENGTH =   0xff
          // Or simpler: 0xfffffffffffffff0

    enable_read_ecc()
      MC01.PORT0.ECC64.SCOM.RECR                    // 0x07010A0A
          [0]     MBSECCQ_DISABLE_MEMORY_ECC_CHECK_CORRECT =  0
          [1]     MBSECCQ_DISABLE_MEMORY_ECC_CORRECT =        0
          [29]    MBSECCQ_USE_ADDRESS_HASH =                  1
          // Docs don't describe the encoding, code suggests this inverts data, toggles checks
          [30-31] MBSECCQ_DATA_INVERSION =                    3

    apply_mark_store()
      MC01.PORT0.ECC64.SCOM.FWMS{0-7}               // 0x07010A18 - 0x07010A1F
          [all]   0
          [0-22]  from ATTR_MSS_MVPD_FWMS         // FIXME: where do the values written to MVPD come from?

  unmask::after_draminit_mc()
    // > All mcbist attentions are already special attentions
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT0             // 0x07012306
          [10]  MCBISTFIRQ_MCBIST_PROGRAM_COMPLETE =  1
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT1             // 0x07012307
          [10]  MCBISTFIRQ_COMMAND_ADDRESS_TIMEOUT =  0
    MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRMASK             // 0x07012303
          [10]  MCBISTFIRQ_COMMAND_ADDRESS_TIMEOUT =  0     // attention (1,0,0)

    workaround::mcbist::broadcast_out_of_sync()
      // Maybe merge with previous RMWs?
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT0             // 0x07012306
          [3]   MCBISTFIRQ_MCBIST_BRODCAST_OUT_OF_SYNC =  0
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRACT1             // 0x07012307
          [3]   MCBISTFIRQ_MCBIST_BRODCAST_OUT_OF_SYNC =  1
      MC01.MCBIST.MBA_SCOMFIR.MCBISTFIRMASK             // 0x07012303
          [3]   MCBISTFIRQ_MCBIST_BRODCAST_OUT_OF_SYNC =  0   // recoverable_error (0,1,0)

      // This loop may as well be merged with the next one, and possibly even with previous writes to this register.
      for each functional MCA
        MC01.PORT0.ECC64.SCOM.RECR                    // 0x07010A0A
            [26]  MBSECCQ_ENABLE_UE_NOISE_WINDOW =  0

    for each functional MCA
      MC01.PORT1.ECC64.SCOM.ACTION0              // 0x07010A46
          [33]  FIR_MAINTENANCE_AUE =                     0
          [36]  FIR_MAINTENANCE_IAUE =                    0
          [41]  FIR_SCOM_PARITY_CLASS_STATUS =            0
          [42]  FIR_SCOM_PARITY_CLASS_RECOVERABLE =       0
          [43]  FIR_SCOM_PARITY_CLASS_UNRECOVERABLE =     0
          [44]  FIR_ECC_CORRECTOR_INTERNAL_PARITY_ERROR = 0
          [45]  FIR_WRITE_RMW_CE =                        0
          [46]  FIR_WRITE_RMW_UE =                        0
          [48]  FIR_WDF_OVERRUN_ERROR_0 =                 0
          [49]  FIR_WDF_OVERRUN_ERROR_1 =                 0
          [50]  FIR_WDF_SCOM_SEQUENCE_ERROR =             0
          [51]  FIR_WDF_STATE_MACHINE_ERROR =             0
          [52]  FIR_WDF_MISC_REGISTER_PARITY_ERROR =      0
          [53]  FIR_WRT_SCOM_SEQUENCE_ERROR =             0
          [54]  FIR_WRT_MISC_REGISTER_PARITY_ERROR =      0
          [55]  FIR_ECC_GENERATOR_INTERNAL_PARITY_ERROR = 0
          [56]  FIR_READ_BUFFER_OVERFLOW_ERROR =          0
          [57]  FIR_WDF_ASYNC_INTERFACE_ERROR =           0
          [58]  FIR_READ_ASYNC_INTERFACE_PARITY_ERROR =   0
          [59]  FIR_READ_ASYNC_INTERFACE_SEQUENCE_ERROR = 0
      MC01.PORT1.ECC64.SCOM.ACTION1              // 0x07010A47
          [33]  FIR_MAINTENANCE_AUE =                     1
          [36]  FIR_MAINTENANCE_IAUE =                    1
          [41]  FIR_SCOM_PARITY_CLASS_STATUS =            1
          [42]  FIR_SCOM_PARITY_CLASS_RECOVERABLE =       1
          [43]  FIR_SCOM_PARITY_CLASS_UNRECOVERABLE =     0
          [44]  FIR_ECC_CORRECTOR_INTERNAL_PARITY_ERROR = 0
          [45]  FIR_WRITE_RMW_CE =                        1
          [46]  FIR_WRITE_RMW_UE =                        0
          [48]  FIR_WDF_OVERRUN_ERROR_0 =                 0
          [49]  FIR_WDF_OVERRUN_ERROR_1 =                 0
          [50]  FIR_WDF_SCOM_SEQUENCE_ERROR =             0
          [51]  FIR_WDF_STATE_MACHINE_ERROR =             0
          [52]  FIR_WDF_MISC_REGISTER_PARITY_ERROR =      0
          [53]  FIR_WRT_SCOM_SEQUENCE_ERROR =             0
          [54]  FIR_WRT_MISC_REGISTER_PARITY_ERROR =      0
          [55]  FIR_ECC_GENERATOR_INTERNAL_PARITY_ERROR = 0
          [56]  FIR_READ_BUFFER_OVERFLOW_ERROR =          0
          [57]  FIR_WDF_ASYNC_INTERFACE_ERROR =           0
          [58]  FIR_READ_ASYNC_INTERFACE_PARITY_ERROR =   0
          [59]  FIR_READ_ASYNC_INTERFACE_SEQUENCE_ERROR = 0
      MC01.PORT1.ECC64.SCOM.MASK                 // 0x07010A43
          [33]  FIR_MAINTENANCE_AUE =                     0   // recoverable_error (0,1,0)
          [36]  FIR_MAINTENANCE_IAUE =                    0   // recoverable_error (0,1,0)
          [41]  FIR_SCOM_PARITY_CLASS_STATUS =            0   // recoverable_error (0,1,0)
          [42]  FIR_SCOM_PARITY_CLASS_RECOVERABLE =       0   // recoverable_error (0,1,0)
          [43]  FIR_SCOM_PARITY_CLASS_UNRECOVERABLE =     0   // checkstop (0,0,0)
          [44]  FIR_ECC_CORRECTOR_INTERNAL_PARITY_ERROR = 0   // checkstop (0,0,0)
          [45]  FIR_WRITE_RMW_CE =                        0   // recoverable_error (0,1,0)
          [46]  FIR_WRITE_RMW_UE =                        0   // checkstop (0,0,0)
          [48]  FIR_WDF_OVERRUN_ERROR_0 =                 0   // checkstop (0,0,0)
          [49]  FIR_WDF_OVERRUN_ERROR_1 =                 0   // checkstop (0,0,0)
          [50]  FIR_WDF_SCOM_SEQUENCE_ERROR =             0   // checkstop (0,0,0)
          [51]  FIR_WDF_STATE_MACHINE_ERROR =             0   // checkstop (0,0,0)
          [52]  FIR_WDF_MISC_REGISTER_PARITY_ERROR =      0   // checkstop (0,0,0)
          [53]  FIR_WRT_SCOM_SEQUENCE_ERROR =             0   // checkstop (0,0,0)
          [54]  FIR_WRT_MISC_REGISTER_PARITY_ERROR =      0   // checkstop (0,0,0)
          [55]  FIR_ECC_GENERATOR_INTERNAL_PARITY_ERROR = 0   // checkstop (0,0,0)
          [56]  FIR_READ_BUFFER_OVERFLOW_ERROR =          0   // checkstop (0,0,0)
          [57]  FIR_WDF_ASYNC_INTERFACE_ERROR =           0   // checkstop (0,0,0)
          [58]  FIR_READ_ASYNC_INTERFACE_PARITY_ERROR =   0   // checkstop (0,0,0)
          [59]  FIR_READ_ASYNC_INTERFACE_SEQUENCE_ERROR = 0   // checkstop (0,0,0)
```
