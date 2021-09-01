#define ATTR_FREQ_PB_MHZ (1866)

void* host_start_occ_xstop_handler( void *io_pArgs )
{
    ISTEP_ERROR::IStepError l_stepError;
    errlHndl_t l_err = nullptr;
    TARGETING::Target* masterproc = NULL;
    TARGETING::targetService().masterProcChipTargetHandle(masterproc);


    // If we have nothing external (FSP or OCC) to handle checkstops we are
    //  better off just crashing and having a chance to pull the HB
    //  traces off the system live

    TARGETING::Target * l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( l_sys );

#ifndef CONFIG_HANG_ON_MFG_SRC_TERM
    //When in MNFG_FLAG_SRC_TERM mode enable reboots to allow HB
    //to analyze now that the OCC is up and alive
    auto l_mnfgFlags = l_sys->getAttr<TARGETING::ATTR_MNFG_FLAGS>();

    // Check to see if SRC_TERM bit is set in MNFG flags
    if ((l_mnfgFlags & TARGETING::MNFG_FLAG_SRC_TERM) &&
        !(l_mnfgFlags & TARGETING::MNFG_FLAG_IMMEDIATE_HALT))
    {
        l_err = nullptr;

        //If HB_VOLATILE MFG_TERM_REBOOT_ENABLE flag is set at this point
        //Create errorlog to terminate the boot.
        Util::semiPersistData_t l_semiData;
        Util::readSemiPersistData(l_semiData);
        if (l_semiData.mfg_term_reboot == Util::MFG_TERM_REBOOT_ENABLE)
        {
            reboot();
        }

        Util::semiPersistData_t l_newSemiData;
        Util::readSemiPersistData(l_newSemiData);
        l_newSemiData.mfg_term_reboot = Util::MFG_TERM_REBOOT_ENABLE;
        Util::writeSemiPersistData(l_newSemiData);

        SENSOR::RebootControlSensor l_rbotCtl;
        l_rbotCtl.setRebootControl(SENSOR::RebootControlSensor::autoRebootSetting::ENABLE_REBOOTS);
    }
#endif

#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    void* l_homerVirtAddrBase = VmmManager::INITIAL_MEM_SIZE;
    uint64_t l_homerPhysAddrBase = mm_virt_to_phys(l_homerVirtAddrBase);
    uint64_t l_commonPhysAddr = l_homerPhysAddrBase + VMM_HOMER_REGION_SIZE;
    HBPM::loadPMComplex(masterproc, l_homerPhysAddrBase, l_commonPhysAddr);
    HBOCC::startOCCFromSRAM(masterproc);
#endif
    uint64_t l_xstopXscom = XSCOM::generate_mmio_addr(masterproc, Kernel::MachineCheck::MCHK_XSTOP_FIR_SCOM_ADDR);
    Kernel::MachineCheck::setCheckstopData(l_xstopXscom, Kernel::MachineCheck::MCHK_XSTOP_FIR_VALUE);
    return l_stepError.getErrorHandle();
}

errlHndl_t loadOCCSetup(
    TARGETING::Target* i_target,
    uint64_t i_occImgPaddr,
    uint64_t i_occImgVaddr,
    uint64_t i_commonPhysAddr)
{
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapiTarg(i_target);

    uint64_t l_occ_addr = i_occImgPaddr & PHYSICAL_ADDR_MASK;
    p9_pm_pba_bar_config(l_fapiTarg, 0, l_occ_addr); // analyzed

    TARGETING::Target* sys = nullptr;
    TARGETING::targetService().getTopLevelTarget(sys);
    sys->setAttr<ATTR_OCC_COMMON_AREA_PHYS_ADDR>(i_commonPhysAddr);

    uint64_t l_common_addr = i_commonPhysAddr & PHYSICAL_ADDR_MASK;
    p9_pm_pba_bar_config(l_fapiTarg, 2, l_common_addr); // analyzed
}

errlHndl_t resetPMComplex(TARGETING::Target * i_target)
{
    uint64_t l_homerPhysAddr;
    l_homerPhysAddr = i_target->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
    void* l_homerVAddr = convertHomerPhysToVirt(i_target,l_homerPhysAddr);
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapiTarg(i_target);
    ATTR_HB_INITIATED_PM_RESET_type l_chipResetState = i_target->getAttr<TARGETING::ATTR_HB_INITIATED_PM_RESET>();
    if (HB_INITIATED_PM_RESET_COMPLETE == l_chipResetState)
    {
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(HB_INITIATED_PM_RESET_INACTIVE);
        return;
    }

#if defined(__HOSTBOOT_RUNTIME) && defined(CONFIG_NVDIMM)
    NVDIMM::notifyNvdimmProtectionChange(i_target, NVDIMM::OCC_INACTIVE);
#endif
    p9_pm_reset(l_fapiTarg, l_homerVAddr );
#ifdef __HOSTBOOT_RUNTIME
    if(HB_INITIATED_PM_RESET_IN_PROGRESS != l_chipResetState)
    {
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(HB_INITIATED_PM_RESET_IN_PROGRESS);
        Singleton<ATTN::Service>::instance().handleAttentions(i_target);
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(l_chipResetState);
    }
#endif
    if ((TARGETING::is_phyp_load()) && (nullptr != l_homerVAddr))
    {
        HBPM_UNMAP(l_homerVAddr);
        i_target->setAttr<ATTR_HOMER_VIRT_ADDR>(0);
    }
}

fapi2::ReturnCode p9_pm_recovery_ffdc_misc (
                    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procChipTarget,
                    void* i_pHomerImage )
{
    PlatPmComplex l_pmFfdc (i_procChipTarget, PLAT_MISC);
    l_pmFfdc.logPmResetPhase (i_pHomerImage);
}

fapi2::ReturnCode p9_pm_recovery_ffdc_qppm( const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip, void * i_pFfdcBuf )
{
    QppmRegs l_qppmFfdc( i_procChip );
    l_qppmFfdc.collectFfdc( i_pFfdcBuf, SCOM_REG);
}

fapi2::ReturnCode p9_pm_recovery_ffdc_cppm( const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip, void * i_pFfdcBuf )
{
    CppmRegs l_cppmFfdc( i_procChip );
    l_cppmFfdc.collectFfdc( i_pFfdcBuf, SCOM_REG );
}

fapi2::ReturnCode p9_pm_recovery_ffdc_occ (
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip,
        void*                                               i_pFfdcBuf )
{
    PlatOcc l_occFfdc( i_procChip );
    l_occFfdc.collectFfdc ( i_pFfdcBuf, (TRACES | SCOM_REG));
}

fapi2::ReturnCode p9_pm_recovery_ffdc_pgpe( const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip,
                                                void * i_pFfdcBuf )
{
    PlatPgpe l_pgpeFfdc( i_procChip );
    l_pgpeFfdc.collectFfdc( i_pFfdcBuf, (ALL & ~PPE_HALT_STATE) );
}

fapi2::ReturnCode p9_pm_recovery_ffdc_sgpe( const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip,
                                            void * i_pFfdcBuf )
{
    PlatSgpe l_sgpeFfdc( i_procChip );
    l_sgpeFfdc.collectFfdc( i_pFfdcBuf, (ALL & ~PPE_HALT_STATE) );
}

fapi2::ReturnCode p9_pm_recovery_ffdc_cme( const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP >& i_procChip, void * i_pFfdcBuf )
{
    PlatCme l_cmeFfdc( i_procChip );
    l_cmeFfdc.collectFfdc( i_pFfdcBuf, (ALL & ~(PPE_HALT_STATE | FIR_STATE)));
}

fapi2::ReturnCode p9_pm_recovery_ffdc_base (
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procChipTarget,
        void* i_pHomerImage )
{
    std::vector<PlatPmComplex*> l_pPlatList;
    fapi2::ATTR_PM_RESET_PHASE_Type l_phase = PM_RESET_FFDC_SEC_INIT;

    // init all the platform FFDC headers
    FAPI_ATTR_SET (fapi2::ATTR_PM_RESET_PHASE, i_procChipTarget, l_phase);

    l_pPlatList.push_back (new PlatPmComplex(i_procChipTarget));
    l_pPlatList.push_back (new PlatCme(i_procChipTarget));
    l_pPlatList.push_back (new PlatSgpe(i_procChipTarget));
    l_pPlatList.push_back (new PlatPgpe(i_procChipTarget));
    l_pPlatList.push_back (new PlatOcc(i_procChipTarget));
    l_pPlatList.push_back (new CppmRegs(i_procChipTarget));
    l_pPlatList.push_back (new QppmRegs(i_procChipTarget));

    for ( auto& it : l_pPlatList )
    {
        it->init (i_pHomerImage);
    }
    l_phase = PM_RESET_FFDC_GET_FIRS;
    FAPI_ATTR_SET(fapi2::ATTR_PM_RESET_PHASE, i_procChipTarget, l_phase);

    for ( auto& it : l_pPlatList )
    {
        it->collectFfdc (i_pHomerImage, (PPE_HALT_STATE | FIR_STATE));
    }

fapi_try_exit:
    for ( auto& it : l_pPlatList )
        delete it;
    return  fapi2::current_err;
}

fapi2::ReturnCode p9_pm_collect_ffdc (
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    void* i_pHomerImage,
    const uint8_t i_plat )
{
    using namespace p9_stop_recov_ffdc;

    fapi2::ReturnCode l_rc;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    fapi2::ATTR_PM_RESET_FFDC_ENABLE_Type l_ffdcEnable = fapi2::ENUM_ATTR_PM_RESET_FFDC_ENABLE_FALSE;
    fapi2::ATTR_PM_RESET_PHASE_Type l_phase = PM_RESET_UNKNOWN;

    if( i_pHomerImage == nullptr )
    {
        return;
    }

    FAPI_ATTR_GET(fapi2::ATTR_PM_RESET_PHASE, i_target, l_phase);
    FAPI_ATTR_GET(fapi2::ATTR_PM_RESET_FFDC_ENABLE, FAPI_SYSTEM, l_ffdcEnable);

    if (l_ffdcEnable == fapi2::ENUM_ATTR_PM_RESET_FFDC_ENABLE_TRUE)
    {
        switch (i_plat)
        {
            case PLAT_INIT:
                p9_pm_recovery_ffdc_base(i_target, i_pHomerImage);
                break;
            case PLAT_CME:
                p9_pm_recovery_ffdc_cme(i_target, i_pHomerImage);
                break;
            case PLAT_SGPE:
                p9_pm_recovery_ffdc_sgpe(i_target, i_pHomerImage);
                break;
            case PLAT_PGPE:
                p9_pm_recovery_ffdc_pgpe(i_target, i_pHomerImage);
                break;
            case PLAT_OCC:
                p9_pm_recovery_ffdc_occ(i_target, i_pHomerImage);
                break;
            case PLAT_CPPM:
                p9_pm_recovery_ffdc_cppm(i_target, i_pHomerImage);
                break;
            case PLAT_QPPM:
                p9_pm_recovery_ffdc_qppm(i_target, i_pHomerImage);
                break;
            case PLAT_MISC:
                p9_pm_recovery_ffdc_misc(i_target, i_pHomerImage);
                break;
        }
    }
}

fapi2::ReturnCode p9_pm_reset_clear_errinj (
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::ATTR_CHIP_UNIT_POS_Type l_chpltNumber = 0;

    auto l_coreChiplets =
        i_target.getChildren<fapi2::TARGET_TYPE_CORE>
        (fapi2::TARGET_STATE_FUNCTIONAL);
    l_data64.flush<0>()
        .setBit<p9hcd::OCCFLG2_SGPE_HCODE_STOP_REQ_ERR_INJ>()
        .setBit<p9hcd::OCCFLG2_PGPE_HCODE_FIT_ERR_INJ>()
        .setBit<p9hcd::OCCFLG2_PGPE_HCODE_PSTATE_REQ_ERR_INJ>();
    fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);
    for (auto l_core_chplt : l_coreChiplets)
    {
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_core_chplt, l_chpltNumber);
        l_data64.flush<0>()
                .setBit<p9hcd::CPPM_CSAR_FIT_HCODE_ERROR_INJECT>()
                .setBit<p9hcd::CPPM_CSAR_ENABLE_PSTATE_REGISTRATION_INTERLOCK>()
                .setBit<p9hcd::CPPM_CSAR_PSTATE_HCODE_ERROR_INJECT>()
                .setBit<p9hcd::CPPM_CSAR_STOP_HCODE_ERROR_INJECT>();
        fapi2::putScom(l_core_chplt, C_CPPM_CSAR_CLEAR, l_data64);
    }
}

fapi2::ReturnCode
p9_pm_set_auto_spwkup(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer <uint64_t> l_deadCoreVector;
    fapi2::buffer <uint64_t> l_ccsrData;
    uint8_t l_malfAlertActive = fapi2::ENUM_ATTR_PM_MALF_CYCLE_INACTIVE;
    FAPI_ATTR_GET(fapi2::ATTR_PM_MALF_CYCLE, i_target, l_malfAlertActive);
    getScom(i_target, P9N2_PU_OCB_OCI_CCSR_SCOM, l_ccsrData );

    if (l_malfAlertActive == fapi2::ENUM_ATTR_PM_MALF_CYCLE_ACTIVE)
    {
        getScom(i_target, P9N2_PU_OCB_OCI_OCCFLG2_SCOM, l_deadCoreVector);
    }

    // For each EX target
    for (auto& l_ex_chplt : i_target.getChildren<fapi2::TARGET_TYPE_EX>
         (fapi2::TARGET_STATE_FUNCTIONAL))
    {

        fapi2::buffer<uint64_t> l_gpmmr;
        fapi2::buffer<uint64_t> l_lmcr;
        uint32_t l_bit;

        fapi2::ATTR_CHIP_UNIT_POS_Type l_ex_num;
        FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, l_ex_chplt, l_ex_num);

        for (auto& l_core : l_ex_chplt.getChildren<fapi2::TARGET_TYPE_CORE>
             (fapi2::TARGET_STATE_FUNCTIONAL))
        {
            fapi2::ATTR_CHIP_UNIT_POS_Type l_core_num;
            FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_core, l_core_num);

            fapi2::getScom(l_core, C_PPM_GPMMR_SCOM,  l_gpmmr);

            if (l_deadCoreVector.getBit(l_core_num))
            {
                continue;
            }

            if (l_gpmmr.getBit<EQ_PPM_GPMMR_SPECIAL_WKUP_DONE>())
            {
                l_bit = EQ_CME_SCOM_LMCR_C0_AUTO_SPECIAL_WAKEUP_DISABLE + (l_core_num % 2);
                l_lmcr.flush<0>().setBit(l_bit);
                fapi2::putScom(l_ex_chplt, EX_CME_SCOM_LMCR_SCOM1,  l_lmcr);
            }
            else
            {
                if (!(l_ccsrData.getBit(l_core_num)))
                {
                    continue;
                }
            }
        }
    }
}

fapi2::ReturnCode
avsIdleFrame(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
             const uint8_t i_avsBusNum,
             const uint8_t i_o2sBridgeNum)
{
    fapi2::buffer<uint64_t> l_idleframe = 0xFFFFFFFFFFFFFFFFull;
    fapi2::buffer<uint64_t> l_scomdata;
    l_scomdata.setBit<1, 1>();
    putScom(i_target, p9avslib::OCB_O2SCMD[i_avsBusNum][i_o2sBridgeNum], l_scomdata);
    l_scomdata = l_idleframe;
    putScom(i_target, p9avslib::OCB_O2SWD[i_avsBusNum][i_o2sBridgeNum], l_scomdata);
    avsPollVoltageTransDone(i_target, i_avsBusNum, i_o2sBridgeNum);
}

fapi2::ReturnCode
avsValidateResponse(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                    const uint8_t i_avsBusNum,
                    const uint8_t i_o2sBridgeNum,
                    const uint8_t i_throw_assert,
                    uint8_t& o_goodResponse
                   )
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint32_t> l_rsp_rcvd_crc;
    fapi2::buffer<uint8_t>  l_data_status_code;
    fapi2::buffer<uint32_t> l_rsp_data;

    uint32_t l_rsp_computed_crc;
    o_goodResponse = false;

    getScom(i_target, p9avslib::OCB_O2SRD[i_avsBusNum][i_o2sBridgeNum], l_data64);

    // Status Return Code and Received CRC
    l_data64.extractToRight(l_data_status_code, 0, 2);
    l_data64.extractToRight(l_rsp_rcvd_crc, 29, 3);
    l_data64.extractToRight(l_rsp_data, 0, 32);

    // Compute CRC on Response frame
    l_rsp_computed_crc = avsCRCcalc(l_rsp_data);

    if ((l_data_status_code == 0) &&                           // no error code
        (l_rsp_rcvd_crc == l_rsp_computed_crc) &&              // good crc
        (l_rsp_data != 0) && (l_rsp_data != 0xFFFFFFFF))       // valid response
    {
        o_goodResponse = true;
    }

}

fapi2::ReturnCode
avsVoltageWrite(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                const uint8_t i_avsBusNum,
                const uint8_t i_o2sBridgeNum,
                const uint32_t i_RailSelect,
                const uint32_t i_Voltage)
{

    uint32_t l_CmdType     = 0; // write and commit
    uint32_t l_CmdGroup    = 0;
    uint32_t l_CmdDataType = 0;

    // Drive a Write Command
    avsDriveCommand(
        i_target,
        i_avsBusNum,
        i_o2sBridgeNum,
        i_RailSelect,
        l_CmdType,
        l_CmdGroup,
        l_CmdDataType,
        i_Voltage);
}

fapi2::ReturnCode
avsIdleFrame(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
             const uint8_t i_avsBusNum,
             const uint8_t i_o2sBridgeNum)
{
    fapi2::buffer<uint64_t> l_idleframe = 0xFFFFFFFFFFFFFFFFull;
    fapi2::buffer<uint64_t> l_scomdata;

    l_scomdata.setBit<1, 1>();
    putScom(i_target, p9avslib::OCB_O2SCMD[i_avsBusNum][i_o2sBridgeNum], l_scomdata);
    l_scomdata = l_idleframe;
    putScom(i_target, p9avslib::OCB_O2SWD[i_avsBusNum][i_o2sBridgeNum], l_scomdata);

    // Wait on o2s_ongoing = 0
    avsPollVoltageTransDone(i_target, i_avsBusNum, i_o2sBridgeNum);
}

fapi2::ReturnCode
avsPollVoltageTransDone(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint8_t i_avsBusNum,
    const uint8_t i_o2sBridgeNum)
{
    fapi2::ReturnCode l_rc = fapi2::FAPI2_RC_SUCCESS;
    fapi2::buffer<uint64_t> l_data64;

    uint8_t l_count = 0;

    while (l_count < p9avslib::MAX_POLL_COUNT_AVS)
    {
        getScom(i_target, p9avslib::OCB_O2SST[i_avsBusNum][i_o2sBridgeNum], l_data64);

        if (!l_data64.getBit<0>())
        {
            break;  // Leave the polling loop as "ongoing" has deasserted
        }

        l_count++;
    }
}

#define AVS_CRC_DATA_MASK 0xFFFFFFF8
uint32_t avsCRCcalc(const uint32_t i_avs_cmd)
{
    //Polynomial = x^3 + x^1 + x^0 = 1*x^3 + 0*x^2 + 1*x^1 + 1*x^0
    //           = divisor(1011)
    uint32_t o_crc_value = 0;
    uint32_t l_polynomial = 0xB0000000;
    uint32_t l_msb =        0x80000000;

    o_crc_value = i_avs_cmd & AVS_CRC_DATA_MASK;

    while (o_crc_value & AVS_CRC_DATA_MASK)
    {
        if (o_crc_value & l_msb)
        {
            o_crc_value = o_crc_value ^ l_polynomial;
            l_polynomial = l_polynomial >> 1;
        }
        else
        {
            l_polynomial = l_polynomial >> 1;
        }

        l_msb = l_msb >> 1;
    }
    return o_crc_value;
}

fapi2::ReturnCode
avsDriveCommand(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                const uint8_t  i_avsBusNum,
                const uint8_t  i_o2sBridgeNum,
                const uint32_t i_RailSelect,
                const uint32_t i_CmdType,
                const uint32_t i_CmdGroup,
                const uint32_t i_CmdDataType,
                const uint32_t i_CmdData,
                enum avsBusOpType i_opType)
{

    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint32_t> l_data64WithoutCRC;
    fapi2::ReturnCode l_rc;

    uint32_t l_StartCode = 0b01;
    uint32_t l_Reserved = 0b000;
    uint32_t l_crc;
    l_data64.setBit<1, 1>();
    putScom(i_target, p9avslib::OCB_O2SCMD[i_avsBusNum][i_o2sBridgeNum], l_data64);
    l_data64.flush<0>();
    l_data64.insertFromRight<0, 2>(l_StartCode);
    l_data64.insertFromRight<2, 2>(i_CmdType);
    l_data64.insertFromRight<4, 1>(i_CmdGroup);
    l_data64.insertFromRight<5, 4>(i_CmdDataType);
    l_data64.insertFromRight<9, 4>(i_RailSelect);
    l_data64.insertFromRight<13, 16>(i_CmdData);
    l_data64.insertFromRight<29, 3>(l_Reserved);

    // Generate CRC
    l_data64.extract(l_data64WithoutCRC, 0, 32);
    l_crc = avsCRCcalc(l_data64WithoutCRC);
    l_data64.insertFromRight<29, 3>(l_crc);
    putScom(i_target, p9avslib::OCB_O2SWD[i_avsBusNum][i_o2sBridgeNum], l_data64);
    FAPI_TRY(avsPollVoltageTransDone(i_target, i_avsBusNum, i_o2sBridgeNum));
}

fapi2::ReturnCode
avsVoltageRead(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
               const uint8_t i_avsBusNum,
               const uint8_t i_o2sBridgeNum,
               const uint32_t i_RailSelect,
               uint32_t& o_Voltage)
{

    fapi2::buffer<uint64_t> l_data64;
    uint32_t l_CmdType     = 3; // read
    uint32_t l_CmdGroup    = 0;
    uint32_t l_CmdDataType = 0;
    uint32_t l_outboundCmdData = 0xFFFF;

    // Drive a Read Command
    avsDriveCommand(
        i_target,
        i_avsBusNum,
        i_o2sBridgeNum,
        i_RailSelect,
        l_CmdType,
        l_CmdGroup,
        l_CmdDataType,
        l_outboundCmdData);

    getScom(i_target, p9avslib::OCB_O2SRD[i_avsBusNum][i_o2sBridgeNum], l_data64);
    o_Voltage = (l_data64 & 0x00FFFF0000000000) >> 40;
}

fapi2::ReturnCode
avsInitExtVoltageControl(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint8_t i_avsBusNum,
    const uint8_t i_o2sBridgeNum)
{

    fapi2::buffer<uint64_t> l_data64;
    uint32_t l_avsbus_frequency, l_value, l_nest_frequency;
    uint16_t l_divider;

    ocb_o2sctrlf0a_t O2SCTRLF_value;
    O2SCTRLF_value.fields.o2s_frame_size_an = p9avslib::O2S_FRAME_SIZE;
    O2SCTRLF_value.fields.o2s_out_count1_an = p9avslib::O2S_FRAME_SIZE;
    O2SCTRLF_value.fields.o2s_in_delay1_an = p9avslib::O2S_IN_DELAY1;

    l_data64.insertFromRight<0, 6>(O2SCTRLF_value.fields.o2s_frame_size_an);
    l_data64.insertFromRight<6, 6>(O2SCTRLF_value.fields.o2s_out_count1_an);
    l_data64.insertFromRight<12, 6>(O2SCTRLF_value.fields.o2s_in_delay1_an);
    putScom(i_target, p9avslib::OCB_O2SCTRLF[i_avsBusNum][i_o2sBridgeNum], l_data64);

    ocb_o2sctrls0a_t O2SCTRLS_value;
    O2SCTRLS_value.fields.o2s_in_count2_an = p9avslib::O2S_FRAME_SIZE;

    l_data64.flush<0>();
    l_data64.insertFromRight<12, 6>(O2SCTRLS_value.fields.o2s_in_count2_an);
    putScom(i_target, p9avslib::OCB_O2SCTRLS[i_avsBusNum][i_o2sBridgeNum], l_data64);

    ocb_o2sctrl10a_t O2SCTRL1_value;
    O2SCTRL1_value.fields.o2s_bridge_enable_an = 1;

    //Nest frequency attribute in MHz
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>(), l_nest_frequency);

    // AVSBus frequency attribute in KHz
    FAPI_ATTR_GET(fapi2::ATTR_AVSBUS_FREQUENCY, fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>(), l_value);

    if (l_value == 0)
    {
        l_avsbus_frequency = p9avslib::AVSBUS_FREQUENCY / 1000;
    }
    else
    {
        l_avsbus_frequency = l_value / 1000;
    }

    // Divider = Nest Frequency / (AVSBus Frequency * 8) - 1
    l_divider = (l_nest_frequency / (l_avsbus_frequency * 8)) - 1;

    O2SCTRL1_value.fields.o2s_clock_divider_an = l_divider;
    O2SCTRL1_value.fields.o2s_nr_of_frames_an = 1;
    O2SCTRL1_value.fields.o2s_cpol_an = 0;
    O2SCTRL1_value.fields.o2s_cpha_an = 1;

    l_data64.flush<0>();
    l_data64.insertFromRight<0, 1>(O2SCTRL1_value.fields.o2s_bridge_enable_an);
    l_data64.insertFromRight<2, 1>(O2SCTRL1_value.fields.o2s_cpol_an);
    l_data64.insertFromRight<3, 1>(O2SCTRL1_value.fields.o2s_cpha_an);
    l_data64.insertFromRight<4, 10>(O2SCTRL1_value.fields.o2s_clock_divider_an);
    l_data64.insertFromRight<17, 1>(O2SCTRL1_value.fields.o2s_nr_of_frames_an);
    putScom(i_target, p9avslib::OCB_O2SCTRL1[i_avsBusNum][i_o2sBridgeNum], l_data64);

    // O2SCTRL1
    // OCC O2S Control2
    // [ 0:16] o2s_inter_frame_delay = filled in with ATTR l_data64
    // Needs to be 10us or greater for SPIVID part operation
    // Set to ~16us for conservatism using a 100ns hang pulse
    // 16us = 16000ns -> 16000/100 = 160 = 0xA0;  aligned to 0:16 -> 0x005
    ocb_o2sctrl20a_t O2SCTRL2_value;
    O2SCTRL2_value.fields.o2s_inter_frame_delay_an = 0x0;

    l_data64.flush<0>();
    l_data64.insertFromRight<0, 17>(O2SCTRL2_value.fields.o2s_inter_frame_delay_an);
    putScom(i_target, p9avslib::OCB_O2SCTRL2[i_avsBusNum][i_o2sBridgeNum], l_data64);
}

fapi2::ReturnCode
p9_setup_evid_voltageWrite(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                           const uint8_t i_bus_num,
                           const uint8_t i_rail_select,
                           const uint32_t i_voltage_mv,
                           const uint32_t i_ext_vrm_step_size_mv,
                           const P9_SETUP_EVID_CONSTANTS i_evid_value)

{

    uint8_t     l_goodResponse = 0;
    uint8_t     l_throwAssert = true;
    uint32_t    l_present_voltage_mv;
    uint32_t    l_target_mv;
    uint32_t    l_count;
    int32_t     l_delta_mv = 0;

    if (i_evid_value != VCS_SETUP)
    {
        avsInitExtVoltageControl(i_target, i_bus_num, BRIDGE_NUMBER);
    }

    avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER);
    l_count = 0;

    do
    {

        avsVoltageRead(i_target, i_bus_num, BRIDGE_NUMBER, i_rail_select, l_present_voltage_mv);
        l_throwAssert =  (l_count >= AVSBUS_RETRY_COUNT);
        avsValidateResponse(i_target,  i_bus_num, BRIDGE_NUMBER, l_throwAssert, l_goodResponse);

        if (!l_goodResponse)
        {
            avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER);
        }

        l_count++;
    }
    while (!l_goodResponse);
    l_delta_mv = (int32_t)l_present_voltage_mv - (int32_t)i_voltage_mv;

    while (l_delta_mv)
    {
        uint32_t l_abs_delta_mv = l_delta_mv < 0 ? -l_delta_mv : l_delta_mv;

        if (i_ext_vrm_step_size_mv > 0 && l_abs_delta_mv > i_ext_vrm_step_size_mv )
        {
            if (l_delta_mv > 0)
            {
                l_target_mv = l_present_voltage_mv - i_ext_vrm_step_size_mv;
            }
            else
            {
                l_target_mv = l_present_voltage_mv + i_ext_vrm_step_size_mv;
            }
        }
        else
        {
            l_target_mv = i_voltage_mv;
        }

        l_count = 0;

        do
        {
            avsVoltageWrite(
                i_target,
                i_bus_num,
                BRIDGE_NUMBER,
                i_rail_select,
                l_target_mv);

            // Throw an assertion if we don't get a good response.
            l_throwAssert =  l_count >= AVSBUS_RETRY_COUNT;
            avsValidateResponse(
                i_target,
                i_bus_num,
                BRIDGE_NUMBER,
                l_throwAssert,
                l_goodResponse);

            if (!l_goodResponse)
            {
                avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER);
            }

            l_count++;
        }
        while (!l_goodResponse);
        l_delta_mv = (int32_t)l_target_mv - (int32_t)i_voltage_mv;
    }

}

fapi2::ReturnCode p9_pm_occ_gpe_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode,
    const p9occgpe::GPE_ENGINES i_engine)
{
    if (i_mode == p9pm::PM_INIT)
    {
        // no-op
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        if (i_engine == p9occgpe::GPE0 || i_engine == p9occgpe::GPEALL)
        {
            pm_occ_gpe_reset(i_target, p9occgpe::GPE0);
        }
        if (i_engine == p9occgpe::GPE1 || i_engine == p9occgpe::GPEALL)
        {
            pm_occ_gpe_reset(i_target, p9occgpe::GPE1);
        }
    }

}

fapi2::ReturnCode
p9_pm_reset_psafe_update(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint32_t l_safe_mode_freq_dpll = 0;
    bool l_external_voltage_update = true;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_EQ>> l_eqChiplets;
    fapi2::Target<fapi2::TARGET_TYPE_EQ> l_firstEqChiplet;
    fapi2::buffer<uint64_t> l_dpll_data64;
    fapi2::buffer<uint64_t> l_vdm_data64;
    fapi2::buffer<uint64_t> l_dpll_fmult;
    uint32_t l_dpll_mhz;
    fapi2::buffer<uint64_t> l_occflg_data(0);
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    uint8_t l_chipNum = 0xFF;
    fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ_Type l_attr_safe_mode_freq_mhz;
    fapi2::ATTR_SAFE_MODE_VOLTAGE_MV_Type l_attr_safe_mode_mv;
    fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ_Type l_attr_reset_safe_mode_freq_mhz = 0;
    fapi2::ATTR_SAFE_MODE_VOLTAGE_MV_Type l_attr_reset_safe_mode_mv = 0;
    fapi2::ATTR_VDD_AVSBUS_BUSNUM_Type l_vdd_bus_num;
    fapi2::ATTR_VDD_AVSBUS_RAIL_Type   l_vdd_bus_rail;
    fapi2::ATTR_VDD_BOOT_VOLTAGE_Type       l_vdd_voltage_mv;
    fapi2::ATTR_FREQ_DPLL_REFCLOCK_KHZ_Type l_freq_proc_refclock_khz;
    fapi2::ATTR_PROC_DPLL_DIVIDER_Type      l_proc_dpll_divider;
    fapi2::ATTR_SAFE_MODE_NOVDM_UPLIFT_MV_Type l_uplift_mv;
    fapi2::ATTR_EXTERNAL_VRM_STEPSIZE_Type l_ext_vrm_step_size_mv;

    FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ, i_target, l_attr_safe_mode_freq_mhz);
    FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_VOLTAGE_MV, i_target, l_attr_safe_mode_mv);
    FAPI_ATTR_GET(fapi2::ATTR_VDD_AVSBUS_BUSNUM, i_target, l_vdd_bus_num);
    FAPI_ATTR_GET(fapi2::ATTR_VDD_AVSBUS_RAIL, i_target, l_vdd_bus_rail);
    FAPI_ATTR_GET(fapi2::ATTR_VDD_BOOT_VOLTAGE, i_target, l_vdd_voltage_mv);
    FAPI_ATTR_GET(fapi2::ATTR_PROC_DPLL_DIVIDER, i_target, l_proc_dpll_divider);
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_DPLL_REFCLOCK_KHZ, FAPI_SYSTEM, l_freq_proc_refclock_khz);
    FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_NOVDM_UPLIFT_MV, i_target, l_uplift_mv);
    FAPI_ATTR_GET(fapi2::ATTR_EXTERNAL_VRM_STEPSIZE, FAPI_SYSTEM, l_ext_vrm_step_size_mv);
    l_attr_safe_mode_mv += l_uplift_mv;
    //Reset safe mode attributes
    FAPI_ATTR_SET(fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ, i_target, l_attr_reset_safe_mode_freq_mhz);
    FAPI_ATTR_SET(fapi2::ATTR_SAFE_MODE_VOLTAGE_MV, i_target, l_attr_reset_safe_mode_mv);

    fapi2::getScom(i_target, PU_OCB_OCI_OCCFLG_SCOM2, l_occflg_data);

    if (l_occflg_data.getBit<p9hcd::PGPE_SAFE_MODE_ACTIVE>() || !l_attr_safe_mode_freq_mhz || !l_attr_safe_mode_mv)
    {
        return;
    }

    l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);

    for ( auto l_itr = l_eqChiplets.begin(); l_itr != l_eqChiplets.end(); ++l_itr)
    {
        l_dpll_fmult.flush<0>();
        fapi2::getScom(*l_itr, EQ_QPPM_DPLL_FREQ , l_dpll_data64);

        l_dpll_data64.extractToRight<EQ_QPPM_DPLL_FREQ_FMULT, EQ_QPPM_DPLL_FREQ_FMULT_LEN>(l_dpll_fmult);

        // Convert frequency value to a format that needs to be written to the
        // register
        l_safe_mode_freq_dpll = ((l_attr_safe_mode_freq_mhz * 1000) * l_proc_dpll_divider) / l_freq_proc_refclock_khz;

        // Convert back to the complete frequency value
        l_dpll_mhz = ((l_dpll_fmult * l_freq_proc_refclock_khz ) / l_proc_dpll_divider ) / 1000;


        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, *l_itr, l_chipNum);
        if (l_dpll_mhz < l_attr_safe_mode_freq_mhz)
        {
            //Here we need to update VDD only once.. because we are in EQ
            //target loop.VDD is updated for the whole proc once.
            if (l_external_voltage_update)
            {
                p9_setup_evid_voltageWrite(
                    i_target,
                    l_vdd_bus_num,
                    l_vdd_bus_rail,
                    l_attr_safe_mode_mv,
                    l_ext_vrm_step_size_mv,
                    VDD_SETUP);
                l_external_voltage_update = false;
            }
        }

        l_dpll_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMAX,EQ_QPPM_DPLL_FREQ_FMAX_LEN>(l_safe_mode_freq_dpll);
        l_dpll_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMIN,EQ_QPPM_DPLL_FREQ_FMIN_LEN>(l_safe_mode_freq_dpll);
        l_dpll_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMULT,EQ_QPPM_DPLL_FREQ_FMULT_LEN>(l_safe_mode_freq_dpll);

        fapi2::putScom(*l_itr, EQ_QPPM_DPLL_FREQ, l_dpll_data64);
    }

    //Update Avs Bus voltage
    //Here this condition will be true, when DPLL is greater than safe mode
    //freq.
    if (l_external_voltage_update)
    {
        p9_setup_evid_voltageWrite(
            i_target,
            l_vdd_bus_num,
            l_vdd_bus_rail,
            l_attr_safe_mode_mv,
            l_ext_vrm_step_size_mv,
            VDD_SETUP);
    }

}

fapi2::ReturnCode p9_pm_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    void* i_pHomerImage = NULL)
{
    using namespace p9_stop_recov_ffdc;

    fapi2::ReturnCode l_rc;
    fapi2::buffer<uint64_t> l_data64;
    fapi2::ATTR_INITIATED_PM_RESET_Type l_pmResetActive = fapi2::ENUM_ATTR_INITIATED_PM_RESET_ACTIVE;
    fapi2::ATTR_PM_MALF_ALERT_ENABLE_Type l_malfEnabled = fapi2::ENUM_ATTR_PM_MALF_ALERT_ENABLE_FALSE;
    fapi2::ATTR_SKIP_WAKEUP_Type l_skip_wakeup;

    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    bool l_malfAlert = false;
    fapi2::buffer<uint64_t> l_cpmmmrVal;
    fapi2::buffer<uint64_t> l_qmeScrVal;
    auto ex_list = i_target.getChildren<fapi2::TARGET_TYPE_EX>();

    fapi2::ATTR_PM_MALF_CYCLE_Type l_pmMalfCycle = fapi2::ENUM_ATTR_PM_MALF_CYCLE_INACTIVE;
    FAPI_ATTR_GET(fapi2::ATTR_PM_MALF_CYCLE, i_target, l_pmMalfCycle);

    // Avoid another PM Reset before we get through the PM Init
    // Protect FIR Masks, Special Wakeup States, PM FFDC, etc. from being
    // trampled.
    if (l_pmMalfCycle == fapi2::ENUM_ATTR_PM_MALF_CYCLE_ACTIVE)
    {
        return fapi2::FAPI2_RC_SUCCESS;
    }

    FAPI_ATTR_GET(fapi2::ATTR_SKIP_WAKEUP, FAPI_SYSTEM, l_skip_wakeup);
    FAPI_ATTR_GET(fapi2::ATTR_PM_MALF_ALERT_ENABLE, FAPI_SYSTEM, l_malfEnabled);
    FAPI_ATTR_SET(fapi2::ATTR_INITIATED_PM_RESET, i_target, l_pmResetActive);

    if (l_malfEnabled == fapi2::ENUM_ATTR_PM_MALF_ALERT_ENABLE_TRUE)
    {
        fapi2::getScom(i_target, P9N2_PU_OCB_OCI_OCCFLG2_SCOM, l_data64);

        if (l_data64.getBit<p9hcd::PM_CALLOUT_ACTIVE>())
        {
            l_malfAlert = true;
        }
        l_data64.flush<0>().setBit<p9hcd::STOP_RECOVERY_TRIGGER_ENABLE>();
        fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);
    }
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_INIT);
    p9_pm_occ_firinit(i_target, p9pm::PM_RESET);
    p9_pm_occ_control(
        i_target,
        p9occ_ctrl::PPC405_RESET_SEQUENCE, //Operation on PPC405
        p9occ_ctrl::PPC405_BOOT_NULL, // Boot instruction location
        0); //Jump to 405 main instruction - not used here
    if (!l_skip_wakeup)
    {
        if (l_malfAlert == false)
        {
            p9_pm_reset_clear_errinj(i_target);
            l_phase = PM_RESET_SPL_WKUP_EX_ALL;
            special_wakeup_all(i_target, true);

            std::vector<fapi2::Target<fapi2::TARGET_TYPE_EQ>> l_eqChiplets =
                        i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);

            for ( auto l_itr = l_eqChiplets.begin(); l_itr != l_eqChiplets.end(); ++l_itr)
            {
                fapi2::putScom(*l_itr, EQ_PPM_GPMMR_SCOM2, l_data64.flush<0>().setBit<0>());
            }
        }
        else
        {
            FAPI_ATTR_SET(fapi2::ATTR_PM_MALF_CYCLE, i_target, fapi2::ENUM_ATTR_PM_MALF_CYCLE_ACTIVE);
        }
        p9_pm_set_auto_spwkup(i_target);
    }
    p9_pm_firinit(i_target, p9pm::PM_RESET);
    p9_pm_occ_gpe_init(
        i_target,
        p9pm::PM_RESET,
        p9occgpe::GPEALL);
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_OCC);

    p9_pm_pstate_gpe_init(i_target, p9pm::PM_RESET);

    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_OCC_LFIR> l_occFir(i_target);
        l_occFir.get(p9pmFIR::REG_ALL);
        l_occFir.mask(4);
        l_occFir.put();
    }
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_PGPE);
    p9_pm_stop_gpe_init(i_target, p9pm::PM_RESET);
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_SGPE);
    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG_SCOM, l_data64);
    fapi2::putScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_data64);
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_CPPM);
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_QPPM);

    p9_pm_corequad_init(
        i_target,
        p9pm::PM_RESET,
        CME_FIRMASK, // CME FIR MASK
        CORE_ERRMASK,// Core Error Mask
        QUAD_ERRMASK); // Quad Error Mask
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_CME);
    p9_pm_reset_psafe_update(i_target);
    p9_pm_ocb_init(
        i_target,
        p9pm::PM_RESET,
        p9ocb::OCB_CHAN0, // Channel
        p9ocb::OCB_TYPE_NULL, // Channel type
        0, // Base address
        0, // Length of circular push/pull queue
        p9ocb::OCB_Q_OUFLOW_NULL, // Channel flow control
        p9ocb::OCB_Q_ITPTYPE_NULL); // Channel interrupt control
    p9_pm_pss_init(i_target, p9pm::PM_RESET);

    if (l_malfAlert == true)
    {
        const uint32_t l_OCC_LFIR_BIT_STOP_RCV_NOTIFY_PRD = 3;
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_OCC_LFIR> l_occFir(i_target);
        l_occFir.get(p9pmFIR::REG_ALL);
        l_occFir.setRecvAttn(l_OCC_LFIR_BIT_STOP_RCV_NOTIFY_PRD);
        l_occFir.put();
        l_data64.flush<0>();
        l_data64.setBit(l_OCC_LFIR_BIT_STOP_RCV_NOTIFY_PRD);
        fapi2::putScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR_OR, l_data64);
    }
    p9_pm_collect_ffdc(i_target, i_pHomerImage, PLAT_MISC);
    FAPI_ATTR_SET (fapi2::ATTR_INITIATED_PM_RESET, i_target, fapi2::ENUM_ATTR_INITIATED_PM_RESET_INACTIVE);
    return l_current_err;
}

errlHndl_t getPnorInfo( HOMER_Data_t & o_data )
{
    PNOR::SectionInfo_t sectionInfo;
    PNOR::getSectionInfo( PNOR::FIRDATA, sectionInfo );
    PNOR::PnorInfo_t pnorInfo;
    PNOR::getPnorInfo( pnorInfo );

    // Saving the flash workarounds in an attribute for when we
    // call getPnorInfo() in the runtime code.
    // Using sys target
    Target* sys = NULL;
    targetService().getTopLevelTarget( sys );
    assert(sys != NULL);

    sys->setAttr<ATTR_PNOR_FLASH_WORKAROUNDS>(pnorInfo.norWorkarounds);

    o_data.pnorInfo.pnorOffset      = sectionInfo.flashAddr;
    o_data.pnorInfo.pnorSize        = sectionInfo.size;
    o_data.pnorInfo.mmioOffset      = pnorInfo.mmioOffset;
    o_data.pnorInfo.norWorkarounds  = pnorInfo.norWorkarounds;
}

void PNOR::getPnorInfo( PnorInfo_t& o_pnorInfo )
{
    o_pnorInfo.mmioOffset = LPC_SFC_MMIO_OFFSET | LPC_FW_SPACE;

    TARGETING::Target* sys = nullptr;
    TARGETING::targetService().getTopLevelTarget(sys);
    o_pnorInfo.norWorkarounds = sys->getAttr<TARGETING::ATTR_PNOR_FLASH_WORKAROUNDS>();
    o_pnorInfo.flashSize = 64*MEGABYTE;
}

errlHndl_t writeData( uint8_t * i_hBuf, size_t i_hBufSize,
                      const HwInitialized_t i_curHw,
                      std::vector<HOMER_ChipInfo_t> &i_chipVector,
                      HOMER_Data_t  &io_homerData )
{
    TrgtMap_t  l_targMap;
    const size_t u32 = sizeof(uint32_t);
    const size_t u64 = sizeof(uint64_t);
    size_t sz_hBuf = 0;
    size_t sz_data = sizeof(io_homerData);
    sz_hBuf += sz_data;
    sz_hBuf += (u32 - (sizeof(HOMER_Data_t) % u32)) % u32;
    getAddresses(l_targMap);
    for(auto & t : l_targMap)
    {
        for(auto & r : t.second)
        {
            if((ALL_PROC_MEM_MASTER_CORE == i_curHw)
            || (ALL_HARDWARE == i_curHw)
            || ((TRGT_MCBIST  != t.first)
                && (TRGT_MCS  != t.first)
                && (TRGT_MCA  != t.first)
                && (TRGT_MC   != t.first)
                && (TRGT_MI   != t.first)
                && (TRGT_MCC  != t.first)
                && (TRGT_OMIC != t.first)))
            {
                io_homerData.regCounts[t.first][r.first] = r.second.size();
            }

        }
    }
    io_homerData.ecDepCounts = sizeof(s_ecDepProcRegisters) / sizeof(HOMER_ChipSpecAddr_t);
    uint32_t idx = 0;
    io_homerData.header = HOMER_FIR2;
    io_homerData.chipCount = i_chipVector.size();
    memcpy( &i_hBuf[idx], &io_homerData,  sz_data   ); idx += sz_data;
    idx += padding;
    if (0 != io_homerData.chipCount)
    {
        std::vector<HOMER_ChipInfo_t>::iterator  l_chipItr;
        uint32_t  l_chipTypeSize = sizeof(HOMER_Chip_t);

        for (l_chipItr = i_chipVector.begin(); l_chipItr < i_chipVector.end(); l_chipItr++ )
        {
            sz_hBuf += l_chipTypeSize;
            errl = homerVerifySizeFits(i_hBufSize, sz_hBuf);
            if (NULL != errl) { break; }

            memcpy( &i_hBuf[idx], &(l_chipItr->hChipType), l_chipTypeSize );
            idx += l_chipTypeSize;

            sz_hBuf += sizeof(HOMER_ChipNimbus_t);
            errl = homerVerifySizeFits(i_hBufSize, sz_hBuf);
            if (NULL != errl) { break; }

            memcpy( &i_hBuf[idx], &(l_chipItr->hChipN), sizeof(HOMER_ChipNimbus_t) );
            idx += sizeof(HOMER_ChipNimbus_t);

        }

        uint32_t  l_reg32Count = 0;
        uint32_t  l_reg64Count = 0;

        for(uint32_t l_regIdx = REG_FIRST; l_regIdx < REG_IDFIR; l_regIdx++)
        {
            for(uint32_t l_tgtIndex = TRGT_FIRST; l_tgtIndex < TRGT_MAX; l_tgtIndex++)
            {
                l_reg32Count += io_homerData.regCounts[l_tgtIndex][l_regIdx];
            }
        }
        for(uint32_t  l_regIdx = REG_IDFIR; l_regIdx < REG_MAX; l_regIdx++)
        {
            for(uint32_t l_tgtIndex = TRGT_FIRST; l_tgtIndex < TRGT_MAX; l_tgtIndex++ )
            {
                l_reg64Count +=io_homerData.regCounts[l_tgtIndex][l_regIdx];
            }
        }

        sz_hBuf +=
            l_reg32Count * sizeof(uint32_t)
            + l_reg64Count * sizeof(uint64_t)
            + io_homerData.ecDepCounts * sizeof(HOMER_ChipSpecAddr_t);

        errl = homerVerifySizeFits(i_hBufSize, sz_hBuf);

        for ( auto & t : l_targMap )
        {
            if((ALL_PROC_MEM_MASTER_CORE == i_curHw)
            || (ALL_HARDWARE == i_curHw)
            || ((TRGT_MCBIST != t.first)
                && (TRGT_MCS    != t.first)
                && (TRGT_MCA    != t.first)
                && (TRGT_MC     != t.first)
                && (TRGT_MI     != t.first)
                && (TRGT_MCC    != t.first)
                && (TRGT_OMIC   != t.first)))
            {
                for ( auto & r : t.second )
                {
                    for ( auto &  rAddr : r.second )
                    {
                        if(REG_IDFIR == r.first || REG_IDREG == r.first)
                        {
                            memcpy( &i_hBuf[idx], &rAddr, u64 );
                            idx += u64;
                        }
                        else
                        {
                            uint32_t  tempAddr = (uint32_t)rAddr;
                            memcpy( &i_hBuf[idx], &tempAddr, u32);
                            idx += u32;
                        }
                    }
                }

            }
        }
        uint8_t  *l_ecDepSourceRegs = (uint8_t *)(s_ecDepProcRegisters);
        memcpy( &i_hBuf[idx], l_ecDepSourceRegs, sizeof(s_ecDepProcRegisters) );
    }
}

errlHndl_t writeHomerFirData( uint8_t * i_hBuf, size_t i_hBufSize)
{
    HOMER_Data_t  l_homerData = HOMER_getData(); // Initializes data
    l_homerData.iplState = FIRDATA_STATE_IPL;

    getPnorInfo(l_homerData);
    std::vector<HOMER_ChipInfo_t> l_chipInfVector;
    getHwConfig(l_chipInfVector, PRDF::MASTER_PROC_CORE);
    writeData(i_hBuf, i_hBufSize, PRDF::MASTER_PROC_CORE, l_chipInfVector, l_homerData);
}

errlHndl_t loadHostDataToSRAM(TARGETING::Target* i_proc)
{
    HBPM::occHostConfigDataArea_t * config_data = new HBPM::occHostConfigDataArea_t();

    config_data->version = HBOCC::OccHostDataVersion;
    config_data->nestFrequency = ATTR_FREQ_PB_MHZ;

    if(INITSERVICE::spBaseServicesEnabled())
    {
        config_data->interruptType = USE_FSI2HOST_MAILBOX;
    }
    else
    {
        config_data->interruptType = USE_PSIHB_COMPLEX;
    }

    config_data->firMaster = IS_FIR_MASTER;
    PRDF::writeHomerFirData(
        config_data->firdataConfig,
        sizeof(config_data->firdataConfig));

    if (SECUREBOOT::SMF::isSmfEnabled())
    {
        config_data->smfMode = SMF_MODE_ENABLED;
    }
    else
    {
        config_data->smfMode = SMF_MODE_DISABLED;
    }

    HBOCC::writeSRAM(i_proc, OCC_SRAM_FIR_DATA, (uint64_t*)config_data->firdataConfig, sizeof(config_data->firdataConfig)); // analyzed
    delete(config_data);
}

static void readSemiPersistData(semiPersistData_t & o_data)
{
    memset(&o_data, 0x0, sizeof(semiPersistData_t));
    PNOR::SectionInfo_t l_pnorHbVolatile;
    Singleton<PnorRP>::instance().getSectionInfo(l_pnorHbVolatile);
    semiPersistData_t *l_data = (l_pnorHbVolatile.size == 0) ? 0 : l_pnorHbVolatile.vaddr;
    if(l_data)
    {
        o_data = *l_data;
    }
}

errlHndl_t PnorRP::getSectionInfo(PNOR::SectionInfo_t& o_info)
{
#ifdef CONFIG_SECUREBOOT
    if (iv_TOC[PNOR::HB_VOLATILE].secure)
    {
        o_info.vaddr = iv_TOC[PNOR::HB_VOLATILE].virtAddr + VMM_VADDR_SPNOR_DELTA + VMM_VADDR_SPNOR_DELTA + PAGESIZE;
        SECUREBOOT::ContainerHeader l_conHdr;
        l_conHdr.setHeader(l_vaddr);
        payloadTextSize = l_conHdr.payloadTextSize();
        if (l_conHdr.sb_flags()->sw_hash)
        {
            o_info.vaddr += payloadTextSize;
        }
    }
    else
#endif
    {
        o_info.vaddr = iv_TOC[PNOR::HB_VOLATILE].virtAddr;
    }
}

//////////////////////////
// Fully analyzed below //
//////////////////////////

static void loadOCCImageDuringIpl(TARGETING::Target* i_target, void* i_occVirtAddr)
{
    UtilLidMgr lidMgr(HBOCC::OCC_LIDID);
    uint8_t* l_occImage = lidMgr.getLidVirtAddr(); // just get pointer to the OCC section
    TARGETING::Target * l_sysTarget = NULL;
    TARGETING::targetService().getTopLevelTarget(l_sysTarget);

    uint32_t* l_ptrToLength = (uint32_t*)((char*)l_occImage + OCC_OFFSET_LENGTH);
    memcpy(i_occVirtAddr, l_occImage, *l_ptrToLength);
    char* l_occMainAppPtr = l_occImage + *l_ptrToLength;
    l_ptrToLength = (uint32_t*)(l_occMainAppPtr + OCC_OFFSET_LENGTH);
    HBOCC::writeSRAM(i_target, HBOCC::OCC_405_SRAM_ADDRESS, (uint64_t*)l_occMainAppPtr, *l_ptrToLength); // analyzed

    void* l_modifiedSectionPtr = malloc(OCC_OFFSET_FREQ + sizeof(ATTR_FREQ_PB_MHZ_type));
    memcpy(l_modifiedSectionPtr, l_occMainAppPtr, OCC_OFFSET_FREQ + sizeof(ATTR_FREQ_PB_MHZ_type));
    HBOCC::writeSRAM(i_target, HBOCC::OCC_405_SRAM_ADDRESS, (uint64_t*)l_modifiedSectionPtr, (uint32_t)OCC_OFFSET_FREQ + sizeof(ATTR_FREQ_PB_MHZ_type)); // analyzed

    char* l_gpe0AppPtr = l_occMainAppPtr + *l_ptrToLength;
    uint32_t* l_ptrToGpe0Length = (uint32_t*)(l_occMainAppPtr + OCC_OFFSET_GPE0_LENGTH);
    HBOCC::writeSRAM(i_target, HBOCC::OCC_GPE0_SRAM_ADDRESS, (uint64_t*)l_gpe0AppPtr, *l_ptrToGpe0Length); // analyzed

    char* l_gpe1AppPtr = l_gpe0AppPtr + *l_ptrToGpe0Length;
    uint32_t* l_ptrToGpe1Length = (uint32_t*)(l_occMainAppPtr + OCC_OFFSET_GPE1_LENGTH);
    HBOCC::writeSRAM(i_target, HBOCC::OCC_GPE1_SRAM_ADDRESS, (uint64_t*)l_gpe1AppPtr, *l_ptrToGpe1Length); // analyzed

    free(l_modifiedSectionPtr);
}

static void startOCCFromSRAM(TARGETING::Target* i_proc)
{
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>master_proc_target(i_proc);

    pm_pss_init(master_proc_target);
    pm_occ_fir_init(i_target);
    clear_occ_special_wakeups(master_proc_target);
    deviceWrite(i_proc, &0x218780f800000000, 8, DEVICE_SCOM_ADDRESS(0x6C040));
    deviceWrite(i_proc, &0x0003d03c00000000, 8, DEVICE_SCOM_ADDRESS(0x6C050));
    deviceWrite(i_proc, &0x2181801800000000, 8, DEVICE_SCOM_ADDRESS(0x6C044));
    deviceWrite(i_proc, &0x0003d00c00000000, 8, DEVICE_SCOM_ADDRESS(0x6C054));
    deviceWrite(i_proc, &0x010280ac00000000, 8, DEVICE_SCOM_ADDRESS(0x6C048));
    deviceWrite(i_proc, &0x0001901400000000, 8, DEVICE_SCOM_ADDRESS(0x6C058));

    uint64_t l_start405MainInstr = 0;
    makeStart405Instruction(i_proc, &l_start405MainInstr);
    p9_pm_occ_control(
        master_proc_target,
        p9occ_ctrl::PPC405_START,
        p9occ_ctrl::PPC405_BOOT_WITHOUT_BL,
        l_start405MainInstr);

    deviceWrite(i_proc, &0xffffffffffffffff, 8, DEVICE_SCOM_ADDRESS(OCB_OITR0));
    deviceWrite(i_proc, &0xffffffffffffffff, 8, DEVICE_SCOM_ADDRESS(OCB_OIEPR0));
}

static void pm_pss_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;

    //  ******************************************************************
    //     - Poll status register for ongoing or no errors to give the
    //       chance for on-going operations to complete
    //  ******************************************************************

    for (uint32_t l_pollcount = 0; l_pollcount < 1000; l_pollcount++)
    {
        FAPI_TRY(fapi2::getScom(i_target, PU_SPIPSS_ADC_STATUS_REG, l_data64));
        if (l_data64.getBit<PU_SPIPSS_ADC_STATUS_REG_HWCTRL_ONGOING>() == 0)
        {
            break;
        }
        fapi2::delay(10000us);
    }
    for (uint32_t l_pollcount = 0; l_pollcount < 1000; l_pollcount++)
    {
        fapi2::getScom(i_target, PU_SPIPSS_P2S_STATUS_REG, l_data64);
        //P2S On-going complete
        if (l_data64.getBit<0>() == 0)
        {
            break;
        }
        fapi2::delay(10000us);
    }

    //  ******************************************************************
    //     - Resetting both ADC and P2S bridge
    //  ******************************************************************

    l_data64.flush<0>();
    l_data64.setBit<PU_SPIPSS_ADC_RESET_REGISTER_HWCTRL+1>();

    fapi2::putScom(i_target, PU_SPIPSS_ADC_RESET_REGISTER, l_data64);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_RESET_REGISTER, l_data64);

    // Clearing reset for cleanliness
    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_SPIPSS_ADC_RESET_REGISTER, l_data64);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_RESET_REGISTER, l_data64);
}

static void pm_pss_init(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint64_t l_data64;

    fapi2::getScom(i_target, PU_SPIMPSS_ADC_CTRL_REG0, l_data64);
    l_data64.insertFromRight<0, 6>(0x20);
    l_data64.insertFromRight<12, 6>(0);
    fapi2::putScom(i_target, PU_SPIMPSS_ADC_CTRL_REG0, l_data64);

    fapi2::getScom(i_target, PU_SPIPSS_ADC_CTRL_REG1, l_data64);
    l_data64.insertFromRight<0, 1>(1);
    l_data64.insertFromRight<1, 1>(0);
    l_data64.insertFromRight<2, 1>(0);
    l_data64.insertFromRight<3, 1>(0);
    l_data64.insertFromRight<4, 10>(0xA);
    l_data64.insertFromRight<14, 4>(0x10);
    fapi2::putScom(i_target, PU_SPIPSS_ADC_CTRL_REG1, l_data64);

    fapi2::getScom(i_target, PU_SPIPSS_ADC_CTRL_REG2, l_data64);
    l_data64.insertFromRight<0, 17>(0);
    fapi2::putScom(i_target, PU_SPIPSS_ADC_CTRL_REG2, l_data64);

    fapi2::putScom(i_target, PU_SPIPSS_ADC_WDATA_REG, 0);

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG0, l_data64);
    l_data64.insertFromRight<0, 6>(0x20);
    l_data64.insertFromRight<12, 6>(0);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG0, l_data64);

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG1, l_data64);
    l_data64.insertFromRight<0, 1>(1);
    l_data64.insertFromRight<1, 1>(0);
    l_data64.insertFromRight<2, 1>(0);
    l_data64.insertFromRight<3, 1>(0);
    l_data64.insertFromRight<4, 10>(0xA);
    l_data64.insertFromRight<17, 1>(1);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG1, l_data64);

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG2, l_data64);
    l_data64.insertFromRight<0, 17>(0);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG2, l_data64);

    fapi2::putScom(i_target, PU_SPIPSS_P2S_WDATA_REG, 0);

    fapi2::getScom(i_target, PU_SPIPSS_100NS_REG, l_data64);
    l_data64.insertFromRight<0, 32>(FREQ_PB_MHZ / 40);
    fapi2::putScom(i_target, PU_SPIPSS_100NS_REG, l_data64);
}

static void p9_pm_occ_control(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9occ_ctrl::PPC_CONTROL i_ppc405_reset_ctrl = p9occ_ctrl::PPC405_START,
    const p9occ_ctrl::PPC_BOOT_CONTROL i_ppc405_boot_ctrl = p9occ_ctrl::PPC405_BOOT_WITHOUT_BL,
    const uint64_t i_ppc405_jump_to_main_instr)
{
    fapi2::buffer<uint64_t> l_data64;
    l_data64.flush<0>().insertFromRight(i_ppc405_jump_to_main_instr, 0, 64);
    fapi2::putScom(i_target, PU_SRAM_SRBV3_SCOM, l_data64);
    fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_AND, ~BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
    fapi2::putScom(i_target, PU_OCB_PIB_OCR_OR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
    fapi2::putScom(i_target, PU_OCB_PIB_OCR_CLEAR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
}

static void makeStart405Instruction(
    const TARGETING::Target* i_target,
    uint64_t* o_instr)
{
    uint64_t l_epAddr;

    // OCC_405_SRAM_ADDRESS  = 0xFFF40000
    // OCC_OFFSET_MAIN_EP    = 0x6C
    HBOCC::readSRAM(
        i_target,
        OCC_405_SRAM_ADDRESS + OCC_OFFSET_MAIN_EP,
        &l_epAddr,
        8);

    // The branch instruction is of the form 0x4BXXXXX200000000, where X
    // is the address of the 405 main's entry point (alligned as shown).
    // Example: If 405 main's EP is FFF5B570, then the branch instruction
    // will be 0x4bf5b57200000000. The last two bits of the first byte of
    // the branch instruction must be '2' according to the OCC instruction
    // set manual.

    // OCC_BRANCH_INSTR = 0x4B00000200000000
    // BRANCH_ADDR_MASK = 0x00FFFFFC
    *o_instr = OCC_BRANCH_INSTR | (((uint64_t)(BRANCH_ADDR_MASK & l_epAddr)) << 32);
}

fapi2::ReturnCode p9_pm_ocb_indir_setup_linear(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint32_t      i_ocb_bar)
{
    p9_pm_ocb_init(
        i_target,
        i_ocb_bar);
}

static void accessOCBIndirectChannel(
    accessOCBIndirectCmd i_cmd,
    const TARGETING::Target * i_pTarget,
    const uint32_t i_addr,
    uint64_t * io_dataBuf,
    size_t i_dataLen)
{
    TARGETING::Target* l_pChipTarget = nullptr;
    getChipTarget(i_pTarget,l_pChipTarget);
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarget(l_pChipTarget);

    p9_pm_ocb_indir_setup_linear(
        l_fapiTarget,
        i_addr);

    p9_pm_ocb_indir_access(
        l_fapiTarget,
        p9ocb::OCB_CHAN0,
        i_cmd == ACCESS_OCB_READ_LINEAR ? p9ocb::OCB_GET : p9ocb::OCB_PUT,
        i_dataLen / 8,
        i_addr,
        io_dataBuf);
}

static void p9_pm_ocb_indir_access(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM  i_ocb_chan,
    const p9ocb::PM_OCB_ACCESS_OP i_ocb_op,
    const uint32_t                i_ocb_req_length,
    const uint32_t                i_oci_address,
    uint64_t*                     io_ocb_buffer)
{
    uint64_t l_OCBAR_address   = PU_OCB_PIB_OCBAR0;
    uint64_t l_OCBDR_address   = PU_OCB_PIB_OCBDR0;
    uint64_t l_OCBCSR_address  = PU_OCB_PIB_OCBCSR0_RO;
    uint64_t l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS0_SCOM;

    fapi2::buffer<uint64_t> l_data64;
    l_data64.insert<0, 32>(i_oci_address);
    fapi2::putScom(i_target, l_OCBAR_address, l_data64);

    if(i_ocb_op == p9ocb::OCB_PUT)
    {
        fapi2::buffer<uint64_t> l_data64;
        fapi2::getScom(i_target, l_OCBCSR_address, l_data64);

        if (l_data64.getBit<4>() && l_data64.getBit<5>())
        {
            fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);
            if (l_data64.getBit<31>())
            for(uint8_t l_counter = 0; l_counter < 4; l_counter++;)
            {
                if (!l_data64.getBit<0>())
                {
                    break;
                }
                // maybe some delay is needed here if coreboot is too fast?
                fapi2::delay(0);
                fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);
            }
        }
        for(uint32_t l_index = 0; l_index < i_ocb_req_length; l_index++)
        {
            l_data64.insertFromRight(io_ocb_buffer[l_index], 0, 64);
            fapi2::putScom(i_target, l_OCBDR_address, l_data64);
        }
    }
    else if(i_ocb_op == p9ocb::OCB_GET)
    {
        fapi2::buffer<uint64_t> l_data64;
        uint64_t l_data = 0;

        for(uint32_t l_loopCount = 0; l_loopCount < i_ocb_req_length; l_loopCount++)
        {
            l_data64.extract(l_data, 0, 64);
            io_ocb_buffer[l_loopCount] = l_data;
        }
    }
}

inline uint64_t getMSR()
{
    register uint64_t msr = 0;
    asm volatile("mfmsr %0" : "=r" (msr));
    return msr;
}

inline void setMSR(uint64_t _msr)
{
    register uint64_t msr = _msr;
    asm volatile("mtmsr %0; isync" :: "r" (msr));
}

void setCheckstopData(uint64_t i_xstopAddr, uint64_t i_xstopData)
{
    // only used in case a checkstop is forced
    g_xstopRegPtr = reinterpret_cast<uint64_t*>(i_xstopAddr | VmmManager::FORCE_PHYS_ADDR);
    g_xstopRegValue = i_xstopData;

    uint64_t l_msr = getMSR();
    l_msr |= 0x0000000000001000;
    setMSR(l_msr);
}

static void writeSRAM(
    const TARGETING::Target * i_pTarget,
    const uint32_t i_addr,
    uint64_t * i_dataBuf,
    size_t i_dataLen)
{
    accessOCBIndirectChannel(
        ACCESS_OCB_WRITE_LINEAR,
        i_pTarget,
        i_addr,
        i_dataBuf,
        i_dataLen);
}

static void readSRAM(
    const TARGETING::Target * i_pTarget,
    const uint32_t i_addr,
    uint64_t * io_dataBuf,
    size_t i_dataLen)
{
    accessOCBIndirectChannel(
        ACCESS_OCB_READ_LINEAR,
        i_pTarget,
        i_addr,
        io_dataBuf,
        i_dataLen);
}

uint32_t get_huid(const Target* i_target)
{
    uint32_t huid = 0;
    if(i_target == NULL)
    {
        huid = 0x0;
    }
    else if( i_target == MASTER_PROCESSOR_CHIP_TARGET_SENTINEL )
    {
        huid = 0xFFFFFFFF;
    }
    else
    {
        i_target->tryGetAttr<ATTR_HUID>(huid);
    }
    return huid;
}

static void p9_pm_pba_bar_config (
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint32_t i_index,
    const uint64_t i_pba_bar_addr)
{
    uint64_t l_bar64 = i_pba_bar_addr;
    l_bar64.insertFromRight<0, 3>(0);
    fapi2::putScom(i_target, PBA_BARs[i_index], l_bar64);
    fapi2::putScom(i_target, PBA_BARMSKs[i_index], 0x300000);
}
