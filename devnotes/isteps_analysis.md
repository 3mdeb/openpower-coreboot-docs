# Step 8 Hostboot – Nest Chiplets

## 8.1 host_slave_sbe_config

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

## 8.2 host_setup_sbe

### src/usr/isteps/istep08/call_host_setup_sbe.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        p9_set_fsi_gp_shadow(l_cpu_target) ## described bellow
```
### src/import/chips/p9/procedures/hwp/perv/p9_set_fsi_gp_shadow.C

```python
# PERV_ROOT_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.ROOT_CTRLx_COPY
# PERV_PERV_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.PERV_CTRLx_COPY
def p9_set_fsi_gp_shadow(i_target_chip):

    if fapi2::i_target_chip.ATTR_CHIP_EC_FEATURE_FSI_GP_SHADOWS_OVERWRITE:
        # Setting flush values for root_ctrl_copy and perv_ctrl_copy registers
        PERV_ROOT_CTRL0_COPY = 0x80FE4003
        PERV_ROOT_CTRL1_COPY = 0x00180000
        PERV_ROOT_CTRL2_COPY = 0x0400E000
        PERV_ROOT_CTRL3_COPY = 0x0080C000
        PERV_ROOT_CTRL4_COPY = 0x00000000
        PERV_ROOT_CTRL5_COPY &= 0xFFFF0000
        PERV_ROOT_CTRL6_COPY = (PERV_ROOT_CTRL6_COPY & 0xF0000000) | 0x00800000
        PERV_ROOT_CTRL7_COPY = 0x00000000
        PERV_ROOT_CTRL8_COPY = (PERV_ROOT_CTRL6_COPY & 0x0000008F) | 0xEEECF300
        PERV_PERV_CTRL0_COPY = 0x7C0E2000
        PERV_PERV_CTRL1_COPY = 0x63C00000

    # Write the value of FUSED_CORE_MODE into PERV_CTRL0(23) regardless of chip EC the bit is nonfunctional on Nimbus DD1
    if fapi2::fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>().ATTR_FUSED_CORE_MODE
        # PERV_PERV_CTRL0_COPY.setBit<23()
        # PERV_PERV_CTRL0_COPY[8] = 1
        PERV_PERV_CTRL0_COPY |= (1ULL << (sizeof(PERV_PERV_CTRL0_COPY) * 8 - 1 - 23))
    else
        # PERV_PERV_CTRL0_COPY.clearBit<23>()
        # PERV_PERV_CTRL0_COPY[8] = 0
        PERV_PERV_CTRL0_COPY &= ~(1ULL << (sizeof(PERV_PERV_CTRL0_COPY) * 8 - 1 - 23))
```

## 8.3 host_cbs_start

### src/usr/isteps/istep08/call_host_cbs_start.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        sendFifoReset(l_cpu_target)
        p9_start_cbs(l_cpu_target, True) ## described bellow
```

### src/import/chips/p9/procedures/hwp/perv/p9_start_cbs.C

```python
def p9_start_cbs(i_target_chip, i_sbe_start):
    temp8_PIBRESET_DELAY = fapi2::i_target_chip.ATTR_CHIP_EC_FEATURE_HW402019_PIBRESET_DELAY
    temp8_RESET_SKIP = fapi2::FAPI_SYSTEM.ATTR_START_CBS_FIFO_RESET_SKIP
    i_target_chip.PERV_SB_MSG = 0
    # PERV_CBS_CS.setBit<3>()
    # PERV_CBS_CS[28] = 1
    i_target_chip.PERV_CBS_CS |= 1<<28

    temp32_PERV_SB_CS = i_target_chip.PERV_SB_CS

    # temp32_PERV_SB_CS.clearBit<12>()
    # temp32_PERV_SB_CS.clearBit<13>()

    # temp32_PERV_SB_CS[20] = 0
    temp32_PERV_SB_CS &= ~(1<<20)
    # temp32_PERV_SB_CS[19] = 0
    temp32_PERV_SB_CS &= ~(1<<19)

    i_target_chip.PERV_SB_CS = temp32_PERV_SB_CS

    # i_target_chip.PERV_CBS_ENVSTAT.getBit<2>()
    #l_read_vdn_pgood_status = i_target_chip.PERV_CBS_ENVSTAT[29]
    l_read_vdn_pgood_status = (i_target_chip.PERV_CBS_ENVSTAT & (1<<29)) == (1<<29)

    temp32_PERV_CBS_CS = i_target_chip.PERV_CBS_CS
    # temp32_PERV_CBS_CS.clearBit<0>()
    # temp32_PERV_CBS_CS.clearBit<2>()

    # temp32_PERV_CBS_CS[31] = 0
    temp32_PERV_CBS_CS &= ~(1<<31)
    # temp32_PERV_CBS_CS[29] = 0
    temp32_PERV_CBS_CS &= ~(1<<29)

    i_target_chip.PERV_CBS_CS = temp32_PERV_CBS_CS

    # temp32_PERV_CBS_CS[31] = 1
    temp32_PERV_CBS_CS |= 1<<31
    i_target_chip.PERV_CBS_CS = temp32_PERV_CBS_CS

    l_timeout = 20
    #UNTIL CBS_CS.CBS_CS_INTERNAL_STATE_VECTOR == CBS_IDLE_VALUE
    while l_poll_data != 0x002:
        temp32_PERV_CBS_CS = i_target_chip.PERV_CBS_CS
        # l_poll_data[15-0] = temp32_PERV_CBS_CS[15-0]
        l_poll_data = temp32_PERV_CBS_CS & 0xffff
        #fapi2::delay(P9_CBS_IDLE_HW_NS_DELAY, P9_CBS_IDLE_SIM_CYCLE_DELAY)
        sleep(640000ns)

    if temp8_PIBRESET_DELAY:
        i_target_chip.PERV_FSI2PIB_SET_PIB_RESET = ~0ULL
        #fapi2::delay(P9_PIBRESET_HW_NS_DELAY, P9_PIBRESET_SIM_CYCLE_DELAY)
        sleep(4000ns)

    if temp8_RESET_SKIP:
        i_target_chip.PERV_FSB_FSB_DOWNFIFO_RESET = 0x80000000
    if i_sbe_start:
        #Setting up hreset
        # i_target_chip.PERV_SB_CS[19] = 0
        i_target_chip.PERV_SB_CS &= ~(1<<19)
        # i_target_chip.PERV_SB_CS[19] = 1
        i_target_chip.PERV_SB_CS |= 1<<19
        # i_target_chip.PERV_SB_CS[19] = 0
        i_target_chip.PERV_SB_CS &= ~(1<<19)
    #l_fsi2pib_status = i_target_chip.PERV_FSI2PIB_STATUS[15]
    l_fsi2pib_status = (i_target_chip.PERV_FSI2PIB_STATUS & (1 << 15)) == (1 << 15)
```

## 8.4 proc_check_slave_sbe_seeprom_complete: Check Slave SBE Complete

### src/usr/isteps/istep08/call_proc_check_slave_sbe_seeprom_complete.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        #Note no PLID passed in
        SBEIO::SbeRetryHandler l_SBEobj = SBEIO::SbeRetryHandler(
                SBEIO::SbeRetryHandler::SBE_MODE_OF_OPERATION::ATTEMPT_REBOOT)

        l_SBEobj.setSbeRestartMethod(
            SBEIO::SbeRetryHandler::SBE_RESTART_METHOD::START_CBS)

        # We want to tell the retry handler that we have just powered
        # on the sbe, to distinguish this case from other cases where
        # we have determine there is something wrong w/ the sbe and
        # want to diagnose the problem
        l_SBEobj.setInitialPowerOn(True)
        l_SBEobj.main_sbe_handler(l_cpu_target)

# probably just error checking below

        # We will judge whether or not the SBE had a successful
        # boot or not depending on if it made it to runtime or not
        if l_SBEobj.isSbeAtRuntime():
            # Set attribute indicating that SBE is started
            l_cpu_target.setAttr<ATTR_SBE_IS_STARTED>(1)
            # Make the FIFO call to get and apply the SBE Capabilities
             SBEIO::getFifoSbeCapabilities(l_cpu_target)
            # Switch to using SBE SCOM
            ScomSwitches l_switches =
                l_cpu_target.getAttr<ATTR_SCOM_SWITCHES>()
            ScomSwitches l_switches_before = l_switches
            # Turn on SBE SCOM and turn off FSI SCOM.
            l_switches.useFsiScom = 0
            l_switches.useSbeScom = 1
            # proc_check_slave_sbe_seeprom_complete: changing SCOM
            l_cpu_target.setAttr<ATTR_SCOM_SWITCHES>(l_switches)
            # SUCCESS : proc_check_slave_sbe_seeprom_complete
        else:
            # FAILURE : proc_check_slave_sbe_seeprom_complete
            pass

    for l_cpu_target in l_cpuTargetList:
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2ProcTarget(
                const_cast<TARGETING::Target*> (l_cpu_target))
        # variable_buffer of size 112 is originally used, but I think it needs to be 192 bytes long
        uint_192t l_fuseString
        p9_getecid(l_fapi2ProcTarget, l_fuseString) # described bellow
```

```python
def p9_getecid(i_target_chip, o_fuseString):
    uint64_t attr_data[2]

    # buffer is 192 bits long!
    # variable_buffer of size 192 is originally used
    uint_192t l_fuseString
    temp64_ECID_PART0 = PU_OTPROM0_ECID_PART0_REGISTER
    temp64_ECID_PART1 = PU_OTPROM0_ECID_PART1_REGISTER
    temp64_ECID_PART2 = PU_OTPROM0_ECID_PART2_REGISTER
    temp64_ECID_PART0.reverse() # function originally used to reverse bits is pasted bellow
    temp64_ECID_PART1.reverse()
    temp64_ECID_PART2.reverse()
    attr_data[0] = temp64_ECID_PART0()
    attr_data[1] = temp64_ECID_PART1()
# src/import/hwpf/fapi2/include/variable_buffer.H:883 -> variable_buffer.H:86
    # l_fuseString.insert(temp64_ECID_PART0(), 0, 64)
    # l_fuseString.insert(temp64_ECID_PART1(), 64, 64)
    # l_fuseString.insert(temp64_ECID_PART2(), 128, 64)
    l_fuseString &= 0xFFFFFFFFFFFFFFFF  << 0
    l_fuseString |= temp64_ECID_PART0() << 0
    l_fuseString &= 0xFFFFFFFFFFFFFFFF  << 64
    l_fuseString |= temp64_ECID_PART1() << 64
    l_fuseString &= 0xFFFFFFFFFFFFFFFF  << 128
    l_fuseString |= temp64_ECID_PART2() << 128
    # probably nothing ever happens with o_fuseString
    o_fuseString = l_fuseString
    #push fuse string into attribute
    FAPI_ATTR_SET(fapi2::ATTR_ECID, i_target_chip, attr_data)
    # Set some attributes memory can used to make work-around decisions.
    setup_memory_work_around_attributes(i_target_chip, temp64_ECID_PART2)
    setup_pcie_work_around_attributes(i_target_chip, temp64_ECID_PART2)
```

```python
def reverse(io_buffer):
    l_result = io_buffer
    l_s = sizeof(T) * 8 - 1
    while io_buffer != 0:
        io_buffer >>= 1
        l_result <<= 1
        l_result |= io_buffer & 1
        l_s -= 1
    l_result <<= l_s
    io_buffer = l_result
```

## 8.5 host_attnlisten_proc: Start attention poll for P9(s)

## 8.6 host_p9_fbc_eff_config: Determine Powerbus config

```
#
# src/usr/isteps/istep08/call_host_p9_fbc_eff_config.C:39
#
fapi2::ReturnCode
p9_fbc_eff_config()
{
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM
    fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_Type l_core_floor_ratio
    fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO_Type l_core_ceiling_ratio
    fapi2::ATTR_FREQ_PB_MHZ_Type l_freq_fbc
    fapi2::ATTR_FREQ_CORE_CEILING_MHZ_Type l_freq_core_ceiling

    #############p9_fbc_eff_config_process_freq_attributes()#############

    # get core floor/nominal/ceiling frequency attributes
    l_freq_core_floor = fapi2::ATTR_FREQ_CORE_FLOOR_MHZ[FAPI_SYSTEM]
    l_freq_core_ceiling = fapi2::ATTR_FREQ_CORE_CEILING_MHZ[FAPI_SYSTEM]
    # calculate fabric/core frequency ratio attributes
    l_freq_fbc = fapi2::ATTR_FREQ_PB_MHZ[FAPI_SYSTEM]

    # determine table index based on fabric/core floor frequency ratio
    # breakpoint ratio: core floor 4.0, pb 2.0 (cache floor :: pb = 8/8)
    if (l_freq_core_floor) >= (2 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_8_8
    # breakpoint ratio: core floor 3.5, pb 2.0 (cache floor :: pb = 7/8)
    elif (4 * l_freq_core_floor) >= (7 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_7_8
    # breakpoint ratio: core floor 3.0, pb 2.0 (cache floor :: pb = 6/8)
    elif (2 * l_freq_core_floor) >= (3 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_6_8
    # breakpoint ratio: core floor 2.5, pb 2.0 (cache floor :: pb = 5/8)
    elif (4 * l_freq_core_floor) >= (5 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_5_8
    # breakpoint ratio: core floor 2.0, pb 2.0 (cache floor :: pb = 4/8)
    elif l_freq_core_floor >= l_freq_fbc:
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_4_8
    # breakpoint ratio: core floor 1.0, pb 2.0 (cache floor :: pb = 2/8)
    elif (2 * l_freq_core_floor) >= l_freq_fbc:
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_2_8

    # determine table index based on fabric/core ceiling frequency ratio
    # breakpoint ratio: core ceiling 4.0, pb 2.0 (cache ceiling :: pb = 8/8)
    if (l_freq_core_ceiling) >= (2 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_8_8
    # breakpoint ratio: core ceiling 3.5, pb 2.0 (cache ceiling :: pb = 7/8)
    elif (4 * l_freq_core_ceiling) >= (7 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_7_8
    # breakpoint ratio: core ceiling 3.0, pb 2.0 (cache ceiling :: pb = 6/8)
    elif (2 * l_freq_core_ceiling) >= (3 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_6_8
    # breakpoint ratio: core ceiling 2.5, pb 2.0 (cache ceiling :: pb = 5/8)
    elif (4 * l_freq_core_ceiling) >= (5 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_5_8
    # breakpoint ratio: core ceiling 2.0, pb 2.0 (cache ceiling :: pb = 4/8)
    elif l_freq_core_ceiling >= l_freq_fbc:
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_4_8
    # breakpoint ratio: core ceiling 1.0, pb 2.0 (cache ceiling :: pb = 2/8)
    elif (2 * l_freq_core_ceiling) >= l_freq_fbc:
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_2_8

    # write attributes
    fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO[FAPI_SYSTEM] = l_core_floor_ratio
    fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO[FAPI_SYSTEM] = l_core_ceiling_ratio
    fapi2::ATTR_PROC_FABRIC_ASYNC_SAFE_MODE[FAPI_SYSTEM] = fapi2::ENUM_ATTR_PROC_FABRIC_ASYNC_SAFE_MODE_PERFORMANCE_MODE

    ##################p9_fbc_eff_config_calc_epsilons()##################

    # epsilon output attributes
    # uint32_t l_eps_r[NUM_EPSILON_READ_TIERS]
    uint32_t l_eps_r[3]
    # uint32_t l_eps_w[NUM_EPSILON_WRITE_TIERS]
    uint32_t l_eps_w[2]
    fapi2::ATTR_PROC_EPS_GB_PERCENTAGE_Type l_eps_gb

    # fetch epsilon table type/pump mode attributes
    fapi2::ATTR_PROC_EPS_TABLE_TYPE_Type l_eps_table_type
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_pump_mode
    l_eps_table_type = fapi2::ATTR_PROC_EPS_TABLE_TYPE[FAPI_SYSTEM]
    l_pump_mode = fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_HE:
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[0] = EPSILON_R_T0_HE[l_core_floor_ratio]
            l_eps_r[1] = EPSILON_R_T1_HE[l_core_floor_ratio]
            l_eps_r[2] = EPSILON_R_T2_HE[l_core_floor_ratio]

            l_eps_w[0] = EPSILON_W_T0_HE[l_core_floor_ratio]
            l_eps_w[1] = EPSILON_W_T1_HE[l_core_floor_ratio]
        else:
            l_eps_r[0] = EPSILON_R_T0_F4[l_core_floor_ratio]
            l_eps_r[1] = EPSILON_R_T1_F4[l_core_floor_ratio]
            l_eps_r[2] = EPSILON_R_T2_F4[l_core_floor_ratio]

            l_eps_w[0] = EPSILON_W_T0_F4[l_core_floor_ratio]
            l_eps_w[1] = EPSILON_W_T1_F4[l_core_floor_ratio]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_HE_F8:
        l_eps_r[0] = EPSILON_R_T0_F8[l_core_floor_ratio]
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[1] = EPSILON_R_T1_F8[l_core_floor_ratio]
        else:
            l_eps_r[1] = EPSILON_R_T0_F8[l_core_floor_ratio]
        l_eps_r[2] = EPSILON_R_T2_F8[l_core_floor_ratio]

        l_eps_w[0] = EPSILON_W_T0_F8[l_core_floor_ratio]
        l_eps_w[1] = EPSILON_W_T1_F8[l_core_floor_ratio]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_LE:
        l_eps_r[0] = EPSILON_R_T0_LE[l_core_floor_ratio]
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[1] = EPSILON_R_T1_LE[l_core_floor_ratio]
        else:
            l_eps_r[1] = EPSILON_R_T0_LE[l_core_floor_ratio]
        l_eps_r[2] = EPSILON_R_T2_LE[l_core_floor_ratio]

        l_eps_w[0] = EPSILON_W_T0_LE[l_core_floor_ratio]
        l_eps_w[1] = EPSILON_W_T1_LE[l_core_floor_ratio]

    # set guardband default value to +20%
    fapi2::ATTR_PROC_EPS_GB_PERCENTAGE[FAPI_SYSTEM] = 20

    # get gardband attribute
    # Note: if a user makes an attribute override with CONST, it would
    # override the above default value settings. This mechanism is to
    # allow users to change the default settings for testing.
    l_eps_gb = fapi2::ATTR_PROC_EPS_GB_PERCENTAGE[FAPI_SYSTEM]

    # scale base epsilon values if core is running 2x nest frequency
    if l_core_ceiling_ratio == fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_8_8:
        uint8_t l_scale_percentage =
            ((100*l_freq_core_ceiling) / (2*l_freq_fbc)) - 100
        # scale/apply guardband read epsilons
        for ii in range(0, NUM_EPSILON_READ_TIERS):
            p9_fbc_eff_config_guardband_epsilon(l_scale_percentage, l_eps_r[ii])
        # Scale write epsilons
        for ii in range(0, NUM_EPSILON_WRITE_TIERS):
            p9_fbc_eff_config_guardband_epsilon(l_scale_percentage, l_eps_w[ii])
    for ii in range(0, NUM_EPSILON_READ_TIERS):
        p9_fbc_eff_config_guardband_epsilon(l_eps_gb, l_eps_r[ii])
    for ii in range(0, NUM_EPSILON_WRITE_TIERS):
        p9_fbc_eff_config_guardband_epsilon(l_eps_gb, l_eps_w[ii])

    # write attributes
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T0[FAPI_SYSTEM] = l_eps_r[0]
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T1[FAPI_SYSTEM] = l_eps_r[1]
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T2[FAPI_SYSTEM] = l_eps_r[2]
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1[FAPI_SYSTEM] = l_eps_w[0]
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2[FAPI_SYSTEM] = l_eps_w[1]

    #############p9_fbc_eff_config_reset_attrs()#############

    # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config.C:516
    fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE_Type l_link_active = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_FALSE
    for l_proc_target in FAPI_SYSTEM.getChildren<fapi2::TARGET_TYPE_PROC_CHIP>():
        for l_obus_chiplet_target in l_proc_target.getChildren<fapi2::TARGET_TYPE_OBUS>():
            fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_obus_chiplet_target] = l_link_active
        for l_xbus_chiplet_target in l_proc_target.getChildren<fapi2::TARGET_TYPE_XBUS>():
            fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_xbus_chiplet_target] = l_link_active
}

#/
#/ @brief Utility function to apply positive/negative scaing factor
#/        to base epsilon value
#/
#/ @param[in] i_gb_percentage       Scaling factor (e.g. 20% = 20)
#/ @param[in/out] io_target_value   Target epsilon value, after application of
#/                                  scaling factor
#/                                  NOTE: scaling will be clamped to
#/                                  minimum/maximum value
#/
#/ @return void.
#/
void p9_fbc_eff_config_guardband_epsilon(
    const uint8_t i_gb_percentage,
    uint32_t& io_target_value)
{
    uint32_t l_delta =  ((io_target_value * i_gb_percentage) / 100)
    if (io_target_value * i_gb_percentage) % 100 == 0:
        l_delta += 1

    # Apply guardband
    if i_gb_percentage >= 0:
        # Clamp to maximum value if necessary
        #
        # 0xFFFFFFFF = EPSILON_MAX_VALUE
        io_target_value = MAX(io_target_value+l_delta, 0xFFFFFFFF)
    else:
        # Clamp to minimum value if necessary
        #
        # 1 = EPSILON_MIN_VALUE
        io_target_value = MIN(io_target_value-l_delta, 1)
    return
}
```

## 8.7 host_p9_eff_config_links: Powerbuslinkconfig

```
void fapiHWPCallWrapper(HWP_CALL_TYPE    P9_FBC_EFF_CONFIG_LINKS_T_F,
                        IStepError      &o_stepError,
                        compId_t         HWPF_COMP_ID,
                        TARGETING::TYPE  TYPE_PROC)
{
    # Get a list of all the processors in the system
    l_targetList = getAllChips(TYPE_PROC)
    # Loop through all processors including master
    for l_target in l_targetList:
        # Get a FAPI2 target of type PROC
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapi2Target(l_target)

        ####################p9_fbc_eff_config_links()####################

        # logical link (X/A) configuration parameters, init arrays to default values
        # enable on local end

        # link/fabric ID on remote end
        # indexed by link ID on local end
        uint8_t l_x_rem_link_id[7] = { 0 }
        uint8_t l_x_rem_fbc_chip_id[7] = { 0 }
        uint8_t l_a_rem_link_id[4] = { 0 }
        uint8_t l_a_rem_fbc_group_id[4] = { 0 }
        # FAPI_SYSTEM = 0x0000000000000001
        fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM
        # process XBUS (electrical) endp targets
        auto l_electrical_targets = l_fapi2Target.getChildren<fapi2::TARGET_TYPE_XBUS>()

        for l_iter in range(0, len(l_electrical_targets)):
            ###################p9_fbc_eff_config_links_query_endp()###################
            # A/X link ID for local end
            uint8_t l_loc_link_id = 0
            # remote end target
            fapi2::Target<fapi2::TARGET_TYPE_XBUS> l_rem_target
            # determine link ID/enable state for local end
            p9_fbc_eff_config_links_map_endp(
                l_iter,
                P9_FBC_XBUS_LINK_CTL_ARR,
                # P9_FBC_UTILS_MAX_X_LINKS = 7
                7,
                l_loc_link_id)
            p9_fbc_eff_config_links_query_link_en(l_iter, l_x_en[l_loc_link_id])
            # local end link target is enabled, query remote end
            if l_x_en[l_loc_link_id]:
                # obtain endpoint target associated with remote end of link
                l_rem_target = getOtherEnd(l_iter)
                if l_rc:
                    # endpoint target for remote end of link is not configured
                    l_x_en[l_loc_link_id] = 0
                else:
                    # endpoint target is configured, qualify local link enable with remote endpoint state
                    p9_fbc_eff_config_links_query_link_en(l_rem_target, l_x_en[l_loc_link_id])

            # link is enabled, gather remaining remote end parameters
            if l_x_en[l_loc_link_id]:
                p9_fbc_eff_config_links_map_endp<fapi2::TARGET_TYPE_XBUS>(
                    l_rem_target,
                    P9_FBC_XBUS_LINK_CTL_ARR,
                    # P9_FBC_UTILS_MAX_X_LINKS = 7
                    7,
                    l_x_rem_link_id[l_loc_link_id])
                if fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
                    # return either chip or group ID of remote chip
                    o_rem_fbc_id[l_loc_link_id] = fapi2::ATTR_PROC_FABRIC_CHIP_ID[l_rem_target]
                else:
                    # p9_fbc_utils_get_group_id_attr(l_rem_target, l_rem_fbc_group_id)
                    o_rem_fbc_id[l_loc_link_id] = fapi2::ATTR_PROC_FABRIC_GROUP_ID[l_rem_target]
            ###########p9_fbc_eff_config_links_set_link_active_attr()###########
            if l_x_en[l_loc_link_id]:
                fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_iter] = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_TRUE
            else:
                fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_iter] = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_FALSE
            ###########end of p9_fbc_eff_config_links_set_link_active_attr()###########
        # const uint32_t P9_FBC_UTILS_MAX_X_LINKS = 7
        # const uint32_t P9_FBC_UTILS_MAX_A_LINKS = 4
        l_x_num = 0
        l_x_en[7] = { 0 }
        l_a_num = 0
        l_a_en[4] = { 0 }

        for l_link_id in range(0, 7): # 7 = P9_FBC_UTILS_MAX_X_LINKS
            if l_x_en[l_link_id]:
                l_x_num += 1
        for l_link_id in range(0, 4): # 4 = P9_FBC_UTILS_MAX_A_LINKS
            if l_a_en[l_link_id]:
                l_a_num += 1

        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[l_fapi2Target] = l_x_en
        fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG[l_fapi2Target]         = l_x_num
        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[l_fapi2Target]   = l_x_rem_link_id
        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[l_fapi2Target]   = l_x_rem_fbc_chip_id
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[l_fapi2Target] = l_a_en
        fapi2::ATTR_PROC_FABRIC_A_LINKS_CNFG[l_fapi2Target]         = l_a_num
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[l_fapi2Target]   = l_a_rem_link_id
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_ID[l_fapi2Target]   = l_a_rem_fbc_group_id

        # aggregate (local+remote) delays
        # const uint32_t P9_FBC_UTILS_MAX_X_LINKS = 7
        # const uint32_t P9_FBC_UTILS_MAX_A_LINKS = 4
        uint32_t l_x_agg_link_delay[7]
        uint32_t l_a_agg_link_delay[4]
        std::fill_n(l_x_agg_link_delay, 7, 0xFFFFFFFF)
        std::fill_n(l_a_agg_link_delay, 4, 0xFFFFFFFF)

        # aggregate model/address disable on local end
        uint8_t l_x_addr_dis[7] = { 0 }
        uint8_t l_a_addr_dis[4] = { 0 }

        fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[l_fapi2Target] = l_x_agg_link_delay
        fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[l_fapi2Target]   = l_x_addr_dis
        fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[l_fapi2Target]  = 0
        fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[l_fapi2Target] = l_a_agg_link_delay
        fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[l_fapi2Target]   = l_a_addr_dis
        fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[l_fapi2Target]  = 0
        ####################end p9_fbc_eff_config_links()####################
}

#
# @brief Map endpoint target to X/A link ID
#
# @tparam T template parameter, passed in target.
# @param[in]  i_loc_target          Endpoint target (of type T) of local end of link
# @param[in]  i_link_ctl_arr        Array of X/A link control structures
# @param[in]  i_link_ctl_arr_size   Number of entries in i_link_ctl_arr
# @param[in]  o_link_id             X/A logical link ID
#
# @return fapi2::ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
#
fapi2::ReturnCode p9_fbc_eff_config_links_map_endp(
    const fapi2::Target<T>& i_target,
    const p9_fbc_link_ctl_t i_link_ctl_arr[],
    const uint8_t i_link_ctl_arr_size,
    uint8_t& o_link_id)
{
    for l_link_id in range(0, i_link_ctl_arr_size):
        if  (static_cast<fapi2::TargetType>(i_link_ctl_arr[l_link_id].endp_type) == T)
        and (i_link_ctl_arr[l_link_id].endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[i_target]):
            o_link_id = l_link_id
            break
}

fapi2::ReturnCode p9_fbc_eff_config_links_query_link_en(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS>& i_target,
    uint8_t& o_link_is_enabled)
{
    l_link_train = fapi2::ATTR_LINK_TRAIN[i_target]
    if l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
    elif l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY
    elif l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY
    else:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
}
```

## 8.8 proc_attr_update :Proc ATTR Update

* Called per processor
* Stub HWP for FW to override attributes programmatically

```
noop
```

## 8.9 proc_chiplet_scominit: Scom inits to all chiplets (sans Quad)

```
void *call_proc_chiplet_fabric_scominit(void *io_pArgs)
{
    for each target:
        // p9_chiplet_fabric_scominit()
        fapi2::ReturnCode l_rc;
        char l_chipletTargetStr[fapi2::MAX_ECMD_STRING_LEN];
        fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
        std::vector<fapi2::Target<fapi2::TARGET_TYPE_XBUS>> l_xbus_chiplets;
        std::vector<fapi2::Target<fapi2::TARGET_TYPE_OBUS>> l_obus_chiplets;
        fapi2::buffer<uint64_t> l_fbc_cent_fir_data;

        fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_Type l_fbc_optics_cfg_mode = {fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_SMP};

        // apply FBC non-hotplug initfile
        FAPI_EXEC_HWP(l_rc, p9_fbc_no_hp_scom, i_target, FAPI_SYSTEM);

        // setup IOE (XBUS FBC IO) TL SCOMs
        FAPI_EXEC_HWP(l_rc, p9_fbc_ioe_tl_scom, i_target, FAPI_SYSTEM);

        l_xbus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_XBUS>();

        // configure TL FIR, only if not already setup by SBE
        l_fbc_cent_fir_data = i_target[PU_PB_CENT_SM0_PB_CENT_FIR_REG];

        if (!l_fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
        {
            // FBC_IOE_TL_FIR_ACTION0 = 0x0000000000000000ULL
            PU_PB_IOE_FIR_ACTION0_REG[i_target] = FBC_IOE_TL_FIR_ACTION0;
            // FBC_IOE_TL_FIR_ACTION1 = 0x0049000000000000ULL
            PU_PB_IOE_FIR_ACTION1_REG[i_target] = FBC_IOE_TL_FIR_ACTION1;

            // FBC_IOE_TL_FIR_MASK = 0xFF24F0303FFFF11FULL
            fapi2::buffer<uint64_t> l_fir_mask = FBC_IOE_TL_FIR_MASK;

            if (len(l_xbus_chiplets) == 0)
            {
                // no valid links, mask
                // l_fir_mask.flush<1>();
                l_fir_mask = 0xFFFFFFFFFFFFFFFF;
            }
            else
            {
                // P9_FBC_UTILS_MAX_ELECTRICAL_LINKS = 3
                bool l_x_functional[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] =
                {
                    false,
                    false,
                    false
                };
                // P9_FBC_UTILS_MAX_ELECTRICAL_LINKS = 3
                uint64_t l_x_non_functional_mask[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] =
                {
                    // FBC_IOE_TL_FIR_MASK_X0_NF = 0x00C00C0C00000880ULL;
                    // FBC_IOE_TL_FIR_MASK_X1_NF = 0x0018030300000440ULL;
                    // FBC_IOE_TL_FIR_MASK_X2_NF = 0x000300C0C0000220ULL;
                    FBC_IOE_TL_FIR_MASK_X0_NF,
                    FBC_IOE_TL_FIR_MASK_X1_NF,
                    FBC_IOE_TL_FIR_MASK_X2_NF
                };

                for l_iter in l_xbus_chiplets:
                {
                    uint8_t l_unit_pos;
                    l_unit_pos = fapi2::ATTR_CHIP_UNIT_POS[l_iter];
                    l_x_functional[l_unit_pos] = true;
                }

                for ll in range(0, P9_FBC_UTILS_MAX_ELECTRICAL_LINKS):
                {
                    if (!l_x_functional[ll])
                    {
                        l_fir_mask |= l_x_non_functional_mask[ll];
                    }
                }
                PU_PB_IOE_FIR_MASK_REG[i_target] = l_fir_mask;
            }
        }

        // setup IOE (XBUS FBC IO) DL SCOMs
        for l_iter in l_xbus_chiplets:
        {
            FAPI_EXEC_HWP(l_rc, p9_fbc_ioe_dl_scom, *l_iter, i_target);
            // configure DL FIR, only if not already setup by SBE
            if (!l_fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
            {
                XBUS_LL0_IOEL_FIR_ACTION0_REG[l_iter] = FBC_IOE_DL_FIR_ACTION0;
                XBUS_LL0_IOEL_FIR_ACTION1_REG[l_iter] = FBC_IOE_DL_FIR_ACTION1;
                XBUS_LL0_LL0_LL0_IOEL_FIR_MASK_REG[l_iter] = FBC_IOE_DL_FIR_MASK;
            }
        }

        // set FBC optics config mode attribute
        l_obus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_OBUS>();

        for l_iter in l_obus_chiplets:
        {
            uint8_t l_unit_pos;
            l_unit_pos = fapi2::ATTR_CHIP_UNIT_POS[l_iter];
            l_fbc_optics_cfg_mode[l_unit_pos] = fapi2::ATTR_OPTICS_CONFIG_MODE[l_iter];
        }
        fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[i_target] = l_fbc_optics_cfg_mode;
}

fapi2::ReturnCode p9_fbc_ioe_dl_scom(const fapi2::Target<fapi2::TARGET_TYPE_XBUS> &TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT1)
{
    fapi2::ATTR_EC_Type l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    l_chip_id = fapi2::ATTR_NAME[TGT1];
    l_chip_ec = fapi2::ATTR_EC[TGT1];

    // REGISTERS read
    PB.IOE.LL0.IOEL_CONFIG = TGT0[0x601180A]; // ELL Configuration Register
    PB.IOE.LL0.IOEL_REPLAY_THRESHOLD = TGT0[0x6011818]; // ELL Replay Threshold Register
    PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD = TGT0[0x6011819]; // ELL SL ECC Threshold Register
    if (fapi2::ATTR_LINK_TRAIN[TGT0] == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH)
    {
        PB.IOE.LL0.IOEL_CONFIG.insert<0, 1, 63, uint64_t>(0x1)
    }
    else
    {
        PB.IOE.LL0.IOEL_CONFIG.insert<0, 1, 63, uint64_t>(0x0)
    }
    if    ((l_chip_id == 0x5 && l_chip_ec == 0x20)
        || (l_chip_id == 0x5 && l_chip_ec == 0x21)
        || (l_chip_id == 0x5 && l_chip_ec == 0x22)
        || (l_chip_id == 0x5 && l_chip_ec == 0x23)
        || (l_chip_id == 0x6 && l_chip_ec == 0x10)
        || (l_chip_id == 0x6 && l_chip_ec == 0x11)
        || (l_chip_id == 0x6 && l_chip_ec == 0x12)
        || (l_chip_id == 0x6 && l_chip_ec == 0x13)
        || (l_chip_id == 0x7 && l_chip_ec == 0x10))
    {
        PB.IOE.LL0.IOEL_CONFIG.insert<11, 5, 59, uint64_t>(0x0F)
        PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD.insert<8, 3, 61, uint64_t>(0b111)
    }
    if (l_chip_id == 0x5 && l_chip_ec == 0x10)
    {
        PB.IOE.LL0.IOEL_CONFIG.insert<12, 4, 60, uint64_t>(0x0F)
        PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD.insert<8, 2, 62, uint64_t>(0b111)
    }
    PB.IOE.LL0.IOEL_CONFIG.insert<2, 1, 63, uint64_t>(0x1)
    PB.IOE.LL0.IOEL_CONFIG.insert<28, 4, 60, uint64_t>(0xF)
    PB.IOE.LL0.IOEL_CONFIG.insert<4, 1, 63, uint64_t>(0x1)

    PB.IOE.LL0.IOEL_REPLAY_THRESHOLD.insert<8, 3, 61, uint64_t>(0b111)
    PB.IOE.LL0.IOEL_REPLAY_THRESHOLD.insert<4, 4, 60, uint64_t>(0xF)
    PB.IOE.LL0.IOEL_REPLAY_THRESHOLD.insert<0, 4, 60, uint64_t>(0x6)

    PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD.insert<4, 4, 60, uint64_t>(0xF)
    PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD.insert<0, 4, 60, uint64_t>(0x7)
    // REGISTERS write
    TGT0[0x601180A] = PB.IOE.LL0.IOEL_CONFIG;
    TGT0[0x6011818] = PB.IOE.LL0.IOEL_REPLAY_THRESHOLD;
    TGT0[0x6011819] = PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD;
}

fapi2::ReturnCode p9_fbc_ioe_tl_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> &TGT1)
{
    fapi2::ATTR_EC_Type l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG;
    fapi2::ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS_Type l_TGT0_ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS;
    fapi2::ATTR_FREQ_X_MHZ_Type l_TGT1_ATTR_FREQ_X_MHZ;
    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE_Type l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE;
    fapi2::ATTR_CHIP_EC_FEATURE_HW384245_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW384245;

    l_chip_id = fapi2::ATTR_NAME[TGT0];
    l_chip_ec = fapi2::ATTR_EC[TGT0];
    l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[TGT0];
    l_TGT0_ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS = fapi2::ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS[TGT0];
    l_TGT1_ATTR_FREQ_X_MHZ = fapi2::ATTR_FREQ_X_MHZ[TGT1];
    l_TGT1_ATTR_FREQ_PB_MHZ = fapi2::ATTR_FREQ_PB_MHZ[TGT1];
    l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE = fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE[TGT1];
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW384245 = fapi2::ATTR_CHIP_EC_FEATURE_HW384245[TGT0];

    uint64_t l_def_DD2X_PARTS = l_TGT0_ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS != 1;
    uint64_t l_def_DD2_LO_LIMIT_D = l_TGT1_ATTR_FREQ_X_MHZ * 10;
    uint64_t l_def_DD2_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 82;
    uint64_t l_def_DD1_LO_LIMIT_D = l_TGT1_ATTR_FREQ_X_MHZ * 100;
    uint64_t l_def_DD1_LO_LIMIT_H = l_def_DD1_LO_LIMIT_D / 2;
    uint64_t l_def_DD1_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 1075;
    uint64_t l_def_DD1_LO_LIMIT_R = l_def_DD1_LO_LIMIT_N % l_def_DD1_LO_LIMIT_D;
    uint64_t l_def_DD1_PARTS = l_TGT0_ATTR_CHIP_EC_FEATURE_DD1_FBC_SETTINGS == 1;
    uint64_t l_def_DD1_LO_LIMIT_P = l_def_DD1_LO_LIMIT_D % 2;
    uint64_t l_def_OPTICS_IS_A_BUS = l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_SMP_OPTICS_MODE_OPTICS_IS_A_BUS;
    uint64_t l_def_X0_ENABLED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[0] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_X1_ENABLED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_X2_ENABLED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[2] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_X0_IS_PAIRED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[0] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE;
    uint64_t l_def_X1_IS_PAIRED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE;
    uint64_t l_def_X2_IS_PAIRED = l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[2] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE;

    // REGISTERS read
    PB.IOE.SCOM.PB_FP01_CFG = TGT0[0x501340A]; // Processor bus Electrical Framer/Parser 01 configuration register
    PB.IOE.SCOM.PB_FP23_CFG = TGT0[0x501340B]; // Power Bus Electrical Framer/Parser 23 Configuration Register
    PB.IOE.SCOM.PB_FP45_CFG = TGT0[0x501340C]; // Power Bus Electrical Framer/Parser 45 Configuration Register
    PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG = TGT0[0x5013410]; // Power Bus Electrical Link Data Buffer 01 Configuration Register
    PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG = TGT0[0x5013411]; // Power Bus Electrical Link Data Buffer 23 Configuration Register
    PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG = TGT0[0x5013412]; // Power Bus Electrical Link Data Buffer 45 Configuration Register
    PB.IOE.SCOM.PB_MISC_CFG = TGT0[0x5013423]; // Power Bus Electrical Miscellaneous Configuration Register
    PB.IOE.SCOM.PB_TRACE_CFG = TGT0[0x5013424]; // Power Bus Electrical Link Trace Configuration Register
    if (l_def_X0_ENABLED)
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<22, 2, 62, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP01_CFG.insert<12, 8, 56, uint64_t>(0x20)
        PB.IOE.SCOM.PB_FP01_CFG.insert<20, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP01_CFG.insert<25, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP01_CFG.insert<44, 8, 56, uint64_t>(0x20)
        PB.IOE.SCOM.PB_FP01_CFG.insert<52, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP01_CFG.insert<57, 1, 63, uint64_t>(0x0)
    }
    else
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<20, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP01_CFG.insert<25, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP01_CFG.insert<52, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP01_CFG.insert<57, 1, 63, uint64_t>(0x1)
    }

    if (l_def_X0_ENABLED && l_def_DD2X_PARTS)
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP01_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R < l_def_DD1_LO_LIMIT_H)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && l_def_DD1_LO_LIMIT_P))
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP01_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && !l_def_DD1_LO_LIMIT_P)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R > l_def_DD1_LO_LIMIT_H))
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<4, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP01_CFG.insert<36, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }

    if (l_def_X2_ENABLED)
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<22, 2, 62, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP45_CFG.insert<12, 8, 56, uint64_t>(0x20)
        PB.IOE.SCOM.PB_FP45_CFG.insert<20, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP45_CFG.insert<25, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP45_CFG.insert<52, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP45_CFG.insert<57, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP45_CFG.insert<44, 8, 56, uint64_t>(0x20)
    }
    else
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<20, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP45_CFG.insert<25, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP45_CFG.insert<52, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP45_CFG.insert<57, 1, 63, uint64_t>(0x1)
    }
    if (l_def_X2_ENABLED && l_def_DD2X_PARTS)
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP45_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R < l_def_DD1_LO_LIMIT_H)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && l_def_DD1_LO_LIMIT_P))
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP45_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && !l_def_DD1_LO_LIMIT_P)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R > l_def_DD1_LO_LIMIT_H))
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<4, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP45_CFG.insert<36, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }

    if (l_def_X0_ENABLED)
    {
        if (l_def_OPTICS_IS_A_BUS)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<24, 5, 59, uint64_t>(0x10)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<24, 5, 59, uint64_t>(0x1F)
        }
        if (l_TGT0_ATTR_CHIP_EC_FEATURE_HW384245 != 0)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<1, 7, 57, uint64_t>(0x3F)
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<33, 7, 57, uint64_t>(0x3F)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
            PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
        }
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<9, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<41, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<17, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<49, 7, 57, uint64_t>(0x3C)

        PB.IOE.SCOM.PB_TRACE_CFG.insert<0, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<8, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<4, 4, 60, uint64_t>(0b0001)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<12, 4, 60, uint64_t>(0b0001)
    }
    else if (l_def_X1_ENABLED)
    {
        PB.IOE.SCOM.PB_TRACE_CFG.insert<16, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<24, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<20, 4, 60, uint64_t>(0b0001)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<28, 4, 60, uint64_t>(0b0001)
    }
    else if (l_def_X2_ENABLED)
    {
        PB.IOE.SCOM.PB_TRACE_CFG.insert<32, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<40, 4, 60, uint64_t>(0b0100)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<36, 4, 60, uint64_t>(0b0001)
        PB.IOE.SCOM.PB_TRACE_CFG.insert<44, 4, 60, uint64_t>(0b0001)
    }

    if (l_def_X1_ENABLED && l_def_DD2X_PARTS)
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP23_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / l_def_DD2_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R < l_def_DD1_LO_LIMIT_H)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && l_def_DD1_LO_LIMIT_P))
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP23_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }
    else if ((l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R == l_def_DD1_LO_LIMIT_H && !l_def_DD1_LO_LIMIT_P)
          || (l_def_X0_ENABLED && l_def_DD1_PARTS && l_def_DD1_LO_LIMIT_R > l_def_DD1_LO_LIMIT_H))
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<4, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
        PB.IOE.SCOM.PB_FP23_CFG.insert<36, 8, 56, uint64_t>(0x19 - (l_def_DD1_LO_LIMIT_N / l_def_DD1_LO_LIMIT_D))
    }


    if(l_def_X1_ENABLED)
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<22, 2, 62, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP23_CFG.insert<12, 8, 56, uint64_t>(0x20)
        PB.IOE.SCOM.PB_FP23_CFG.insert<20, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP23_CFG.insert<25, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP23_CFG.insert<44, 8, 56, uint64_t>(0x20)
        PB.IOE.SCOM.PB_FP23_CFG.insert<52, 1, 63, uint64_t>(0x0)
        PB.IOE.SCOM.PB_FP23_CFG.insert<57, 1, 63, uint64_t>(0x0)
        if (l_def_OPTICS_IS_A_BUS)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<24, 5, 59, uint64_t>(0x10)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<24, 5, 59, uint64_t>(0x1F)
        }
        if (l_TGT0_ATTR_CHIP_EC_FEATURE_HW384245 != 0)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<1, 7, 57, uint64_t>(0x3F)
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<33, 7, 57, uint64_t>(0x3F)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
            PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
        }
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<9, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<41, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<17, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<49, 7, 57, uint64_t>(0x3C)
    }
    else
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<20, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP23_CFG.insert<25, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP23_CFG.insert<52, 1, 63, uint64_t>(0x1)
        PB.IOE.SCOM.PB_FP23_CFG.insert<57, 1, 63, uint64_t>(0x1)
    }

    if(l_def_X2_ENABLED)
    {
        if (l_def_OPTICS_IS_A_BUS)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<24, 5, 59, uint64_t>(0x10)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<24, 5, 59, uint64_t>(0x1F)
        }
        if (l_TGT0_ATTR_CHIP_EC_FEATURE_HW384245 != 0)
        {
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<1, 7, 57, uint64_t>(0x3F)
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<33, 7, 57, uint64_t>(0x3F)
        }
        else
        {
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
            PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
        }
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<9, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<41, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<17, 7, 57, uint64_t>(0x3C)
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<49, 7, 57, uint64_t>(0x3C)
    }

    if (l_def_X0_IS_PAIRED)
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<0, 1, 63, uint64_t>(0x1)
    }
    else
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<0, 1, 63, uint64_t>(0x0)
    }
    if (l_def_X1_IS_PAIRED)
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<1, 1, 63, uint64_t>(0x1)
    }
    else
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<1, 1, 63, uint64_t>(0x0)
    }
    if (l_def_X2_IS_PAIRED)
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<2, 1, 63, uint64_t>(0x1)
    }
    else
    {
        PB.IOE.SCOM.PB_MISC_CFG.insert<2, 1, 63, uint64_t>(0x0)
    }

    // REGISTERS write
    TGT0[0x501340A] = PB.IOE.SCOM.PB_FP01_CFG;
    TGT0[0x501340B] = PB.IOE.SCOM.PB_FP23_CFG;
    TGT0[0x501340C] = PB.IOE.SCOM.PB_FP45_CFG;
    TGT0[0x5013410] = PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG;
    TGT0[0x5013411] = PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG;
    TGT0[0x5013412] = PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG;
    TGT0[0x5013423] = PB.IOE.SCOM.PB_MISC_CFG;
    TGT0[0x5013424] = PB.IOE.SCOM.PB_TRACE_CFG;
}

fapi2::ReturnCode p9_fbc_no_hp_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT0,
                                    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> &TGT1)
{
    fapi2::ATTR_EC_Type l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    fapi2::ATTR_PROC_FABRIC_A_LINKS_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_A_LINKS_CNFG;
    fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG;
    fapi2::ATTR_PROC_EPS_TABLE_TYPE_Type l_TGT1_ATTR_PROC_EPS_TABLE_TYPE;
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE;

    uint64_t l_def_NUM_A_LINKS_CFG = l_TGT0_ATTR_PROC_FABRIC_A_LINKS_CNFG;
    uint64_t l_def_NUM_X_LINKS_CFG = l_TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG;
    uint64_t l_def_IS_FLAT_8 = l_TGT1_ATTR_PROC_EPS_TABLE_TYPE == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_HE_F8;

    l_chip_id = fapi2::ATTR_NAME[TGT0];
    l_chip_ec = fapi2::ATTR_EC[TGT0];
    l_TGT0_ATTR_PROC_FABRIC_A_LINKS_CNFG = fapi2::ATTR_PROC_FABRIC_A_LINKS_CNFG[TGT0];
    l_TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG = fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG[TGT0];
    l_TGT1_ATTR_PROC_EPS_TABLE_TYPE = fapi2::ATTR_PROC_EPS_TABLE_TYPE[TGT1];
    l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE = fapi2::ATTR_PROC_FABRIC_PUMP_MODE[TGT1];

    // REGISTERS read
    PB.COM.PB_WEST_MODE = TGT0[0x501180A]; // Power Bus PB West Mode Configuration Register
    PB.COM.PB_CENT_MODE = TGT0[0x5011C0A]; // Power Bus PB CENT Mode Register
    PB.COM.PB_CENT_GP_CMD_RATE_DP0 = TGT0[0x5011C26]; // Power Bus PB CENT GP command RATE DP0 Register
    PB.COM.PB_CENT_GP_CMD_RATE_DP1 = TGT0[0x5011C27]; // Power Bus PB CENT GP command RATE DP1 Register
    PB.COM.PB_CENT_RGP_CMD_RATE_DP0 = TGT0[0x5011C28]; // Power Bus PB CENT RGP command RATE DP0 Register
    PB.COM.PB_CENT_RGP_CMD_RATE_DP1 = TGT0[0x5011C29]; // Power Bus PB CENT RGP command RATE DP1 Register
    PB.COM.PB_CENT_SP_CMD_RATE_DP0 = TGT0[0x5011C2A]; // Power Bus PB CENT SP command RATE DP0 Register
    PB.COM.PB_CENT_SP_CMD_RATE_DP1 = TGT0[0x5011C2B]; // Power Bus PB CENT SP command RATE DP1 Register
    PB.COM.PB_EAST_MODE = TGT0[0x501200A]; // Power Bus PB East Mode Configuration Register

    if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP
    || (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG == 0))
    {
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x0)

        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x0)
    }
    else if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG > 0 && l_def_NUM_X_LINKS_CFG < 3)
    {
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x17)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x1C)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x24)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x34)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x48)

        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x19)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x1F)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x3A)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x50)
    }
    else if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG > 2)
    {
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x32)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x40)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x5C)
        PB.COM.PB_CENT_GP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x80)

        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x2F)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x3B)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x4C)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x6D)
        PB.COM.PB_CENT_GP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x98)
    }

    if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG == 0)
    {
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x0)

        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x0)

        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x0)

        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x0)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x0)
    }
    else if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG > 0 && l_def_NUM_X_LINKS_CFG < 3)
    {
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0xC)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x12)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x18)

        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0xC)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x12)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x18)

        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0xC)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x12)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x18)

        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0xC)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x12)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x18)
    }
    else if ((l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG == 3)
          || (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG == 0))
    {
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0xD)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x10)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x1D)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x28)

        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0xD)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x10)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x1D)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x28)

        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x7)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0xD)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x10)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x1D)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x28)

        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x7)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0xD)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x10)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x1D)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x28)
    }
    else if ((l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_IS_FLAT_8)
          || (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG > 0 && l_def_NUM_X_LINKS_CFG < 3))
    {
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x17)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x1C)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x24)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x34)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x48)

        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x19)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x1F)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x3A)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x50)

        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0xC)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x12)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x17)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x1C)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x24)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x34)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x48)

        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0xD)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x19)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x1F)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x3A)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x50)
    }
    else if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_NUM_X_LINKS_CFG > 2)
    {
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x3)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x6)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x32)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x40)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x5C)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x80)

        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0x4)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x5)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x2F)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x3B)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x4C)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x6D)
        PB.COM.PB_CENT_RGP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x98)

        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<0, 8, 56, uint64_t>(0x8)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<8, 8, 56, uint64_t>(0x14)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<16, 8, 56, uint64_t>(0x1F)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<24, 8, 56, uint64_t>(0x28)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<32, 8, 56, uint64_t>(0x32)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<40, 8, 56, uint64_t>(0x40)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<48, 8, 56, uint64_t>(0x5C)
        PB.COM.PB_CENT_SP_CMD_RATE_DP0.insert<56, 8, 56, uint64_t>(0x80)

        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<0, 8, 56, uint64_t>(0xA)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<8, 8, 56, uint64_t>(0x18)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<16, 8, 56, uint64_t>(0x25)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<24, 8, 56, uint64_t>(0x2F)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<32, 8, 56, uint64_t>(0x3B)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<40, 8, 56, uint64_t>(0x4C)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<48, 8, 56, uint64_t>(0x6D)
        PB.COM.PB_CENT_SP_CMD_RATE_DP1.insert<56, 8, 56, uint64_t>(0x98)
    }

    if (l_def_NUM_X_LINKS_CFG == 0 && l_def_NUM_A_LINKS_CFG == 0)
    {
        PB.COM.PB_WEST_MODE.insert<4, 1, 61, uint64_t>(0x7)
        PB.COM.PB_CENT_MODE.insert<4, 1, 62, uint64_t>(0x7)
        PB.COM.PB_EAST_MODE.insert<4, 1, 63, uint64_t>(0x7)
    }
    else
    {
        PB.COM.PB_WEST_MODE.insert<4, 1, 61, uint64_t>(0x0)
        PB.COM.PB_CENT_MODE.insert<4, 1, 62, uint64_t>(0x0)
        PB.COM.PB_EAST_MODE.insert<4, 1, 63, uint64_t>(0x0)
    }
    if (l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP && l_def_IS_FLAT_8)
    {
        PB.COM.PB_WEST_MODE.insert<16, 7, 43, uint64_t>(0x7cf9f)
        PB.COM.PB_WEST_MODE.insert<23, 7, 43, uint64_t>(0x81020)

        PB.COM.PB_CENT_MODE.insert<16, 7, 50, uint64_t>(0x7cf9f)
        PB.COM.PB_CENT_MODE.insert<23, 7, 50, uint64_t>(0x81020)

        PB.COM.PB_EAST_MODE.insert<16, 7, 57, uint64_t>(0x7cf9f)
        PB.COM.PB_EAST_MODE.insert<23, 7, 57, uint64_t>(0x81020)
    }
    else
    {
        PB.COM.PB_WEST_MODE.insert<16, 7, 43, uint64_t>(0xfdfbf)
        PB.COM.PB_WEST_MODE.insert<23, 7, 43, uint64_t>(0xfdfbf)

        PB.COM.PB_CENT_MODE.insert<16, 7, 50, uint64_t>(0xfdfbf)
        PB.COM.PB_CENT_MODE.insert<23, 7, 50, uint64_t>(0xfdfbf)

        PB.COM.PB_EAST_MODE.insert<16, 7, 57, uint64_t>(0xfdfbf)
        PB.COM.PB_EAST_MODE.insert<23, 7, 57, uint64_t>(0xfdfbf)
    }
    if (l_chip_id == 0x7 && l_chip_ec == 0x10)
    {
        PB.COM.PB_WEST_MODE.insert<49, 1, 61, uint64_t>(0x7)
        PB.COM.PB_CENT_MODE.insert<49, 1, 62, uint64_t>(0x7)
        PB.COM.PB_EAST_MODE.insert<49, 1, 63, uint64_t>(0x7)
    }

    PB.COM.PB_WEST_MODE.insert<30, 6, 46, uint64_t>(0x2aaaa)
    PB.COM.PB_CENT_MODE.insert<30, 6, 52, uint64_t>(0x2aaaa)
    PB.COM.PB_EAST_MODE.insert<30, 6, 58, uint64_t>(0x2aaaa)

    // REGISTERS write
    TGT0[0x501180A] = PB.COM.PB_WEST_MODE;
    TGT0[0x5011C0A] = PB.COM.PB_CENT_MODE;
    TGT0[0x5011C26] = PB.COM.PB_CENT_GP_CMD_RATE_DP0;
    TGT0[0x5011C27] = PB.COM.PB_CENT_GP_CMD_RATE_DP1;
    TGT0[0x5011C28] = PB.COM.PB_CENT_RGP_CMD_RATE_DP0;
    TGT0[0x5011C29] = PB.COM.PB_CENT_RGP_CMD_RATE_DP1;
    TGT0[0x5011C2A] = PB.COM.PB_CENT_SP_CMD_RATE_DP0;
    TGT0[0x5011C2B] = PB.COM.PB_CENT_SP_CMD_RATE_DP1;
    TGT0[0x501200A] = PB.COM.PB_EAST_MODE;
}

```

## 8.10 proc_xbus_scominit: Apply scom inits to Xbus

This step can be viewed [here](https://gitlab.com/3mdeb/openpower/docs/-/blob/master/nest_configuration_8.10.md)

## 8.11 proc_chiplet_enable_ridi: Enable RI/DI for xbus
