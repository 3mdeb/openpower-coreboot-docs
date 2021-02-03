# 8.9 proc_chiplet_scominit: Scom inits to all chiplets (sans Quad)

```cpp
void *call_proc_chiplet_fabric_scominit(void *io_pArgs)
{
    // i_targetType = TARGETING::TYPE_PROC
    for each target in getAllChips():
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
    PB.IOE.LL0.IOEL_CONFIG.insert<11, 5, 59, uint64_t>(0x0F)
    PB.IOE.LL0.IOEL_SL_ECC_THRESHOLD.insert<8, 3, 61, uint64_t>(0x7)
    PB.IOE.LL0.IOEL_CONFIG.insert<2, 1, 63, uint64_t>(0x1)
    PB.IOE.LL0.IOEL_CONFIG.insert<28, 4, 60, uint64_t>(0xF)
    PB.IOE.LL0.IOEL_CONFIG.insert<4, 1, 63, uint64_t>(0x1)

    PB.IOE.LL0.IOEL_REPLAY_THRESHOLD.insert<8, 3, 61, uint64_t>(0x7)
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
    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE_Type l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE;

    l_chip_id = fapi2::ATTR_NAME[TGT0];
    l_chip_ec = fapi2::ATTR_EC[TGT0];
    l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[TGT0];
    l_TGT1_ATTR_FREQ_PB_MHZ = fapi2::ATTR_FREQ_PB_MHZ[TGT1];
    l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE = fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE[TGT1];

    uint64_t l_def_DD2_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 82;
    uint64_t l_def_DD1_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 1075;
    uint64_t l_def_DD1_LO_LIMIT_R = l_def_DD1_LO_LIMIT_N % 200000;
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

    if (l_def_X0_ENABLED)
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
        PB.IOE.SCOM.PB_FP01_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
    }
    else
    {
        PB.IOE.SCOM.PB_FP01_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
        PB.IOE.SCOM.PB_FP01_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
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
    if (l_def_X2_ENABLED)
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
        PB.IOE.SCOM.PB_FP45_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
    }
    else
    {
        PB.IOE.SCOM.PB_FP45_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
        PB.IOE.SCOM.PB_FP45_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
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
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
        PB.IOE.SCOM.PB_ELINK_DATA_01_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
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

    if (l_def_X1_ENABLED)
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<4, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
        PB.IOE.SCOM.PB_FP23_CFG.insert<36, 8, 56, uint64_t>(0x15 - (l_def_DD2_LO_LIMIT_N / 20000))
    }
    else
    {
        PB.IOE.SCOM.PB_FP23_CFG.insert<4, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
        PB.IOE.SCOM.PB_FP23_CFG.insert<36, 8, 56, uint64_t>(0x1A - (l_def_DD1_LO_LIMIT_N / 200000))
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
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
        PB.IOE.SCOM.PB_ELINK_DATA_23_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
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
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<1, 7, 57, uint64_t>(0x40)
        PB.IOE.SCOM.PB_ELINK_DATA_45_CFG_REG.insert<33, 7, 57, uint64_t>(0x40)
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
