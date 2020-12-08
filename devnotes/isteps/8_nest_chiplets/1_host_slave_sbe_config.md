# 8.1 host_slave_sbe_config

### src/usr/isteps/istep08/call_host_slave_sbe_config.C

```python
for l_cpu_target in l_cpuTargetList:
    setChipletGardsOnProc(l_cpu_target)
    if l_cpu_target is not l_pMasterProcTarget:
        p9_setup_sbe_config(l_cpu_target) # described bellow
        updateSbeBootSeeprom(l_cpu_target) # described bellow

```

### src/import/chips/p9/procedures/hwp/perv/p9_setup_sbe_config.C

registers used in this file are described best here `src/include/usr/initservice/mboxRegs.H`

```python
# SCRATCH_REGISTER_X is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.SCRATCH_REGISTER_X
def p9_setup_sbe_config(i_target):
    # Registers are accessed by SCOM or FSI depending on whether the code is being executed by the MASTER_CHIP
    temp32_REGISTER_8 = SCRATCH_REGISTER_8 >> 32
############################### REGISTER_1 ####################################
   # temp32_REGISTER_1 = SCRATCH_REGISTER_1[63-32]
    temp32_REGISTER_1 = SCRATCH_REGISTER_1 >> 32

    # ATTR_EQ_GARD and ATTR_EC_GARD are read from FAPI and written to correct
    # places in SCRATCH_REGISTER_1

    # SCRATCH_REGISTER_1.insertFromRight< STARTBIT = 0, LENGTH = 6 >
    #     (fapi2::i_target_chip.ATTR_EQ_GARD)
    # SCRATCH_REGISTER_1.insertFromRight< STARTBIT = 8, LENGTH = 24>
    #     (fapi2::i_target_chip.ATTR_EC_GARD)

    # temp32_REGISTER_1[31-27] = fapi2::i_target_chip.ATTR_EQ_GARD[5-0]
    temp32_REGISTER_1 = (temp32_REGISTER_1 & ~(0x3fULL << (sizeof(temp32_REGISTER_1) * 8 - 0 - 6)))
        | ((fapi2::i_target_chip.ATTR_EQ_GARD & 0x3fULL) << (sizeof(temp32_REGISTER_1) * 8 - 0 - 6))
    # temp32_REGISTER_1[23-0] = fapi2::i_target_chip.ATTR_EC_GARD[23-0]
    temp32_REGISTER_1 = (temp32_REGISTER_1 & ~(0xffffffULL << (sizeof(temp32_REGISTER_1) * 8 - 8 - 24)))
        | ((fapi2::i_target_chip.ATTR_EC_GARD & 0xffffffULL) << (sizeof(temp32_REGISTER_1) * 8 - 8 - 24))

    #SCRATCH_REGISTER_1[63-32] = temp32_REGISTER_1
    SCRATCH_REGISTER_1 = (SCRATCH_REGISTER_1 & ~(0xffffffffULL << 32)) |
        (temp32_REGISTER_1 << 32)
    # SCRATCH_REGISTER_8.setBit<0>()
    # temp32_REGISTER_8[31] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1))
############################### REGISTER_2 ####################################
    # temp32_REGISTER_2 = SCRATCH_REGISTER_2[63-32]
    temp32_REGISTER_2 = SCRATCH_REGISTER_2 >> 32
    # ATTR_I2C_BUS_DIV_REF is read from FAPI and written to correct place in SCRATCH_REGISTER_2

    # SCRATCH_REGISTER_2.insertFromRight< STARTBIT = 0, LENGTH = 16>
    #     (fapi2::i_target_chip.ATTR_I2C_BUS_DIV_REF)
    # temp32_REGISTER_2[31-15] = fapi2::i_target_chip.ATTR_I2C_BUS_DIV_REF[15-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0xffffULL << (sizeof(temp32_REGISTER_2) * 8 - 0 - 16)))
        | ((fapi2::i_target_chip.ATTR_I2C_BUS_DIV_REF & 0xffffULL) << (sizeof(temp32_REGISTER_2) * 8 - 0 - 16))

    #@@ I'm not sure if any of this chips are used by Talos2 @@
    for l_chiplet in i_target_chip.getChildren:
        l_optics_cfg_mode = l_chiplet.ATTR_OPTICS_CONFIG_MODE
        bit_to_write = l_optics_cfg_mode == ENUM_ATTR_OPTICS_CONFIG_MODE_SMP or
            l_optics_cfg_mode == ENUM_ATTR_OPTICS_CONFIG_MODE_CAPI
        bit_to_write &= 1 # just to be sure that it is actually a bit.

        if l_chiplet.ID == OB0_CHIPLET_ID:
            # SCRATCH_REGISTER_2.writeBit<16>(bit_to_write)
            # temp32_REGISTER_2[15] = bit_to_write[0]
            temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(1ULL << (sizeof(temp32_REGISTER_2) * 8 - 1 - 16)))
                | bit_to_write << (sizeof(temp32_REGISTER_2) * 8 -1 - 16)
        if l_chiplet.ID == OB1_CHIPLET_ID:
            # SCRATCH_REGISTER_2.writeBit<17>(bit_to_write)
            # temp32_REGISTER_2[14] = bit_to_write[0]
            temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(1ULL << (sizeof(temp32_REGISTER_2) * 8 - 1 - 17)))
                | bit_to_write << (sizeof(temp32_REGISTER_2) * 8 -1 - 16)
        if l_chiplet.ID == OB2_CHIPLET_ID:
            # SCRATCH_REGISTER_2.writeBit<18>(bit_to_write)
            # temp32_REGISTER_2[13] = bit_to_write[0]
            temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(1ULL << (sizeof(temp32_REGISTER_2) * 8 - 1 - 18)))
                | bit_to_write << (sizeof(temp32_REGISTER_2) * 8 -1 - 18)
        if l_chiplet.ID == OB3_CHIPLET_ID:
            # SCRATCH_REGISTER_2.writeBit<19>(bit_to_write)
            # temp32_REGISTER_2[12] = bit_to_write[0]
            temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(1ULL << (sizeof(temp32_REGISTER_2) * 8 - 1 - 19)))
                | bit_to_write << (sizeof(temp32_REGISTER_2) * 8 -1 - 19)

    # Reading MC PLL buckets and writing them to SCRATCH_REGISTER_2. ATTR_MC_PLL_BUCKET is read from FAPI
    # SCRATCH_REGISTER_2.insertFromRight< STARTBIT = 21, LENGTH = 3>
    #     (fapi2::FAPI_SYSTEM.ATTR_MC_PLL_BUCKET)
    # temp32_REGISTER_2[10-8] = fapi2::FAPI_SYSTEM.ATTR_MC_PLL_BUCKET[2-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0x7ULL << (sizeof(temp32_REGISTER_2) * 8 - 21 - 3)))
        | ((fapi2::FAPI_SYSTEM.ATTR_MC_PLL_BUCKET & 0x7ULL) << (sizeof(temp32_REGISTER_2) * 8 - 21 - 3))

    # Reading OB PLL buckets and writing them to SCRATCH_REGISTER_2. ATTR_OBX_PLL_BUCKET are read from FAPI
    # SCRATCH_REGISTER_2.insertFromRight<STARTBIT = 24, LENGTH = 2>
    #     (fapi2::i_target_chip.ATTR_OB0_PLL_BUCKET)
    # SCRATCH_REGISTER_2.insertFromRight<STARTBIT = 26, LENGTH = 2>
    #     (fapi2::i_target_chip.ATTR_OB1_PLL_BUCKET)
    # SCRATCH_REGISTER_2.insertFromRight<STARTBIT = 28, LENGTH = 2>
    #     (fapi2::i_target_chip.ATTR_OB2_PLL_BUCKET)
    # SCRATCH_REGISTER_2.insertFromRight<STARTBIT = 30, LENGTH = 2>
    #     (fapi2::i_target_chip.ATTR_OB3_PLL_BUCKET)

    # temp32_REGISTER_2[7-6] = fapi2::i_target_chip.ATTR_OB0_PLL_BUCKET[1-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0x3ULL << (sizeof(temp32_REGISTER_2) * 8 - 24 - 2)))
        | ((fapi2::i_target_chip.ATTR_OB0_PLL_BUCKET & 0x3ULL) << (sizeof(temp32_REGISTER_2) * 8 - 24 - 2))
    # temp32_REGISTER_2[5-4] = fapi2::i_target_chip.ATTR_OB1_PLL_BUCKET[1-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0x3ULL << (sizeof(temp32_REGISTER_2) * 8 - 26 - 2)))
        | ((fapi2::i_target_chip.ATTR_OB1_PLL_BUCKET & 0x3ULL) << (sizeof(temp32_REGISTER_2) * 8 - 26 - 2))
    # temp32_REGISTER_2[3-2] = fapi2::i_target_chip.ATTR_OB2_PLL_BUCKET[1-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0x3ULL << (sizeof(temp32_REGISTER_2) * 8 - 28 - 2)))
        | ((fapi2::i_target_chip.ATTR_OB2_PLL_BUCKET & 0x3ULL) << (sizeof(temp32_REGISTER_2) * 8 - 28 - 2))
    # temp32_REGISTER_2[1-0] = fapi2::i_target_chip.ATTR_OB3_PLL_BUCKET[1-0]
    temp32_REGISTER_2 = (temp32_REGISTER_2 & ~(0x3ULL << (sizeof(temp32_REGISTER_2) * 8 - 30 - 2)))
        | ((fapi2::i_target_chip.ATTR_OB3_PLL_BUCKET & 0x3ULL) << (sizeof(temp32_REGISTER_2) * 8 - 30 - 2))

    # SCRATCH_REGISTER_2[63-32] = temp32_REGISTER_2
    SCRATCH_REGISTER_2 = (SCRATCH_REGISTER_2 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_2 << 32)

    # SCRATCH_REGISTER_8.setBit<1>()
    # temp32_REGISTER_8[30] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1 - 1))
############################### REGISTER_3 ####################################
    # temp32_REGISTER_3 = SCRATCH_REGISTER_3[63-32]
    temp32_REGISTER_3 = SCRATCH_REGISTER_3 >> 32

    # ATTR_BOOT_FLAGS and ATTR_RISK_LEVEL are read from FAPI
    # SCRATCH_REGISTER_3.insertFromRight<STARTBIT = 0, LENGTH = 32>
    #     (fapi2::FAPI_SYSTEM.ATTR_BOOT_FLAGS)
    # SCRATCH_REGISTER_3.insertFromRight<STARTBIT = 28, LENGTH = 4>
    #     (fapi2::FAPI_SYSTEM.ATTR_RISK_LEVEL)

    # temp32_REGISTER_3[31-0] = fapi2::FAPI_SYSTEM.ATTR_BOOT_FLAGS[31-0]
    temp32_REGISTER_3 = (temp32_REGISTER_3 & ~(0xffffffffULL))
        | (fapi2::FAPI_SYSTEM.ATTR_BOOT_FLAGS & 0xffffffffULL)
    # temp32_REGISTER_3[3-0] = fapi2::FAPI_SYSTEM.ATTR_RISK_LEVEL[3-0]
    temp32_REGISTER_3 = (temp32_REGISTER_3 & ~0xfULL)
        | (fapi2::FAPI_SYSTEM.ATTR_RISK_LEVEL & 0xfULL)

    #SCRATCH_REGISTER_3[63-32] = temp32_REGISTER_3
    SCRATCH_REGISTER_3 = (SCRATCH_REGISTER_3 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_3 << 32)
    # SCRATCH_REGISTER_8.setBit<2>()
    # temp32_REGISTER_8[29] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1 - 2))
############################### REGISTER_4 ####################################
    # temp32_REGISTER_4 = SCRATCH_REGISTER_4[63-32]
    temp32_REGISTER_4 = SCRATCH_REGISTER_4 >> 32

    # SCRATCH_REGISTER_4.insertFromRight<STARTBIT = 0, LENGTH = 16>
    #     (fapi2::i_target_chip.ATTR_BOOT_FREQ_MULT)
    # SCRATCH_REGISTER_4.insertFromRight<STARTBIT = 29, LENGTH = 3>
    #     (fapi2::FAPI_SYSTEM.ATTR_NEST_PLL_BUCKET & 0x7)

    # temp32_REGISTER_4[31-15] = fapi2::i_target_chip.ATTR_BOOT_FREQ_MULT[15-0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(0xffffULL << (sizeof(temp32_REGISTER_4) * 8 - 0 - 16)))
        | ((fapi2::i_target_chip.ATTR_BOOT_FREQ_MULT & 0xffffULL) << (sizeof(temp32_REGISTER_4) * 8 - 0 - 16))
    # temp32_REGISTER_4[2-0] = fapi2::FAPI_SYSTEM.ATTR_NEST_PLL_BUCKET[2-0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~0x7ULL)
        | (fapi2::FAPI_SYSTEM.ATTR_NEST_PLL_BUCKET & 0x7ULL)

    # SCRATCH_REGISTER_4.writeBit<16>
    #     (fapi2::i_target_chip.ATTR_CP_FILTER_BYPASS & 0x1)
    # SCRATCH_REGISTER_4.writeBit<17>
    #     (fapi2::i_target_chip.ATTR_SS_FILTER_BYPASS & 0x1)
    # SCRATCH_REGISTER_4.writeBit<18>
    #     (fapi2::i_target_chip.ATTR_IO_FILTER_BYPASS & 0x1)
    # SCRATCH_REGISTER_4.writeBit<19>
    #     (fapi2::i_target_chip.ATTR_DPLL_BYPASS & 0x1)
    # SCRATCH_REGISTER_4.writeBit<20>
    #     (fapi2::i_target_chip.ATTR_NEST_MEM_X_O_PCI_BYPASS & 0x1)

    # temp32_REGISTER_4[15] = fapi2::i_target_chip.ATTR_CP_FILTER_BYPASS[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 16)))
        | (fapi2::i_target_chip.ATTR_CP_FILTER_BYPASS & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 16)
    # temp32_REGISTER_4[14] = fapi2::i_target_chip.ATTR_SS_FILTER_BYPASS[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 17)))
        | (fapi2::i_target_chip.ATTR_SS_FILTER_BYPASS & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 17)
    # temp32_REGISTER_4[13] = fapi2::i_target_chip.ATTR_IO_FILTER_BYPASS[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 18)))
        | (fapi2::i_target_chip.ATTR_IO_FILTER_BYPASS & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 18)
    # temp32_REGISTER_4[12] = fapi2::i_target_chip.ATTR_DPLL_BYPASS[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 19)))
        | (fapi2::i_target_chip.ATTR_DPLL_BYPASS & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 19)
    # temp32_REGISTER_4[11] = fapi2::i_target_chip.ATTR_NEST_MEM_X_O_PCI_BYPASS[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 20)))
        | (fapi2::i_target_chip.ATTR_NEST_MEM_X_O_PCI_BYPASS & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 20)

    # SCRATCH_REGISTER_4.writeBit<21>
    #     (fapi2::i_target_chip.ATTR_OBUS_RATIO_VALUE & 0x1)
    # temp32_REGISTER_4[10] = fapi2::i_target_chip.ATTR_OBUS_RATIO_VALUE[0]
    temp32_REGISTER_4 = (temp32_REGISTER_4 & ~(1ULL << (sizeof(temp32_REGISTER_4) * 8 - 1 - 21)))
        | (fapi2::i_target_chip.ATTR_OBUS_RATIO_VALUE & 0x1) << (sizeof(temp32_REGISTER_4) * 8 - 1 - 21)

    #SCRATCH_REGISTER_4[63-32] = temp32_REGISTER_4
    SCRATCH_REGISTER_4 = (SCRATCH_REGISTER_4 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_4 << 32)
    # SCRATCH_REGISTER_8.setBit<3>()
    # temp32_REGISTER_8[28] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1 - 3))
############################### REGISTER_5 ####################################
    # temp32_REGISTER_5 = SCRATCH_REGISTER_5[63-32]
    temp32_REGISTER_5 = SCRATCH_REGISTER_5 >> 32
    # set cache contained flag
    if fapi2::FAPI_SYSTEM.ATTR_SYSTEM_IPL_PHASE
        == fapi2::ENUM_ATTR_SYSTEM_IPL_PHASE_CACHE_CONTAINED:

        # SCRATCH_REGISTER_5.setBit<0>()
        # temp32_REGISTER_5[31] = 1
        temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 0))
    else:
        # SCRATCH_REGISTER_5.clearBit<0>()
        # temp32_REGISTER_5[31] = 0
        temp32_REGISTER_5 &= ~(1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 0))

    # set all cores flag
    if fapi2::FAPI_SYSTEM.ATTR_SYS_FORCE_ALL_CORES:
        # SCRATCH_REGISTER_5.setBit<1>()
        # temp32_REGISTER_5[30] = 1
        temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 1))
    else:
        # SCRATCH_REGISTER_5.clearBit<1>()
        # temp32_REGISTER_5[30] = 0
        temp32_REGISTER_5 &= ~(1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 1))

    # risk level flag is deprecated here, moved to scratch3
    # SCRATCH_REGISTER_5.clearBit<2>()
    # temp32_REGISTER_5[29] = 0
    temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 2)) # outside if

    # set disable of HBBL exception vector flag
    if fapi2::FAPI_SYSTEM.ATTR_DISABLE_HBBL_VECTORS
        == fapi2::ENUM_ATTR_DISABLE_HBBL_VECTORS_True:

        # SCRATCH_REGISTER_5.setBit<3>()
        # temp32_REGISTER_5[28] = 1
        temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 3))
    else:
        # SCRATCH_REGISTER_5.clearBit<3>()
        # temp32_REGISTER_5[28] = 0
        temp32_REGISTER_5 &= ~(1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 3))

    # set MC sync mode
    if fapi2::i_target_chip.ATTR_MC_SYNC_MODE:
        # SCRATCH_REGISTER_5.setBit<4>()
        # temp32_REGISTER_5[27] = 1
        temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 4))
    else:
        # SCRATCH_REGISTER_5.clearBit<4>()
        # temp32_REGISTER_5[27] = 0
        temp32_REGISTER_5 &= ~(1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 4))

    # set slow PCI ref clock bit
    if fapi2::FAPI_SYSTEM.ATTR_DD1_SLOW_PCI_REF_CLOCK
        == fapi2::ENUM_ATTR_DD1_SLOW_PCI_REF_CLOCK_SLOW:

        # SCRATCH_REGISTER_5.setBit<5>()
        # temp32_REGISTER_5[26] = 1
        temp32_REGISTER_5 |= (1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 5))
    else:
        # SCRATCH_REGISTER_5.clearBit<5>()
        # temp32_REGISTER_5[26] = 0
        temp32_REGISTER_5 &= ~(1ULL << (sizeof(temp32_REGISTER_5) * 8 - 1 - 5))

    # PLL mux attributes
    #!!!! I'm not sure if SOURCE_STARTBIT is counted from right or left. I assumed right, same as TARGET_STARTBIT. !!!!
    # SCRATCH_REGISTER_5.insert<TARGET_STARTBIT = 12, LENGTH = 20, SOURCE_STARTBIT = 0>
    #     (fapi2::i_target_chip.ATTR_CLOCK_PLL_MUX)
    # temp32_REGISTER_5[19-0] = fapi2::i_target_chip.ATTR_CLOCK_PLL_MUX[19-0]
    temp32_REGISTER_5 = (temp32_REGISTER_5 & ~(0xfffffULL << (sizeof(temp32_REGISTER_5) * 8 - 12 - 20))) # shift value zeros out, but I leave it as a example
        | (((fapi2::i_target_chip.ATTR_CLOCK_PLL_MUX >> sizeof(fapi2::i_target_chip.ATTR_CLOCK_PLL_MUX) * 8 - 20) & 0xfffffULL)
        << (sizeof(temp32_REGISTER_5) * 8 - 12 - 20))

    #SCRATCH_REGISTER_5[63-32] = temp32_REGISTER_5
    SCRATCH_REGISTER_5 = (SCRATCH_REGISTER_5 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_5 << 32)
    # SCRATCH_REGISTER_8.setBit<4>()
    # temp32_REGISTER_8[27] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1 - 4))
############################### REGISTER_6 ####################################
    # temp32_REGISTER_6 = SCRATCH_REGISTER_6[63-32]
    temp32_REGISTER_6 = SCRATCH_REGISTER_6 >> 32

    # attribute for Hostboot slave bit
    if fapi2::i_target_chip.ATTR_PROC_SBE_MASTER_CHIP:
        # SCRATCH_REGISTER_6.setBit<24>()
        # temp32_REGISTER_6[7] = 1
        temp32_REGISTER_6 |= (1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 24))
    else:
        # SCRATCH_REGISTER_6.clearBit<24>()
        # temp32_REGISTER_6[7] = 0
        temp32_REGISTER_6 &= ~(1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 24))
    # SMF_CONFIG
    if fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>().ATTR_SMF_CONFIG
        == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED:

        # SCRATCH_REGISTER_6.setBit<16>()
        # temp32_REGISTER_6[15] = 1
        temp32_REGISTER_6 |= (1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 16))
    else:
        # SCRATCH_REGISTER_6.clearBit<16>()
        # temp32_REGISTER_6[15] = 0
        temp32_REGISTER_6 &= ~(1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 16))

    # PUMP MODE
    if fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>().ATTR_PROC_FABRIC_PUMP_MODE
        == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP:

        # SCRATCH_REGISTER_6.setBit<23>()
        # temp32_REGISTER_6[8] = 1
        temp32_REGISTER_6 |= (1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 23))
    else:
        # SCRATCH_REGISTER_6.clearBit<23>()
        # temp32_REGISTER_6[8] = 0
        temp32_REGISTER_6 &= ~(1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 23))

    # ATTR_PROC_FABRIC_GROUP and CHIP_ID
    # SCRATCH_REGISTER_6.insertFromRight<STARTBIT = 26, LENGTH = 3>
    #     (fapi2::i_target_chip.ATTR_PROC_FABRIC_GROUP_ID)
    # SCRATCH_REGISTER_6.insertFromRight<STARTBIT = 29, LENGTH = 3>
    #     (fapi2::i_target_chip.ATTR_PROC_FABRIC_CHIP_ID)
    # ATTR_PROC_EFF_FABRIC_GROUP and CHIP_ID
    # SCRATCH_REGISTER_6.insertFromRight<STARTBIT = 17, LENGTH = 3>
    #     (fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_GROUP_ID)
    # SCRATCH_REGISTER_6.insertFromRight<STARTBIT = 20, LENGTH = 3>
    #     (fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_CHIP_ID)

    # temp32_REGISTER_6[5-3] = fapi2::i_target_chip.ATTR_PROC_FABRIC_GROUP_ID[2-0]
    temp32_REGISTER_6 = (temp32_REGISTER_6 & ~(0x7ULL << (sizeof(temp32_REGISTER_6) * 8 - 26 - 3)))
        | ((fapi2::i_target_chip.ATTR_PROC_FABRIC_GROUP_ID & 0x7ULL) << (sizeof(temp32_REGISTER_6) * 8 - 26 - 3))
    # temp32_REGISTER_6[2-0] = ffapi2::i_target_chip.ATTR_PROC_FABRIC_CHIP_ID[2-0]
    temp32_REGISTER_6 = (temp32_REGISTER_6 & ~(0x7ULL << (sizeof(temp32_REGISTER_6) * 8 - 29 - 3)))
        | ((fapi2::i_target_chip.ATTR_PROC_FABRIC_CHIP_ID & 0x7ULL) << (sizeof(temp32_REGISTER_6) * 8 - 29 - 3))
    # temp32_REGISTER_6[14-12] = fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_GROUP_ID[2-0]
    temp32_REGISTER_6 = (temp32_REGISTER_6 & ~(0x7ULL << (sizeof(temp32_REGISTER_6) * 8 - 17 - 3)))
        | ((fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_GROUP_ID & 0x7ULL) << (sizeof(temp32_REGISTER_6) * 8 - 17 - 3))
    # temp32_REGISTER_6[11-9] = fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_CHIP_ID[2-0]
    temp32_REGISTER_6 = (temp32_REGISTER_6 & ~(0x7ULL << (sizeof(temp32_REGISTER_6) * 8 - 20 - 3)))
        | ((fapi2::i_target_chip.ATTR_PROC_EFF_FABRIC_CHIP_ID & 0x7ULL) << (sizeof(temp32_REGISTER_6) * 8 - 20 - 3))

    # SCRATCH_REGISTER_6.setBit<0>()
    # temp32_REGISTER_6[31] = 0
    temp32_REGISTER_6 |= (1ULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 0))

    #ATTR_PROC_MEM_TO_USE
    # SCRATCH_REGISTER_6.insertFromRight<STARTBIT = 1, LENGTH = 6>
    #     (fapi2::i_target_chip.ATTR_PROC_MEM_TO_USE)
    # temp32_REGISTER_6[30-25] = fapi2::i_target_chip.ATTR_PROC_MEM_TO_USE[5-0]
    temp32_REGISTER_6 = (temp32_REGISTER_6 & ~(0x3fULL << (sizeof(temp32_REGISTER_6) * 8 - 1 - 6)))
        | ((fapi2::i_target_chip.ATTR_PROC_MEM_TO_USE & 0x3fULL) << (sizeof(temp32_REGISTER_6) * 8 - 1 - 6))


    #SCRATCH_REGISTER_6[63-32] = temp32_REGISTER_6
    SCRATCH_REGISTER_6 = (SCRATCH_REGISTER_6 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_6 << 32)
    # SCRATCH_REGISTER_8.setBit<5>()
    # temp32_REGISTER_8[26] = 1
    temp32_REGISTER_8 |= (1ULL << (sizeof(temp32_REGISTER_8) * 8 - 1 - 5))
    #SCRATCH_REGISTER_8[63-32] = temp32_REGISTER_8
    SCRATCH_REGISTER_8 = (SCRATCH_REGISTER_8 & ~(0xffffffffULL << 32))
        | (temp32_REGISTER_8 << 32)

#### PERV_CBS_CS_ is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.CBS_CS
    # SECURITY_MODE attribute
    # if fapi2::ATTR_SECURITY_MODE.getBit<7>() == 0:
    #     #SMD=0 indicate chip is in secure mode
    #     if PERV_CBS_CS_SCOM.getBit<5>() == 0:
    #         #Changing SAB bit to unsecure mode
    #         PERV_CBS_CS_SCOM.clearBit<4>()

    temp8_ATTR_SECURITY_MODE = fapi2::FAPI_SYSTEM.ATTR_SECURITY_MODE
    temp64_PERV_CBS_CS = fapi2::i_target_chip.PERV_CBS_CS
    # if temp8_ATTR_SECURITY_MODE[0] == 0
    if temp8_ATTR_SECURITY_MODE & (1ULL << (sizeof(temp8_ATTR_SECURITY_MODE) * 8 - 1 - 7):
        #SMD=0 indicate chip is in secure mode
        # if temp64_PERV_CBS_CS[58] == 0
        if temp64_PERV_CBS_CS & (1ULL << (sizeof(temp64_PERV_CBS_CS_SCOM) * 8 - 1 - 5):
            #Changing SAB bit to unsecure mode
            # temp64_PERV_CBS_CS[59] = 0
            temp64_PERV_CBS_CS &= ~(1ULL << (sizeof(temp64_PERV_CBS_CS_SCOM) * 8 - 1 - 4))
            PERV_CBS_CS_ = temp64_PERV_CBS_CS_SCOM
```

### updateSbeBootSeeprom() from src/usr/sbe/sbe_update.C

```python
# SB_CS is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.SB_CS

def updateSbeBootSeeprom(i_target):
    l_sbeBootSelectMask = 0x0000400000000000 >> 32
    l_targetReg = i_target.SB_CS
    masterProcChipTargetHandle(l_masterTarget) # write to l_masterTarget
    l_bootside = getSbeBootSeeprom(l_masterTarget)
    l_bootSide0 = (l_bootside == 0)
    if l_bootSide0:
        l_targetReg &= ~l_sbeBootSelectMask
    else:
        l_targetReg |= l_sbeBootSelectMask
    i_target.SB_CS = l_targetReg
```
