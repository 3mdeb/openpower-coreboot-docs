Power management common functions are used in both 6.11 and 21.1 isteps.

```cpp
static void loadPMComplex(
    TARGETING::Target * i_target,
    uint64_t i_homerPhysAddr,
    uint64_t i_commonPhysAddr)
{
    resetPMComplex(i_target);
    void* l_homerVAddr = convertHomerPhysToVirt(i_target, i_homerPhysAddr);
    if(nullptr == l_homerVAddr)
    {
        return;
    }
    uint64_t l_occImgPaddr = i_homerPhysAddr + HOMER_OFFSET_TO_OCC_IMG;
    uint64_t l_occImgVaddr = l_homerVAddr + HOMER_OFFSET_TO_OCC_IMG;
    loadOCCSetup(i_target, l_occImgPaddr, l_occImgVaddr, i_commonPhysAddr);
#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    HBOCC::loadOCCImageDuringIpl(i_target, l_occImgVaddr); // analyzed
#endif
#if defined(CONFIG_IPLTIME_CHECKSTOP_ANALYSIS) && !defined(__HOSTBOOT_RUNTIME)
    HBOCC::loadHostDataToSRAM(i_target);
#else
    loadHostDataToHomer(i_target, l_occImgVaddr + HOMER_OFFSET_TO_OCC_HOST_DATA);
    loadHcode(i_target, l_homerVAddr, HBPM::PM_LOAD);
#endif
}

fapi2::ReturnCode p9_pm_corequad_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode,
    const uint32_t i_cmeFirMask,
    const uint32_t i_cppmErrMask,
    const uint32_t i_qppmErrMask)
{
    if (i_mode == p9pm::PM_INIT)
    {
        pm_corequad_init(i_target);
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        pm_corequad_reset(i_target, i_cmeFirMask, i_cppmErrMask, i_qppmErrMask);
    }
}

fapi2::ReturnCode p9_pm_ocb_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint32_t                   i_ocb_bar)
{
    pm_ocb_setup(
        i_target,
        p9ocb::OCB_CHAN0,
        p9ocb::OCB_TYPE_LINSTR,
        i_ocb_bar,
        p9ocb::OCB_UPD_PIB_REG,
        0,
        p9ocb::OCB_Q_OUFLOW_NULL,
        p9ocb::OCB_Q_ITPTYPE_NULL);
}

static void pm_ocb_setup(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM    i_ocb_chan,
    const p9ocb::PM_OCB_CHAN_TYPE   i_ocb_type,
    const uint32_t                  i_ocb_bar,
    const p9ocb::PM_OCB_CHAN_REG    i_ocb_upd_reg,
    const uint8_t                   i_ocb_q_len,
    const p9ocb::PM_OCB_CHAN_OUFLOW i_ocb_ouflow_en,
    const p9ocb::PM_OCB_ITPTYPE     i_ocb_itp_type)
{
    fapi2::buffer<uint64_t> l_mask_or(0);
    fapi2::buffer<uint64_t> l_mask_clear(0);

    if (i_ocb_type == p9ocb::OCB_TYPE_LIN)
    {
        l_mask_clear.setBit<4, 2>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_LINSTR)
    {
        l_mask_or.setBit<4>();
        l_mask_clear.setBit<5>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_CIRC)
    {
        l_mask_or.setBit<4, 2>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_PUSHQ)
    {
        l_mask_or.setBit<4, 2>();

        if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_EN)
        {
            l_mask_or.setBit<3>();
        }
        else if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_DIS)
        {
            l_mask_clear.setBit<3>();
        }
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_PULLQ)
    {
        l_mask_or.setBit<4, 2>();

        if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_EN)
        {
            l_mask_or.setBit<2>();
        }
        else if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_DIS)
        {
            l_mask_clear.setBit<2>();
        }
    }

    fapi2::putScom(i_target, OCBCSRn_OR[i_ocb_chan], l_mask_or);
    fapi2::putScom(i_target, OCBCSRn_CLEAR[i_ocb_chan], l_mask_clear);

    fapi2::buffer<uint64_t> l_data64;
    if(!(i_ocb_type == p9ocb::OCB_TYPE_NULL
    || i_ocb_type == p9ocb::OCB_TYPE_CIRC))
    {
        uint32_t l_ocbase;
        if(i_ocb_type == p9ocb::OCB_TYPE_LIN
        || i_ocb_type == p9ocb::OCB_TYPE_LINSTR)
        {
            l_ocbase = OCBARn[i_ocb_chan];
        }
        else if (i_ocb_type == p9ocb::OCB_TYPE_PUSHQ)
        {
            l_ocbase = OCBSHBRn[i_ocb_chan];
        }
        else
        {
            l_ocbase = OCBSLBRn[i_ocb_chan];
        }

        l_data64.flush<0>().insertFromRight<0, 32>(i_ocb_bar);
        fapi2::putScom(i_target, l_ocbase, l_data64);
    }
    if(i_ocb_type == p9ocb::OCB_TYPE_PUSHQ
    && i_ocb_upd_reg == p9ocb::OCB_UPD_PIB_OCI_REG)
    {
        l_data64.flush<0>().insertFromRight<6, 5>(i_ocb_q_len);
        l_data64.insertFromRight<4, 2>(i_ocb_itp_type);
        l_data64.setBit<31>();
        fapi2::putScom(i_target, OCBSHCSn[i_ocb_chan], l_data64);
    }
    if ((i_ocb_type == p9ocb::OCB_TYPE_PULLQ) &&
        (i_ocb_upd_reg == p9ocb::OCB_UPD_PIB_OCI_REG))
    {
        l_data64.flush<0>().insertFromRight<6, 5>(i_ocb_q_len);
        l_data64.insertFromRight<4, 2>(i_ocb_itp_type);
        l_data64.setBit<31>();
        fapi2::putScom(i_target, OCBSLCSn[i_ocb_chan], l_data64);
    }
}

fapi2::ReturnCode p9_pm_pss_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    if (i_mode == p9pm::PM_INIT)
    {
        pm_pss_init(i_target);
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        pm_pss_reset(i_target);
    }
}

fapi2::ReturnCode pm_occ_gpe_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9occgpe::GPE_ENGINES i_engine)
{
    fapi2::buffer<uint64_t> l_data64;
    uint64_t l_controlReg = 0;
    uint64_t l_statusReg = 0;
    uint64_t l_instrAddrReg = 0;
    uint64_t l_intVecReg = 0;
    uint32_t l_pollCount = 10; // poll 10 times
    std::vector<uint64_t> l_gpeBaseAddress;

    if (i_engine == p9occgpe::GPE0)
    {
        l_controlReg    =   PU_GPE0_PPE_XIXCR;
        l_statusReg     =   PU_GPE0_GPEXIXSR_SCOM;
        l_instrAddrReg  =   PU_GPE0_PPE_XIDBGPRO;
        l_intVecReg     =   PU_GPE0_GPEIVPR_SCOM;
        l_gpeBaseAddress.push_back( GPE0_BASE_ADDRESS );
    }
    else if (i_engine == p9occgpe::GPE1)
    {
        l_controlReg = PU_GPE1_PPE_XIXCR;
        l_statusReg = PU_GPE1_GPEXIXSR_SCOM;
        l_instrAddrReg = PU_GPE1_PPE_XIDBGPRO;
        l_intVecReg = PU_GPE1_GPEIVPR_SCOM;
        l_gpeBaseAddress.push_back( GPE1_BASE_ADDRESS );
    }
    l_data64.flush<0>().insertFromRight(p9hcd::HALT, 1, 3);
    putScom(i_target, l_controlReg, l_data64);
    do
    {
        fapi2::getScom(i_target, l_statusReg, l_data64);

        if (l_data64.getBit<0>() == 1)
        {
            break;
        }

        fapi2::delay(1000); // In microseconds
    }
    while(--l_pollCount != 0);

    if (i_engine == p9occgpe::GPE0 || i_engine == p9occgpe::GPE1)
    {
        fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;
        return;
    }

    l_data64.flush<0>();
    fapi2::putScom(i_target, l_instrAddrReg, l_data64);
    putScom(i_target, l_intVecReg, l_data64);
}

fapi2::ReturnCode p9_pm_occ_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint64_t> l_mask64;

    uint64_t l_fir;
    uint64_t l_mask;
    uint64_t l_unmaskedErrors;

    fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR, l_data64);
    fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIRMASK, l_mask64);
    l_data64.extractToRight<0, 64>(l_fir);
    l_mask64.extractToRight<0, 64>(l_mask);

    if(i_mode == p9pm::PM_RESET)
    {
        pm_occ_fir_reset(i_target);
    }
    else if(i_mode == p9pm::PM_INIT)
    {
        pm_occ_fir_init(i_target);
    }
}

fapi2::ReturnCode p9_pm_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    uint8_t l_pm_firinit_flag;
    fapi2::buffer<uint64_t> l_data64;

    fapi2::getScom(i_target, PU_PBAFIR , l_data64);
    p9_pm_pba_firinit(i_target, i_mode);
    p9_pm_ppm_firinit(i_target, i_mode);
    p9_pm_cme_firinit(i_target, i_mode);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, l_pm_firinit_flag);
    if (i_mode == p9pm::PM_INIT)
    {
        if (l_pm_firinit_flag != fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED)
        {
            l_pm_firinit_flag = fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED;
            FAPI_ATTR_SET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, l_pm_firinit_flag);
        }
    }

}

fapi2::ReturnCode p9_pm_stop_gpe_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    const char* PM_MODE_NAME_VAR;
    uint8_t                 fusedModeState = 0;
    uint8_t                 coreQuiesceDis = 0;
    uint8_t                 l_core_number  = 0;
    fapi2::buffer<uint64_t> l_data64       = 0;

    if (i_mode == p9pm::PM_INIT)
    {
        const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
        FAPI_ATTR_GET(fapi2::ATTR_FUSED_CORE_MODE, FAPI_SYSTEM, fusedModeState);
        FAPI_ATTR_GET(fapi2::ATTR_SYSTEM_CORE_PERIODIC_QUIESCE_DISABLE, FAPI_SYSTEM, coreQuiesceDis)
        auto l_functional_core_vector = i_target.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);
        auto l_functional_ex_vector = i_target.getChildren<fapi2::TARGET_TYPE_EX>(fapi2::TARGET_STATE_FUNCTIONAL);
        for(auto l_ex_trgt : l_functional_ex_vector)
        {
            auto l_functional_core_vector = l_ex_trgt.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);
        }

        if (fusedModeState == 1)
        {
            auto l_functional_core_vector =
                i_target.getChildren<fapi2::TARGET_TYPE_CORE>
                (fapi2::TARGET_STATE_FUNCTIONAL);

            for(auto l_chplt_trgt : l_functional_core_vector)
            {
                FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_chplt_trgt, l_core_number);
                l_data64.flush<0>().setBit<C_CPPM_CPMMR_FUSED_CORE_MODE>();
                fapi2::putScom(l_chplt_trgt, C_CPPM_CPMMR_OR, l_data64);
            }

            l_data64.flush<0>();
            fapi2::getScom(i_target, PU_INT_TCTXT_CFG, l_data64);

            l_data64.setBit<PU_INT_TCTXT_CFG_CFG_FUSE_CORE_EN>();
            fapi2::putScom(i_target, PU_INT_TCTXT_CFG, l_data64);
        }
        if (coreQuiesceDis == 1)
        {
            auto l_functional_core_vector =
                i_target.getChildren<fapi2::TARGET_TYPE_CORE>
                (fapi2::TARGET_STATE_FUNCTIONAL);

            for(auto l_chplt_trgt : l_functional_core_vector)
            {
                FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_chplt_trgt, l_core_number);

                l_data64.flush<0>().setBit<p9hcd::CPPM_CPMMR_DISABLE_PERIODIC_CORE_QUIESCE>();
                fapi2::putScom(l_chplt_trgt, C_CPPM_CPMMR_OR, l_data64);
            }
        }
        p9_pm_pfet_init(i_target, i_mode);
        p9_pm_pba_init(i_target, p9pm::PM_RESET);


        uint8_t l_ivrm_attrval = 0;
        uint8_t l_vdm_attrval = 0;

        FAPI_ATTR_GET(fapi2::ATTR_IVRM_ENABLED, i_target, l_ivrm_attrval);

        FAPI_ATTR_GET(fapi2::ATTR_VDM_ENABLED, i_target, l_vdm_attrval);

        if((l_vdm_attrval || l_ivrm_attrval))
        {
            fapi2::getScom(i_target, 0x01020007, l_data64);
        }
        l_data64.flush<0>().setBit<P9N2_PU_OCB_OCI_OISR0_GPE2_ERROR>();

        fapi2::putScom(i_target, P9N2_PU_OCB_OCI_OIMR0_SCOM2, l_data64);
        fapi2::putScom(i_target, P9N2_PU_OCB_OCI_OISR0_SCOM1, l_data64);

        l_data64.flush<0>()
            .insertFromRight<0, 4>(0x1)
            .insertFromRight<4, 4>(0xA);
        fapi2::putScom(i_target, PU_GPE3_GPETSEL_SCOM, l_data64);
        l_data64.flush<0>().setBit<p9hcd::OCCFLG2_SGPE_HCODE_STOP_REQ_ERR_INJ>();
        fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);
        stop_gpe_init(i_target);
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        stop_gpe_reset(i_target);
    }
}

fapi2::ReturnCode p9_pm_pstate_gpe_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    const char* PM_MODE_NAME_VAR;
    if (i_mode == p9pm::PM_INIT)
    {
        pstate_gpe_init(i_target);
        p9_pm_pba_init(i_target, p9pm::PM_INIT);
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        pstate_gpe_reset(i_target);
    }
}

static void clear_occ_special_wakeups(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    // EX targets CHIPLET_IDs [0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x13, 0x13, 0x14, 0x14]
    auto l_exChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EX>(fapi2::TARGET_STATE_FUNCTIONAL);
    for (auto l_ex_chplt : l_exChiplets)
    {
        fapi2::getScom(l_ex_chplt, EX_PPM_SPWKUP_OCC, 0);
        fapi2::putScom(l_ex_chplt, EX_PPM_SPWKUP_OCC, 0);
    }
}

errlHndl_t callWakeupHwp(TARGETING::Target* i_target,
                         HandleOptions_t i_enable)
{
    if(i_target->getAttr<TARGETING::ATTR_TYPE>() == TARGETING::TYPE_PROC)
    {
        TargetHandleList pCoreList;
        getChildChiplets( pCoreList, i_target, TARGETING::TYPE_CORE );

        for (auto pCore_it = pCoreList.begin();
             pCore_it != pCoreList.end();
             ++pCore_it )
        {
            callWakeupHwp(*pCore_it, i_enable);
        }
        return;
    }

    // Need to handle multiple calls to enable special wakeup
    // Count attribute will keep track and disable when zero
    // Assume HBRT is single-threaded, so no issues with concurrency
    uint32_t l_count = (i_target)->getAttr<ATTR_SPCWKUP_COUNT>();

    // Only call the HWP if 0-->1 or 1-->0 or if it is a force
    if(((l_count == 0) && (i_enable==WAKEUP::ENABLE))
    || ((l_count == 1) && (i_enable==WAKEUP::DISABLE))
    || ((l_count > 1)  && (i_enable==WAKEUP::FORCE_DISABLE)) )
    {
        p9specialWakeup::PROC_SPCWKUP_OPS l_spcwkupType;
        p9specialWakeup::PROC_SPCWKUP_ENTITY l_spcwkupSrc;
        if(!INITSERVICE::spBaseServicesEnabled())
        {
            l_spcwkupSrc = p9specialWakeup::FSP;
        }
        else
        {
            l_spcwkupSrc = p9specialWakeup::HOST;
        }

        if(i_enable==WAKEUP::ENABLE)
        {
            l_spcwkupType = p9specialWakeup::SPCWKUP_ENABLE;
        }
        else  // DISABLE or FORCE_DISABLE
        {
            l_spcwkupType = p9specialWakeup::SPCWKUP_DISABLE;
        }

        if(l_type == TARGETING::TYPE_EQ)
        {
            fapi2::Target<fapi2::TARGET_TYPE_EQ>
                l_fapi_target(const_cast<TARGETING::Target*>(i_target));

            p9_cpu_special_wakeup_eq(
                l_fapi_target,
                l_spcwkupType,
                l_spcwkupSrc);
        }
        else if(l_type == TARGETING::TYPE_EX)
        {
            fapi2::Target<fapi2::TARGET_TYPE_EX_CHIPLET>
                l_fapi_target(const_cast<TARGETING::Target*>(i_target));

            p9_cpu_special_wakeup_ex(
                l_fapi_target,
                l_spcwkupType,
                l_spcwkupSrc);
        }
        else if(l_type == TARGETING::TYPE_CORE)
        {
            fapi2::Target<fapi2::TARGET_TYPE_CORE>l_fapi_target(const_cast<TARGETING::Target*>(i_target));

            p9_cpu_special_wakeup_core(
                l_fapi_target,
                l_spcwkupType,
                l_spcwkupSrc);
        }
    }

    // Update the counter
    if(!l_errl)
    {
        if(i_enable == WAKEUP::ENABLE)
        {
            l_count++;
        }
        else if(i_enable == WAKEUP::DISABLE)
        {
            l_count--;
        }
        else if(i_enable == WAKEUP::FORCE_DISABLE)
        {
            l_count = 0;
        }
        i_target->setAttr<ATTR_SPCWKUP_COUNT>(l_count);
    }

    return l_errl;
}

errlHndl_t callWakeupHyp(TARGETING::Target* i_target,
                         HandleOptions_t i_enable)
{
#ifdef __HOSTBOOT_RUNTIME
    TargetHandleList pCoreList;
    if(i_target->getAttr<TARGETING::ATTR_TYPE>() == TARGETING::TYPE_CORE)
    {
        pCoreList.clear();
        pCoreList.push_back(i_target);
    }
    else
    {
        getChildChiplets( pCoreList, i_target, TARGETING::TYPE_CORE );
    }

    for ( auto pCore_it = pCoreList.begin();
          pCore_it != pCoreList.end();
          ++pCore_it )
    {
        TARGETING::rtChipId_t rtTargetId = 0;
        TARGETING::getRtTarget(*pCore_it, rtTargetId);

        uint32_t mode;
        if(i_enable == WAKEUP::ENABLE)
        {
            mode = HBRT_WKUP_FORCE_AWAKE;
        }
        else if(i_enable == WAKEUP::DISABLE)
        {
            mode = HBRT_WKUP_CLEAR_FORCE;
        }
        else if(i_enable == WAKEUP::FORCE_DISABLE)
        {
            mode = HBRT_WKUP_CLEAR_FORCE_COMPLETELY;
        }

        if((mode == HBRT_WKUP_CLEAR_FORCE_COMPLETELY)
            && !TARGETING::is_phyp_load()
            && !(g_hostInterfaces->get_interface_capabilities(HBRT_CAPS_SET1_OPAL) & HBRT_CAPS_OPAL_HAS_WAKEUP_CLEAR) )
        {
            break;
        }
        g_hostInterfaces->wakeup(rtTargetId, mode);
    }
#endif
}

bool useHypWakeup( void )
{
#ifdef __HOSTBOOT_RUNTIME
    // FSP and BMC runtime use hostservice for wakeup, provided that
    //  we are using a level of opal-prd that supports it

    // Always use the hyp call on FSP systems
    if(INITSERVICE::spBaseServicesEnabled()
    || TARGETING::is_phyp_load()
    || ((g_hostInterfaces != NULL)
    && (g_hostInterfaces->get_interface_capabilities != NULL)
    && (g_hostInterfaces->get_interface_capabilities(HBRT_CAPS_SET1_OPAL) & HBRT_CAPS_OPAL_HAS_WAKEUP)
    && (g_hostInterfaces->wakeup != NULL)))
    {
        return true;
    }
#endif
    return false;
}

errlHndl_t handleSpecialWakeup(TARGETING::Target* i_target,
                               HandleOptions_t i_enable)
{
    if(useHypWakeup())
    {
        callWakeupHyp( i_target, i_enable );
    }
    else
    {
        callWakeupHwp( i_target, i_enable );
    }
}

fapi2::ReturnCode platSpecialWakeup(const Target<TARGET_TYPE_ALL>& i_target,
                                    const bool i_enable)
{
    TARGETING::Target* l_target = i_target.get();
    WAKEUP::HandleOptions_t l_option = WAKEUP::DISABLE;
    if(i_enable)
    {
        l_option = WAKEUP::ENABLE;
    }
    WAKEUP::handleSpecialWakeup(l_target, l_option);
}

template<TargetType T, MulticastType M, typename V>
inline ReturnCode specialWakeup(const Target<T, M, V>& i_target,
                                const bool i_enable)
{
    platSpecialWakeup( i_target, i_enable);
}

fapi2::ReturnCode special_wakeup_all(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const bool i_enable)
{
    auto l_exChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EX>(fapi2::TARGET_STATE_FUNCTIONAL);

    // For each EX target
    for (auto l_ex_chplt : l_exChiplets)
    {
        fapi2::ATTR_CHIP_UNIT_POS_Type l_ex_num;
        FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, l_ex_chplt, l_ex_num);
        fapi2::specialWakeup( l_ex_chplt, i_enable );
    }
}

fapi2::ReturnCode p9_pm_occ_control
(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
 const p9occ_ctrl::PPC_CONTROL i_ppc405_reset_ctrl,
 const p9occ_ctrl::PPC_BOOT_CONTROL i_ppc405_boot_ctrl,
 const uint64_t i_ppc405_jump_to_main_instr)
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint64_t> l_firMask;
    fapi2::buffer<uint64_t> l_occfir;
    fapi2::buffer<uint64_t> l_jtagcfg;

    if (i_ppc405_boot_ctrl != p9occ_ctrl::PPC405_BOOT_NULL)
    {
        fapi2::putScom(i_target, PU_SRAM_SRBV0_SCOM, l_data64);
        fapi2::putScom(i_target, PU_SRAM_SRBV1_SCOM, l_data64);
        fapi2::putScom(i_target, PU_SRAM_SRBV2_SCOM, l_data64);
        if (i_ppc405_boot_ctrl == p9occ_ctrl::PPC405_BOOT_SRAM)
        {
            l_data64.flush<0>().insertFromRight(PPC405_BRANCH_SRAM_INSTR, 0, 32);
        }
        else if (i_ppc405_boot_ctrl == p9occ_ctrl::PPC405_BOOT_MEM)
        {
            bootMemory(i_target, l_data64);
        }
        else if(i_ppc405_boot_ctrl == p9occ_ctrl::PPC405_BOOT_WITHOUT_BL)
        {
            l_data64.flush<0>().insertFromRight(i_ppc405_jump_to_main_instr, 0, 64);
        }
        else
        {
            l_data64.flush<0>().insertFromRight(PPC405_BRANCH_OLD_INSTR, 0, 32);
        }
        fapi2::putScom(i_target, PU_SRAM_SRBV3_SCOM, l_data64);
    }

    switch (i_ppc405_reset_ctrl)
    {
        case p9occ_ctrl::PPC405_RESET_NULL:
            // no-op
            break;
        case p9occ_ctrl::PPC405_RESET_OFF:
            fapi2::putScom(i_target, PU_OCB_PIB_OCR_CLEAR, ~BIT(OCB_PIB_OCR_CORE_RESET_BIT));
            break;
        case p9occ_ctrl::PPC405_RESET_ON:
            fapi2::putScom(i_target, PU_OCB_PIB_OCR_OR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
            break;
        case p9occ_ctrl::PPC405_HALT_OFF:
            fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_AND, ~BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
            break;
        case p9occ_ctrl::PPC405_HALT_ON:
            fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_OR, BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
            break;
        case p9occ_ctrl::PPC405_RESET_SEQUENCE:

            /// It is unsafe in general to simply reset the 405, as this is an
            /// asynchronous reset that can leave OCI slaves in unrecoverable
            /// states.
            /// This is a "safe" reset-entry sequence that includes
            /// halting the 405 (a synchronous operation) before issuing the
            /// reset. Since this sequence halts/unhalts the 405 and modifies
            /// FIRs it is called apart from the simple PPC405_RESET_OFF
            /// that simply sets the 405 reset bit.
            ///
            /// The sequence:
            ///
            /// 1. Mask the "405 halted" FIR bit to avoid FW thinking the halt
            /// we are about to inject on the 405 is an error.
            ///
            /// 2. Halt the 405. If the 405 does not halt in 1ms we note that
            /// but press on, hoping (probably in vain) that any subsequent
            /// reset actions will clear up the issue.
            /// To check if the 405 halted we must clear the FIR and verify
            /// that the FIR is set again.
            ///
            /// 3. Put the 405 into reset.
            ///
            /// 4. Clear the halt bit.
            ///
            /// 5. Restore the original FIR mask
            /// Save the FIR mask, and mask the halted FIR

            fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIRMASK, l_firMask);
            fapi2::putScom(i_target, PERV_TP_OCC_SCOM_OCCLFIRMASK_OR, BIT(OCCLFIR_PPC405_DBGSTOPACK_BIT));
            fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_OR, BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
            fapi2::delay(NS_DELAY, SIM_CYCLE_DELAY);
            fapi2::putScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR_AND, ~BIT(OCCLFIR_PPC405_DBGSTOPACK_BIT));
            fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR, l_occfir);
            fapi2::putScom(i_target, PU_OCB_PIB_OCR_OR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
            fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_AND, ~BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
            fapi2::putScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR_AND, ~BIT(OCCLFIR_PPC405_DBGSTOPACK_BIT));
            fapi2::putScom(i_target, PERV_TP_OCC_SCOM_OCCLFIRMASK, l_firMask);
            break;

        case p9occ_ctrl::PPC405_START:
            // Clear the halt bit
            fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_AND, ~BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
            // Set the reset bit
            fapi2::putScom(i_target, PU_OCB_PIB_OCR_OR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
            // Clear the reset bit
            fapi2::putScom(i_target, PU_OCB_PIB_OCR_CLEAR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
            break;

    }
}
```
