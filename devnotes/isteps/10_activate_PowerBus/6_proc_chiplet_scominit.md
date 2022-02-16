From IPL flow document:

> 10.6 `proc_chiplet_scominit` : Scom inits to all chiplets (sans Quad)
> a) `p9_chiplet_scominit.C`
>  * Initfiles in procedure defined on VBU ENGD wiki
>  * Apply scom overrides to all good chiplets (except EX and MC)
>    * `p9.fbc.no_hp.scom.initfile`
> b) `p9_psi_scominit.C`
>  * Each instance of bus must have unique id set for it – personalize it
>  * Must set present and valid bits based on topology (Attributes indicate
>    present and valid)

Analysis assumptions:
 * `INITSERVICE::isSMPWrapConfig() == false`
 * `INITSERVICE::spBaseServicesEnabled() == false`
 * No OBus targets.
 * No DMI targets.
 * No MC targets.
 * Only XBUS1 link is present and functional.
 * `ATTR_SMF_CONFIG != ENUM_ATTR_SMF_CONFIG_ENABLED`
 * `ATTR_PROC_FABRIC_PUMP_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_GROUP`

```cpp
// src/usr/isteps/istep10/call_proc_chiplet_scominit.C
void* call_proc_chiplet_scominit( void *io_pArgs )
{
    errlHndl_t l_err(nullptr);
    IStepError l_stepError;

    // Make the FAPI call to p9_chiplet_scominit
    // Make the FAPI call to p9_io_obus_firmask_save_restore, if previous call succeeded
    // Make the FAPI call to p9_psi_scominit, if previous call succeeded
    fapiHWPCallWrapperHandler(P9_CHIPLET_SCOMINIT, l_stepError,
                                HWPF_COMP_ID, TYPE_PROC)                &&
    fapiHWPCallWrapperHandler(P9_OBUS_FIRMASK_SAVE_RESTORE, l_stepError,
                                HWPF_COMP_ID, TYPE_PROC)                &&
    fapiHWPCallWrapperHandler(P9_PSI_SCOMINIT, l_stepError,
                                HWPF_COMP_ID, TYPE_PROC);

    return l_stepError.getErrorHandle();
}

// src/import/chips/p9/procedures/hwp/nest/p9_chiplet_scominit.C
fapi2::ReturnCode p9_chiplet_scominit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    /// ATTR_CHIP_EC_FEATURE_NMMU_NDD1 == false

    fapi2::ReturnCode l_rc;
    fapi2::buffer<uint64_t> l_scom_data;
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    uint8_t l_xbus_present[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = { 0, 1, 0 };
    uint8_t l_xbus_functional[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] = { 0, 1, 0 };

    for (const auto& l_mcs_target : i_target.getChildren<fapi2::TARGET_TYPE_MCS>()) {
        FAPI_EXEC_HWP(l_rc, p9n_mcs_scom, l_mcs_target, FAPI_SYSTEM, i_target,
                      l_mcs_target.getParent<fapi2::TARGET_TYPE_MCBIST>());
        if (l_rc) goto fapi_try_exit;
    }

    // read spare FBC FIR bit -- if set, SBE has configured XBUS FIR resources for all
    // present units, and code here will be run to mask resources associated with
    // non-functional units
    getScom(i_target, PU_PB_CENT_SM0_PB_CENT_FIR_REG, l_scom_data);

    if (l_scom_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
    {
        // Masking XBUS FIR resources for unused links

        if (!l_xbus_functional[0])
        {
            // XBUS0 FBC TL
            putScom(i_target, PU_PB_IOE_FIR_MASK_REG_OR, FBC_IOE_TL_FIR_MASK_X0_NF);
            // XBUS0 EXTFIR
            putScom(i_target, PU_PB_CENT_SM1_EXTFIR_MASK_REG_OR, FBC_EXT_FIR_MASK_X0_NF);
        }

        if (!l_xbus_functional[2])
        {
            // XBUS2 FBC TL
            putScom(i_target, PU_PB_IOE_FIR_MASK_REG_OR, FBC_IOE_TL_FIR_MASK_X2_NF);
            // XBUS2 EXTFIR
            putScom(i_target, PU_PB_CENT_SM1_EXTFIR_MASK_REG_OR, FBC_EXT_FIR_MASK_X2_NF);
        }
    }

    FAPI_EXEC_HWP(l_rc, p9_fbc_ioo_tl_scom, i_target, FAPI_SYSTEM);
    if (l_rc) goto fapi_try_exit;

    // Invoke NX SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_nx_scom, i_target, FAPI_SYSTEM);
    if (l_rc) goto fapi_try_exit;

    // Invoke CXA SCOM initfile
    for (auto l_capp : i_target.getChildren<fapi2::TARGET_TYPE_CAPP>())
    {
        FAPI_EXEC_HWP(l_rc, p9_cxa_scom, l_capp, FAPI_SYSTEM, i_target);
        if (l_rc) goto fapi_try_exit;
    }

    // Invoke INT SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_int_scom, i_target, FAPI_SYSTEM);
    if (l_rc) goto fapi_try_exit;

    // Invoke VAS SCOM initfile
    FAPI_EXEC_HWP(l_rc, p9_vas_scom, i_target, FAPI_SYSTEM);
    if (l_rc) goto fapi_try_exit;

    // Setup NMMU epsilon write cycles
    FAPI_ATTR_GET(fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1, FAPI_SYSTEM, l_eps_write_cycles_t1);
    FAPI_ATTR_GET(fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2, FAPI_SYSTEM, l_eps_write_cycles_t2);

    fapi2::getScom(i_target, PU_NMMU_MM_EPSILON_COUNTER_VALUE, l_scom_data);

    l_scom_data.insertFromRight<PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_1_CNT_VAL, PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_1_CNT_VAL_LEN>
    (l_eps_write_cycles_t1);
    l_scom_data.insertFromRight<PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_2_CNT_VAL, PU_NMMU_MM_EPSILON_COUNTER_VALUE_WR_TIER_2_CNT_VAL_LEN>
    (l_eps_write_cycles_t2);

    fapi2::putScom(i_target, PU_NMMU_MM_EPSILON_COUNTER_VALUE, l_scom_data);

fapi_try_exit:
    return fapi2::current_err;
}

// src/import/chips/p9/procedures/hwp/initfiles/p9n_mcs_scom.C
fapi2::ReturnCode p9n_mcs_scom(const fapi2::Target<fapi2::TARGET_TYPE_MCS>& TGT0,
                               const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1,
                               const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT2,
                               const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& TGT3)
{
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x5010810ull, l_scom_buffer );

        l_scom_buffer.insert<46, 4, 60, uint64_t>(7);
        l_scom_buffer.insert<62, 1, 63, uint64_t>(0);

        constexpr auto l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PF_DROP_CMDLIST_ON = 0x1;
        l_scom_buffer.insert<61, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PF_DROP_CMDLIST_ON );

        l_scom_buffer.insert<32, 7, 57, uint64_t>(25);
        l_scom_buffer.insert<55, 6, 58, uint64_t>(0xF);

        constexpr auto l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PREFETCH_PROMOTE_ON = 0x1;
        l_scom_buffer.insert<63, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCPERF1_ENABLE_PREFETCH_PROMOTE_ON );

        fapi2::putScom(TGT0, 0x5010810ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5010811ull, l_scom_buffer );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_CENTAUR_SYNC_ON = 0x1;
        l_scom_buffer.insert<20, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_CENTAUR_SYNC_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_64_128B_READ_ON = 0x1;
        l_scom_buffer.insert<9, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_64_128B_READ_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_DROP_FP_DYN64_ACTIVE_ON = 0x1;
        l_scom_buffer.insert<8, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_ENABLE_DROP_FP_DYN64_ACTIVE_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_CENTAURP_ENABLE_ECRESP_OFF = 0x0;
        l_scom_buffer.insert<7, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_CENTAURP_ENABLE_ECRESP_OFF );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_SYNC_ON = 0x1;
        l_scom_buffer.insert<27, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_SYNC_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_PAIR_SYNC_ON = 0x1;
        l_scom_buffer.insert<28, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_DISABLE_MC_PAIR_SYNC_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE0_FORCE_COMMANDLIST_VALID_ON = 0x1;
        l_scom_buffer.insert<17, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE0_FORCE_COMMANDLIST_VALID_ON );

        fapi2::putScom(TGT0, 0x5010811ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5010812ull, l_scom_buffer );
        constexpr auto l_MC01_PBI01_SCOMFIR_MCMODE1_DISABLE_FP_M_BIT_ON = 0x1;
        l_scom_buffer.insert<10, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCMODE1_DISABLE_FP_M_BIT_ON );
        l_scom_buffer.insert<33, 19, 45, uint64_t>(0x40);
        fapi2::putScom(TGT0, 0x5010812ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5010813ull, l_scom_buffer );
        l_scom_buffer.insert<24, 16, 48, uint64_t>(8);
        fapi2::putScom(TGT0, 0x5010813ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501081bull, l_scom_buffer );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCTO_SELECT_PB_HANG_PULSE_ON = 0x1;
        l_scom_buffer.insert<0, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCTO_SELECT_PB_HANG_PULSE_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCTO_SELECT_LOCAL_HANG_PULSE_OFF = 0x0;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCTO_SELECT_LOCAL_HANG_PULSE_OFF );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_NONMIRROR_HANG_ON = 0x1;
        l_scom_buffer.insert<32, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_NONMIRROR_HANG_ON );

        constexpr auto l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_APO_HANG_ON = 0x1;
        l_scom_buffer.insert<34, 1, 63, uint64_t>(l_MC01_PBI01_SCOMFIR_MCTO_ENABLE_APO_HANG_ON );

        l_scom_buffer.insert<2, 2, 62, uint64_t>(0x1);
        l_scom_buffer.insert<24, 8, 56, uint64_t>(1);
        l_scom_buffer.insert<5, 3, 61, uint64_t>(7);

        fapi2::putScom(TGT0, 0x501081bull, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_fbc_ioo_tl_scom.C
fapi2::ReturnCode p9_fbc_ioo_tl_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x501380aull, l_scom_buffer );

        constexpr auto l_PB_IOO_SCOM_A0_MODE_BLOCKED = 0xf;
        l_scom_buffer.insert<20, 1, 60, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED );
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED );
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED );
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A0_MODE_BLOCKED );

        fapi2::putScom(TGT0, 0x501380aull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501380bull, l_scom_buffer );

        constexpr auto l_PB_IOO_SCOM_A1_MODE_BLOCKED = 0xf;
        l_scom_buffer.insert<20, 1, 60, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED );
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED );
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED );
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A1_MODE_BLOCKED );

        fapi2::putScom(TGT0, 0x501380bull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501380cull, l_scom_buffer );

        constexpr auto l_PB_IOO_SCOM_A2_MODE_BLOCKED = 0xf;
        l_scom_buffer.insert<20, 1, 60, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED );
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED );
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED );
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A2_MODE_BLOCKED );

        fapi2::putScom(TGT0, 0x501380cull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501380dull, l_scom_buffer );

        constexpr auto l_PB_IOO_SCOM_A3_MODE_BLOCKED = 0xf;
        l_scom_buffer.insert<20, 1, 60, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED );
        l_scom_buffer.insert<25, 1, 61, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED );
        l_scom_buffer.insert<52, 1, 62, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED );
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_PB_IOO_SCOM_A3_MODE_BLOCKED );

        fapi2::putScom(TGT0, 0x501380dull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013810ull, l_scom_buffer );

        fapi2::putScom(TGT0, 0x5013810ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013811ull, l_scom_buffer );

        fapi2::putScom(TGT0, 0x5013811ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013812ull, l_scom_buffer );

        fapi2::putScom(TGT0, 0x5013812ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013813ull, l_scom_buffer );

        fapi2::putScom(TGT0, 0x5013813ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013823ull, l_scom_buffer );

        constexpr auto l_PB_IOO_SCOM_PB_CFG_IOO01_IS_LOGICAL_PAIR_OFF = 0x0;
        l_scom_buffer.insert<0, 1, 63, uint64_t>(l_PB_IOO_SCOM_PB_CFG_IOO01_IS_LOGICAL_PAIR_OFF );

        constexpr auto l_PB_IOO_SCOM_LINKS01_TOD_ENABLE_OFF = 0x0;
        l_scom_buffer.insert<8, 1, 63, uint64_t>(l_PB_IOO_SCOM_LINKS01_TOD_ENABLE_OFF );

        constexpr auto l_PB_IOO_SCOM_PB_CFG_IOO23_IS_LOGICAL_PAIR_OFF = 0x0;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_PB_IOO_SCOM_PB_CFG_IOO23_IS_LOGICAL_PAIR_OFF );

        constexpr auto l_PB_IOO_SCOM_PB_CFG_IOO45_IS_LOGICAL_PAIR_OFF = 0x0;
        l_scom_buffer.insert<2, 1, 63, uint64_t>(l_PB_IOO_SCOM_PB_CFG_IOO45_IS_LOGICAL_PAIR_OFF );

        constexpr auto l_PB_IOO_SCOM_PB_CFG_IOO67_IS_LOGICAL_PAIR_OFF = 0x0;
        l_scom_buffer.insert<3, 1, 63, uint64_t>(l_PB_IOO_SCOM_PB_CFG_IOO67_IS_LOGICAL_PAIR_OFF );

        constexpr auto l_PB_IOO_SCOM_LINKS23_TOD_ENABLE_OFF = 0x0;
        l_scom_buffer.insert<9, 1, 63, uint64_t>(l_PB_IOO_SCOM_LINKS23_TOD_ENABLE_OFF );

        constexpr auto l_PB_IOO_SCOM_LINKS45_TOD_ENABLE_OFF = 0x0;
        l_scom_buffer.insert<10, 1, 63, uint64_t>(l_PB_IOO_SCOM_LINKS45_TOD_ENABLE_OFF );

        constexpr auto l_PB_IOO_SCOM_LINKS67_TOD_ENABLE_OFF = 0x0;
        l_scom_buffer.insert<11, 1, 63, uint64_t>(l_PB_IOO_SCOM_LINKS67_TOD_ENABLE_OFF );

        fapi2::putScom(TGT0, 0x5013823ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013824ull, l_scom_buffer );

        fapi2::putScom(TGT0, 0x5013824ull, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_nx_scom.C
fapi2::ReturnCode p9_nx_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                             const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    /// ATTR_CHIP_EC_FEATURE_HW414700 == (dd == 0x20)

    fapi2::ATTR_EC_Type   l_chip_ec;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x2011041ull, l_scom_buffer );

        constexpr auto l_NX_DMA_CH0_EFT_ENABLE_ON = 0x1;
        l_scom_buffer.insert<63, 1, 63, uint64_t>(l_NX_DMA_CH0_EFT_ENABLE_ON );
        constexpr auto l_NX_DMA_CH1_EFT_ENABLE_ON = 0x1;
        l_scom_buffer.insert<62, 1, 63, uint64_t>(l_NX_DMA_CH1_EFT_ENABLE_ON );
        constexpr auto l_NX_DMA_CH2_SYM_ENABLE_ON = 0x1;
        l_scom_buffer.insert<58, 1, 63, uint64_t>(l_NX_DMA_CH2_SYM_ENABLE_ON );
        constexpr auto l_NX_DMA_CH3_SYM_ENABLE_ON = 0x1;
        l_scom_buffer.insert<57, 1, 63, uint64_t>(l_NX_DMA_CH3_SYM_ENABLE_ON );
        constexpr auto l_NX_DMA_CH4_GZIP_ENABLE_ON = 0x1;
        l_scom_buffer.insert<61, 1, 63, uint64_t>(l_NX_DMA_CH4_GZIP_ENABLE_ON );

        fapi2::putScom(TGT0, 0x2011041ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011042ull, l_scom_buffer );

        constexpr auto l_NX_DMA_EFTCOMP_MAX_INRD_MAX_15_INRD = 0xf;
        l_scom_buffer.insert<33, 4, 60, uint64_t>(l_NX_DMA_EFTCOMP_MAX_INRD_MAX_15_INRD );
        constexpr auto l_NX_DMA_EFTDECOMP_MAX_INRD_MAX_15_INRD = 0xf;
        l_scom_buffer.insert<37, 4, 60, uint64_t>(l_NX_DMA_EFTDECOMP_MAX_INRD_MAX_15_INRD );
        constexpr auto l_NX_DMA_GZIPCOMP_MAX_INRD_MAX_15_INRD = 0xf;
        l_scom_buffer.insert<8, 4, 60, uint64_t>(l_NX_DMA_GZIPCOMP_MAX_INRD_MAX_15_INRD );
        constexpr auto l_NX_DMA_GZIPDECOMP_MAX_INRD_MAX_15_INRD = 0xf;
        l_scom_buffer.insert<12, 4, 60, uint64_t>(l_NX_DMA_GZIPDECOMP_MAX_INRD_MAX_15_INRD );
        constexpr auto l_NX_DMA_SYM_MAX_INRD_MAX_3_INRD = 0x3;
        l_scom_buffer.insert<25, 4, 60, uint64_t>(l_NX_DMA_SYM_MAX_INRD_MAX_3_INRD );

        constexpr auto l_NX_DMA_EFT_COMP_PREFETCH_ENABLE_ON = 0x1;
        l_scom_buffer.insert<23, 1, 63, uint64_t>(l_NX_DMA_EFT_COMP_PREFETCH_ENABLE_ON );
        constexpr auto l_NX_DMA_EFT_DECOMP_PREFETCH_ENABLE_ON = 0x1;
        l_scom_buffer.insert<24, 1, 63, uint64_t>(l_NX_DMA_EFT_DECOMP_PREFETCH_ENABLE_ON );
        constexpr auto l_NX_DMA_GZIP_COMP_PREFETCH_ENABLE_ON = 0x1;
        l_scom_buffer.insert<16, 1, 63, uint64_t>(l_NX_DMA_GZIP_COMP_PREFETCH_ENABLE_ON );
        constexpr auto l_NX_DMA_GZIP_DECOMP_PREFETCH_ENABLE_ON = 0x1;
        l_scom_buffer.insert<17, 1, 63, uint64_t>(l_NX_DMA_GZIP_DECOMP_PREFETCH_ENABLE_ON );
        constexpr auto l_NX_DMA_EFT_SPBC_WRITE_ENABLE_OFF = 0x0;
        l_scom_buffer.insert<56, 1, 63, uint64_t>(l_NX_DMA_EFT_SPBC_WRITE_ENABLE_OFF );

        fapi2::putScom(TGT0, 0x2011042ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x201105cull, l_scom_buffer );

        constexpr auto l_NX_DMA_CH0_WATCHDOG_REF_DIV_DIVIDE_BY_512 = 0x9;
        l_scom_buffer.insert<1, 4, 60, uint64_t>(l_NX_DMA_CH0_WATCHDOG_REF_DIV_DIVIDE_BY_512 );
        constexpr auto l_NX_DMA_CH0_WATCHDOG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<0, 1, 63, uint64_t>(l_NX_DMA_CH0_WATCHDOG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_CH1_WATCHDOG_REF_DIV_DIVIDE_BY_512 = 0x9;
        l_scom_buffer.insert<6, 4, 60, uint64_t>(l_NX_DMA_CH1_WATCHDOG_REF_DIV_DIVIDE_BY_512 );
        constexpr auto l_NX_DMA_CH1_WATCHDOG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<5, 1, 63, uint64_t>(l_NX_DMA_CH1_WATCHDOG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_CH2_WATCHDOG_REF_DIV_DIVIDE_BY_512 = 0x9;
        l_scom_buffer.insert<11, 4, 60, uint64_t>(l_NX_DMA_CH2_WATCHDOG_REF_DIV_DIVIDE_BY_512 );
        constexpr auto l_NX_DMA_CH2_WATCHDOG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<10, 1, 63, uint64_t>(l_NX_DMA_CH2_WATCHDOG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_CH3_WATCHDOG_REF_DIV_DIVIDE_BY_512 = 0x9;
        l_scom_buffer.insert<16, 4, 60, uint64_t>(l_NX_DMA_CH3_WATCHDOG_REF_DIV_DIVIDE_BY_512 );
        constexpr auto l_NX_DMA_CH3_WATCHDOG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<15, 1, 63, uint64_t>(l_NX_DMA_CH3_WATCHDOG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_CH4_WATCHDOG_REF_DIV_DIVIDE_BY_512 = 0x9;
        l_scom_buffer.insert<21, 4, 60, uint64_t>(l_NX_DMA_CH4_WATCHDOG_REF_DIV_DIVIDE_BY_512 );
        constexpr auto l_NX_DMA_CH4_WATCHDOG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<20, 1, 63, uint64_t>(l_NX_DMA_CH4_WATCHDOG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_DMA_HANG_TIMER_ENBL_ON = 0x1;
        l_scom_buffer.insert<25, 1, 63, uint64_t>(l_NX_DMA_DMA_HANG_TIMER_ENBL_ON );
        constexpr auto l_NX_DMA_DMA_HANG_TIMER_REF_DIV_DIVIDE_BY_1024 = 0x8;
        l_scom_buffer.insert<26, 4, 60, uint64_t>(l_NX_DMA_DMA_HANG_TIMER_REF_DIV_DIVIDE_BY_1024 );

        fapi2::putScom(TGT0, 0x201105cull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011083ull, l_scom_buffer );

        l_scom_buffer &= ~0xEEF8FF9CFD000000;
        l_scom_buffer |= 0x1107006302F00000;

        fapi2::putScom(TGT0, 0x2011083ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011086ull, l_scom_buffer );

        l_scom_buffer &= ~0xFFFFFFFFFFF00000;
        l_scom_buffer |= 0x0000000000000000;

        fapi2::putScom(TGT0, 0x2011086ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011087ull, l_scom_buffer );

        l_scom_buffer &= ~0x93EFDFFF3FF00000;
        l_scom_buffer |= 0x48102000C0000000;

        if (l_chip_ec == 0x20)
            l_scom_buffer &= ~0x2400000000000000;
        else
            l_scom_buffer |= ~0x2400000000000000;

        fapi2::putScom(TGT0, 0x2011087ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011095ull, l_scom_buffer );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_SKIP_G_ON = 0x1;
        l_scom_buffer.insert<24, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_SKIP_G_ON );

        l_scom_buffer.insert<56, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID );
        l_scom_buffer.insert<60, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_GROUP_ON = 0x1;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_GROUP_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_GROUP_ON = 0x1;
        l_scom_buffer.insert<5, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_GROUP_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_GROUP_ON = 0x1;
        l_scom_buffer.insert<9, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_GROUP_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_GROUP_ON = 0x1;
        l_scom_buffer.insert<13, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_GROUP_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<2, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_WR_DISABLE_VG_NOT_SYS_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<6, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_DMA_RD_DISABLE_VG_NOT_SYS_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<10, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_WR_DISABLE_VG_NOT_SYS_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<14, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_UMAC_RD_DISABLE_VG_NOT_SYS_ON );

        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_RD_GO_M_QOS_ON = 0x1;
        l_scom_buffer.insert<22, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_RD_GO_M_QOS_ON );
        constexpr auto l_NX_PBI_CQ_WRAP_NXCQ_SCOM_ADDR_BAR_MODE_OFF = 0x0;
        l_scom_buffer.insert<23, 1, 63, uint64_t>(l_NX_PBI_CQ_WRAP_NXCQ_SCOM_ADDR_BAR_MODE_OFF );
        l_scom_buffer.insert<25, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<40, 8, 56, uint64_t>(0xFC);
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0xFC);

        fapi2::putScom(TGT0, 0x2011095ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110a8ull, l_scom_buffer );

        l_scom_buffer &= ~0x0FFFF00000000000;
        l_scom_buffer |= 0x0888800000000000;

        fapi2::putScom(TGT0, 0x20110a8ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110c3ull, l_scom_buffer );

        l_scom_buffer &= ~0x0000001F00000000;
        l_scom_buffer |= 0x0000000080000000;

        fapi2::putScom(TGT0, 0x20110c3ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110c4ull, l_scom_buffer );
        l_scom_buffer.insert<27, 9, 55, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110c4ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110c5ull, l_scom_buffer );
        l_scom_buffer.insert<27, 9, 55, uint64_t>(8);
        fapi2::putScom(TGT0, 0x20110c5ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110d5ull, l_scom_buffer );
        constexpr auto l_NX_PBI_PBI_UMAC_CRB_READS_ENBL_ON = 0x1;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_NX_PBI_PBI_UMAC_CRB_READS_ENBL_ON );
        fapi2::putScom(TGT0, 0x20110d5ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x20110d6ull, l_scom_buffer );

        l_scom_buffer.insert<9, 3, 61, uint64_t>(2);
        constexpr auto l_NX_PBI_DISABLE_PROMOTE_ON = 0x1;
        l_scom_buffer.insert<6, 1, 63, uint64_t>(l_NX_PBI_DISABLE_PROMOTE_ON );

        fapi2::putScom(TGT0, 0x20110d6ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011103ull, l_scom_buffer );
        l_scom_buffer &= ~0xCF7DEF81BF003000;
        l_scom_buffer |= 0x3082107E40FFC000;
        fapi2::putScom(TGT0, 0x2011103ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011106ull, l_scom_buffer );
        l_scom_buffer &= ~0xFFFFFFFFFFFFC000;
        fapi2::putScom(TGT0, 0x2011106ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2011107ull, l_scom_buffer );

        l_scom_buffer &= ~0xF0839FFFC2FFC000;
        l_scom_buffer |= 0x0A7400003D000000;

        if (l_chip_ec == 0x20)
            l_scom_buffer &= ~0x0508600000000000;
        else
            l_scom_buffer |= ~0x0508600000000000;

        fapi2::putScom(TGT0, 0x2011107ull, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_cxa_scom.C
fapi2::ReturnCode p9_cxa_scom(const fapi2::Target<fapi2::TARGET_TYPE_CAPP>& TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1, const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT2)
{
    fapi2::ATTR_EC_Type   l_chip_ec;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x2010803ull, l_scom_buffer );

        if (l_chip_ec == 0x20)
        {
            l_scom_buffer.insert<0, 53, 0, uint64_t>(0x801B1F98C8717000 );
        }
        else
        {
            l_scom_buffer.insert<0, 53, 0, uint64_t>(0x801B1F98D8717000 );
        }

        fapi2::putScom(TGT0, 0x2010803ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2010806ull, l_scom_buffer );
        l_scom_buffer.insert<0, 53, 11, uint64_t>(0x0000000000000 );
        fapi2::putScom(TGT0, 0x2010806ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2010807ull, l_scom_buffer );

        l_scom_buffer.insert<2, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<44, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<8, 1, 63, uint64_t>(1);

        fapi2::putScom(TGT0, 0x2010807ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2010818ull, l_scom_buffer );

        constexpr auto l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_ADR_BAR_MODE_OFF = 0x0;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_ADR_BAR_MODE_OFF );

        constexpr auto l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_SKIP_G_ON = 0x1;
        l_scom_buffer.insert<6, 1, 63, uint64_t>(l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_SKIP_G_ON );

        l_scom_buffer.insert<21, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID );
        l_scom_buffer.insert<25, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID );

        constexpr auto l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_G_ON = 0x1;
        l_scom_buffer.insert<4, 1, 63, uint64_t>(l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_G_ON );

        constexpr auto l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<3, 1, 63, uint64_t>(l_CAPP0_CXA_TOP_CXA_APC0_APCCTL_DISABLE_VG_NOT_SYS_ON );

        fapi2::putScom(TGT0, 0x2010818ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x2010819ull, l_scom_buffer );
        l_scom_buffer.insert<4, 4, 60, uint64_t>(0);
        fapi2::putScom(TGT0, 0x2010819ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x201081bull, l_scom_buffer );

        l_scom_buffer.insert<45, 3, 61, uint64_t>(7);
        l_scom_buffer.insert<48, 4, 60, uint64_t>(2);

        fapi2::putScom(TGT0, 0x201081bull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x201081cull, l_scom_buffer );
        l_scom_buffer.insert<18, 4, 60, uint64_t>(1);
        fapi2::putScom(TGT0, 0x201081cull, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_int_scom.C
fapi2::ReturnCode p9_int_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::ATTR_EC_Type   l_chip_ec;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x501300aull, l_scom_buffer );

        l_scom_buffer.insert<0, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<1, 1, 63, uint64_t>(1);

        l_scom_buffer.insert<5, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID );
        l_scom_buffer.insert<9, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID );

        fapi2::putScom(TGT0, 0x501300aull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013021ull, l_scom_buffer );

        l_scom_buffer.insert<49, 1, 63, uint64_t>(1);

        constexpr auto l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_G_ON = 0x1;
        l_scom_buffer.insert<47, 1, 63, uint64_t>(l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_G_ON );

        constexpr auto l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_VG_NOT_SYS_ON = 0x1;
        l_scom_buffer.insert<46, 1, 63, uint64_t>(l_INT_INT_CQ_INT_CQ_PBO_CTL_DISABLE_VG_NOT_SYS_ON );

        fapi2::putScom(TGT0, 0x5013021ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013033ull, l_scom_buffer );

        if (l_chip_ec <= 0x20)
        {
            l_scom_buffer = 0x2000005C040281C3;
        }
        else
        {
            l_scom_buffer = 0x0000005C040081C3;
        }

        fapi2::putScom(TGT0, 0x5013033ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013036ull, l_scom_buffer );
        l_scom_buffer = 0;
        fapi2::putScom(TGT0, 0x5013036ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013037ull, l_scom_buffer );
        l_scom_buffer = 0x9554021F80110E0C;
        fapi2::putScom(TGT0, 0x5013037ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013130ull, l_scom_buffer );
        l_scom_buffer.insert<2, 6, 58, uint64_t>(0x18 );
        l_scom_buffer.insert<10, 6, 58, uint64_t>(0x18 );
        fapi2::putScom(TGT0, 0x5013130ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013140ull, l_scom_buffer );
        l_scom_buffer = 0x050043EF00100020;
        fapi2::putScom(TGT0, 0x5013140ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013141ull, l_scom_buffer );
        l_scom_buffer = 0xFADFBB8CFFAFFFD7;
        fapi2::putScom(TGT0, 0x5013141ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013178ull, l_scom_buffer );
        l_scom_buffer = 0x0002000610000000;
        fapi2::putScom(TGT0, 0x5013178ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501320eull, l_scom_buffer );
        l_scom_buffer.insert<0, 48, 0, uint64_t>(0x6262220242160000 );
        fapi2::putScom(TGT0, 0x501320eull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5013214ull, l_scom_buffer );
        l_scom_buffer.insert<16, 16, 48, uint64_t>(0x5BBF );
        fapi2::putScom(TGT0, 0x5013214ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501322bull, l_scom_buffer );
        l_scom_buffer.insert<58, 6, 58, uint64_t>(0x18 );
        fapi2::putScom(TGT0, 0x501322bull, l_scom_buffer);
    }
    if (l_chip_ec == 0x20)
    {
        fapi2::getScom( TGT0, 0x5013272ull, l_scom_buffer );
        l_scom_buffer.insert<0, 44, 20, uint64_t>(0x0002C018006 );
        fapi2::putScom(TGT0, 0x5013272ull, l_scom_buffer);

        fapi2::getScom( TGT0, 0x5013273ull, l_scom_buffer );
        l_scom_buffer.insert<0, 44, 20, uint64_t>(0xFFFCFFEFFFA );
        fapi2::putScom(TGT0, 0x5013273ull, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_vas_scom.C
fapi2::ReturnCode p9_vas_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                              const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    /// ATTR_CHIP_EC_FEATURE_HW414700 == (dd == 0x20)

    fapi2::ATTR_EC_Type   l_chip_ec;
    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_EC, TGT0, l_chip_ec);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID_Type l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID;
    FAPI_ATTR_GET(fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID, TGT1, l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x3011803ull, l_scom_buffer );
        l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00210102540D7FFF );
        fapi2::putScom(TGT0, 0x3011803ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x3011806ull, l_scom_buffer );
        l_scom_buffer.insert<0, 54, 0, uint64_t>(0x0000000000000000 );
        fapi2::putScom(TGT0, 0x3011806ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x3011807ull, l_scom_buffer );

        if (l_chip_ec == 0x20)
        {
            l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00DD020180000000 );
        }
        else
        {
            l_scom_buffer.insert<0, 54, 0, uint64_t>(0x00DF020180000000 );
        }

        fapi2::putScom(TGT0, 0x3011807ull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x301184dull, l_scom_buffer );

        l_scom_buffer.insert<0, 4, 60, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID );
        l_scom_buffer.insert<4, 3, 61, uint64_t>(l_TGT1_ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID );

        fapi2::putScom(TGT0, 0x301184dull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x301184eull, l_scom_buffer );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_ADDR_BAR_MODE_OFF = 0x0;
        l_scom_buffer.insert<13, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_ADDR_BAR_MODE_OFF );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_SKIP_G_ON = 0x1;
        l_scom_buffer.insert<14, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_SKIP_G_ON );

        l_scom_buffer.insert<20, 8, 56, uint64_t>(0xFC );
        l_scom_buffer.insert<28, 8, 56, uint64_t>(0xFC );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_WR_ON = 0x1;
        l_scom_buffer.insert<1, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_WR_ON );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_RD_ON = 0x1;
        l_scom_buffer.insert<5, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_G_RD_ON );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_WR_ON = 0x1;
        l_scom_buffer.insert<2, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_WR_ON );

        constexpr auto l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_RD_ON = 0x1;
        l_scom_buffer.insert<6, 1, 63, uint64_t>(l_VA_VA_SOUTH_VA_EG_EG_SCF_DISABLE_VG_RD_ON );

        fapi2::putScom(TGT0, 0x301184eull, l_scom_buffer);
    }

    if (l_chip_ec == 0x20)
    {
        fapi2::getScom( TGT0, 0x301184full, l_scom_buffer );
        l_scom_buffer.insert<0, 1, 63, uint64_t>(0x1 );
        fapi2::putScom(TGT0, 0x301184full, l_scom_buffer);
    }
}

// src/import/chips/p9/procedures/hwp/initfiles/p9_psi_scom.C
fapi2::ReturnCode p9_psi_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0)
{
    fapi2::buffer<uint64_t> l_scom_buffer;

    fapi2::getScom( TGT0, 0x4011803ull, l_scom_buffer );
    l_scom_buffer.insert<0, 7, 0, uint64_t>(0xFE00000000000000 );
    fapi2::putScom(TGT0, 0x4011803ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x4011806ull, l_scom_buffer );
    l_scom_buffer.insert<0, 7, 0, uint64_t>(0x0000000000000000 );
    fapi2::putScom(TGT0, 0x4011806ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x4011807ull, l_scom_buffer );
    l_scom_buffer.insert<0, 7, 0, uint64_t>(0x0000000000000000 );
    fapi2::putScom(TGT0, 0x4011807ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x5012903ull, l_scom_buffer );
    l_scom_buffer.insert<0, 29, 35, uint64_t>(0x7E040DF);
    fapi2::putScom(TGT0, 0x5012903ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x5012906ull, l_scom_buffer );
    l_scom_buffer.insert<0, 29, 35, uint64_t>(0);
    fapi2::putScom(TGT0, 0x5012906ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x5012907ull, l_scom_buffer );
    l_scom_buffer.insert<0, 29, 35, uint64_t>(0x18050020);
    fapi2::putScom(TGT0, 0x5012907ull, l_scom_buffer);

    fapi2::getScom( TGT0, 0x501290full, l_scom_buffer );
    l_scom_buffer.insert<16, 12, 52, uint64_t>(0x000 );
    l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    fapi2::putScom(TGT0, 0x501290full, l_scom_buffer);
}
```
