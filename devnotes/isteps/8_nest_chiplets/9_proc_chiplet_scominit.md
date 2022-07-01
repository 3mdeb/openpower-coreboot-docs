### 8.9 proc_chiplet_scominit: Scom inits to all chiplets (sans Quad)

Analysis assumptions:
 * `ATTR_NAME == 0x05`
 * `ATTR_EC >= 0x20`
 * `ATTR_PROC_EPS_TABLE_TYPE == EPS_TYPE_LE`
 * `ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP`
 * `ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[0] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE`
 * `ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[2] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE`
 * `ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH`
 * Reads from SCOM whose values don't matter can be safely omitted.

```cpp
enum {
    P9_FBC_UTILS_MAX_ELECTRICAL_LINKS = 3,
    FBC_IOE_TL_FIR_MASK_X0_NF = 0x00C00C0C00000880ULL,
    FBC_IOE_TL_FIR_MASK_X1_NF = 0x0018030300000440ULL,
    FBC_IOE_TL_FIR_MASK_X2_NF = 0x000300C0C0000220ULL,

    FBC_IOE_TL_FIR_ACTION0 = 0x0000000000000000ULL,
    FBC_IOE_TL_FIR_ACTION1 = 0x0049000000000000ULL,
    FBC_IOE_TL_FIR_MASK = 0xFF24F0303FFFF11FULL,

    PU_PB_CENT_SM0_PB_CENT_FIR_REG = 0x05011C00,
    PU_PB_IOE_FIR_ACTION0_REG = 0x05013406,
    PU_PB_IOE_FIR_ACTION1_REG = 0x05013407,
    PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13 = 13,
    PU_PB_IOE_FIR_MASK_REG = 0x05013403,

    XBUS_LL0_IOEL_FIR_ACTION0_REG = 0x06011806,
    XBUS_LL0_IOEL_FIR_ACTION1_REG = 0x06011807,
    XBUS_LL0_LL0_LL0_IOEL_FIR_MASK_REG = 0x06011803,
    FBC_IOE_DL_FIR_ACTION0 = 0,
    FBC_IOE_DL_FIR_ACTION1 = 0x0303C00000001FFC,
    FBC_IOE_DL_FIR_MASK = 0xFCFC3FFFFFFFE003,
};

// src/import/chips/p9/procedures/hwp/nest/p9_chiplet_fabric_scominit.C
fapi2::ReturnCode p9_chiplet_fabric_scominit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    // apply FBC non-hotplug initfile
    if (p9_fbc_no_hp_scom(i_target))
        die();

    // setup IOE (XBUS FBC IO) TL SCOMs
    if (p9_fbc_ioe_tl_scom(i_target)))
        die();

    // configure TL FIR, only if not already setup by SBE
    fapi2::buffer<uint64_t> fbc_cent_fir_data;
    fapi2::getScom(i_target, PU_PB_CENT_SM0_PB_CENT_FIR_REG, fbc_cent_fir_data);

    if (!fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>()) {
        fapi2::putScom(i_target, PU_PB_IOE_FIR_ACTION0_REG, FBC_IOE_TL_FIR_ACTION0));
        fapi2::putScom(i_target, PU_PB_IOE_FIR_ACTION1_REG, FBC_IOE_TL_FIR_ACTION1));

        fapi2::buffer<uint64_t> fir_mask = FBC_IOE_TL_FIR_MASK;

        const uint64_t x_non_functional_mask[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = {
            FBC_IOE_TL_FIR_MASK_X0_NF,
            FBC_IOE_TL_FIR_MASK_X1_NF,
            FBC_IOE_TL_FIR_MASK_X2_NF
        };

        bool x_functional[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = {
            false,
            true,
            false
        };

        for (uint8_t ll = 0; ll < P9_FBC_UTILS_MAX_ELECTRICAL_LINKS; ll++) {
            if (!x_functional[ll])
                fir_mask |= x_non_functional_mask[ll];
        }

        fapi2::putScom(i_target, PU_PB_IOE_FIR_MASK_REG, fir_mask);
    }

    // setup IOE (XBUS FBC IO) DL SCOMs
    for (auto iter = xbus_chiplets.begin(); iter != xbus_chiplets.end(); iter++) {
        if (p9_fbc_ioe_dl_scom(*iter, i_target))
            die();

        // configure DL FIR, only if not already setup by SBE
        if (!fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>()) {
            fapi2::putScom(*iter, XBUS_LL0_IOEL_FIR_ACTION0_REG, FBC_IOE_DL_FIR_ACTION0);
            fapi2::putScom(*iter, XBUS_LL0_IOEL_FIR_ACTION1_REG, FBC_IOE_DL_FIR_ACTION1));
            fapi2::putScom(*iter, XBUS_LL0_LL0_LL0_IOEL_FIR_MASK_REG, FBC_IOE_DL_FIR_MASK);
        }
    }

    // set FBC optics config mode attribute
    /// skipped because Talos has no optics ///
}

enum {
    // Power Bus PB West Mode Configuration Register
    PB_WEST_MODE             = 0x501180A,
    // Power Bus PB CENT Mode Register
    PB_CENT_MODE             = 0x5011C0A,
    // Power Bus PB CENT GP command RATE DP0 Register
    PB_CENT_GP_CMD_RATE_DP0  = 0x5011C26,
    // Power Bus PB CENT GP command RATE DP1 Register
    PB_CENT_GP_CMD_RATE_DP1  = 0x5011C27,
    // Power Bus PB CENT RGP command RATE DP0 Register
    PB_CENT_RGP_CMD_RATE_DP0 = 0x5011C28,
    // Power Bus PB CENT RGP command RATE DP1 Register
    PB_CENT_RGP_CMD_RATE_DP1 = 0x5011C29,
    // Power Bus PB CENT SP command RATE DP0 Register
    PB_CENT_SP_CMD_RATE_DP0  = 0x5011C2A,
    // Power Bus PB CENT SP command RATE DP1 Register
    PB_CENT_SP_CMD_RATE_DP1  = 0x5011C2B,
    // Power Bus PB East Mode Configuration Register
    PB_EAST_MODE             = 0x501200A,
};

enum {
    // Processor bus Electrical Framer/Parser 01 configuration register
    PB_FP01_CFG              = 0x501340A,
    // Power Bus Electrical Framer/Parser 23 Configuration Register
    PB_FP23_CFG              = 0x501340B,
    // Power Bus Electrical Framer/Parser 45 Configuration Register
    PB_FP45_CFG              = 0x501340C,
    // Power Bus Electrical Link Data Buffer 01 Configuration Register
    PB_ELINK_DATA_01_CFG_REG = 0x5013410,
    // Power Bus Electrical Link Data Buffer 23 Configuration Register
    PB_ELINK_DATA_23_CFG_REG = 0x5013411,
    // Power Bus Electrical Link Data Buffer 45 Configuration Register
    PB_ELINK_DATA_45_CFG_REG = 0x5013412,
    // Power Bus Electrical Miscellaneous Configuration Register
    PB_MISC_CFG              = 0x5013423,
    // Power Bus Electrical Link Trace Configuration Register
    PB_TRACE_CFG             = 0x5013424,
};

enum {
    // ELL Configuration Register
    IOEL_CONFIG           = 0x601180A,
    // ELL Replay Threshold Register
    IOEL_REPLAY_THRESHOLD = 0x6011818,
    // ELL SL ECC Threshold Register
    IOEL_SL_ECC_THRESHOLD = 0x6011819,
};

// src/import/chips/p9/procedures/hwp/initfiles/p9_fbc_no_hp_scom.C
fapi2::ReturnCode p9_fbc_no_hp_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG_Type TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG, TGT0, TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG);
    uint64_t NUM_X_LINKS_CFG = TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG;

    fapi2::buffer<uint64_t> scom_buffer;
    {
        fapi2::getScom(TGT0, PB_WEST_MODE, scom_buffer);

        if (NUM_X_LINKS_CFG == 0) {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON = 0x7;
            scom_buffer.insert<4, 1, 61, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON);
        } else {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF = 0x0;
            scom_buffer.insert<4, 1, 61, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF);
        }

        constexpr auto PB_COM_PB_CFG_SP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<16, 7, 43, uint64_t>(PB_COM_PB_CFG_SP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_GP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<23, 7, 43, uint64_t>(PB_COM_PB_CFG_GP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_LCL_HW_MARK_CNT_42 = 0x2aaaa;
        scom_buffer.insert<30, 6, 46, uint64_t>(PB_COM_PB_CFG_LCL_HW_MARK_CNT_42);

        fapi2::putScom(TGT0, PB_WEST_MODE, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_CENT_MODE, scom_buffer);

        if (NUM_X_LINKS_CFG == 0) {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON = 0x7;
            scom_buffer.insert<4, 1, 62, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON);
        } else {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF = 0x0;
            scom_buffer.insert<4, 1, 62, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF);
        }

        constexpr auto PB_COM_PB_CFG_SP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<16, 7, 50, uint64_t>(PB_COM_PB_CFG_SP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_GP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<23, 7, 50, uint64_t>(PB_COM_PB_CFG_GP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_LCL_HW_MARK_CNT_42 = 0x2aaaa;
        scom_buffer.insert<30, 6, 52, uint64_t>(PB_COM_PB_CFG_LCL_HW_MARK_CNT_42);

        fapi2::putScom(TGT0, PB_CENT_MODE, scom_buffer);
    }
    {
        scom_buffer = 0;
        fapi2::putScom(TGT0, PB_CENT_GP_CMD_RATE_DP0, scom_buffer);
    }
    {
        scom_buffer = 0;
        fapi2::putScom(TGT0, PB_CENT_GP_CMD_RATE_DP1, scom_buffer);
    }
    {
        scom_buffer = 0;
        if (NUM_X_LINKS_CFG != 0)
            scom_buffer = 0x030406080A0C1218;

        fapi2::putScom(TGT0, PB_CENT_RGP_CMD_RATE_DP0, scom_buffer);
        // HB updates PB_CENT_RGP_CMD_RATE_DP1 in here
        fapi2::putScom(TGT0, PB_CENT_SP_CMD_RATE_DP0, scom_buffer);
        fapi2::putScom(TGT0, PB_CENT_SP_CMD_RATE_DP1, scom_buffer);
    }
    {
        scom_buffer = 0;
        if (NUM_X_LINKS_CFG != 0)
            scom_buffer = 0x040508080A0C1218;

        fapi2::putScom(TGT0, PB_CENT_RGP_CMD_RATE_DP1, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_EAST_MODE, scom_buffer);

        if (NUM_X_LINKS_CFG == 0) {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON = 0x7;
            scom_buffer.insert<4, 1, 63, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_ON);
        } else {
            constexpr auto PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF = 0x0;
            scom_buffer.insert<4, 1, 63, uint64_t>(PB_COM_PB_CFG_CHIP_IS_SYSTEM_OFF);
        }

        constexpr auto PB_COM_PB_CFG_SP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<16, 7, 57, uint64_t>(PB_COM_PB_CFG_SP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_GP_HW_MARK_CNT_63 = 0xfdfbf;
        scom_buffer.insert<23, 7, 57, uint64_t>(PB_COM_PB_CFG_GP_HW_MARK_CNT_63);

        constexpr auto PB_COM_PB_CFG_LCL_HW_MARK_CNT_42 = 0x2aaaa;
        scom_buffer.insert<30, 6, 58, uint64_t>(PB_COM_PB_CFG_LCL_HW_MARK_CNT_42);

        fapi2::putScom(TGT0, PB_EAST_MODE, scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_fbc_ioe_tl_scom.C
fapi2::ReturnCode p9_fbc_ioe_tl_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    /// Frequency of XBus, 2000 MHz for Nimbus DD2 ///
    uint32_t TGT1_ATTR_FREQ_X_MHZ = 2000;

    uint64_t DD2_LO_LIMIT_D = (TGT1_ATTR_FREQ_X_MHZ * 10);

    fapi2::ATTR_FREQ_PB_MHZ_Type TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, TGT1_ATTR_FREQ_PB_MHZ);

    uint64_t DD2_LO_LIMIT_N = (TGT1_ATTR_FREQ_PB_MHZ * 82);

    uint8_t X1_ENABLED = ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE;
    uint8_t X1_IS_PAIRED = ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE;

    fapi2::buffer<uint64_t> scom_buffer;
    {
        fapi2::getScom(TGT0, PB_FP01_CFG, scom_buffer);

        constexpr auto PB_IOE_SCOM_FP0_FMR_DISABLE_ON = 0x1;
        scom_buffer.insert<20, 1, 63, uint64_t>(PB_IOE_SCOM_FP0_FMR_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP0_PRS_DISABLE_ON = 0x1;
        scom_buffer.insert<25, 1, 63, uint64_t>(PB_IOE_SCOM_FP0_PRS_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP1_FMR_DISABLE_ON = 0x1;
        scom_buffer.insert<52, 1, 63, uint64_t>(PB_IOE_SCOM_FP1_FMR_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP1_PRS_DISABLE_ON = 0x1;
        scom_buffer.insert<57, 1, 63, uint64_t>(PB_IOE_SCOM_FP1_PRS_DISABLE_ON);

        fapi2::putScom(TGT0, PB_FP01_CFG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_FP23_CFG, scom_buffer);

        if (X1_ENABLED) {
            scom_buffer.insert<22, 2, 62, uint64_t>(0x1);
            scom_buffer.insert<12, 8, 56, uint64_t>(0x20);

            constexpr auto PB_IOE_SCOM_FP2_FMR_DISABLE_OFF = 0x0;
            scom_buffer.insert<20, 1, 63, uint64_t>(PB_IOE_SCOM_FP2_FMR_DISABLE_OFF);

            constexpr auto PB_IOE_SCOM_FP2_PRS_DISABLE_OFF = 0x0;
            scom_buffer.insert<25, 1, 63, uint64_t>(PB_IOE_SCOM_FP2_PRS_DISABLE_OFF);

            scom_buffer.insert<4, 8, 56, uint64_t>(0x15 - (DD2_LO_LIMIT_N / DD2_LO_LIMIT_D));
            scom_buffer.insert<44, 8, 56, uint64_t>(0x20);

            constexpr auto PB_IOE_SCOM_FP3_FMR_DISABLE_OFF = 0x0;
            scom_buffer.insert<52, 1, 63, uint64_t>(PB_IOE_SCOM_FP3_FMR_DISABLE_OFF);

            constexpr auto PB_IOE_SCOM_FP3_PRS_DISABLE_OFF = 0x0;
            scom_buffer.insert<57, 1, 63, uint64_t>(PB_IOE_SCOM_FP3_PRS_DISABLE_OFF);

            scom_buffer.insert<36, 8, 56, uint64_t>(0x15 - (DD2_LO_LIMIT_N / DD2_LO_LIMIT_D));
        } else {
            constexpr auto PB_IOE_SCOM_FP2_FMR_DISABLE_ON = 0x1;
            scom_buffer.insert<20, 1, 63, uint64_t>(PB_IOE_SCOM_FP2_FMR_DISABLE_ON);

            constexpr auto PB_IOE_SCOM_FP2_PRS_DISABLE_ON = 0x1;
            scom_buffer.insert<25, 1, 63, uint64_t>(PB_IOE_SCOM_FP2_PRS_DISABLE_ON);

            constexpr auto PB_IOE_SCOM_FP3_FMR_DISABLE_ON = 0x1;
            scom_buffer.insert<52, 1, 63, uint64_t>(PB_IOE_SCOM_FP3_FMR_DISABLE_ON);

            constexpr auto PB_IOE_SCOM_FP3_PRS_DISABLE_ON = 0x1;
            scom_buffer.insert<57, 1, 63, uint64_t>(PB_IOE_SCOM_FP3_PRS_DISABLE_ON);
        }

        fapi2::putScom(TGT0, PB_FP23_CFG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_FP45_CFG, scom_buffer);

        constexpr auto PB_IOE_SCOM_FP4_FMR_DISABLE_ON = 0x1;
        scom_buffer.insert<20, 1, 63, uint64_t>(PB_IOE_SCOM_FP4_FMR_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP4_PRS_DISABLE_ON = 0x1;
        scom_buffer.insert<25, 1, 63, uint64_t>(PB_IOE_SCOM_FP4_PRS_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP5_FMR_DISABLE_ON = 0x1;
        scom_buffer.insert<52, 1, 63, uint64_t>(PB_IOE_SCOM_FP5_FMR_DISABLE_ON);

        constexpr auto PB_IOE_SCOM_FP5_PRS_DISABLE_ON = 0x1;
        scom_buffer.insert<57, 1, 63, uint64_t>(PB_IOE_SCOM_FP5_PRS_DISABLE_ON);

        fapi2::putScom(TGT0, PB_FP45_CFG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_ELINK_DATA_01_CFG_REG, scom_buffer);

        fapi2::putScom(TGT0, PB_ELINK_DATA_01_CFG_REG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_ELINK_DATA_23_CFG_REG, scom_buffer);

        if (is_xbus_active) {
            scom_buffer.insert<24, 5, 59, uint64_t>(0x1F);
            scom_buffer.insert<1, 7, 57, uint64_t>(0x40);
            scom_buffer.insert<33, 7, 57, uint64_t>(0x40);
            scom_buffer.insert<9, 7, 57, uint64_t>(0x3C);
            scom_buffer.insert<41, 7, 57, uint64_t>(0x3C);
            scom_buffer.insert<17, 7, 57, uint64_t>(0x3C);
            scom_buffer.insert<49, 7, 57, uint64_t>(0x3C);
        }

        fapi2::putScom(TGT0, PB_ELINK_DATA_23_CFG_REG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_ELINK_DATA_45_CFG_REG, scom_buffer);

        fapi2::putScom(TGT0, PB_ELINK_DATA_45_CFG_REG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_MISC_CFG, scom_buffer);

        constexpr auto PB_IOE_SCOM_PB_CFG_IOE01_IS_LOGICAL_PAIR_OFF = 0x0;
        scom_buffer.insert<0, 1, 63, uint64_t>(PB_IOE_SCOM_PB_CFG_IOE01_IS_LOGICAL_PAIR_OFF);

        constexpr auto PB_IOE_SCOM_PB_CFG_IOE23_IS_LOGICAL_PAIR = X1_IS_PAIRED;
        scom_buffer.insert<1, 1, 63, uint64_t>(PB_IOE_SCOM_PB_CFG_IOE23_IS_LOGICAL_PAIR);

        constexpr auto PB_IOE_SCOM_PB_CFG_IOE45_IS_LOGICAL_PAIR_OFF = 0x0;
        scom_buffer.insert<2, 1, 63, uint64_t>(PB_IOE_SCOM_PB_CFG_IOE45_IS_LOGICAL_PAIR_OFF);

        fapi2::putScom(TGT0, PB_MISC_CFG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, PB_TRACE_CFG, scom_buffer);

        if (X1_ENABLED) {
            scom_buffer.insert<16, 4, 60, uint64_t>(0x4);
            scom_buffer.insert<24, 4, 60, uint64_t>(0x4);
            scom_buffer.insert<20, 4, 60, uint64_t>(0x1);
            scom_buffer.insert<28, 4, 60, uint64_t>(0x1);
        }

        fapi2::putScom(TGT0, PB_TRACE_CFG, scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_fbc_ioe_dl_scom.C
fapi2::ReturnCode p9_fbc_ioe_dl_scom(const fapi2::Target<fapi2::TARGET_TYPE_XBUS>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT1)
{
    fapi2::buffer<uint64_t> scom_buffer;
    {
        fapi2::getScom(TGT0, IOEL_CONFIG, scom_buffer);

        constexpr auto PB_IOE_LL1_CONFIG_LINK_PAIR_ON = 0x1;
        scom_buffer.insert<0, 1, 63, uint64_t>(PB_IOE_LL1_CONFIG_LINK_PAIR_ON);

        constexpr auto PB_IOE_LL1_CONFIG_CRC_LANE_ID_ON = 0x1;
        scom_buffer.insert<2, 1, 63, uint64_t>(PB_IOE_LL1_CONFIG_CRC_LANE_ID_ON);

        scom_buffer.insert<11, 5, 59, uint64_t>(0xF);
        scom_buffer.insert<28, 4, 60, uint64_t>(0xF);

        constexpr auto PB_IOE_LL1_CONFIG_SL_UE_CRC_ERR_ON = 0x1;
        scom_buffer.insert<4, 1, 63, uint64_t>(PB_IOE_LL1_CONFIG_SL_UE_CRC_ERR_ON);

        fapi2::putScom(TGT0, IOEL_CONFIG, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, IOEL_REPLAY_THRESHOLD, scom_buffer);

        scom_buffer.insert<8, 3, 61, uint64_t>(0x7);
        scom_buffer.insert<4, 4, 60, uint64_t>(0xF);
        scom_buffer.insert<0, 4, 60, uint64_t>(0x6);

        fapi2::putScom(TGT0, IOEL_REPLAY_THRESHOLD, scom_buffer);
    }
    {
        fapi2::getScom(TGT0, IOEL_SL_ECC_THRESHOLD, scom_buffer);

        scom_buffer.insert<8, 3, 61, uint64_t>(0x7);
        scom_buffer.insert<4, 4, 60, uint64_t>(0xF);
        scom_buffer.insert<0, 4, 60, uint64_t>(0x7);

        fapi2::putScom(TGT0, IOEL_SL_ECC_THRESHOLD, scom_buffer);
    }
}
```
