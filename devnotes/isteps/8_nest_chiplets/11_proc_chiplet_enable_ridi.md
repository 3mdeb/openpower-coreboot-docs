```cpp
void *call_proc_xbus_enable_ridi(void *io_pArgs)
{
    // Make the FAPI call to p9_xbus_enable_ridi
    fapiHWPCallWrapper(P9_XBUS_ENABLE_RIDI, HWPF_COMP_ID, TYPE_PROC);
    if(INITSERVICE::isSMPWrapConfig())
    {
        for each target:
        {
            // Make the FAPI call to p9_chiplet_scominit
            // Make the FAPI call to p9_io_obus_firmask_save_restore, ifprevious call succeeded
            // Make the FAPI call to p9_psi_scominit, ifprevious call succeeded
            // Make the FAPI call to p9_io_obus_scominit, ifprevious call succeeded
            // Make the FAPI call to p9_npu_scominit, ifprevious call succeeded
            // Make the FAPI call to p9_chiplet_enable_ridi, ifprevious call succeeded
               p9_chiplet_scominit(target)
            && p9_io_obus_firmask_save(target, i_target_chip.getChildren<fapi2::TARGET_TYPE_OBUS>(fapi2::TARGET_STATE_FUNCTIONAL));
            && p9_psi_scom(target)
            && p9_io_obus_scominit(target)
            && p9_npu_scominit(target)
            && p9_chiplet_enable_ridi(target);
        }
    }
}

fapi2::ReturnCode p9_io_obus_firmask_save(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target_chip,
        const std::vector<fapi2::Target<fapi2::TARGET_TYPE_OBUS>> i_obus_targets)
{
    fapi2::buffer<uint64_t> l_scomBuffer = 0;
    fapi2::buffer<uint64_t> l_action0Buffer = 0;
    fapi2::buffer<uint64_t> l_action1Buffer = 0;

    // First read the PB IOO fir mask register
    fapi2::getScom(i_target_chip, PU_IOE_PB_IOO_FIR_MASK_REG, l_scomBuffer);

    // Save off scom value we read into ATTR_IO_PB_IOOFIR_MASK
    fapi2::ATTR_IO_PB_IOOFIR_MASK[i_target_chip] = l_scomBuffer;

    // Apply mask required for Hostboot IPL time
    // Set bits 28-35 (see above for more details)
    l_scomBuffer.insertFromRight<PU_IOE_PB_IOO_FIR_MASK_REG_PARSER00_ATTN, SET_LENGTH_8>(SET_BYTE);
    // Set bits 52-59 (see above for more details)
    l_scomBuffer.insertFromRight<PU_IOE_PB_IOO_FIR_MASK_REG_DOB01_ERR, SET_LENGTH_8>(SET_BYTE);
    // Write modified mask back to scom register
    fapi2::putScom(i_target_chip, PU_IOE_PB_IOO_FIR_MASK_REG, l_scomBuffer);

    // Loop through obus targets and save off IOO LFIR
    for(const auto& l_obusTarget : i_obus_targets)
    {
        // For each obus target read the IOOL FIR mask and store it in
        // the ATTR_IO_OLLFIR_MASK attribute for later
        fapi2::getScom(l_obusTarget, OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG, l_scomBuffer);
        FAPI_ATTR_SET(fapi2::ATTR_IO_OLLFIR_MASK, l_obusTarget, l_scomBuffer);
        // For each obus target read the IOOL FIR action registers
        fapi2::getScom(l_obusTarget, OBUS_LL0_PB_IOOL_FIR_ACTION0_REG, l_action0Buffer);
        fapi2::getScom(l_obusTarget, OBUS_LL0_PB_IOOL_FIR_ACTION1_REG, l_action1Buffer);

        // Apply mask required for Hostboot IPL time, we must mask additional
        // bits during IPL time because Hostboot does not know about OBUS
        // peer targets yet. When PRD attempts to handle some of these FIRs
        // it will expect the PEER_TARGET information to be there.

        // Set bits 42-47 ifthe action register indicate the error as recoverable
        for i in range(PB_IOOL_FIR_MASK_REG_FIR_LINK0_NO_SPARE_MASK, OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_TOO_MANY_CRC_ERRORS+1):
        {
            if(l_action0Buffer.getBit(i) == 0 &&
               l_action1Buffer.getBit(i) == 1)
            {
                l_scomBuffer.setBit(i);
            }
        }
        // Set bits 52-59 ifthe action register indicate the error as recoverable
        for i in range(OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK0_CORRECTABLE_ARRAY_ERROR, OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_TOO_MANY_CRC_ERRORS+1)
        {
            if(l_action0Buffer.getBit(i) == 0 &&
               l_action1Buffer.getBit(i) == 1)
            {
                l_scomBuffer.setBit(i);
            }
        }
        // Write modified mask back to scom register
        fapi2::putScom(l_obusTarget, OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG, l_scomBuffer);
    }
}

fapi2::ReturnCode p9_chiplet_scominit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_target)
{
    fapi2::ReturnCode l_rc;
    fapi2::buffer<uint64_t> l_scom_data;
    char l_procTargetStr[fapi2::MAX_ECMD_STRING_LEN];
    char l_chipletTargetStr[fapi2::MAX_ECMD_STRING_LEN];
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    uint8_t l_xbus_present[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = {0};
    uint8_t l_xbus_functional[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = {0};
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_XBUS>> l_xbus_chiplets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_OBUS>> l_obus_chiplets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_MCS>>  l_mcs_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_MI>>   l_mi_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_MC>>   l_mc_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_DMI>>  l_dmi_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_CAPP>> l_capp_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_MCC>>  l_mcc_targets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_OMI>>  l_omi_targets;
    uint8_t l_nmmu_ndd1 = 0;
    uint32_t l_eps_write_cycles_t1 = 0;
    uint32_t l_eps_write_cycles_t2 = 0;
    uint8_t l_npu_enabled = 0;
    uint8_t l_hw461448 = 0;
    uint8_t l_chip_type = 0;

    l_chip_type  = fapi2::ATTR_NAME[i_target];

    // check to see ifNPU region in N3 chiplet partial good data is enabled
    // init PG data to disabled
    fapi2::buffer<uint16_t> l_pg_value = 0xFFFF;
    for (auto l_tgt : i_target.getChildren<fapi2::TARGET_TYPE_PERV>(fapi2::TARGET_FILTER_NEST_WEST, fapi2::TARGET_STATE_FUNCTIONAL))
    {
        uint8_t l_attr_chip_unit_pos = 0;
        l_attr_chip_unit_pos = fapi2::ATTR_CHIP_UNIT_POS[l_tgt];

        if(l_attr_chip_unit_pos == N3_CHIPLET_ID)
        {
            l_pg_value = fapi2::ATTR_PG[l_tgt];
            break;
        }
    }

    // a bit value of 0 in the PG attribute means the associated region is good
    if(!l_pg_value.getBit<N3_PG_NPU_REGION_BIT>())
    {
        l_npu_enabled = 1;
    }

    fapi2::ATTR_PROC_NPU_REGION_ENABLED[i_target] = l_npu_enabled;

    l_xbus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_XBUS>();
    l_obus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_OBUS>();
    l_mcs_targets = i_target.getChildren<fapi2::TARGET_TYPE_MCS>();
    l_mi_targets = i_target.getChildren<fapi2::TARGET_TYPE_MI>();
    l_dmi_targets = i_target.getChildren<fapi2::TARGET_TYPE_DMI>();
    l_mc_targets = i_target.getChildren<fapi2::TARGET_TYPE_MC>();
    l_omi_targets = i_target.getChildren<fapi2::TARGET_TYPE_OMI>();

    if(l_mcs_targets.size())
    {
        for (const auto &l_mcs_target : l_mcs_targets)
        {
            FAPI_EXEC_HWP(l_rc, p9n_mcs_scom, l_mcs_target, FAPI_SYSTEM, i_target, l_mcs_target.getParent<fapi2::TARGET_TYPE_MCBIST>());
        }
    }
    else if(l_mc_targets.size())
    {
        for (const auto &l_mi_target : l_mi_targets)
        {
            // HW461448 Configure MC WAT for Cumulus using indirect scoms
            l_hw461448 = fapi2::ATTR_CHIP_EC_FEATURE_HW461448[i_target];
            if(l_hw461448)
            {
                getScom(l_mi_target, MCS_MCWATCNTL, l_scom_data);
                // MCWATDATA0
                l_scom_data.insertFromRight<MCS_MCWATCNTL_WAT_CNTL_REG_SEL, MCS_MCWATCNTL_WAT_CNTL_REG_SEL_LEN>(MCWAT_SELECT0);
                putScom(l_mi_target, MCS_MCWATCNTL, l_scom_data);
                putScom(l_mi_target, MCS_MCWATDATA, MCWAT_DATA0);
                // MCWATDATA3
                l_scom_data.insertFromRight<MCS_MCWATCNTL_WAT_CNTL_REG_SEL, MCS_MCWATCNTL_WAT_CNTL_REG_SEL_LEN>(MCWAT_SELECT3);
                putScom(l_mi_target, MCS_MCWATCNTL, l_scom_data);
                putScom(l_mi_target, MCS_MCWATDATA, MCWAT_DATA3);
                // MCWATDATA9
                l_scom_data.insertFromRight<MCS_MCWATCNTL_WAT_CNTL_REG_SEL, MCS_MCWATCNTL_WAT_CNTL_REG_SEL_LEN>(MCWAT_SELECT9);
                putScom(l_mi_target, MCS_MCWATCNTL, l_scom_data);
                putScom(l_mi_target, MCS_MCWATDATA, MCWAT_DATA9);
                // Enable MC WAT
                l_scom_data.setBit<MCS_MCWATCNTL_ENABLE_WAT>();
                putScom(l_mi_target, MCS_MCWATCNTL, l_scom_data);
            }
        }

        for (auto l_dmi_target : l_dmi_targets)
        {
            //--------------------------------------------------
            //-- Cumulus
            //--------------------------------------------------
            FAPI_EXEC_HWP(l_rc, p9c_dmi_scom, l_dmi_target, FAPI_SYSTEM, i_target);
        }
    }

    // read spare FBC FIR bit -- ifset, SBE has configured XBUS FIR resources for all
    // present units, and code here will be run to mask resources associated with
    // non-functional units
    getScom(i_target, PU_PB_CENT_SM0_PB_CENT_FIR_REG, l_scom_data);

    if(l_scom_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
    {
        for (auto &l_cplt_target : i_target.getChildren<fapi2::TARGET_TYPE_PERV>(static_cast<fapi2::TargetFilter>(fapi2::TARGET_FILTER_XBUS), fapi2::TARGET_STATE_FUNCTIONAL))
        {
            fapi2::buffer<uint16_t> l_attr_pg = 0;
            // obtain partial good information to determine viable X links
            l_attr_pg = fapi2::ATTR_PG[l_cplt_target];

            for (uint8_t ii = 0; ii < P9_FBC_UTILS_MAX_ELECTRICAL_LINKS; ii++)
            {
                l_xbus_present[ii] = !l_attr_pg.getBit(X_PG_IOX0_REGION_BIT + ii) && !l_attr_pg.getBit(X_PG_PBIOX0_REGION_BIT + ii);
            }
        }

        for (auto l_iter = l_xbus_chiplets.begin(); l_iter != l_xbus_chiplets.end(); l_iter++)
        {
            uint8_t l_unit_pos;
            l_unit_pos = fapi2::ATTR_CHIP_UNIT_POS[*l_iter];
            l_xbus_functional[l_unit_pos] = 1;
        }

        if(l_xbus_present[0] && !l_xbus_functional[0])
        {
            // XBUS0 FBC DL
            putScom(i_target, XBUS_0_LL0_LL0_LL0_IOEL_FIR_MASK_REG, FBC_IOE_DL_FIR_MASK_NF);
            // XBUS0 PHY
            putScom(i_target, XBUS_FIR_MASK_REG, XBUS_PHY_FIR_MASK_NF);
        }

        if(!l_xbus_functional[0])
        {
            // XBUS0 FBC TL
            putScom(i_target, PU_PB_IOE_FIR_MASK_REG_OR, FBC_IOE_TL_FIR_MASK_X0_NF);
            // XBUS0 EXTFIR
            putScom(i_target, PU_PB_CENT_SM1_EXTFIR_MASK_REG_OR, FBC_EXT_FIR_MASK_X0_NF);
        }

        if(l_xbus_present[1] && !l_xbus_functional[1])
        {
            // XBUS1 FBC DL
            putScom(i_target, XBUS_1_LL1_LL1_LL1_IOEL_FIR_MASK_REG, FBC_IOE_DL_FIR_MASK_NF);
            // XBUS1 PHY
            putScom(i_target, XBUS_1_FIR_MASK_REG, XBUS_PHY_FIR_MASK_NF);
        }

        if(!l_xbus_functional[1])
        {
            // XBUS1 FBC TL
            putScom(i_target, PU_PB_IOE_FIR_MASK_REG_OR, FBC_IOE_TL_FIR_MASK_X1_NF);
            // XBUS1 EXTFIR
            putScom(i_target, PU_PB_CENT_SM1_EXTFIR_MASK_REG_OR, FBC_EXT_FIR_MASK_X1_NF);
        }

        if(l_xbus_present[2] && !l_xbus_functional[2])
        {
            // XBUS2 FBC DL
            putScom(i_target, XBUS_2_LL2_LL2_LL2_IOEL_FIR_MASK_REG, FBC_IOE_DL_FIR_MASK_NF);
            // XBUS2 PHY
            putScom(i_target, XBUS_2_FIR_MASK_REG, XBUS_PHY_FIR_MASK_NF);
        }

        if(!l_xbus_functional[2])
        {
            // XBUS2 FBC TL
            putScom(i_target, PU_PB_IOE_FIR_MASK_REG_OR, FBC_IOE_TL_FIR_MASK_X2_NF);
            // XBUS2 EXTFIR
            putScom(i_target, PU_PB_CENT_SM1_EXTFIR_MASK_REG_OR, FBC_EXT_FIR_MASK_X2_NF);
        }
    }

    FAPI_EXEC_HWP(l_rc, p9_fbc_ioo_tl_scom, i_target, FAPI_SYSTEM);

    if(l_obus_chiplets.size())
    {
        fapi2::putScom(i_target, PU_IOE_PB_IOO_FIR_ACTION0_REG, FBC_IOO_TL_FIR_ACTION0);
        fapi2::putScom(i_target, PU_IOE_PB_IOO_FIR_ACTION1_REG, FBC_IOO_TL_FIR_ACTION1);
        fapi2::putScom(i_target, PU_IOE_PB_IOO_FIR_MASK_REG, FBC_IOO_TL_FIR_MASK);
    }

    for (auto l_iter = l_obus_chiplets.begin();
         l_iter != l_obus_chiplets.end();
         l_iter++)
    {
        // configure action registers & unmask
        fapi2::putScom(*l_iter, OBUS_LL0_PB_IOOL_FIR_ACTION0_REG, FBC_IOO_DL_FIR_ACTION0);
        fapi2::putScom(*l_iter, OBUS_LL0_PB_IOOL_FIR_ACTION1_REG, FBC_IOO_DL_FIR_ACTION1);
        fapi2::putScom(*l_iter, OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG, FBC_IOO_DL_FIR_MASK);
        FAPI_EXEC_HWP(l_rc, p9_fbc_ioo_dl_scom, *l_iter, i_target, FAPI_SYSTEM);
    }
    // Invoke NX SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_nx_scom, i_target, FAPI_SYSTEM);
    // Invoke CXA SCOM initfile
    l_capp_targets = i_target.getChildren<fapi2::TARGET_TYPE_CAPP>();
    for (auto l_capp : l_capp_targets)
    {
        FAPI_EXEC_HWP(l_rc, p9_cxa_scom, l_capp, FAPI_SYSTEM, i_target);
    }
    // Invoke INT SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_int_scom, i_target, FAPI_SYSTEM);
    // Invoke VAS SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_vas_scom, i_target, FAPI_SYSTEM);
    // Setup NMMU epsilon write cycles
    l_nmmu_ndd1 = fapi2::ATTR_CHIP_EC_FEATURE_NMMU_NDD1[i_target];
    l_eps_write_cycles_t1 = fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1[FAPI_SYSTEM];
    l_eps_write_cycles_t2 = fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2[FAPI_SYSTEM];

    if(!l_nmmu_ndd1)
    {
        fapi2::getScom(i_target, PU_NMMU_MM_EPSILON_COUNTER_VALUE, l_scom_data);
        l_scom_data.insertFromRight<PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_1_CNT_VAL, PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_1_CNT_VAL_LEN>(l_eps_write_cycles_t1);
        l_scom_data.insertFromRight<PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_2_CNT_VAL, PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_2_CNT_VAL_LEN>(l_eps_write_cycles_t2);
        fapi2::putScom(i_target, PU_NMMU_MM_EPSILON_COUNTER_VALUE, l_scom_data);
    }
}

fapi2::ReturnCode p9_vas_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{

    fapi2::ATTR_EC_Type   l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT0, l_chip_id);
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_CHIP_EC_FEATURE_HW414700_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414700, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
    FAPI_ATTR_GET(fapi2::ATTR_SMF_CONFIG, TGT1, l_TGT1_ATTR_SMF_CONFIG);
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_PUMP_MODE, TGT1, l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE);
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom(TGT0, 0x3011803, l_scom_buffer);
    l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00210102540D7FFF);
    fapi2::putScom(TGT0, 0x3011803, l_scom_buffer);
    fapi2::getScom(TGT0, 0x3011806, l_scom_buffer);
    l_scom_buffer.insert<0, 54, 0, uint64_t>(0);
    fapi2::putScom(TGT0, 0x3011806, l_scom_buffer);
    fapi2::getScom(TGT0, 0x3011807, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700 != 0)
    {
        l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00DD020180000000);
    }
    else
    {
        l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00DF020180000000);
    }
    fapi2::putScom(TGT0, 0x3011807, l_scom_buffer);
    fapi2::getScom(TGT0, 0x301184D, l_scom_buffer);
    l_scom_buffer.insert<0, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    l_scom_buffer.insert<4, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    if((l_chip_id == 0x5 && l_chip_ec == 0x22)
    || (l_chip_id == 0x5 && l_chip_ec == 0x23)
    || (l_chip_id == 0x6 && l_chip_ec == 0x12)
    || (l_chip_id == 0x6 && l_chip_ec == 0x13)
    || (l_chip_id == 0x7 && l_chip_ec == 0x10))
    {
        if(l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED)
        {
            l_scom_buffer.insert<11, 2, 62, uint64_t>(2);
        }
    }
    fapi2::putScom(TGT0, 0x301184D, l_scom_buffer);
    fapi2::getScom(TGT0, 0x301184E, l_scom_buffer);
    l_scom_buffer.insert<13, 1, 63, uint64_t>(0); // l_VA_VA_SOUTH_VA_EG_EG_SCF_ADDR_BAR_MODE_OFF
    if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP)
    {
        l_scom_buffer.insert<14, 1, 63, uint64_t>(1); // l_VA_VA_SOUTH_VA_EG_EG_SCF_SKIP_G_ON
    }
    else if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE)
    {
        l_scom_buffer.insert<14, 1, 63, uint64_t>(0); // l_VA_VA_SOUTH_VA_EG_EG_SCF_SKIP_G_OFF
    }
    l_scom_buffer.insert<20, 8, 56, uint64_t>(0xFC);
    l_scom_buffer.insert<28, 8, 56, uint64_t>(0xFC);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_WR_ON
    l_scom_buffer.insert<5, 1, 63, uint64_t>(1); // l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_RD_ON
    l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_WR_ON
    l_scom_buffer.insert<6, 1, 63, uint64_t>(1); // l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_RD_ON
    fapi2::putScom(TGT0, 0x301184E, l_scom_buffer);
    if((l_chip_id == 0x5 && l_chip_ec == 0x10)
    || (l_chip_id == 0x5 && l_chip_ec == 0x20))
    {
        fapi2::getScom(TGT0, 0x301184F, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
        fapi2::putScom(TGT0, 0x301184F, l_scom_buffer);
    }
}

fapi2::ReturnCode p9_int_scom
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::ATTR_EC_Type   l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT0, l_chip_id);
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_PUMP_MODE, TGT1, l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
    FAPI_ATTR_GET(fapi2::ATTR_SMF_CONFIG, TGT1, l_TGT1_ATTR_SMF_CONFIG);
    fapi2::ATTR_CHIP_EC_FEATURE_HW426891_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW426891, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891);
    fapi2::ATTR_CHIP_EC_FEATURE_HW411637_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW411637;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW411637, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW411637);
    fapi2::ATTR_CHIP_EC_FEATURE_P9N_INT_DD10_Type l_TGT0_ATTR_CHIP_EC_FEATURE_P9N_INT_DD10;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_P9N_INT_DD10, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_P9N_INT_DD10);
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom(TGT0, 0x501300A, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
    if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP)
    {
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    }
    else if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE)
    {
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
    }
    l_scom_buffer.insert<5, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    l_scom_buffer.insert<9, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    if((l_chip_id == 0x5 && l_chip_ec == 0x22)
    || (l_chip_id == 0x5 && l_chip_ec == 0x23)
    || (l_chip_id == 0x6 && l_chip_ec == 0x12)
    || (l_chip_id == 0x6 && l_chip_ec == 0x13)
    || (l_chip_id == 0x7 && l_chip_ec == 0x10))
    {
        if((l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED))
        {
            l_scom_buffer.insert<12, 2, 62, uint64_t>(2);
        }
    }
    fapi2::putScom(TGT0, 0x501300A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013021, l_scom_buffer);
    if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP)
    {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(1);
    }
    else if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE)
    {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
    }
    l_scom_buffer.insert<47, 1, 63, uint64_t>(1); // l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_G_ON
    l_scom_buffer.insert<46, 1, 63, uint64_t>(1); // l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_VG_NOT_SYS_ON
    fapi2::putScom(TGT0, 0x5013021, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013033, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW411637 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891 == 0)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0070000072040140);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW411637 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x2000005C040281C3);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW411637 == 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0000005C040081C3);
    }
    fapi2::putScom(TGT0, 0x5013033, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013036, l_scom_buffer);
    l_scom_buffer.insert<0, 64, 0, uint64_t>(0);
    fapi2::putScom(TGT0, 0x5013036, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013037, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_P9N_INT_DD10 == 1)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x9554021F80110FCF);
    }
    else
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x9554021F80110E0C);
    }
    fapi2::putScom(TGT0, 0x5013037, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013130, l_scom_buffer);
    l_scom_buffer.insert<2, 6, 58, uint64_t>(0x18);
    l_scom_buffer.insert<10, 6, 58, uint64_t>(0x18);
    fapi2::putScom(TGT0, 0x5013130, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013140, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891 == 1)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x050043EF00100020);
    }
    else
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x010003FF00100020);
    }
    fapi2::putScom(TGT0, 0x5013140, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013141, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFADFBB8CFFAFFFD7);
    }
    else
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xD8DFB200DFAFFFD7);
    }
    fapi2::putScom(TGT0, 0x5013141, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013178, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426891)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0002000610000000);

    }
    else
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0002000410000000);
    }
    fapi2::putScom(TGT0, 0x5013178, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501320E, l_scom_buffer);
    l_scom_buffer.insert<0, 48, 0, uint64_t>(0x6262220242160000);
    fapi2::putScom(TGT0, 0x501320E, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013214, l_scom_buffer);
    l_scom_buffer.insert<16, 16, 48, uint64_t>(0x5BBF);
    fapi2::putScom(TGT0, 0x5013214, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501322B, l_scom_buffer);
    l_scom_buffer.insert<58, 6, 58, uint64_t>(0x18);
    fapi2::putScom(TGT0, 0x501322B, l_scom_buffer);
    if((l_chip_id == 0x5 && l_chip_ec == 0x10)
    || (l_chip_id == 0x5 && l_chip_ec == 0x20))
    {
        fapi2::getScom(TGT0, 0x5013272, l_scom_buffer);
        l_scom_buffer.insert<0, 44, 20, uint64_t>(0x0002C018006);
        fapi2::putScom(TGT0, 0x5013272, l_scom_buffer);
        fapi2::getScom(TGT0, 0x5013273, l_scom_buffer);
        l_scom_buffer.insert<0, 44, 20, uint64_t>(0xFFFCFFEFFFA);
        fapi2::putScom(TGT0, 0x5013273, l_scom_buffer);
    }

}


fapi2::ReturnCode p9_fbc_ioo_dl_scom(const fapi2::Target<fapi2::TARGET_TYPE_OBUS>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT1, const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT2)
{
    {
        fapi2::ATTR_EC_Type   l_chip_ec;
        fapi2::ATTR_NAME_Type l_chip_id;
        l_chip_id = fapi2::ATTR_NAME[TGT1]
        l_chip_ec = fapi2::ATTR_EC[TGT1]
        fapi2::ATTR_LINK_TRAIN_Type l_TGT0_ATTR_LINK_TRAIN;
        l_TGT0_ATTR_LINK_TRAIN = fapi2::ATTR_LINK_TRAIN[TGT0];
        fapi2::ATTR_PROC_NPU_REGION_ENABLED_Type l_TGT1_ATTR_PROC_NPU_REGION_ENABLED;
        l_TGT1_ATTR_PROC_NPU_REGION_ENABLED = fapi2::ATTR_PROC_NPU_REGION_ENABLED[TGT1];
        fapi2::ATTR_OPTICS_CONFIG_MODE_Type l_TGT0_ATTR_OPTICS_CONFIG_MODE;
        l_TGT0_ATTR_OPTICS_CONFIG_MODE = fapi2::ATTR_OPTICS_CONFIG_MODE[TGT0];
        uint64_t l_def_OBUS_NV_ENABLED = l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_NV && l_TGT1_ATTR_PROC_NPU_REGION_ENABLED;
        fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE_Type l_TGT0_ATTR_PROC_FABRIC_LINK_ACTIVE;
        l_TGT0_ATTR_PROC_FABRIC_LINK_ACTIVE = fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[TGT0];
        uint64_t l_def_OBUS_FBC_ENABLED = l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP && l_TGT0_ATTR_PROC_FABRIC_LINK_ACTIVE;
        fapi2::ATTR_CHIP_EC_FEATURE_HW419022_Type l_TGT1_ATTR_CHIP_EC_FEATURE_HW419022;
        l_TGT1_ATTR_CHIP_EC_FEATURE_HW419022 = fapi2::ATTR_CHIP_EC_FEATURE_HW419022[TGT1];
        uint64_t l_def_DLL_DD10_TRAIN = (l_TGT1_ATTR_CHIP_EC_FEATURE_HW419022 != 0);
        fapi2::buffer<uint64_t> l_scom_buffer;

        // Power Bus OLL Configuration Register
        // PB.IOO.LL0.IOOL_CONFIG
        // Processor bus OLL configuration register
        fapi2::getScom(TGT0, 0x901080A, l_scom_buffer);
        if(l_TGT0_ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH)
        {
            l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_LINK_PAIR_ON
        }
        else
        {
            l_scom_buffer.insert<0, 1, 63, uint64_t>(0); // l_PB_IOO_LL0_CONFIG_LINK_PAIR_OFF
        }
        l_scom_buffer.insert<11, 5, 59, uint64_t>(0x0F);
        l_scom_buffer.insert<32, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<28, 4, 60, uint64_t>(0x0F);
        l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_CRC_LANE_ID_ON
        l_scom_buffer.insert<4, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_SL_UE_CRC_ERR_ON
        l_scom_buffer.insert<48, 4, 60, uint64_t>(0x0F);
        fapi2::putScom(TGT0, 0x901080A, l_scom_buffer);
        // Power Bus OLL PHY Training Configuration Register
        // PB.IOO.LL0.IOOL_PHY_CONFIG
        // Processor bus OLL PHY training configuration register
        fapi2::getScom(TGT0, 0x901080C, l_scom_buffer);
        if(l_def_OBUS_NV_ENABLED)
        {
            l_scom_buffer.insert<61, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_NV0_NPU_ENABLED_ON
            l_scom_buffer.insert<62, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_NV1_NPU_ENABLED_ON
            l_scom_buffer.insert<63, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_NV2_NPU_ENABLED_ON
        }
        if(l_def_OBUS_FBC_ENABLED && l_TGT0_ATTR_LINK_TRAIN != fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY)
        {
            l_scom_buffer.insert<58, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_LINK0_OLL_ENABLED_ON
        }
        else
        {
            l_scom_buffer.insert<58, 1, 63, uint64_t>(0); // l_PB_IOO_LL0_CONFIG_LINK0_OLL_ENABLED_OFF
        }
        if(l_def_OBUS_FBC_ENABLED && l_TGT0_ATTR_LINK_TRAIN != fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY)
        {
            l_scom_buffer.insert<59, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_LINK1_OLL_ENABLED_ON
        }
        else
        {
            l_scom_buffer.insert<59, 1, 63, uint64_t>(0); // l_PB_IOO_LL0_CONFIG_LINK1_OLL_ENABLED_OFF
        }
        if(l_def_DLL_DD10_TRAIN)
        {
            l_scom_buffer.insert<8, 4, 60, uint64_t>(0xE);
            l_scom_buffer.insert<12, 4, 60, uint64_t>(0xE);
            l_scom_buffer.insert<4, 4, 60, uint64_t>(0);
        }
        else
        {
            l_scom_buffer.insert<0, 2, 62, uint64_t>(0x2); // l_PB_IOO_LL0_CONFIG_PHY_TRAIN_A_ADJ_USE4
            l_scom_buffer.insert<2, 2, 62, uint64_t>(0x2); // l_PB_IOO_LL0_CONFIG_PHY_TRAIN_B_ADJ_USE12
            l_scom_buffer.insert<8, 4, 60, uint64_t>(0);
            l_scom_buffer.insert<12, 4, 60, uint64_t>(0);
            l_scom_buffer.insert<4, 4, 60, uint64_t>(0x0F);
        }
        fapi2::putScom(TGT0, 0x901080C, l_scom_buffer);
        // Power Bus OLL Optical Configuration Register
        // PB.IOO.LL0.IOOL_OPTICAL_CONFIG
        // Processor bus OLL optical configuration register
        fapi2::getScom(TGT0, 0x901080F, l_scom_buffer);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0x5);
        l_scom_buffer.insert<9, 7, 57, uint64_t>(0x0F);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_ELEVEN_LANE_MODE_ON
        l_scom_buffer.insert<3, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_LINK_FAIL_CRC_ERROR_ON
        l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_CONFIG_LINK_FAIL_NO_SPARE_ON
        l_scom_buffer.insert<20, 4, 60, uint64_t>(0x5);
        l_scom_buffer.insert<25, 7, 57, uint64_t>(0x3F);
        l_scom_buffer.insert<56, 2, 62, uint64_t>(0x2); // l_PB_IOO_LL0_CONFIG_REPLAY_BUFFER_SIZE_REPLAY
        l_scom_buffer.insert<39, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_LINK1_ELEVEN_LANE_SHIFT_ON
        l_scom_buffer.insert<42, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_LINK1_RX_LANE_SWAP_ON
        l_scom_buffer.insert<43, 1, 63, uint64_t>(1); // l_PB_IOO_LL0_LINK1_TX_LANE_SWAP_ON
        fapi2::putScom(TGT0, 0x901080F, l_scom_buffer);
        // Power Bus OLL Replay Threshold Register
        // PB.IOO.LL0.IOOL_REPLAY_THRESHOLD
        // Processor bus OLL replay threshold register
        fapi2::getScom(TGT0, 0x9010818, l_scom_buffer);
        l_scom_buffer.insert<8, 3, 61, uint64_t>(0x7);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0x0F);
        l_scom_buffer.insert<0, 4, 60, uint64_t>(0x6);
        fapi2::putScom(TGT0, 0x9010818, l_scom_buffer);
        // Power Bus OLL SL ECC Threshold Register
        // PB.IOO.LL0.IOOL_SL_ECC_THRESHOLD
        // Processor bus OLL SL ECC Threshold register
        fapi2::getScom(TGT0, 0x9010819, l_scom_buffer);
        l_scom_buffer.insert<8, 2, 62, uint64_t>(0x7);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0x0F);
        l_scom_buffer.insert<0, 4, 60, uint64_t>(0x7);
        fapi2::putScom(TGT0, 0x9010819, l_scom_buffer);
        // Power Bus OLL Retrain Threshold Register
        // PB.IOO.LL0.IOOL_RETRAIN_THRESHOLD
        // Processor bus OLL retrain threshold register
        fapi2::getScom(TGT0, 0x901081A, l_scom_buffer);
        l_scom_buffer.insert<8, 9, 55, uint64_t>(0x7);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0x0F);
        l_scom_buffer.insert<0, 4, 60, uint64_t>(0x7);
        fapi2::putScom(TGT0, 0x901081A, l_scom_buffer);
    };
}


fapi2::ReturnCode p9_psi_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0)
{
    {
        fapi2::ATTR_EC_Type   l_chip_ec;
        fapi2::ATTR_NAME_Type l_chip_id;
        l_chip_id = fapi2::ATTR_NAME[TGT0]
        l_chip_ec = fapi2::ATTR_EC[TGT0]
        fapi2::buffer<uint64_t> l_scom_buffer;
        // PSI FIR Mask Register
        // PSI.PSI.PSI_MAC.PSI_SCOM.FIR_MASK_REG
        // Error mask register (Action0,Mask) = Action Select (0,0) = Recoverable Error (0,1) = Masked
        fapi2::getScom(TGT0, 0x4011803, l_scom_buffer);
        l_scom_buffer.insert<0, 7, 0, uint64_t>(0xFE00000000000000);
        fapi2::putScom(TGT0, 0x4011803, l_scom_buffer);
        // Fault Isolation Action0 Register
        // PSI.PSI.PSI_MAC.PSI_SCOM.FIR_ACTION0_REG
        fapi2::getScom(TGT0, 0x4011806, l_scom_buffer);
        l_scom_buffer.insert<0, 7, 0, uint64_t>(0);
        fapi2::putScom(TGT0, 0x4011806, l_scom_buffer);
        // Fault Isolation Action1 Register
        // PSI.PSI.PSI_MAC.PSI_SCOM.FIR_ACTION1_REG
        fapi2::getScom(TGT0, 0x4011807, l_scom_buffer);
        l_scom_buffer.insert<0, 7, 0, uint64_t>(0);
        fapi2::putScom(TGT0, 0x4011807, l_scom_buffer);
        // PSI Host Bridge FIR Mask Register
        // BRIDGE.PSIHB.PSIHB_FIR_MASK_REG
        fapi2::getScom(TGT0, 0x5012903, l_scom_buffer);
        l_scom_buffer.insert<0, 29, 35, uint64_t>(0x7E040DF);
        fapi2::putScom(TGT0, 0x5012903, l_scom_buffer);
        // PSI Host Bridge FIR Action0 Register
        // BRIDGE.PSIHB.PSIHB_FIR_ACTION0_REG
        // PSI Host Bridge FIR Action0 Register
        // Action select for corresponding bit in FIR
        // (Action0,Action1) = Action Select
        // (0,0) = No Error
        // (0,1) = recoverable error
        // (1,0) = Checkstop Error
        // (1,1) = unused
        fapi2::getScom(TGT0, 0x5012906, l_scom_buffer);
        l_scom_buffer.insert<0, 29, 35, uint64_t>(0);
        fapi2::putScom(TGT0, 0x5012906, l_scom_buffer);
        // PSI Host Bridge FIR Action1 Register
        // BRIDGE.PSIHB.PSIHB_FIR_ACTION1_REG
        // PSI Host Bridge FIR action1 Register
        // Action select for corresponding bit in FIR
        // (Action0,Action1) = Action Select
        // (0,0) = No Error
        // (0,1) = recoverable error
        // (1,0) = Checkstop Error
        // (1,1) = unused
        fapi2::getScom(TGT0, 0x5012907, l_scom_buffer);
        l_scom_buffer.insert<0, 29, 35, uint64_t>(0x18050020);
        fapi2::putScom(TGT0, 0x5012907, l_scom_buffer);
        // PSI Host Bridge Error Mask Register
        // BRIDGE.PSIHB.PSIHB_ERROR_MASK_REG
        fapi2::getScom(TGT0, 0x501290F, l_scom_buffer);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
        fapi2::putScom(TGT0, 0x501290F, l_scom_buffer);
    };
fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode p9_io_obus_scominit(const fapi2::Target<fapi2::TARGET_TYPE_OBUS> &i_target)
{
    // mark HWP entry

    const uint8_t GROUP_00 = 0;
    const uint8_t LANE_00 = 0;
    const uint8_t SET_RESET = 1;
    const uint8_t CLEAR_RESET = 0;
    fapi2::ReturnCode rc = fapi2::FAPI2_RC_SUCCESS;

    // get system target
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> l_system_target;
    // get a proc target
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc_target = i_target.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    // assert IO reset to power-up bus endpoint logic
    io::rmw(OPT_IORESET_HARD_BUS0, i_target, GROUP_00, LANE_00, SET_RESET);
    // Bus Reset is relatively fast, only needing < a hundred cycles to allow the signal to propogate.
    sleep(10ns);
    io::rmw(OPT_IORESET_HARD_BUS0, i_target, GROUP_00, LANE_00, CLEAR_RESET);
    FAPI_EXEC_HWP(rc, p9_obus_scom, i_target, l_system_target, l_proc_target);

    // configure FIR
    {
        fapi2::putScom(i_target, OBUS_FIR_ACTION0_REG, OBUS_PHY_FIR_ACTION0);
        fapi2::putScom(i_target, OBUS_FIR_ACTION1_REG, OBUS_PHY_FIR_ACTION1);
        fapi2::putScom(i_target, OBUS_FIR_MASK_REG, OBUS_PHY_FIR_MASK);
    }
}

fapi2::ReturnCode p9_npu_scominit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_target)
{

    uint8_t l_npu_enabled;

    // check to see ifNPU region in N3 chiplet is enabled
    l_npu_enabled = fapi2::ATTR_PROC_NPU_REGION_ENABLED[i_target];

    if(l_npu_enabled)
    {
        fapi2::ReturnCode l_rc;
        fapi2::buffer<uint64_t> l_atrmiss = 0;
        fapi2::ATTR_CHIP_EC_FEATURE_SETUP_BARS_NPU_DD1_ADDR_Type l_npu_p9n_dd1;
        fapi2::ATTR_CHIP_EC_FEATURE_SETUP_BARS_NPU_AXONE_ADDR_Type l_axone;

        // read attribute to determine ifP9N DD1 NPU addresses should be used
        l_npu_p9n_dd1 = fapi2::ATTR_CHIP_EC_FEATURE_SETUP_BARS_NPU_DD1_ADDR[i_target];

        // apply NPU SCOM inits from initfile

        FAPI_EXEC_HWP(l_rc, p9_npu_scom, i_target, fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>());

        // apply additional SCOM inits
        l_atrmiss.setBit<PU_NPU_SM2_XTS_ATRMISS_FLAG_MAP>().setBit<PU_NPU_SM2_XTS_ATRMISS_ENA>();

        fapi2::putScomUnderMask(
            i_target,
            ((l_npu_p9n_dd1) ? (PU_NPU_SM2_XTS_ATRMISS) : (PU_NPU_SM2_XTS_ATRMISS_POST_P9NDD1)),
            l_atrmiss,
            l_atrmiss);

        // enable NVLINK refclocks
        FAPI_EXEC_HWP(l_rc, p9_nv_ref_clk_enable, i_target);
    }
}

fapi2::ReturnCode p9_chiplet_enable_ridi(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_target_chip)
{
    for (auto l_target_cplt : i_target_chip.getChildren<fapi2::TARGET_TYPE_PERV>(static_cast<fapi2::TargetFilter>(fapi2::TARGET_FILTER_ALL_MC |
                                                                                                                  fapi2::TARGET_FILTER_ALL_PCI |
                                                                                                                  fapi2::TARGET_FILTER_ALL_OBUS),
                                                                                 fapi2::TARGET_STATE_FUNCTIONAL))
    {
        p9_chiplet_enable_ridi_net_ctrl_action_function(l_target_cplt);
    }
}

fapi2::ReturnCode
p9_nv_ref_clk_enable(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_target)
{
    fapi2::buffer<uint64_t> l_root_ctrl;
    fapi2::ATTR_CHIP_EC_FEATURE_ONE_NPU_TOP_Type l_one_npu;
    l_one_npu = fapi2::ATTR_CHIP_EC_FEATURE_ONE_NPU_TOP[i_target];
    if(l_one_npu)
    {
        fapi2::getScom(i_target, PERV_ROOT_CTRL6_SCOM, l_root_ctrl);
        l_root_ctrl.insertFromRight<PERV_ROOT_CTRL6_TSFSI_NV_REFCLK_EN_DC, PERV_ROOT_CTRL6_TSFSI_NV_REFCLK_EN_DC_LEN>(TPFSI_OFFCHIP_REFCLK_EN_NV);
        fapi2::putScom(i_target, PERV_ROOT_CTRL6_SCOM, l_root_ctrl);
    }
    else
    {
        fapi2::getScom(i_target, P9A_PERV_ROOT_CTRL7_SCOM, l_root_ctrl);
        l_root_ctrl.setBit<P9A_PERV_ROOT_CTRL7_TP_TPIO_NV_REFCLK_EN_DC, P9A_NV_REFCLK_BIT_LEN>();
        fapi2::putScom(i_target, P9A_PERV_ROOT_CTRL7_SCOM, l_root_ctrl);
    }
}

fapi2::ReturnCode p9_cxa_scom(const fapi2::Target<fapi2::TARGET_TYPE_CAPP>& TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1, const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT2)
{
    {
        fapi2::ATTR_EC_Type   l_chip_ec;
        fapi2::ATTR_NAME_Type l_chip_id;
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT2, l_chip_id);
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT2, l_chip_ec);
        fapi2::ATTR_CHIP_EC_FEATURE_HW414700_Type l_TGT2_ATTR_CHIP_EC_FEATURE_HW414700;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414700, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_HW414700);
        fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE;
        FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_PUMP_MODE, TGT1, l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE);
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
        FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
        FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
        fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
        FAPI_ATTR_GET(fapi2::ATTR_SMF_CONFIG, TGT1, l_TGT1_ATTR_SMF_CONFIG);
        fapi2::buffer<uint64_t> l_scom_buffer;

        fapi2::getScom(TGT0, 0x2010803, l_scom_buffer);
        if((l_TGT2_ATTR_CHIP_EC_FEATURE_HW414700 != 0))
        {
            l_scom_buffer.insert<0, 53, 0, uint64_t>(0x801B1F98C8717000);
        }
        else
        {
            l_scom_buffer.insert<0, 53, 0, uint64_t>(0x801B1F98D8717000);
        }
        fapi2::putScom(TGT0, 0x2010803, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2010806, l_scom_buffer);
        l_scom_buffer.insert<0, 53, 11, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2010806, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2010807, l_scom_buffer);
        l_scom_buffer.insert<2, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<44, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(1);
        fapi2::putScom(TGT0, 0x2010807, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2010818, l_scom_buffer);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0); // l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_ADR_BAR_MODE_OFF
        if((l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP))
        {
            l_scom_buffer.insert<6, 1, 63, uint64_t>(1); // l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_SKIP_G_ON
        }
        else if((l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE))
        {
            l_scom_buffer.insert<6, 1, 63, uint64_t>(0); // l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_SKIP_G_OFF
        }
        l_scom_buffer.insert<21, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
        l_scom_buffer.insert<25, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
        if((l_chip_id == 0x5 && l_chip_ec == 0x22)
        || (l_chip_id == 0x5 && l_chip_ec == 0x23)
        || (l_chip_id == 0x6 && l_chip_ec == 0x12)
        || (l_chip_id == 0x6 && l_chip_ec == 0x13)
        || (l_chip_id == 0x7 && l_chip_ec == 0x10))
        {
            if((l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED))
            {
                l_scom_buffer.insert<19, 2, 62, uint64_t>(2);
            }
        }
        l_scom_buffer.insert<4, 1, 63, uint64_t>(1); // l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_G_ON
        l_scom_buffer.insert<3, 1, 63, uint64_t>(1); // l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_VG_NOT_SYS_ON
        fapi2::putScom(TGT0, 0x2010818, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2010819, l_scom_buffer);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2010819, l_scom_buffer);
        fapi2::getScom(TGT0, 0x201081B, l_scom_buffer);
        l_scom_buffer.insert<45, 3, 61, uint64_t>(7);
        l_scom_buffer.insert<48, 4, 60, uint64_t>(2);
        fapi2::putScom(TGT0, 0x201081B, l_scom_buffer);
        fapi2::getScom(TGT0, 0x201081C, l_scom_buffer);
        l_scom_buffer.insert<18, 4, 60, uint64_t>(1);
        fapi2::putScom(TGT0, 0x201081C, l_scom_buffer);
    };
}


fapi2::ReturnCode p9_nx_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                             const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    {
        fapi2::ATTR_EC_Type   l_chip_ec;
        fapi2::ATTR_NAME_Type l_chip_id;
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT0, l_chip_id);
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
        fapi2::ATTR_CHIP_EC_FEATURE_HW403701_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW403701;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW403701, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW403701);
        fapi2::ATTR_CHIP_EC_FEATURE_HW414700_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414700, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700);
        fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE;
        FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_PUMP_MODE, TGT1, l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE);
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
        FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
        FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
        fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
        FAPI_ATTR_GET(fapi2::ATTR_SMF_CONFIG, TGT1, l_TGT1_ATTR_SMF_CONFIG);
        fapi2::buffer<uint64_t> l_scom_buffer;

        fapi2::getScom(TGT0, 0x2011041, l_scom_buffer);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(1); // l_NX_DMA_CH0_EFT_ENABLE_ON
        l_scom_buffer.insert<62, 1, 63, uint64_t>(1); // l_NX_DMA_CH1_EFT_ENABLE_ON
        l_scom_buffer.insert<58, 1, 63, uint64_t>(1); // l_NX_DMA_CH2_SYM_ENABLE_ON
        l_scom_buffer.insert<57, 1, 63, uint64_t>(1); // l_NX_DMA_CH3_SYM_ENABLE_ON
        l_scom_buffer.insert<61, 1, 63, uint64_t>(1); // l_NX_DMA_CH4_GZIP_ENABLE_ON
        fapi2::putScom(TGT0, 0x2011041, l_scom_buffer);

        fapi2::getScom(TGT0, 0x2011042, l_scom_buffer);
        l_scom_buffer.insert<33, 4, 60, uint64_t>(0xF); // l_NX_DMA_EFTCOMP_MAX_INRD_MAX_15_INRD
        l_scom_buffer.insert<37, 4, 60, uint64_t>(0xF); // l_NX_DMA_EFTDECOMP_MAX_INRD_MAX_15_INRD
        l_scom_buffer.insert<8, 4, 60, uint64_t>(0xF); // l_NX_DMA_GZIPCOMP_MAX_INRD_MAX_15_INRD
        l_scom_buffer.insert<25, 4, 60, uint64_t>(0x3); // l_NX_DMA_SYM_MAX_INRD_MAX_3_INRD
        l_scom_buffer.insert<23, 1, 63, uint64_t>(1); // l_NX_DMA_EFT_COMP_PREFETCH_ENABLE_ON
        l_scom_buffer.insert<24, 1, 63, uint64_t>(1); // l_NX_DMA_EFT_DECOMP_PREFETCH_ENABLE_ON
        l_scom_buffer.insert<16, 1, 63, uint64_t>(1); // l_NX_DMA_GZIP_COMP_PREFETCH_ENABLE_ON
        l_scom_buffer.insert<17, 1, 63, uint64_t>(1); // l_NX_DMA_GZIP_DECOMP_PREFETCH_ENABLE_ON
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0); // l_NX_DMA_EFT_SPBC_WRITE_ENABLE_OFF
        fapi2::putScom(TGT0, 0x2011042, l_scom_buffer);

        fapi2::getScom(TGT0, 0x201105C, l_scom_buffer);
        l_scom_buffer.insert<1, 4, 60, uint64_t>(0x9); // l_NX_DMA_CH0_WATCHDOG_REF_DIV_DIVIDE_BY_512
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_NX_DMA_CH0_WATCHDOG_TIMER_ENBL_ON
        l_scom_buffer.insert<6, 4, 60, uint64_t>(0x9); // l_NX_DMA_CH1_WATCHDOG_REF_DIV_DIVIDE_BY_512
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1); // l_NX_DMA_CH1_WATCHDOG_TIMER_ENBL_ON
        l_scom_buffer.insert<11, 4, 60, uint64_t>(0x9); // l_NX_DMA_CH2_WATCHDOG_REF_DIV_DIVIDE_BY_512
        l_scom_buffer.insert<10, 1, 63, uint64_t>(1); // l_NX_DMA_CH2_WATCHDOG_TIMER_ENBL_ON
        l_scom_buffer.insert<16, 4, 60, uint64_t>(0x9); // l_NX_DMA_CH3_WATCHDOG_REF_DIV_DIVIDE_BY_512
        l_scom_buffer.insert<15, 1, 63, uint64_t>(1); // l_NX_DMA_CH3_WATCHDOG_TIMER_ENBL_ON
        l_scom_buffer.insert<21, 4, 60, uint64_t>(0x9); // l_NX_DMA_CH4_WATCHDOG_REF_DIV_DIVIDE_BY_512
        l_scom_buffer.insert<20, 1, 63, uint64_t>(1); // l_NX_DMA_CH4_WATCHDOG_TIMER_ENBL_ON
        l_scom_buffer.insert<25, 1, 63, uint64_t>(1); // l_NX_DMA_DMA_HANG_TIMER_ENBL_ON
        l_scom_buffer.insert<26, 4, 60, uint64_t>(0x8); // l_NX_DMA_DMA_HANG_TIMER_REF_DIV_DIVIDE_BY_1024
        fapi2::putScom(TGT0, 0x201105C, l_scom_buffer);

        fapi2::getScom(TGT0, 0x2011083, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<26, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<27, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<28, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<29, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<2, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<30, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(1);
        if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW403701 == 1)
        {
            l_scom_buffer.insert<32, 1, 63, uint64_t>(1);
        }
        else
        {
            l_scom_buffer.insert<32, 1, 63, uint64_t>(0);
        }
        if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW403701)
        {
            l_scom_buffer.insert<33, 1, 63, uint64_t>(1);
        }
        else
        {
            l_scom_buffer.insert<33, 1, 63, uint64_t>(0);
        }
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<3, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<40, 2, 62, uint64_t>(3);
        l_scom_buffer.insert<42, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<43, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<7, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2011083, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011086, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<26, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<27, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<28, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<29, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<2, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<30, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<3, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<42, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<43, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2011086, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011087, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<26, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<27, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<28, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<29, 1, 63, uint64_t>(0);
        if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700)
        {
            l_scom_buffer.insert<2, 1, 63, uint64_t>(0);
        }
        else
        {
            l_scom_buffer.insert<2, 1, 63, uint64_t>(1);
        }
        l_scom_buffer.insert<30, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<3, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<42, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<43, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(1);
        if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700)
        {
            l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
        }
        else
        {
            l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
        }
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2011087, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011095, l_scom_buffer);
        if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP)
        {
            l_scom_buffer.insert<24, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_SKIP_G_ON
        }
        else if(l_TGT1_ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE)
        {
            l_scom_buffer.insert<24, 1, 63, uint64_t>(0); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_SKIP_G_OFF
        }
        l_scom_buffer.insert<56, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
        l_scom_buffer.insert<60, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
        if((l_chip_id == 0x5 && l_chip_ec == 0x22)
         || (l_chip_id == 0x5 && l_chip_ec == 0x23)
         || (l_chip_id == 0x6 && l_chip_ec == 0x12)
         || (l_chip_id == 0x6 && l_chip_ec == 0x13)
         || (l_chip_id == 0x7 && l_chip_ec == 0x10))
        {
            if((l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED))
            {
                l_scom_buffer.insert<34, 2, 62, uint64_t>(2);
            }
        }
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_GROUP_ON
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_GROUP_ON
        l_scom_buffer.insert<9, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_GROUP_ON
        l_scom_buffer.insert<13, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_GROUP_ON
        l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_VG_NOT_SYS_ON
        l_scom_buffer.insert<6, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_VG_NOT_SYS_ON
        l_scom_buffer.insert<10, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_VG_NOT_SYS_ON
        l_scom_buffer.insert<14, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_VG_NOT_SYS_ON
        l_scom_buffer.insert<22, 1, 63, uint64_t>(1); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_RD_GO_M_QOS_ON
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0); // l_NX_PBI_CQ_WRAP_NXCQ_SCOM_ADDR_BAR_MODE_OFF
        l_scom_buffer.insert<25, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<40, 8, 56, uint64_t>(0xFC);
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0xFC);
        fapi2::putScom(TGT0, 0x2011095, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110A8, l_scom_buffer);
        l_scom_buffer.insert<8, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<12, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<16, 4, 60, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110A8, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110C3, l_scom_buffer);
        l_scom_buffer.insert<27, 9, 55, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110C3, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110C4, l_scom_buffer);
        l_scom_buffer.insert<27, 9, 55, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110C4, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110C5, l_scom_buffer);
        l_scom_buffer.insert<27, 9, 55, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110C5, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110D5, l_scom_buffer);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_NX_PBI_PBI_UMAC_CRB_READS_ENBL_ON
        fapi2::putScom(TGT0, 0x20110D5, l_scom_buffer);
        fapi2::getScom(TGT0, 0x20110D6, l_scom_buffer);
        l_scom_buffer.insert<9, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(1); // l_NX_PBI_DISABLE_PROMOTE_ON
        fapi2::putScom(TGT0, 0x20110D6, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011103, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 6, 58, uint64_t>(0x3F);
        l_scom_buffer.insert<2, 2, 62, uint64_t>(3);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 8, 56, uint64_t>(0xFF);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2011103, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011106, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 6, 58, uint64_t>(0);
        l_scom_buffer.insert<2, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 8, 56, uint64_t>(0);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2011106, l_scom_buffer);
        fapi2::getScom(TGT0, 0x2011107, l_scom_buffer);
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<10, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<11, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<13, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<14, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<21, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<22, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<24, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<25, 6, 58, uint64_t>(0);
        l_scom_buffer.insert<2, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<36, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<37, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<40, 8, 56, uint64_t>(0);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<4, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<6, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<9, 1, 63, uint64_t>(1);
        if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW414700)
        {
            l_scom_buffer.insert<12, 1, 63, uint64_t>(0);
            l_scom_buffer.insert<17, 1, 63, uint64_t>(0);
            l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
            l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
            l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
        }
        else
        {
            l_scom_buffer.insert<12, 1, 63, uint64_t>(1);
            l_scom_buffer.insert<17, 1, 63, uint64_t>(1);
            l_scom_buffer.insert<18, 1, 63, uint64_t>(1);
            l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
            l_scom_buffer.insert<7, 1, 63, uint64_t>(1);
        }
        fapi2::putScom(TGT0, 0x2011107, l_scom_buffer);
    };
}

fapi2::ReturnCode p9_npu_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> &TGT1)
{
    fapi2::ATTR_EC_Type l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    l_chip_id = fapi2::ATTR_NAME[TGT0]
    l_chip_ec = fapi2::ATTR_EC[TGT0]
    fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_Type l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE;
    l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE = fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[TGT0];
    uint64_t l_def_NPU_ACTIVE =
            l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[0] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[1] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[2] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[3] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[0] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_OCAPI
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[1] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_OCAPI
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[2] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_OCAPI
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[3] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_OCAPI;
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T0_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0;
    l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0 = fapi2::ATTR_PROC_EPS_READ_CYCLES_T0[TGT1];
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T1_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1;
    l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1 = fapi2::ATTR_PROC_EPS_READ_CYCLES_T1[TGT1];
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T2_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2;
    l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2 = fapi2::ATTR_PROC_EPS_READ_CYCLES_T2[TGT1];
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1_Type l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1;
    l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1 = fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1[TGT1];
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2_Type l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2;
    l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2 = fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2[TGT1];
    uint64_t l_def_NVLINK_ACTIVE_OB0 = l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[0] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV;
    fapi2::ATTR_CHIP_EC_FEATURE_DISABLE_NPU_FREEZE_Type l_TGT0_ATTR_CHIP_EC_FEATURE_DISABLE_NPU_FREEZE;
    l_TGT0_ATTR_CHIP_EC_FEATURE_DISABLE_NPU_FREEZE = fapi2::ATTR_CHIP_EC_FEATURE_DISABLE_NPU_FREEZE[TGT0];
    uint64_t l_def_ENABLE_NPU_FREEZE = l_TGT0_ATTR_CHIP_EC_FEATURE_DISABLE_NPU_FREEZE == 0;
    fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG;
    l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[TGT0];
    uint64_t l_def_NUM_X_LINKS_CFG = ((l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[0] + l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1]) + l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[2]);
    fapi2::ATTR_CHIP_EC_FEATURE_HW372457_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457;
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457 = fapi2::ATTR_CHIP_EC_FEATURE_HW372457[TGT0];
    fapi2::ATTR_CHIP_EC_FEATURE_HW423589_OPTION1_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1;
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1 = fapi2::ATTR_CHIP_EC_FEATURE_HW423589_OPTION1[TGT0];
    fapi2::ATTR_CHIP_EC_FEATURE_HW410625_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625;
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625 = fapi2::ATTR_CHIP_EC_FEATURE_HW410625[TGT0];
    fapi2::ATTR_CHIP_EC_FEATURE_HW364887_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887;
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887 = fapi2::ATTR_CHIP_EC_FEATURE_HW364887[TGT0];
    fapi2::ATTR_CHIP_EC_FEATURE_HW426816_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816;
    l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816 = fapi2::ATTR_CHIP_EC_FEATURE_HW426816[TGT0];
    uint64_t l_def_NVLINK_ACTIVE_OB3 = l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[3] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV;
    fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
    l_TGT1_ATTR_SMF_CONFIG = fapi2::ATTR_SMF_CONFIG[TGT1];
    fapi2::buffer<uint64_t> l_scom_buffer;
    fapi2::getScom(TGT0, 0x5011000, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011000, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011001, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011001, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011002, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011002, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011003, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011003, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011008, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011008, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011010, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011010, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501101A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501101A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501101B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501101B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011030, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011030, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011031, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011031, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011032, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011032, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011033, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011033, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011038, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011038, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011040, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011040, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501104A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501104A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501104B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501104B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011060, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011060, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011061, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011061, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011062, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011062, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011063, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011063, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011068, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011068, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011070, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011070, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501107A, l_scom_buffer);
        l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501107A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501107B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501107B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011090, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011090, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011091, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011091, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011092, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011092, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011093, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011093, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011098, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011098, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50110A0, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x50110A0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50110AA, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50110AA, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50110AB, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50110AB, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50110C0, l_scom_buffer);
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    l_scom_buffer.insert<16, 6, 58, uint64_t>(0x04);
    l_scom_buffer.insert<22, 6, 58, uint64_t>(0x0C);
    l_scom_buffer.insert<28, 6, 58, uint64_t>(0x04);
    l_scom_buffer.insert<34, 6, 58, uint64_t>(0x0C);
    l_scom_buffer.insert<40, 4, 60, uint64_t>(0x04);
    l_scom_buffer.insert<44, 4, 60, uint64_t>(0x04);
    fapi2::putScom(TGT0, 0x50110C0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50110D0, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<1, 3, 61, uint64_t>(0x04);
        l_scom_buffer.insert<4, 10, 54, uint64_t>(0x100);
        l_scom_buffer.insert<14, 10, 54, uint64_t>(0x200);
        l_scom_buffer.insert<24, 10, 54, uint64_t>(0x300);
    }
    fapi2::putScom(TGT0, 0x50110D0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011120, l_scom_buffer);
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011120, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011200, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }

    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011200, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011201, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011201, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011202, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011202, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011203, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011203, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011208, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011208, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011210, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011210, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501121A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501121A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501121B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501121B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011230, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011230, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011231, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011231, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011232, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011232, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011233, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011233, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011238, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011238, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011240, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011240, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501124A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501124A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501124B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501124B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011260, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011260, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011261, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011261, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011262, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011262, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011263, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011263, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011268, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011268, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011270, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011270, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501127A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501127A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501127B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501127B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011290, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011290, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011291, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011291, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011292, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011292, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011293, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011293, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011298, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011298, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50112A0, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x50112A0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50112AA, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50112AA, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50112AB, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50112AB, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50112C0, l_scom_buffer);
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<16, 6, 58, uint64_t>(0x04);
        l_scom_buffer.insert<22, 6, 58, uint64_t>(0x0C);
        l_scom_buffer.insert<28, 6, 58, uint64_t>(0x04);
        l_scom_buffer.insert<34, 6, 58, uint64_t>(0x0C);
        l_scom_buffer.insert<40, 4, 60, uint64_t>(0x04);
        l_scom_buffer.insert<44, 4, 60, uint64_t>(0x04);
    }
    fapi2::putScom(TGT0, 0x50112C0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50112D0, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<1, 3, 61, uint64_t>(0x04);
        l_scom_buffer.insert<4, 10, 54, uint64_t>(0x100);
        l_scom_buffer.insert<14, 10, 54, uint64_t>(0x200);
        l_scom_buffer.insert<24, 10, 54, uint64_t>(0x300);
    }
    fapi2::putScom(TGT0, 0x50112D0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501135A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<32, 8, 56, uint64_t>(0x7E);
    }
    fapi2::putScom(TGT0, 0x501135A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501138A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<32, 8, 56, uint64_t>(0x7E);
    }
    fapi2::putScom(TGT0, 0x501138A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50113B0, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0FFE6FC00FF1B000);
    }
    fapi2::putScom(TGT0, 0x50113B0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011400, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011400, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011401, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011401, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011402, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011402, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011403, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011403, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011408, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011408, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011410, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011410, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501141A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501141A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501141B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501141B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011430, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG == 1)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625 || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011430, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011431, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011431, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011432, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011432, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011433, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011433, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011438, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011438, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011440, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011440, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501144A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501144A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501144B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501144B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011460, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625)
    || (l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011460, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011461, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011461, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011462, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011462, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011463, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011463, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011468, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011468, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011470, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x5011470, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501147A, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501147A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501147B, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x501147B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011490, l_scom_buffer);
    if(l_def_NUM_X_LINKS_CFG)
    {
        l_scom_buffer.insert<5, 1, 63, uint64_t>(1);
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW410625
    || l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION10)
    {
        l_scom_buffer.insert<6, 1, 63, uint64_t>(0);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011490, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011491, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW364887)
    {
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011491, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011492, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<28, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
        l_scom_buffer.insert<4, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T1);
        l_scom_buffer.insert<16, 12, 52, uint64_t>(l_TGT1_ATTR_PROC_EPS_WRITE_CYCLES_T2);
    }
    fapi2::putScom(TGT0, 0x5011492, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011493, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(0x08);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<26, 6, 58, uint64_t>(1);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<32, 6, 58, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011493, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011498, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
    }
    fapi2::putScom(TGT0, 0x5011498, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50114A0, l_scom_buffer);
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(0x66);
    }
    if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW372457)
    {
        l_scom_buffer.insert<4, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x67);
    }
    else if(l_TGT0_ATTR_CHIP_EC_FEATURE_HW426816)
    {
        l_scom_buffer.insert<8, 8, 56, uint64_t>(0x4B);
    }
    fapi2::putScom(TGT0, 0x50114A0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50114AA, l_scom_buffer);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50114AA, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50114AB, l_scom_buffer);
    l_scom_buffer.insert<0, 1, 63, uint64_t>(1);
    fapi2::putScom(TGT0, 0x50114AB, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50114C0, l_scom_buffer);
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<16, 6, 58, uint64_t>(0x04);
        l_scom_buffer.insert<22, 6, 58, uint64_t>(0x0C);
        l_scom_buffer.insert<28, 6, 58, uint64_t>(0x04);
        l_scom_buffer.insert<34, 6, 58, uint64_t>(0x0C);
        l_scom_buffer.insert<40, 4, 60, uint64_t>(0x04);
        l_scom_buffer.insert<44, 4, 60, uint64_t>(0x04);
    }
    fapi2::putScom(TGT0, 0x50114C0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x50114D0, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<1, 3, 61, uint64_t>(0x04);
        l_scom_buffer.insert<4, 10, 54, uint64_t>(0x100);
        l_scom_buffer.insert<14, 10, 54, uint64_t>(0x200);
        l_scom_buffer.insert<24, 10, 54, uint64_t>(0x300);
    }
    fapi2::putScom(TGT0, 0x50114D0, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501155A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<32, 8, 56, uint64_t>(0x7E);
    }
    fapi2::putScom(TGT0, 0x501155A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501158A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<32, 8, 56, uint64_t>(0x7E);
    }
    fapi2::putScom(TGT0, 0x501158A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011645, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<56, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);
    }
    fapi2::putScom(TGT0, 0x5011645, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501165A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<32, 8, 56, uint64_t>(0x7E);
    }
    fapi2::putScom(TGT0, 0x501165A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011682, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 12, 52, uint64_t>(0xFFF);
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0x04);
        l_scom_buffer.insert<16, 4, 60, uint64_t>(0x8);
    }
    fapi2::putScom(TGT0, 0x5011682, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011683, l_scom_buffer);
    if(l_def_NVLINK_ACTIVE_OB0)
    {
        l_scom_buffer.insert<0, 7, 0, uint64_t>(0xE000000000000000);
    }
    else
    {
        l_scom_buffer.insert<0, 7, 0, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5011683, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011689, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0000740000000000);
    }
    fapi2::putScom(TGT0, 0x5011689, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501168A, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x7F60B04500AC0000);
    }
    fapi2::putScom(TGT0, 0x501168A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501168B, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0xAAA70A55F0000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0xA8070A55F0000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x02A70A55F0000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x00070A55F0000000);
        }
    }
    fapi2::putScom(TGT0, 0x501168B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501168D, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x5550740000000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x5400740000000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0150740000000000);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0000740000000000);
        }
    }
    fapi2::putScom(TGT0, 0x501168D, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5011700, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0FFE6FC00FF1B000);
    }
    fapi2::putScom(TGT0, 0x5011700, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C03, l_scom_buffer);
    if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 0)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x009A48180F63FFFF);
    }
    else if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 1)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x009A48180F03FFFF);
    }
    else if((l_def_NPU_ACTIVE == 0))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFFFFFFFFFFFFFFF);
    }
    fapi2::putScom(TGT0, 0x5013C03, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C06, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        if(l_def_ENABLE_NPU_FREEZE)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x7F60B04500AE0000);
        }
        else
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0);
        }
    }
    fapi2::putScom(TGT0, 0x5013C06, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C07, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        if(l_def_ENABLE_NPU_FREEZE)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFF65B04700FE0000);
        }
        else
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x8005000200100000);
        }
    }
    fapi2::putScom(TGT0, 0x5013C07, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C0A, l_scom_buffer);
    if((l_chip_id == 0x5 && l_chip_ec == 0x22)
    || (l_chip_id == 0x5 && l_chip_ec == 0x23)
    || (l_chip_id == 0x6 && l_chip_ec == 0x12)
    || (l_chip_id == 0x6 && l_chip_ec == 0x13))
    {
        if((l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED))
        {
            l_scom_buffer.insert<0, 2, 62, uint64_t>(0x02);
        }
    }
    fapi2::putScom(TGT0, 0x5013C0A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C43, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x5550F4000FFFFFFF);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 1 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0x57F0F4000FFFFFFF);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 1)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFD50F4000FFFFFFF);
        }
        else if(l_def_NVLINK_ACTIVE_OB0 == 0 && l_def_NVLINK_ACTIVE_OB3 == 0)
        {
            l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFF0F4000FFFFFFF);
        }
    }
    else
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFFFFFFFFFFFFFFF);
    }
    fapi2::putScom(TGT0, 0x5013C43, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C46, l_scom_buffer);
    if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 0)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0);
    }
    else if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 1)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFF70A5DF0000000);
    }
    fapi2::putScom(TGT0, 0x5013C46, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C47, l_scom_buffer);
    if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 0)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x000801A200000000);
    }
    else if(((l_def_NPU_ACTIVE == 1) && (l_def_ENABLE_NPU_FREEZE == 1)))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFFF0BFFF0000000);
    }
    fapi2::putScom(TGT0, 0x5013C47, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C83, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xF001803FF00C0FFF);
    }
    else if((l_def_NPU_ACTIVE == 0))
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0xFFFFFFFFFFFFFFFF);
    }
    fapi2::putScom(TGT0, 0x5013C83, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C86, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0);
    }
    fapi2::putScom(TGT0, 0x5013C86, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013C87, l_scom_buffer);
    if(l_def_NPU_ACTIVE)
    {
        l_scom_buffer.insert<0, 64, 0, uint64_t>(0x0FFE7FC00FF3F000);
    }
    fapi2::putScom(TGT0, 0x5013C87, l_scom_buffer);
}

fapi2::ReturnCode p9n_mcs_scom(const fapi2::Target<fapi2::TARGET_TYPE_MCS>& TGT0,
                               const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1, const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT2,
                               const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& TGT3)
{

    fapi2::ATTR_EC_Type   l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT2, l_chip_id);
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT2, l_chip_ec);
    fapi2::ATTR_CHIP_EC_FEATURE_HW398139_Type l_TGT2_ATTR_CHIP_EC_FEATURE_HW398139;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW398139, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_HW398139);
    fapi2::ATTR_ENABLE_MEM_EARLY_DATA_SCOM_Type l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM;
    FAPI_ATTR_GET(fapi2::ATTR_ENABLE_MEM_EARLY_DATA_SCOM, TGT1, l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM);
    fapi2::ATTR_SMF_CONFIG_Type l_TGT1_ATTR_SMF_CONFIG;
    FAPI_ATTR_GET(fapi2::ATTR_SMF_CONFIG, TGT1, l_TGT1_ATTR_SMF_CONFIG);
    fapi2::ATTR_RISK_LEVEL_Type l_TGT1_ATTR_RISK_LEVEL;
    FAPI_ATTR_GET(fapi2::ATTR_RISK_LEVEL, TGT1, l_TGT1_ATTR_RISK_LEVEL);
    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, l_TGT1_ATTR_FREQ_PB_MHZ);
    fapi2::ATTR_MSS_FREQ_Type l_TGT3_ATTR_MSS_FREQ;
    FAPI_ATTR_GET(fapi2::ATTR_MSS_FREQ, TGT3, l_TGT3_ATTR_MSS_FREQ);
    uint64_t l_def_mn_freq_ratio = ((1000 * l_TGT3_ATTR_MSS_FREQ) / l_TGT1_ATTR_FREQ_PB_MHZ);
    uint64_t l_def_ENABLE_MCU_TIMEOUTS = 1;
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom(TGT0, 0x5010810, l_scom_buffer);
    l_scom_buffer.insert<46, 4, 60, uint64_t>(7);
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0);
    l_scom_buffer.insert<61, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PF_DROP_CMDLIST_ON
    if((l_TGT2_ATTR_CHIP_EC_FEATURE_HW398139 == 1))
    {
        l_scom_buffer.insert<32, 7, 57, uint64_t>(8);
    }
    else if((l_TGT2_ATTR_CHIP_EC_FEATURE_HW398139 != 1))
    {
        l_scom_buffer.insert<32, 7, 57, uint64_t>(25);
    }

    l_scom_buffer.insert<55, 6, 58, uint64_t>(0x0F);
    l_scom_buffer.insert<63, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PREFETCH_PROMOTE_ON
    fapi2::putScom(TGT0, 0x5010810, l_scom_buffer);
    l_scom_buffer.insert<20, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_CENTAUR_SYNC_ON
    l_scom_buffer.insert<9, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_64_128B_READ_ON
    l_scom_buffer.insert<8, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_DROP_FP_DYN64_ACTIVE_ON
    l_scom_buffer.insert<27, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_SYNC_ON
    l_scom_buffer.insert<28, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_PAIR_SYNC_ON
    l_scom_buffer.insert<17, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_FORCE_COMMANDLIST_VALID_ON
    if(l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM == fapi2::ENUM_ATTR_ENABLE_MEM_EARLY_DATA_SCOM_OFF)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(0); // l_MC01_PBI01_SCOMFIR_MCMODE0_CENTAURP_ENABLE_ECRESP_OFF
    }
    else if(l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM == fapi2::ENUM_ATTR_ENABLE_MEM_EARLY_DATA_SCOM_ON)
    {
        l_scom_buffer.insert<7, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE0_CENTAURP_ENABLE_ECRESP_ON
    }
    fapi2::putScom(TGT0, 0x5010811, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010812, l_scom_buffer);
    l_scom_buffer.insert<10, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE1_DISABLE_FP_M_BIT_ON
    l_scom_buffer.insert<33, 19, 45, uint64_t>(0x40);
    fapi2::putScom(TGT0, 0x5010812, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010813, l_scom_buffer);
    if((l_chip_id == 0x5 && l_chip_ec == 0x22)
    || (l_chip_id == 0x5 && l_chip_ec == 0x23))
    {
        if((l_TGT1_ATTR_SMF_CONFIG == fapi2::ENUM_ATTR_SMF_CONFIG_ENABLED))
        {
            l_scom_buffer.insert<19, 2, 62, uint64_t>(2);
        }
    }
    if((l_TGT1_ATTR_RISK_LEVEL == 0))
    {
        l_scom_buffer.insert<1, 13, 51, uint64_t>(0x300);
    }
    if(l_def_mn_freq_ratio <= 1350)
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0); // l_MC01_PBI01_SCOMFIR_MCMODE2_FORCE_SFSTAT_ACTIVE_OFF
    }
    else if(l_def_mn_freq_ratio > 1350)
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCMODE2_FORCE_SFSTAT_ACTIVE_ON
    }
    l_scom_buffer.insert<24, 16, 48, uint64_t>(0x08);
    fapi2::putScom(TGT0, 0x5010813, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501081B, l_scom_buffer);
    if(l_def_ENABLE_MCU_TIMEOUTS == 1)
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCTO_SELECT_PB_HANG_PULSE_ON
        l_scom_buffer.insert<32, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_NONMIRROR_HANG_ON
        l_scom_buffer.insert<34, 1, 63, uint64_t>(1); // l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_APO_HANG_ON
    }
    l_scom_buffer.insert<1, 1, 63, uint64_t>(0); // l_MC01_PBI01_SCOMFIR_MCTO_SELECT_LOCAL_HANG_PULSE_OFF
    l_scom_buffer.insert<2, 2, 62, uint64_t>(1);
    if(l_def_ENABLE_MCU_TIMEOUTS == 1)
    {
        l_scom_buffer.insert<24, 8, 56, uint64_t>(1);
        l_scom_buffer.insert<5, 3, 61, uint64_t>(7);
    }
    fapi2::putScom(TGT0, 0x501081B, l_scom_buffer);
}

fapi2::ReturnCode p9c_dmi_scom(const fapi2::Target<fapi2::TARGET_TYPE_DMI>& TGT0,
                               const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1, const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT3)
{

    fapi2::ATTR_EC_Type   l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT3, l_chip_id);
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT3, l_chip_ec);
    uint64_t l_def_ENABLE_AMO_CACHING = 1;
    uint64_t l_def_ENABLE_AMO_CLEAN_LINES = 1;
    fapi2::ATTR_RISK_LEVEL_Type l_TGT1_ATTR_RISK_LEVEL;
    FAPI_ATTR_GET(fapi2::ATTR_RISK_LEVEL, TGT1, l_TGT1_ATTR_RISK_LEVEL);
    fapi2::ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13_Type l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13;
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13, TGT3,
                            l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13));
    uint64_t l_def_ENABLE_DYNAMIC_64_128B_READS = 0;
    uint64_t l_def_ENABLE_PREFETCH_DROP_PROMOTE_BASIC = 1;
    uint64_t l_def_ENABLE_RMW_IN_PROC = 1;
    uint64_t l_def_ENABLE_PREFETCH_DROP_PROMOTE_PERFORMANCE = 1;
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T0_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_EPS_READ_CYCLES_T0, TGT1, l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0);
    uint64_t l_def_MC_EPSILON_CFG_T0 = ((l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T0 + 6) / 4);
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T1_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_EPS_READ_CYCLES_T1, TGT1, l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1);
    uint64_t l_def_MC_EPSILON_CFG_T1 = ((l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4);
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T2_Type l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_EPS_READ_CYCLES_T2, TGT1, l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2);
    uint64_t l_def_MC_EPSILON_CFG_T2 = ((l_TGT1_ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4);
    uint64_t l_def_ENABLE_MCBUSY = 1;
    fapi2::ATTR_CHIP_UNIT_POS_Type l_TGT0_ATTR_CHIP_UNIT_POS;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, TGT0, l_TGT0_ATTR_CHIP_UNIT_POS);
    uint64_t l_def_POSITION = l_TGT0_ATTR_CHIP_UNIT_POS;
    uint64_t l_def_CHAN0_OR_1 = ((l_def_POSITION % 4) < 2);
    fapi2::ATTR_ENABLE_MEM_EARLY_DATA_SCOM_Type l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM;
    FAPI_ATTR_GET(fapi2::ATTR_ENABLE_MEM_EARLY_DATA_SCOM, TGT1, l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM);
    uint64_t l_def_ENABLE_MCU_TIMEOUTS = 1;
    uint64_t l_def_MCICFG_REPLAY_DELAY = 1;
    fapi2::ATTR_MC_SYNC_MODE_Type l_TGT3_ATTR_MC_SYNC_MODE;
    FAPI_ATTR_GET(fapi2::ATTR_MC_SYNC_MODE, TGT3, l_TGT3_ATTR_MC_SYNC_MODE);
    fapi2::ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC_Type l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC;
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC, TGT3,
                            l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC));
    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, l_TGT1_ATTR_FREQ_PB_MHZ);
    fapi2::ATTR_FREQ_MCA_MHZ_Type l_TGT1_ATTR_FREQ_MCA_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_MCA_MHZ, TGT1, l_TGT1_ATTR_FREQ_MCA_MHZ);
    uint64_t l_def_MCA_FREQ = l_TGT1_ATTR_FREQ_MCA_MHZ;
    uint64_t l_def_MN_FREQ_RATIO = ((1000 * l_def_MCA_FREQ) / l_TGT1_ATTR_FREQ_PB_MHZ);
    uint64_t l_def_ENABLE_HWFM = 1;
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom(TGT0, 0x5010823, l_scom_buffer);
    if(l_def_ENABLE_AMO_CACHING)
    {
        l_scom_buffer.insert<22, 6, 58, uint64_t>(24);
    }
    fapi2::putScom(TGT0, 0x5010823, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010824, l_scom_buffer);
    if((l_def_ENABLE_AMO_CLEAN_LINES == 1))
    {
        l_scom_buffer.insert<44, 6, 58, uint64_t>(12);
    }
    if(!l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13
    || (l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13 && l_TGT1_ATTR_RISK_LEVEL == 0)
    || (l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13 && l_TGT1_ATTR_RISK_LEVEL == 1))
    {
        l_scom_buffer.insert<55, 5, 59, uint64_t>(0x0A);
    }
    else if((l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13 && l_TGT1_ATTR_RISK_LEVEL == 0x4)
            || (l_TGT3_ATTR_CHIP_EC_FEATURE_HW439321_FIXED_IN_P9UDD13 && l_TGT1_ATTR_RISK_LEVEL == 0x5))
    {
        l_scom_buffer.insert<55, 5, 59, uint64_t>(0x1F);
    }
    if(l_def_ENABLE_DYNAMIC_64_128B_READS)
    {
        l_scom_buffer.insert<36, 1, 63, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF2_EN_64_128_PB_READ_ON
        l_scom_buffer.insert<19, 5, 59, uint64_t>(8);
    }
    else
    {
        l_scom_buffer.insert<36, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF2_EN_64_128_PB_READ_OFF
    }
    if(l_def_ENABLE_PREFETCH_DROP_PROMOTE_BASIC)
    {
        l_scom_buffer.insert<0, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<3, 3, 61, uint64_t>(0x3);
        l_scom_buffer.insert<6, 3, 61, uint64_t>(0x5);
        l_scom_buffer.insert<9, 3, 61, uint64_t>(0x7);
    }
    l_scom_buffer.insert<40, 4, 60, uint64_t>(4);
    l_scom_buffer.insert<28, 4, 60, uint64_t>(0x4);
    l_scom_buffer.insert<16, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF2_ENABLE_REFRESH_BLOCK_SQ_OFF
    l_scom_buffer.insert<17, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF2_ENABLE_REFRESH_BLOCK_NSQ_OFF
    l_scom_buffer.insert<18, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF2_ENABLE_REFRESH_BLOCK_DISP_OFF
    l_scom_buffer.insert<50, 5, 59, uint64_t>(28);
    fapi2::putScom(TGT0, 0x5010824, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010825, l_scom_buffer);
    if((l_def_ENABLE_AMO_CLEAN_LINES == 1))
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCAMOC_ENABLE_CLEAN_ON
    }
    if((l_def_ENABLE_RMW_IN_PROC == 1))
    {
        l_scom_buffer.insert<32, 25, 39, uint64_t>(0x1FF);
    }
    if(l_def_ENABLE_AMO_CACHING)
    {
        l_scom_buffer.insert<4, 25, 39, uint64_t>(0x19FFFFF);
        l_scom_buffer.insert<29, 3, 61, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCAMOC_AMO_SIZE_SELECT_128B_RW_64B_DATA
    }
    if(l_def_ENABLE_PREFETCH_DROP_PROMOTE_PERFORMANCE)
    {
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCAMOC_FORCE_PF_DROP0_OFF
    }
    fapi2::putScom(TGT0, 0x5010825, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010826, l_scom_buffer);
    l_scom_buffer.insert<0, 8, 56, uint64_t>(1);
    l_scom_buffer.insert<8, 8, 56, uint64_t>(l_def_MC_EPSILON_CFG_T0);
    l_scom_buffer.insert<16, 8, 56, uint64_t>(l_def_MC_EPSILON_CFG_T1);
    l_scom_buffer.insert<24, 8, 56, uint64_t>(l_def_MC_EPSILON_CFG_T1);
    l_scom_buffer.insert<32, 8, 56, uint64_t>(l_def_MC_EPSILON_CFG_T2);
    l_scom_buffer.insert<40, 8, 56, uint64_t>(l_def_MC_EPSILON_CFG_T2);
    fapi2::putScom(TGT0, 0x5010826, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5010827, l_scom_buffer);
    if(l_def_ENABLE_MCBUSY)
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCBUSYQ_ENABLE_BUSY_COUNTERS_ON
        l_scom_buffer.insert<1, 3, 61, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCBUSYQ_BUSY_COUNTER_WINDOW_SELECT_1024_CYCLES
        l_scom_buffer.insert<4, 10, 54, uint64_t>(0x26);
        l_scom_buffer.insert<14, 10, 54, uint64_t>(0x33);
        l_scom_buffer.insert<24, 10, 54, uint64_t>(0x40);
    }
    fapi2::putScom(TGT0, 0x5010827, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501082A, l_scom_buffer);
    l_scom_buffer.insert<12, 5, 59, uint64_t>(0x0F);
    l_scom_buffer.insert<17, 5, 59, uint64_t>(1);
    if((l_def_CHAN0_OR_1 == 1))
    {
        l_scom_buffer.insert<22, 10, 54, uint64_t>(0x20);
    }
    else
    {
        l_scom_buffer.insert<22, 10, 54, uint64_t>(0x10);
    }
    fapi2::putScom(TGT0, 0x501082A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501082B, l_scom_buffer);
    if(l_def_ENABLE_AMO_CACHING)
    {
        l_scom_buffer.insert<45, 1, 63, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF3_AMO_LIMIT_SEL_ON
    }
    if(l_def_ENABLE_PREFETCH_DROP_PROMOTE_PERFORMANCE)
    {
        l_scom_buffer.insert<2, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF3_EN_PF_CONF_RETRY_OFF
        l_scom_buffer.insert<15, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<19, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<23, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<27, 4, 60, uint64_t>(0);
    }
    if(l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM == fapi2::ENUM_ATTR_ENABLE_MEM_EARLY_DATA_SCOM_OFF)
    {
        l_scom_buffer.insert<43, 1, 63, uint64_t>(1); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF3_ENABLE_CP_M_MDI0_LOCAL_ONLY_ON
    }
    else if(l_TGT1_ATTR_ENABLE_MEM_EARLY_DATA_SCOM == fapi2::ENUM_ATTR_ENABLE_MEM_EARLY_DATA_SCOM_ON)
    {
        l_scom_buffer.insert<43, 1, 63, uint64_t>(0); // l_MC01_CHAN0_ATCL_CL_CLSCOM_MCPERF3_ENABLE_CP_M_MDI0_LOCAL_ONLY_OFF
    }
    fapi2::putScom(TGT0, 0x501082B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x701090A, l_scom_buffer);
    if((l_def_ENABLE_MCU_TIMEOUTS == 1))
    {
        l_scom_buffer.insert<47, 3, 61, uint64_t>(3);
    }
    l_scom_buffer.insert<21, 4, 60, uint64_t>(l_def_MCICFG_REPLAY_DELAY);
    fapi2::putScom(TGT0, 0x701090A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x701090E, l_scom_buffer);
    if((l_TGT3_ATTR_MC_SYNC_MODE == 1))
    {
        l_scom_buffer.insert<33, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MCICFG1Q_ECC_RDC_CFG_FIFO_TENURE_3_OFF
    }
    else
    {
        else if((l_def_MN_FREQ_RATIO < 1167 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 1)
                || (l_def_MN_FREQ_RATIO >= 1167 && l_def_MN_FREQ_RATIO < 1273))
        {
            l_scom_buffer.insert<33, 1, 63, uint64_t>(1); // l_MCP_CHAN0_CHI_MCICFG1Q_ECC_RDC_CFG_FIFO_TENURE_3_ON
        }
        else if(l_def_MN_FREQ_RATIO >= 1273 && l_def_MN_FREQ_RATIO < 1500)
        {
            l_scom_buffer.insert<33, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MCICFG1Q_ECC_RDC_CFG_FIFO_TENURE_3_OFF
        }
    }
    if((l_def_ENABLE_HWFM == 1))
    {
        l_scom_buffer.insert<39, 1, 63, uint64_t>(1); // l_MCP_CHAN0_CHI_MCICFG1Q_CFG_SEL_UE_4_FORCE_MIRROR_MODE_ON
    }
    else
    {
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MCICFG1Q_CFG_SEL_UE_4_FORCE_MIRROR_MODE_OFF
    }
    l_scom_buffer.insert<4, 1, 63, uint64_t>(0);
    l_scom_buffer.insert<47, 1, 63, uint64_t>(1);
    l_scom_buffer.insert<5, 1, 63, uint64_t>(0);
    l_scom_buffer.insert<40, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MCICFG1Q_CFG_SEL_SUE_4_FORCE_MIRROR_MODE_OFF
    fapi2::putScom(TGT0, 0x701090E, l_scom_buffer);
    fapi2::getScom(TGT0, 0x7010914, l_scom_buffer);
    if((l_TGT3_ATTR_MC_SYNC_MODE == 1 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 0)
    || (l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO >= 1167 && l_def_MN_FREQ_RATIO < 1200))
    {
        l_scom_buffer.insert<37, 1, 63, uint64_t>(1); // l_MCP_CHAN0_CHI_MBSECCQ_DELAY_NONBYPASS_ON
    }
    else if((l_TGT3_ATTR_MC_SYNC_MODE == 1 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 1)
            || (l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO < 1167)
            || (l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO >= 1200 && l_def_MN_FREQ_RATIO < 1500))
    {
        l_scom_buffer.insert<37, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MBSECCQ_DELAY_NONBYPASS_OFF
    }
    if(l_TGT3_ATTR_MC_SYNC_MODE == 1 || l_def_MN_FREQ_RATIO < 1273)
    {
        l_scom_buffer.insert<35, 2, 62, uint64_t>(0);
    }
    else if(l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO >= 1273 && l_def_MN_FREQ_RATIO < 1400)
    {
        l_scom_buffer.insert<35, 2, 62, uint64_t>(1);
    }
    else if(l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO >= 1400 && l_def_MN_FREQ_RATIO < 1500)
    {
        l_scom_buffer.insert<35, 2, 62, uint64_t>(2);
    }
    if((l_TGT3_ATTR_MC_SYNC_MODE == 1 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 0)
    || (l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO >= 1167 && l_def_MN_FREQ_RATIO < 1500 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 1))
    {
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0); // l_MCP_CHAN0_CHI_MBSECCQ_DELAY_VALID_1X_OFF
    }
    else if((l_TGT3_ATTR_MC_SYNC_MODE == 1 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 1)
            || (l_TGT3_ATTR_MC_SYNC_MODE == 0 && l_def_MN_FREQ_RATIO < 1167 && l_TGT3_ATTR_CHIP_EC_FEATURE_HW413362_P9UDD11_ASYNC == 1))
    {
        l_scom_buffer.insert<34, 1, 63, uint64_t>(1); // l_MCP_CHAN0_CHI_MBSECCQ_DELAY_VALID_1X_ON
    }
    fapi2::putScom(TGT0, 0x7010914, l_scom_buffer);
    fapi2::getScom(TGT0, 0x7012344, l_scom_buffer);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_WDF_ON
    l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ0_DEBUG_0_ON
    l_scom_buffer.insert<4, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ1_DEBUG_0_ON
    l_scom_buffer.insert<6, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ2_DEBUG_0_ON
    l_scom_buffer.insert<8, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ3_DEBUG_0_ON
    l_scom_buffer.insert<10, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ4_DEBUG_0_ON
    l_scom_buffer.insert<12, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WDF_DBG_SEL_PWSEQ5_DEBUG_0_ON
    fapi2::putScom(TGT0, 0x7012344, l_scom_buffer);
    fapi2::getScom(TGT0, 0x7012348, l_scom_buffer);
    if(l_def_ENABLE_AMO_CACHING)
    {
        l_scom_buffer.insert<9, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WRITE_NEW_WRITE_64B_MODE_ON
    }
    fapi2::putScom(TGT0, 0x7012348, l_scom_buffer);
    fapi2::getScom(TGT0, 0x701234B, l_scom_buffer);
    l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WRITE_NEST_DBG_SEL_WRT_ON
    l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_MCP_CHAN0_WRITE_WBMGR_DBG_0_SELECT_ON
    fapi2::putScom(TGT0, 0x701234B, l_scom_buffer);
}

fapi2::ReturnCode p9_fbc_ioo_tl_scom(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::ATTR_EC_Type   l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT0, l_chip_id);
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG, TGT0, l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG);
    fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG, TGT0, l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG);
    uint64_t l_def_OBUS0_FBC_ENABLED =
            (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[3] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[0] != fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_FALSE);
    fapi2::ATTR_FREQ_A_MHZ_Type l_TGT1_ATTR_FREQ_A_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_A_MHZ, TGT1, l_TGT1_ATTR_FREQ_A_MHZ);
    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, l_TGT1_ATTR_FREQ_PB_MHZ);
    uint64_t l_def_LO_LIMIT_R = l_TGT1_ATTR_FREQ_PB_MHZ * 10 > l_TGT1_ATTR_FREQ_A_MHZ * 12;
    uint64_t l_def_OBUS0_LO_LIMIT_D = l_TGT1_ATTR_FREQ_A_MHZ * 10;
    uint64_t l_def_OBUS0_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 154;
    uint64_t l_def_OBUS1_FBC_ENABLED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[4] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_OBUS1_LO_LIMIT_D = l_TGT1_ATTR_FREQ_A_MHZ;
    uint64_t l_def_OBUS1_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 12;
    uint64_t l_def_OBUS2_FBC_ENABLED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[5] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[2] != fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_OBUS2_LO_LIMIT_D = l_TGT1_ATTR_FREQ_A_MHZ * 10;
    uint64_t l_def_OBUS2_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 74;
    uint64_t l_def_OBUS3_FBC_ENABLED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[6] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[3] != fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_FALSE;
    uint64_t l_def_OBUS3_LO_LIMIT_D = l_TGT1_ATTR_FREQ_A_MHZ * 10;
    uint64_t l_def_OBUS3_LO_LIMIT_N = l_TGT1_ATTR_FREQ_PB_MHZ * 95;
    fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE_Type l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_SMP_OPTICS_MODE, TGT1, l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE);
    uint64_t l_def_OPTICS_IS_A_BUS =
            l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_SMP_OPTICS_MODE_OPTICS_IS_A_BUS;
    uint64_t l_def_OB0_IS_PAIRED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[3] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[0] == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE;
    uint64_t l_def_OB1_IS_PAIRED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[4] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[1] == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE;
    uint64_t l_def_OB2_IS_PAIRED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[5] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[2] == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE;
    uint64_t l_def_OB3_IS_PAIRED =
            l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[6] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
        || l_TGT0_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[3] == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE;
    fapi2::ATTR_PROC_NPU_REGION_ENABLED_Type l_TGT0_ATTR_PROC_NPU_REGION_ENABLED;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_NPU_REGION_ENABLED, TGT0, l_TGT0_ATTR_PROC_NPU_REGION_ENABLED);
    fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_Type l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE, TGT0, l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE);
    uint64_t l_def_NVLINK_ACTIVE =
            l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[0] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[1] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[2] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
        || l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[3] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_NV
                                        && l_TGT0_ATTR_PROC_NPU_REGION_ENABLED));
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom(TGT0, 0x501380A, l_scom_buffer);
    if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0); // l_PB_IOO_SCOM_A0_MODE_NORMAL
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A0_MODE_NORMAL);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A0_MODE_NORMAL);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A0_MODE_NORMAL);
        l_scom_buffer.insert<22, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<12, 8, 56, uint64_t>(0x40);
        l_scom_buffer.insert<44, 8, 56, uint64_t>(0x40);
    }
    else
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0xF); // l_PB_IOO_SCOM_A0_MODE_BLOCKED
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED);
    }
    if(l_def_OBUS0_FBC_ENABLED && l_def_LO_LIMIT_R == 1)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x36 - (l_def_OBUS0_LO_LIMIT_N / l_def_OBUS0_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x36 - (l_def_OBUS0_LO_LIMIT_N / l_def_OBUS0_LO_LIMIT_D));
    }
    else if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x37 - (l_def_OBUS0_LO_LIMIT_N / l_def_OBUS0_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x37 - (l_def_OBUS0_LO_LIMIT_N / l_def_OBUS0_LO_LIMIT_D));
    }
    fapi2::putScom(TGT0, 0x501380A, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501380B, l_scom_buffer);
    if(l_def_OBUS1_FBC_ENABLED)
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0); // l_PB_IOO_SCOM_A1_MODE_NORMAL
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A1_MODE_NORMAL);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A1_MODE_NORMAL);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A1_MODE_NORMAL);
        l_scom_buffer.insert<22, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<12, 8, 56, uint64_t>(0x40);
        l_scom_buffer.insert<44, 8, 56, uint64_t>(0x40);
    }
    else
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0xF); // l_PB_IOO_SCOM_A1_MODE_BLOCKED
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED);
    }
    if(l_def_OBUS1_FBC_ENABLED && l_def_LO_LIMIT_R == 1)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x2A - (l_def_OBUS1_LO_LIMIT_N / l_def_OBUS1_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x2A - (l_def_OBUS1_LO_LIMIT_N / l_def_OBUS1_LO_LIMIT_D));
    }
    else if(l_def_OBUS1_FBC_ENABLED)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x2C - (l_def_OBUS1_LO_LIMIT_N / l_def_OBUS1_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x2C - (l_def_OBUS1_LO_LIMIT_N / l_def_OBUS1_LO_LIMIT_D));
    }
    fapi2::putScom(TGT0, 0x501380B, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501380C, l_scom_buffer);

    if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0); // l_PB_IOO_SCOM_A2_MODE_NORMAL
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A2_MODE_NORMAL);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A2_MODE_NORMAL);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A2_MODE_NORMAL);
        l_scom_buffer.insert<22, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<12, 8, 56, uint64_t>(0x40);
        l_scom_buffer.insert<44, 8, 56, uint64_t>(0x40);
    }
    else
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0xF); // l_PB_IOO_SCOM_A2_MODE_BLOCKED
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED);
    }

    if(l_def_OBUS2_FBC_ENABLED && l_def_LO_LIMIT_R == 1)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x1B - (l_def_OBUS2_LO_LIMIT_N / l_def_OBUS2_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x1B - (l_def_OBUS2_LO_LIMIT_N / l_def_OBUS2_LO_LIMIT_D));
    }
    else if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x1C - (l_def_OBUS2_LO_LIMIT_N / l_def_OBUS2_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x1C - (l_def_OBUS2_LO_LIMIT_N / l_def_OBUS2_LO_LIMIT_D));
    }
    fapi2::putScom(TGT0, 0x501380C, l_scom_buffer);
    fapi2::getScom(TGT0, 0x501380D, l_scom_buffer);
    if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0); // l_PB_IOO_SCOM_A3_MODE_NORMAL
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A3_MODE_NORMAL);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A3_MODE_NORMAL);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A3_MODE_NORMAL);
        l_scom_buffer.insert<22, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<12, 8, 56, uint64_t>(0x40);
        l_scom_buffer.insert<44, 8, 56, uint64_t>(0x40);
    }
    else
    {
        l_scom_buffer.insert<20, 1, 60, uint64_t>(0xF); // l_PB_IOO_SCOM_A3_MODE_BLOCKED
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED);
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED);
    }
    if(l_def_OBUS3_FBC_ENABLED && l_def_LO_LIMIT_R == 1)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x22 - (l_def_OBUS3_LO_LIMIT_N / l_def_OBUS3_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x22 - (l_def_OBUS3_LO_LIMIT_N / l_def_OBUS3_LO_LIMIT_D));
    }
    else if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<4, 8, 56, uint64_t>(0x24 - (l_def_OBUS3_LO_LIMIT_N / l_def_OBUS3_LO_LIMIT_D));
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x24 - (l_def_OBUS3_LO_LIMIT_N / l_def_OBUS3_LO_LIMIT_D));
    }
    fapi2::putScom(TGT0, 0x501380D, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013810, l_scom_buffer);
    if(l_def_OBUS0_FBC_ENABLED && l_def_OPTICS_IS_A_BUS)
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x10);
    }
    else if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x1F);
    }
    if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<9, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<17, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<41, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<49, 7, 57, uint64_t>(0x3C);
    }
    fapi2::putScom(TGT0, 0x5013810, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013811, l_scom_buffer);
    if(l_def_OBUS1_FBC_ENABLED)
    {
        if(l_def_OPTICS_IS_A_BUS)
        {
            l_scom_buffer.insert<24, 5, 59, uint64_t>(0x10);
        }
        else
        {
            l_scom_buffer.insert<24, 5, 59, uint64_t>(0x1F);
        }
        l_scom_buffer.insert<1, 7, 57, uint64_t>(0x40);
        l_scom_buffer.insert<9, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<17, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<33, 7, 57, uint64_t>(0x40);
        l_scom_buffer.insert<41, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<49, 7, 57, uint64_t>(0x3C);
    }
    fapi2::putScom(TGT0, 0x5013811, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013812, l_scom_buffer);
    if(l_def_OBUS2_FBC_ENABLED && l_def_OPTICS_IS_A_BUS)
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x10);
    }
    else if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x1F);
    }
    if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<1, 7, 57, uint64_t>(0x40);
        l_scom_buffer.insert<9, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<17, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<33, 7, 57, uint64_t>(0x40);
        l_scom_buffer.insert<41, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<49, 7, 57, uint64_t>(0x3C);
    }
    fapi2::putScom(TGT0, 0x5013812, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013813, l_scom_buffer);
    if((l_def_OBUS3_FBC_ENABLED && l_def_OPTICS_IS_A_BUS))
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x0E);
    }
    else if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<24, 5, 59, uint64_t>(0x1C);
    }
    if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<9, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<17, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<41, 7, 57, uint64_t>(0x3C);
        l_scom_buffer.insert<49, 7, 57, uint64_t>(0x3C);
    }
    fapi2::putScom(TGT0, 0x5013813, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013823, l_scom_buffer);
    if(l_def_OB0_IS_PAIRED)
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_PB_CFG_IOO01_IS_LOGICAL_PAIR_ON
    }
    else
    {
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_PB_CFG_IOO01_IS_LOGICAL_PAIR_OFF
    }
    if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<8, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_LINKS01_TOD_ENABLE_ON
    }
    else
    {
        l_scom_buffer.insert<8, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_LINKS01_TOD_ENABLE_OFF
    }
    if(l_def_OB1_IS_PAIRED)
    {
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_PB_CFG_IOO23_IS_LOGICAL_PAIR_ON
    }
    else
    {
        l_scom_buffer.insert<1, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_PB_CFG_IOO23_IS_LOGICAL_PAIR_OFF
    }
    if(l_def_OBUS1_FBC_ENABLED)
    {
        l_scom_buffer.insert<9, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_LINKS23_TOD_ENABLE_ON
    }
    else
    {
        l_scom_buffer.insert<9, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_LINKS23_TOD_ENABLE_OFF
    }
    if(l_def_OB2_IS_PAIRED)
    {
        l_scom_buffer.insert<2, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_PB_CFG_IOO45_IS_LOGICAL_PAIR_ON
    }
    else
    {
        l_scom_buffer.insert<2, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_PB_CFG_IOO45_IS_LOGICAL_PAIR_OFF
    }
    if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<10, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_LINKS45_TOD_ENABLE_ON
    }
    else
    {
        l_scom_buffer.insert<10, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_LINKS45_TOD_ENABLE_OFF
    }
    if(l_def_OB3_IS_PAIRED)
    {
        l_scom_buffer.insert<3, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_PB_CFG_IOO67_IS_LOGICAL_PAIR_ON
    }
    else
    {
        l_scom_buffer.insert<3, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_PB_CFG_IOO67_IS_LOGICAL_PAIR_OFF
    }
    if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<11, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_LINKS67_TOD_ENABLE_ON
    }
    else
    {
        l_scom_buffer.insert<11, 1, 63, uint64_t>(0); // l_PB_IOO_SCOM_LINKS67_TOD_ENABLE_OFF
    }
    if(l_def_NVLINK_ACTIVE)
    {
        l_scom_buffer.insert<13, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_SEL_03_NPU_NOT_PB_ON
        l_scom_buffer.insert<14, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_SEL_04_NPU_NOT_PB_ON
        l_scom_buffer.insert<15, 1, 63, uint64_t>(1); // l_PB_IOO_SCOM_SEL_05_NPU_NOT_PB_ON
    }
    fapi2::putScom(TGT0, 0x5013823, l_scom_buffer);
    fapi2::getScom(TGT0, 0x5013824, l_scom_buffer);
    if(l_def_OBUS0_FBC_ENABLED)
    {
        l_scom_buffer.insert<0, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<8, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0x4);
        l_scom_buffer.insert<12, 4, 60, uint64_t>(0x4);
    }
    else if(l_def_OBUS1_FBC_ENABLED)
    {
        l_scom_buffer.insert<16, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<24, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<20, 4, 60, uint64_t>(0x4);
        l_scom_buffer.insert<28, 4, 60, uint64_t>(0x4);
    }
    else if(l_def_OBUS2_FBC_ENABLED)
    {
        l_scom_buffer.insert<32, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<40, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<36, 4, 60, uint64_t>(0x4);
        l_scom_buffer.insert<44, 4, 60, uint64_t>(0x4);
    }
    else if(l_def_OBUS3_FBC_ENABLED)
    {
        l_scom_buffer.insert<48, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<56, 4, 60, uint64_t>(1);
        l_scom_buffer.insert<52, 4, 60, uint64_t>(0x4);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x4);
    }
    fapi2::putScom(TGT0, 0x5013824, l_scom_buffer);
}

fapi2::ReturnCode p9_obus_scom(
    const fapi2::Target<fapi2::TARGET_TYPE_OBUS>& TGT0,
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1,
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT2)
{
    {
        fapi2::ATTR_EC_Type   l_chip_ec;
        fapi2::ATTR_NAME_Type l_chip_id;
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, TGT2, l_chip_id);
        FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT2, l_chip_ec);
        fapi2::ATTR_IS_SIMULATION_Type l_TGT1_ATTR_IS_SIMULATION;
        FAPI_ATTR_GET(fapi2::ATTR_IS_SIMULATION, TGT1, l_TGT1_ATTR_IS_SIMULATION);
        fapi2::ATTR_CHIP_EC_FEATURE_OBUS_HW419305_Type l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_OBUS_HW419305, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305);
        fapi2::ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES_Type l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES);
        fapi2::ATTR_OPTICS_CONFIG_MODE_Type l_TGT0_ATTR_OPTICS_CONFIG_MODE;
        FAPI_ATTR_GET(fapi2::ATTR_OPTICS_CONFIG_MODE, TGT0, l_TGT0_ATTR_OPTICS_CONFIG_MODE);
        fapi2::ATTR_CHIP_EC_FEATURE_HW422471_Type l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW422471, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471);
        fapi2::ATTR_IO_O_CHANNEL_TYPE_Type l_TGT0_ATTR_IO_O_CHANNEL_TYPE;
        FAPI_ATTR_GET(fapi2::ATTR_IO_O_CHANNEL_TYPE, TGT0, l_TGT0_ATTR_IO_O_CHANNEL_TYPE);
        fapi2::ATTR_CHIP_EC_FEATURE_HW422471_HW446964_Type l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471_HW446964;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW422471_HW446964, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471_HW446964);
        uint64_t l_def_OBUS_FBC_ENABLED = (l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP);
        fapi2::ATTR_CHIP_EC_FEATURE_SW387041_Type l_TGT2_ATTR_CHIP_EC_FEATURE_SW387041;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_SW387041, TGT2, l_TGT2_ATTR_CHIP_EC_FEATURE_SW387041);
        fapi2::buffer<uint64_t> l_scom_buffer;

        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000009010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000009010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000109010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000109010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000209010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000309010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000409010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000509010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000509010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000609010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000709010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000809010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000809010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000809010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000909010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        fapi2::getScom(TGT0, 0x8000000909010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000909010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000A09010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000A09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000B09010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000C09010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000C09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000000D09010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000D09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000E09010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000000F09010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000000F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000000F09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000001009010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        // Advance                                                                               POWER9 Registers
        fapi2::getScom(TGT0, 0x8000001009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000001109010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000001209010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        fapi2::getScom(TGT0, 0x8000001209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000001309010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_4_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000001409010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000001509010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address                8000001609010C3F (SCOM)
        //  Description            This register contains per-lane spare mode latches.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48          RWX         RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49          RWX         RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001609010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DATA_DAC_SPARE_MODE_PL
        //  Address          8000001709010C3F (SCOM)
        //  Description      This register contains per-lane spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PL_DATA_DAC_SPARE_MODE_0: Per-lane spare mode latch.
        // 49        RWX     RX_PL_DATA_DAC_SPARE_MODE_1: Per-lane spare mode latch.
        fapi2::getScom(TGT0, 0x8000001709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXPACKS_5_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000001709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280009010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280109010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280209010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280309010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280409010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280509010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280509010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280609010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        fapi2::getScom(TGT0, 0x8000280609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280709010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280809010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280809010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280909010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280909010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280909010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280A09010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280A09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280B09010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280C09010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280C09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280D09010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000280D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280D09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000280E09010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        fapi2::getScom(TGT0, 0x8000280E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000280F09010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000280F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000280F09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000281009010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000281009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000281109010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000281109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000281209010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000281209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000281309010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000281309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000281409010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000281409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000281509010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000281509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281509010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address          8000281609010C3F (SCOM)
        //  Description      This register contains the fifth set of EO DAC controls.
        fapi2::getScom(TGT0, 0x8000281609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_EO_PL
        //  Address                8000281709010C3F (SCOM)
        //  Description            This register contains the fifth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000281709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000281709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300009010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300009010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300109010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300109010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300209010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300309010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300409010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300509010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300609010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300609010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300709010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300809010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300809010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300909010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300909010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300909010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300A09010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300A09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300B09010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000300C09010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000300C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300C09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300D09010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300D09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300E09010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000300F09010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000300F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000300F09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000301009010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000301009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000301109010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000301109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000301209010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000301209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000301309010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000301309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000301409010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000301409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address          8000301509010C3F (SCOM)
        //  Description      This register contains the sixth set of EO DAC controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52        RO      constant = 0b0
        fapi2::getScom(TGT0, 0x8000301509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000301609010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000301609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL6_EO_PL
        //  Address                8000301709010C3F (SCOM)
        //  Description            This register contains the sixth set of EO DAC controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_A_CTLE_COARSE: This is the CTLE coarse peak value. Only 4 bits are currently used.
        // 52          RO          constant = 0b0
        fapi2::getScom(TGT0, 0x8000301709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000301709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980009010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        fapi2::getScom(TGT0, 0x8000980009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980109010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);

        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980209010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980309010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980409010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980509010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980609010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980709010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980809010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        fapi2::getScom(TGT0, 0x8000980809010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980909010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980909010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980909010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980A09010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980A09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980B09010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980C09010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980C09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000980D09010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000980D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980D09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980E09010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000980F09010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000980F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000980F09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000981009010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        fapi2::getScom(TGT0, 0x8000981009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000981109010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000981109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000981209010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000981209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000981309010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000981309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000981409010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000981409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address          8000981509010C3F (SCOM)
        //  Description      This register contains the fourth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000981509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000981609010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000981609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL4_O_PL
        //  Address                8000981709010C3F (SCOM)
        //  Description            This register contains the fourth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000981709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000981709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00009010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        fapi2::getScom(TGT0, 0x8000A00009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000A00009010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00109010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000A00109010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00209010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000A00209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00309010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00409010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00509010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00609010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        // Advance                                                                               POWER9 Registers
        fapi2::getScom(TGT0, 0x8000A00609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00709010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        fapi2::getScom(TGT0, 0x8000A00709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00809010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00809010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00909010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00909010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00909010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00A09010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00A09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00B09010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00C09010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00C09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00D09010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00D09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A00E09010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        fapi2::getScom(TGT0, 0x8000A00E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A00F09010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A00F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A00F09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A01009010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01009010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A01109010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A01209010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A01309010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A01409010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address                8000A01509010C3F (SCOM)
        //  Description            This register contains the fifth set of O controls.
        fapi2::getScom(TGT0, 0x8000A01509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01509010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A01609010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01609010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL5_O_PL
        //  Address          8000A01709010C3F (SCOM)
        //  Description      This register contains the fifth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_B_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_B_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000A01709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_P9NDD1_SPY_NAMES)
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000A01709010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00009010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00109010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00209010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        fapi2::getScom(TGT0, 0x8000C00209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00309010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00409010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00509010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00609010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00709010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00809010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00809010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00909010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00909010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00909010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00A09010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        fapi2::getScom(TGT0, 0x8000C00A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00A09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00B09010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C00C09010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C00C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00C09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00D09010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00D09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00E09010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C00F09010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C00F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C00F09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01009010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C01009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01109010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C01109010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01209010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        fapi2::getScom(TGT0, 0x8000C01209010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01209010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C01309010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C01309010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01309010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address          8000C01409010C3F (SCOM)
        //  Description      This register contains the ninth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                   adjustments.
        fapi2::getScom(TGT0, 0x8000C01409010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01509010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C01509010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01609010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C01609010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01609010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL9_O_PL
        //  Address                8000C01709010C3F (SCOM)
        //  Description            This register contains the ninth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_INTEG_COARSE_GAIN: This is the integrator coarse-gain control used in making common mode
        //                         adjustments.
        fapi2::getScom(TGT0, 0x8000C01709010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<57, 5, 59, uint64_t>(0x10);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
        }
        fapi2::putScom(TGT0, 0x8000C01709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80009010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80009010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80109010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80109010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80209010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80309010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000C80309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80409010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80409010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80509010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000C80509010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80609010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        fapi2::getScom(TGT0, 0x8000C80609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80609010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80709010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80709010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80809010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80809010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80809010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80909010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80909010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        if((l_TGT0_ATTR_OPTICS_CONFIG_MODE == fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP))
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x14);
        }
        else
        {
            l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        }
        fapi2::putScom(TGT0, 0x8000C80909010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80A09010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80A09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80A09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80B09010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80B09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80B09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80C09010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80C09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80C09010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C80D09010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        fapi2::getScom(TGT0, 0x8000C80D09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80D09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80E09010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80E09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80E09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C80F09010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C80F09010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C80F09010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C81009010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81009010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81009010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C81109010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81109010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81109010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C81209010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81209010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81209010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C81309010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55       RWX         RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81309010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81309010C3F, l_scom_buffer);
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address                8000C81409010C3F (SCOM)
        //  Description            This register contains the tenth set of O controls.
        fapi2::getScom(TGT0, 0x8000C81409010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81409010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C81509010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81509010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81509010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C81609010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81609010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81609010C3F, l_scom_buffer);
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RX_DAC_REGS.RX_DAC_REGS.RX_DAC_CNTL10_O_PL
        //  Address          8000C81709010C3F (SCOM)
        //  Description      This register contains the tenth set of O controls.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_E_CTLE_COARSE: This is the CTLE coarse peak value.
        // 52:55     RWX     RX_E_CTLE_GAIN: This is the CTLE gain setting.
        fapi2::getScom(TGT0, 0x8000C81709010C3F, l_scom_buffer);
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0xA);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x3);
        fapi2::putScom(TGT0, 0x8000C81709010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280009010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the phase-rotator (PR) accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280009010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280009010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280109010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280109010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280109010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280209010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280209010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }

        fapi2::putScom(TGT0, 0x8002280209010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#0.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280309010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280309010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280309010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280409010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280409010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280409010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280509010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280509010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280509010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280609010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280609010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280609010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#1.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280709010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280709010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280709010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280809010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280809010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280809010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280909010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280909010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280909010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280A09010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280A09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280A09010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#2.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280B09010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280B09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
        }
        fapi2::putScom(TGT0, 0x8002280B09010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280C09010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280C09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280C09010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280D09010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280D09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280D09010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002280E09010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280E09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }

        fapi2::putScom(TGT0, 0x8002280E09010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#3.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002280F09010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002280F09010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002280F09010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002281009010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281009010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281009010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002281109010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281109010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281109010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002281209010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281209010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281209010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#4.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002281309010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281309010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281309010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#0.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002281409010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281409010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281409010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#1.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002281509010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281509010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281509010C3F, l_scom_buffer);
        //  Register Name          Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#2.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address                8002281609010C3F (SCOM)
        //  Description            This register contains the second set of EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:55        RO          constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56          RWX         RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                         Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281609010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281609010C3F, l_scom_buffer);
        //  Register Name    Receive Bit Mode 2 EO Per-Lane Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXPACKS#5.RXPACK.RD.SLICE#3.RD.RX_BIT_REGS.RX_BIT_MODE2_EO_PL
        //  Address          8002281709010C3F (SCOM)
        //  Description      This register contains the second set of EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:55      RO      constant = 0b00000000000000000000000000000000000000000000000000000000
        // 56        RWX     RX_PR_FW_OFF: Removes the flywheel from the PR accumulator.
        //                   Note: This is not the same as setting the inertia amount to zero.
        fapi2::getScom(TGT0, 0x8002281709010C3F, l_scom_buffer);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0x8);
        }
        else
        {
            l_scom_buffer.insert<57, 3, 61, uint64_t>(0x4);
            l_scom_buffer.insert<60, 4, 60, uint64_t>(0xC);
        }
        fapi2::putScom(TGT0, 0x8002281709010C3F, l_scom_buffer);
        //  Register Name    Receive Spare Mode Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_SPARE_MODE_PG
        //  Address          8008000009010C3F (SCOM)
        //  Description      This register contains per-group spare mode latches.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_PG_SPARE_MODE_0: Per-group spare mode latch.
        // 49        RWX     RX_PG_SPARE_MODE_1: Per-group spare mode latch.
        fapi2::getScom(TGT0, 0x8008000009010C3F, l_scom_buffer);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PG_SPARE_MODE_4_OFF
        fapi2::putScom(TGT0, 0x8008000009010C3F, l_scom_buffer);
        //  Register Name          Receive ID1 Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_ID1_PG
        //  Address                8008080009010C3F (SCOM)
        //  Description            This register is used to set the bus number of a clock group.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:53       RWX         RX_BUS_ID: This field is used to programmatically set the bus number that a clock group belongs to.
        // 54:63       RO          constant = 0b0000000000
        fapi2::getScom(TGT0, 0x8008080009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 6, 58, uint64_t>(0);
        fapi2::putScom(TGT0, 0x8008080009010C3F, l_scom_buffer);
        //  Register Name    Receive CTL Mode 2 EO Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE2_EO_PG
        //  Address          8008180009010C3F (SCOM)
        //  Description      This register contains the second set of receive CTL EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:49     RWX     RX_DFE_CA_CFG: Receive DFE clock adjustment settings. This 2-bit field contains an encoded value for K
        //                   as follows:
        fapi2::getScom(TGT0, 0x8008180009010C3F, l_scom_buffer);
        if(l_TGT0_ATTR_IO_O_CHANNEL_TYPE == fapi2::ENUM_ATTR_IO_O_CHANNEL_TYPE_CABLE && !l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471)
        {
            l_scom_buffer.insert<54, 1, 63, uint64_t>(1); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RECAL_REQ_DL_MASK_ON
        }
        else
        {
            l_scom_buffer.insert<54, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RECAL_REQ_DL_MASK_OFF
        }
        if(l_TGT0_ATTR_IO_O_CHANNEL_TYPE == fapi2::ENUM_ATTR_IO_O_CHANNEL_TYPE_CABLE && !l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471_HW446964)
        {
            l_scom_buffer.insert<57, 1, 63, uint64_t>(1); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RECAL_ABORT_DL_MASK_ON
        }
        else
        {
            l_scom_buffer.insert<57, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RECAL_ABORT_DL_MASK_OFF
        }
        fapi2::putScom(TGT0, 0x8008180009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 10 EO Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE10_EO_PG
        //  Address                8008580009010C3F (SCOM)
        //  Description            This register contains the tenth set of receive CTL EO mode fields.
        fapi2::getScom(TGT0, 0x8008580009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<51, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(2);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(5);
        l_scom_buffer.insert<57, 3, 61, uint64_t>(5);
        fapi2::putScom(TGT0, 0x8008580009010C3F, l_scom_buffer);
        //  Register Name    Receive CTL Mode 11 EO Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE11_EO_PG
        //  Address          8008600009010C3F (SCOM)
        //  Description      This register contains the 11th set of receive CTL EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:50     RWX     RX_OFF_INIT_CFG: This register controls the servo filter for offset measurements during initialization.
        // 51:53     RWX     RX_OFF_RECAL_CFG: This register controls the servo filter for offset measurements during recalibration.
        fapi2::getScom(TGT0, 0x8008600009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<51, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(2);
        fapi2::putScom(TGT0, 0x8008600009010C3F, l_scom_buffer);
        //  Register Name    Receive CTL Mode 12 EO Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE12_EO_PG
        //  Address          8008680009010C3F (SCOM)
        //  Description      This register contains the 12th set of receive CTL EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:51     RWX     RX_SERVO_CHG_CFG: This register controls the minimum acceptable changes of the accumulator for a
        //                   valid servo operation. It is used to assure that we have reached a stable point.
        fapi2::getScom(TGT0, 0x8008680009010C3F, l_scom_buffer);
        l_scom_buffer.insert<60, 3, 61, uint64_t>(0x4);
        fapi2::putScom(TGT0, 0x8008680009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 13 EO Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE13_EO_PG
        //  Address                8008700009010C3F (SCOM)
        //  Description            This register contains the 13th set of receive CTL EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:48        RO          constant = 0b0000000000000000000000000000000000000000000000000
        // 49:55       RWX         RX_CM_OFFSET_VAL: This field contains the value used to offset the amp DAC when running common
        //                         mode.
        fapi2::getScom(TGT0, 0x8008700009010C3F, l_scom_buffer);
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<49, 7, 57, uint64_t>(0x46);
        }
        fapi2::putScom(TGT0, 0x8008700009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 14 EO Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE14_EO_PG
        //  Address                8008780009010C3F (SCOM)
        //  Description            This register contains the 14th set of receive CTL EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_AMP_INIT_TIMEOUT: This field is used for amplitude measurements during initialization.
        // 52:55       RWX         RX_AMP_RECAL_TIMEOUT: This field is used for amplitude measurements during recalibration.
        fapi2::getScom(TGT0, 0x8008780009010C3F, l_scom_buffer);
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x6);
            l_scom_buffer.insert<52, 4, 60, uint64_t>(0x6);
        }
        fapi2::putScom(TGT0, 0x8008780009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 15 EO Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE15_EO_PG
        //  Address                8008800009010C3F (SCOM)
        //  Description            This register contains the 15th set of receive CTL EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:51       RWX         RX_OFF_INIT_TIMEOUT: This field is used for offset measurements during initialization.
        // 52:55       RWX         RX_OFF_RECAL_TIMEOUT: This field is used for offset measurements during recalibration.
        fapi2::getScom(TGT0, 0x8008800009010C3F, l_scom_buffer);
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 4, 60, uint64_t>(0x6);
            l_scom_buffer.insert<52, 4, 60, uint64_t>(0x6);
        }
        fapi2::putScom(TGT0, 0x8008800009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 29 EO Per-Group Register
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE29_EO_PG
        //  Address                8008D00009010C3F (SCOM)
        //  Description            This register contains the 29th set of receive CTL EO mode fields.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:55       RWX         RX_APX111_HIGH: This field contains the receive Amax high target, in amplitude DAC steps (as measured
        //                         by ap_x111 and an_x000). The default is d102.
        fapi2::getScom(TGT0, 0x8008D00009010C3F, l_scom_buffer);
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 8, 56, uint64_t>(0x78);
        }
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<56, 8, 56, uint64_t>(0x5A);
        }
        fapi2::putScom(TGT0, 0x8008D00009010C3F, l_scom_buffer);
        //  Register Name    Receive CTL Mode 27 EO Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE27_EO_PG
        //  Address          8009700009010C3F (SCOM)
        //  Description      This register contains the 27th set of receive CTL EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL: Receive recalibration; first latch offset adjustment
        //                   enable with CTLE-based disable.
        fapi2::getScom(TGT0, 0x8009700009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(1); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL_ON
        if(l_TGT0_ATTR_IO_O_CHANNEL_TYPE == fapi2::ENUM_ATTR_IO_O_CHANNEL_TYPE_CABLE && !l_TGT2_ATTR_CHIP_EC_FEATURE_HW422471)
        {
            l_scom_buffer.insert<51, 1, 63, uint64_t>(1); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RC_ENABLE_AUTO_RECAL_ON
        }
        else
        {
            l_scom_buffer.insert<51, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RC_ENABLE_AUTO_RECAL_OFF
        }
        fapi2::putScom(TGT0, 0x8009700009010C3F, l_scom_buffer);
        //  Register Name    Receive CTL Mode 28 EO Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE28_EO_PG
        //  Address          8009780009010C3F (SCOM)
        //  Description      This register contains the 28th set of receive CTL EO mode fields.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48        RWX     RX_DC_ENABLE_CM_COARSE_CAL: This bit enables receive DC calibration eye-optimization common-
        //                   mode coarse calibration.
        fapi2::getScom(TGT0, 0x8009780009010C3F, l_scom_buffer);
        if(!l_TGT2_ATTR_CHIP_EC_FEATURE_OBUS_HW419305)
        {
            l_scom_buffer.insert<48, 1, 63, uint64_t>(0); // l_IOO0_IOO_CPLT_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DC_ENABLE_CM_COARSE_CAL_OFF
        }
        fapi2::putScom(TGT0, 0x8009780009010C3F, l_scom_buffer);
        //  Register Name          Receive CTL Mode 2 O Per-Group
        //  Mnemonic               IOO0.IOO_CPLT.RX0.RXCTL.CTL_REGS.RX_CTL_REGS.RX_CTL_MODE2_O_PG
        //  Address                8009880009010C3F (SCOM)
        //  Description            This is a per-group receive CTL mode O register.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:50       RWX         RX_OCTANT_SELECT: This field selects which c16 clock is used on IOO.
        // 51:52       RWX         RX_SPEED_SELECT: This field selects the IOO speed control.
        fapi2::getScom(TGT0, 0x8009880009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 3, 61, uint64_t>(0x5);
        if(l_TGT2_ATTR_CHIP_EC_FEATURE_SW387041)
        {
            l_scom_buffer.insert<51, 2, 62, uint64_t>(1);
        }
        else
        {
            l_scom_buffer.insert<51, 2, 62, uint64_t>(0);
        }
        fapi2::putScom(TGT0, 0x8009880009010C3F, l_scom_buffer);
        //  Register Name    Transmit ID1 Per-Group Register
        //  Mnemonic         IOO0.IOO_CPLT.TX0.TXCTL.CTL_REGS.TX_CTL_REGS.TX_ID1_PG
        //  Address          800C0C0009010C3F (SCOM)
        //  Description      This register is used to programmatically set the bus number that a group belongs to.
        //  Bits     SCOM    Field Mnemonic: Description
        // 0:47      RO      constant = 0b000000000000000000000000000000000000000000000000
        // 48:53     RWX     TX_BUS_ID: This field is used to programmatically set the bus number that a group belongs to.
        // 54:63     RO      constant = 0b0000000000
        fapi2::getScom(TGT0, 0x800C0C0009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 6, 58, uint64_t>(0);
        fapi2::putScom(TGT0, 0x800C0C0009010C3F, l_scom_buffer);
        //  Register Name          Transmit Impedance Calibration P 4X Per-Bus Register
        //  Mnemonic               IOO0.IOO_CPLT.BUSCTL.BUS_REG_IF.BUS_CTL_REGS.TX_IMPCAL_P_4X_PB
        //  Address                800F1C0009010C3F (SCOM)
        //  Description            This register is used for impedance calibration.
        //  Bits       SCOM        Field Mnemonic: Description
        // 0:47        RO          constant = 0b000000000000000000000000000000000000000000000000
        // 48:52       RWX         TX_ZCAL_P_4X: Calibration circuit PSeg-4X enable value. This field holds the current value of the enabled
        //                         segments. It is a 2x multiple of the actual segment count. It can be read for the current calibration result set
        fapi2::getScom(TGT0, 0x800F1C0009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0E);
        fapi2::putScom(TGT0, 0x800F1C0009010C3F, l_scom_buffer);
        fapi2::getScom(TGT0, 0x800F2C0009010C3F, l_scom_buffer);
        l_scom_buffer.insert<48, 7, 57, uint64_t>(0x15);
        l_scom_buffer.insert<55, 7, 57, uint64_t>(0x46);
        fapi2::putScom(TGT0, 0x800F2C0009010C3F, l_scom_buffer);
    };
}
```
