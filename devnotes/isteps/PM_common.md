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
    loadOCCImageToHomer(
        i_target,
        l_occImgPaddr,
        l_occImgVaddr,
        i_mode);
#if defined(CONFIG_IPLTIME_CHECKSTOP_ANALYSIS) && !defined(__HOSTBOOT_RUNTIME)
    HBOCC::loadHostDataToSRAM(i_target);
#else
    loadHostDataToHomer(i_target, l_occImgVaddr + HOMER_OFFSET_TO_OCC_HOST_DATA);
    loadHcode(i_target, l_homerVAddr, HBPM::PM_LOAD);
#endif
}

 static int rt_lid_load(uint32_t lid, void** buffer, size_t* size)
{
    errlHndl_t l_errl = NULL;
    UtilLidMgr* lidmgr = new UtilLidMgr(lid);

    do
    {
        l_errl = lidmgr->getLidSize(*size);
        if (l_errl) break;

        *buffer = malloc(*size);
        l_errl = lidmgr->getLid(*buffer, *size);
        if (l_errl) break;

    } while(0);

    if (l_errl)
    {
        free(*buffer);
        *buffer = NULL;
        *size = 0;

        delete l_errl;
        delete lidmgr;
        return -1;
    }
    else
    {
        cv_loadedLids[*buffer] = lidmgr;
        return 0;
    }
}

errlHndl_t UtilLidMgr::loadLid()
{
    if (nullptr != iv_lidBuffer) return nullptr;


    const char* l_addr = nullptr;
    size_t l_size = 0;

    if(iv_isLidInHbResvMem)
    {
        const auto pnorSectionId = Util::getLidPnorSection(
            static_cast<Util::LidId>(iv_lidId));

        iv_lidBuffer = reinterpret_cast<void*>(g_hostInterfaces->
            get_reserved_mem(
                PNOR::SectionIdToString(pnorSectionId),
                0));

        // If nullptr returned, set size to 0 to indicate we could not find
        // the lid in HB resv memory
        if (iv_lidBuffer == nullptr)
        {
            iv_lidSize = 0;
        }
        else
        {
            // Build a container header object to parse protected size
            SECUREBOOT::ContainerHeader l_conHdr;
            l_conHdr.setHeader(iv_lidBuffer);
            if (l_conHdr.sb_flags()->sw_hash)
            {
                // Size of lid has to be size of unprotected data. So we
                // need to take out header and hash table sizes
                iv_lidSize = l_conHdr.totalContainerSize() - PAGESIZE -
                    l_conHdr.payloadTextSize();
                iv_lidBuffer = static_cast<uint8_t*>(iv_lidBuffer) +
                                PAGESIZE + l_conHdr.payloadTextSize();
            }
            else
            {
                iv_lidSize = l_conHdr.payloadTextSize();
                iv_lidBuffer = static_cast<uint8_t*>(iv_lidBuffer)+
                                PAGESIZE;
            }
        }
    }
    else if(iv_isLidInVFS)
    {
        VFS::module_address(iv_lidFileName, l_addr, l_size);
        iv_lidBuffer = const_cast<void*>(reinterpret_cast<const void*>(l_addr));
        iv_lidSize = l_size;
    }
    else if (iv_isLidInPnor)
    {
        iv_lidSize = iv_lidPnorInfo.size;
        iv_lidBuffer = reinterpret_cast<char *>(iv_lidPnorInfo.vaddr);
    }
    else if( g_hostInterfaces->lid_load
                && iv_spBaseServicesEnabled  )
    {
        // pointer to rt_lid_load
        g_hostInterfaces->lid_load(iv_lidId, &iv_lidBuffer, &iv_lidSize);
    }
}

errlHndl_t UtilLidMgr::getStoredLidImage(void*& o_pLidImage,
                                         size_t& o_lidImageSize)
{
    if((nullptr == iv_lidBuffer) || (0 == iv_lidSize))
    {
        loadLid();
    }

    if(l_err)
    {
        o_lidImageSize = 0;
        o_pLidImage = nullptr;
    }
    else
    {
        o_lidImageSize = iv_lidSize;
        o_pLidImage = iv_lidBuffer;
    }
}


errlHndl_t UtilLidMgr::releaseLidImage(void)
{
    bool l_inPnor = iv_isLidInPnor;
    bool l_inVFS = iv_isLidInVFS;
    bool l_inHbResvMem = iv_isLidInHbResvMem;

    cleanup();

    iv_isLidInPnor = l_inPnor;
    iv_isLidInVFS = l_inVFS;
    iv_isLidInHbResvMem = l_inHbResvMem;
}

errlHndl_t loadOCCImageToHomer(TARGETING::Target* i_target,
                                uint64_t i_occImgPaddr,
                                uint64_t i_occImgVaddr, // dest
                                loadPmMode i_mode)
{
    if(g_pOccLidMgr.get() == nullptr)
    {
        g_pOccLidMgr = std::shared_ptr<UtilLidMgr>
                        (new UtilLidMgr(Util::OCC_LIDID));
    }
    void* l_pLidImage = nullptr;
    size_t l_lidImageSize = 0;

    if(PM_RELOAD == i_mode)
    {
        g_pOccLidMgr->releaseLidImage();
    }

    g_pOccLidMgr->getStoredLidImage(l_pLidImage, l_lidImageSize);

    void* l_occVirt = reinterpret_cast<void *>(i_occImgVaddr);
    memcpy(l_occVirt, l_pLidImage, l_lidImageSize);
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

        fapi2::delay(1000ns);
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

static void pm_occ_fir_init(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    p9pmFIR::PMFir <p9pmFIR::FIRTYPE_OCC_LFIR> l_occFir(i_target);
    fapi2::getScom(iv_proc, iv_mask_address, iv_mask);

    putScom(iv_proc, iv_fir_address, 0);

    uint64_t iv_action0 = 0;
    iv_action0.clearBit(C405_ECC_CE);
    iv_action0.clearBit(C405_OCI_MC_CHK);
    iv_action0.clearBit(C405DCU_M_TIMEOUT);
    iv_action0.clearBit(GPE0_ERR);
    iv_action0.clearBit(GPE0_OCISLV_ERR);
    iv_action0.clearBit(GPE1_ERR);
    iv_action0.clearBit(GPE1_OCISLV_ERR);
    iv_action0.clearBit(GPE2_OCISLV_ERR);
    iv_action0.clearBit(GPE3_OCISLV_ERR);
    iv_action0.clearBit(JTAGACC_ERR);
    iv_action0.clearBit(OCB_DB_OCI_RDATA_PARITY);
    iv_action0.clearBit(OCB_DB_OCI_SLVERR);
    iv_action0.clearBit(OCB_DB_OCI_TIMEOUT);
    iv_action0.clearBit(OCB_DB_PIB_DATA_PARITY_ERR);
    iv_action0.clearBit(OCB_IDC0_ERR);
    iv_action0.clearBit(OCB_IDC1_ERR);
    iv_action0.clearBit(OCB_IDC2_ERR);
    iv_action0.clearBit(OCB_IDC3_ERR);
    iv_action0.clearBit(OCB_PIB_ADDR_PARITY_ERR);
    iv_action0.clearBit(OCC_CMPLX_FAULT);
    iv_action0.clearBit(OCC_CMPLX_NOTIFY);
    iv_action0.clearBit(SRAM_CE);
    iv_action0.clearBit(SRAM_DATAOUT_PERR);
    iv_action0.clearBit(SRAM_OCI_ADDR_PARITY_ERR);
    iv_action0.clearBit(SRAM_OCI_BE_PARITY_ERR);
    iv_action0.clearBit(SRAM_OCI_WDATA_PARITY);
    iv_action0.clearBit(SRAM_READ_ERR);
    iv_action0.clearBit(SRAM_SPARE_DIRERR0);
    iv_action0.clearBit(SRAM_SPARE_DIRERR1);
    iv_action0.clearBit(SRAM_SPARE_DIRERR2);
    iv_action0.clearBit(SRAM_SPARE_DIRERR3);
    iv_action0.clearBit(SRAM_UE);
    iv_action0.clearBit(SRAM_WRITE_ERR);
    iv_action0.clearBit(SRT_FSM_ERR);
    iv_action0.clearBit(STOP_RCV_NOTIFY_PRD);
    iv_action0.setBit(C405_ECC_UE);
    putScom(iv_proc, iv_action0_address, iv_action0);

    uint64_t iv_action1 = 0;
    iv_action1.clearBit(C405_ECC_UE);
    iv_action1.setBit(C405_ECC_CE);
    iv_action1.setBit(C405_OCI_MC_CHK);
    iv_action1.setBit(C405DCU_M_TIMEOUT);
    iv_action1.setBit(GPE0_ERR);
    iv_action1.setBit(GPE0_OCISLV_ERR);
    iv_action1.setBit(GPE1_ERR);
    iv_action1.setBit(GPE1_OCISLV_ERR);
    iv_action1.setBit(GPE2_OCISLV_ERR);
    iv_action1.setBit(GPE3_OCISLV_ERR);
    iv_action1.setBit(JTAGACC_ERR);
    iv_action1.setBit(OCB_DB_OCI_RDATA_PARITY);
    iv_action1.setBit(OCB_DB_OCI_SLVERR);
    iv_action1.setBit(OCB_DB_OCI_TIMEOUT);
    iv_action1.setBit(OCB_DB_PIB_DATA_PARITY_ERR);
    iv_action1.setBit(OCB_IDC0_ERR);
    iv_action1.setBit(OCB_IDC1_ERR);
    iv_action1.setBit(OCB_IDC2_ERR);
    iv_action1.setBit(OCB_IDC3_ERR);
    iv_action1.setBit(OCB_PIB_ADDR_PARITY_ERR);
    iv_action1.setBit(OCC_CMPLX_FAULT);
    iv_action1.setBit(OCC_CMPLX_NOTIFY);
    iv_action1.setBit(SRAM_CE);
    iv_action1.setBit(SRAM_DATAOUT_PERR);
    iv_action1.setBit(SRAM_OCI_ADDR_PARITY_ERR);
    iv_action1.setBit(SRAM_OCI_BE_PARITY_ERR);
    iv_action1.setBit(SRAM_OCI_WDATA_PARITY);
    iv_action1.setBit(SRAM_READ_ERR);
    iv_action1.setBit(SRAM_SPARE_DIRERR0);
    iv_action1.setBit(SRAM_SPARE_DIRERR1);
    iv_action1.setBit(SRAM_SPARE_DIRERR2);
    iv_action1.setBit(SRAM_SPARE_DIRERR3);
    iv_action1.setBit(SRAM_UE);
    iv_action1.setBit(SRAM_WRITE_ERR);
    iv_action1.setBit(SRT_FSM_ERR);
    iv_action1.setBit(STOP_RCV_NOTIFY_PRD);
    putScom(iv_proc, iv_action1_address, iv_action1);

    uint64_t iv_or_mask = iv_mask;
    iv_or_mask.setBit(C405ICU_M_TIMEOUT);
    iv_or_mask.setBit(CME_ERR_NOTIFY);
    iv_or_mask.setBit(EXT_TRAP);
    iv_or_mask.setBit(FIR_PARITY_ERR_DUP);
    iv_or_mask.setBit(FIR_PARITY_ERR);
    iv_or_mask.setBit(GPE0_HALTED);
    iv_or_mask.setBit(GPE0_WD_TIMEOUT);
    iv_or_mask.setBit(GPE1_HALTED);
    iv_or_mask.setBit(GPE1_WD_TIMEOUT);
    iv_or_mask.setBit(GPE2_ERR);
    iv_or_mask.setBit(GPE2_HALTED);
    iv_or_mask.setBit(GPE2_WD_TIMEOUT);
    iv_or_mask.setBit(GPE3_ERR);
    iv_or_mask.setBit(GPE3_HALTED);
    iv_or_mask.setBit(GPE3_WD_TIMEOUT);
    iv_or_mask.setBit(OCB_ERR);
    iv_or_mask.setBit(OCC_FW0);
    iv_or_mask.setBit(OCC_FW1);
    iv_or_mask.setBit(OCC_HB_NOTIFY);
    iv_or_mask.setBit(PPC405_CHIP_RESET);
    iv_or_mask.setBit(PPC405_CORE_RESET);
    iv_or_mask.setBit(PPC405_DBGSTOPACK);
    iv_or_mask.setBit(PPC405_SYS_RESET);
    iv_or_mask.setBit(PPC405_WAIT_STATE);
    iv_or_mask.setBit(SPARE_59);
    iv_or_mask.setBit(SPARE_60);
    iv_or_mask.setBit(SPARE_61);
    iv_or_mask.setBit(SPARE_ERR_38);
    putScom(iv_proc, iv_fir_address + MASK_WOR_INCR, iv_or_mask);

    uint64_t iv_and_mask = iv_mask;
    iv_and_mask.clearBit(C405_ECC_CE);
    iv_and_mask.clearBit(C405_ECC_UE);
    iv_and_mask.clearBit(C405_OCI_MC_CHK);
    iv_and_mask.clearBit(C405DCU_M_TIMEOUT);
    iv_and_mask.clearBit(GPE0_ERR);
    iv_and_mask.clearBit(GPE0_OCISLV_ERR);
    iv_and_mask.clearBit(GPE1_ERR);
    iv_and_mask.clearBit(GPE1_OCISLV_ERR);
    iv_and_mask.clearBit(GPE2_OCISLV_ERR);
    iv_and_mask.clearBit(GPE3_OCISLV_ERR);
    iv_and_mask.clearBit(JTAGACC_ERR);
    iv_and_mask.clearBit(OCB_DB_OCI_RDATA_PARITY);
    iv_and_mask.clearBit(OCB_DB_OCI_SLVERR);
    iv_and_mask.clearBit(OCB_DB_OCI_TIMEOUT);
    iv_and_mask.clearBit(OCB_DB_PIB_DATA_PARITY_ERR);
    iv_and_mask.clearBit(OCB_IDC0_ERR);
    iv_and_mask.clearBit(OCB_IDC1_ERR);
    iv_and_mask.clearBit(OCB_IDC2_ERR);
    iv_and_mask.clearBit(OCB_IDC3_ERR);
    iv_and_mask.clearBit(OCB_PIB_ADDR_PARITY_ERR);
    iv_and_mask.clearBit(OCC_CMPLX_FAULT);
    iv_and_mask.clearBit(OCC_CMPLX_NOTIFY);
    iv_and_mask.clearBit(SRAM_CE);
    iv_and_mask.clearBit(SRAM_DATAOUT_PERR);
    iv_and_mask.clearBit(SRAM_OCI_ADDR_PARITY_ERR);
    iv_and_mask.clearBit(SRAM_OCI_BE_PARITY_ERR);
    iv_and_mask.clearBit(SRAM_OCI_WDATA_PARITY);
    iv_and_mask.clearBit(SRAM_READ_ERR);
    iv_and_mask.clearBit(SRAM_SPARE_DIRERR0);
    iv_and_mask.clearBit(SRAM_SPARE_DIRERR1);
    iv_and_mask.clearBit(SRAM_SPARE_DIRERR2);
    iv_and_mask.clearBit(SRAM_SPARE_DIRERR3);
    iv_and_mask.clearBit(SRAM_UE);
    iv_and_mask.clearBit(SRAM_WRITE_ERR);
    iv_and_mask.clearBit(SRT_FSM_ERR);
    iv_and_mask.clearBit(STOP_RCV_NOTIFY_PRD);
    putScom(iv_proc, iv_fir_address + MASK_WAND_INCR, iv_and_mask);
}


static void pm_occ_fir_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint64_t iv_mask;
    fapi2::getScom(iv_proc, iv_mask_address, iv_mask);

    uint64_t iv_or_mask = iv_mask;
    iv_or_mask.flush<1>();
    putScom(iv_proc, iv_fir_address + MASK_WOR_INCR, iv_or_mask);

    uint64_t iv_and_mask = iv_mask;
    iv_and_mask.flush<1>();
    iv_and_mask.clearBit(OCC_HB_NOTIFY);
    putScom(iv_proc, iv_fir_address + MASK_WAND_INCR, iv_and_mask);

    uint64_t iv_action0;
    fapi2::getScom(iv_proc, iv_action0_address, iv_action0);
    iv_action0.setBit(OCC_HB_NOTIFY);
    putScom(iv_proc, iv_action0_address, iv_action0);

    uint64_t iv_action1;
    fapi2::getScom(iv_proc, iv_action1_address, iv_action1);
    iv_action1.clearBit(OCC_HB_NOTIFY);
    putScom(iv_proc, iv_action1_address, iv_action1);
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

fapi2::ReturnCode pm_cme_fir_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    auto l_exChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EX>(fapi2::TARGET_STATE_FUNCTIONAL);
    for (auto l_ex_chplt : l_exChiplets)
    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_CME_LFIR> l_cmeFir(l_ex_chplt);
        l_cmeFir.clearAllRegBits(p9pmFIR::REG_FIR);
        l_cmeFir.restoreSavedMask();
        l_cmeFir.put();
    }
}

fapi2::ReturnCode
p9_query_cache_clock_state(
    const fapi2::Target<fapi2::TARGET_TYPE_EQ>& i_target,
    bool o_l2_is_scomable[MAX_L2_PER_QUAD],
    bool o_l3_is_scomable[MAX_L3_PER_QUAD])
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::getScom(i_target, EQ_CLOCK_STAT_SL, l_data64);
    for (auto l_l2Pos = 0; l_l2Pos < MAX_L2_PER_QUAD; l_l2Pos++)
    {
        o_l2_is_scomable[l_l2Pos] = !l_data64.getBit(eq_clk_l2_pos[l_l2Pos]);
    }

    for (auto l_l3Pos = 0; l_l3Pos < MAX_L3_PER_QUAD; l_l3Pos++)
    {
        o_l3_is_scomable[l_l3Pos] = !l_data64.getBit(eq_clk_l3_pos[l_l3Pos]);
    }
}

fapi2::ReturnCode
p9_query_cache_access_state(
    const fapi2::Target<fapi2::TARGET_TYPE_EQ>& i_target,
    bool o_l2_is_scomable[MAX_L2_PER_QUAD],
    bool o_l2_is_scannable[MAX_L2_PER_QUAD],
    bool o_l3_is_scomable[MAX_L3_PER_QUAD],
    bool o_l3_is_scannable[MAX_L3_PER_QUAD])
{
    fapi2::buffer<uint64_t> l_qsshsrc;
    uint32_t l_quadStopLevel = 0;
    fapi2::buffer<uint64_t> l_data64;
    uint8_t l_execution_platform = 0;
    uint32_t l_stop_state_reg = 0;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    fapi2::getScom(i_target, EQ_PPM_PFSNS, l_data64);

    if (l_data64.getBit<EQ_PPM_PFSNS_VDD_PFETS_DISABLED_SENSE>())
    {
        for (auto cnt = 0; cnt < MAX_L2_PER_QUAD; ++cnt)
        {
            o_l2_is_scomable[cnt]  = 0;
            o_l2_is_scannable[cnt] = 0;
        }

        for (auto cnt = 0; cnt < MAX_L3_PER_QUAD; ++cnt)
        {
            o_l3_is_scomable[cnt]  = 0;
            o_l3_is_scannable[cnt] = 0;
        }

        return fapi2::current_err;
    }
    FAPI_ATTR_GET(fapi2::ATTR_EXECUTION_PLATFORM, FAPI_SYSTEM, l_execution_platform);

    if (l_execution_platform == fapi2::ENUM_ATTR_EXECUTION_PLATFORM_FSP)
    {
        l_stop_state_reg =  EQ_PPM_SSHFSP;
    }
    else
    {
        l_stop_state_reg = EQ_PPM_SSHHYP;
    }

    fapi2::getScom(i_target, l_stop_state_reg, l_qsshsrc);
    if (l_qsshsrc.getBit(SSH_REG_STOP_GATED))
    {
        l_qsshsrc.extractToRight<uint32_t>(l_quadStopLevel, SSH_REG_STOP_LEVEL, SSH_REG_STOP_LEVEL_LEN);
    }
    for (auto cnt = 0; cnt < MAX_L2_PER_QUAD; ++cnt)
    {
        o_l2_is_scomable[cnt] = 1;
        o_l2_is_scannable[cnt] = 1;
    }

    for (auto cnt = 0; cnt < MAX_L3_PER_QUAD; ++cnt)
    {
        o_l3_is_scomable[cnt] = 1;
        o_l3_is_scannable[cnt] = 1;
    }
    if (l_qsshsrc.getBit(SSH_REG_STOP_GATED))
    {
        if (l_quadStopLevel >= 11)
        {
            for (auto cnt = 0; cnt < MAX_L2_PER_QUAD; ++cnt)
            {
                o_l2_is_scomable[cnt]  = 0;
                o_l2_is_scannable[cnt] = 0;
            }

            for (auto cnt = 0; cnt < MAX_L3_PER_QUAD; ++cnt)
            {
                o_l3_is_scomable[cnt]  = 0;
                o_l3_is_scannable[cnt] = 0;
            }
        }
        else
        {
            p9_query_cache_clock_state(i_target, o_l2_is_scomable, o_l3_is_scomable);
        }
    }
    else
    {
        p9_query_cache_clock_state(i_target, o_l2_is_scomable, o_l3_is_scomable);
    }
    return fapi2::current_err;
}

fapi2::ReturnCode pm_cme_fir_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
    uint8_t firinit_done_flag;

    FAPI_ATTR_GET(
        fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG,
        i_target,
        firinit_done_flag);

    for (auto l_eq_chplt : l_eqChiplets)
    {
        fapi2::ReturnCode l_rc;
        bool l_l2_is_scanable[MAX_L2_PER_QUAD];
        bool l_l2_is_scomable[MAX_L2_PER_QUAD];
        bool l_l3_is_scanable[MAX_L3_PER_QUAD];
        bool l_l3_is_scomable[MAX_L3_PER_QUAD];

        for (auto cnt = 0; cnt < MAX_L2_PER_QUAD; ++cnt)
        {
            l_l2_is_scomable[cnt] = false;
            l_l2_is_scanable[cnt] = false;
        }

        for (auto cnt = 0; cnt < MAX_L3_PER_QUAD; ++cnt)
        {
            l_l3_is_scanable[cnt] = false;
            l_l3_is_scomable[cnt] = false;
        }

        uint8_t l_chip_unit_pos;

        FAPI_ATTR_GET(
            fapi2::ATTR_CHIP_UNIT_POS,
            l_eq_chplt,
            l_chip_unit_pos);

        p9_query_cache_access_state(
            l_eq_chplt,
            l_l2_is_scomable,
            l_l2_is_scanable,
            l_l3_is_scomable,
            l_l3_is_scanable);


        auto l_exChiplets = l_eq_chplt.getChildren<fapi2::TARGET_TYPE_EX>
                            (fapi2::TARGET_STATE_FUNCTIONAL);

        for(auto l_ex_chplt : l_exChiplets)
        {
            FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_ex_chplt, l_chip_unit_pos);
            //look ex is scommable
            l_chip_unit_pos = l_chip_unit_pos % 2;

            if(!l_l2_is_scomable[l_chip_unit_pos]
            && !l_l3_is_scomable[l_chip_unit_pos])
            {
                continue;
            }
            p9pmFIR::PMFir <p9pmFIR::FIRTYPE_CME_LFIR> l_cmeFir(l_ex_chplt);
            if ( firinit_done_flag != fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_RESET_IN_HB )
            {
                l_cmeFir.get(p9pmFIR::REG_FIRMASK);
                l_cmeFir.saveMask();
                l_cmeFir.setAllRegBits(p9pmFIR::REG_FI;
                l_cmeFir.put();
            }
        }
    }

    if (firinit_done_flag == fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_NO_INIT)
    {
        firinit_done_flag = fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_RESET_IN_HB;
        FAPI_ATTR_SET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, firinit_done_flag);
    }
}

fapi2::ReturnCode p9_pm_cme_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    if(i_mode == p9pm::PM_RESET)
    {
        pm_cme_fir_reset(i_target);
    }
    else if(i_mode == p9pm::PM_INIT)
    {
        pm_cme_fir_init(i_target);
    }
}

fapi2::ReturnCode pm_ppm_fir_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t l_firinit_done_flag;
    auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
    auto l_coreChiplets = i_target.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);
    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, l_firinit_done_flag);

    for (auto l_eq_chplt : l_eqChiplets)
    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PPM_LFIR> l_qppmFir(l_eq_chplt);
        l_qppmFir.setAllRegBits(p9pmFIR::REG_ERRMASK);
        if (l_firinit_done_flag)
        {
            l_qppmFir.restoreSavedMask();
        }
        l_qppmFir.put();
    }

    for (auto l_core_chplt : l_coreChiplets)
    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PPM_LFIR> l_cppmFir(l_core_chplt);
        l_cppmFir.setAllRegBits(p9pmFIR::REG_ERRMASK);
        if (l_firinit_done_flag)
        {
            l_cppmFir.restoreSavedMask();
        }
        l_cppmFir.put();
    }
}

fapi2::ReturnCode pm_ppm_fir_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t l_firinit_done_flag;
    auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
    auto l_coreChiplets = i_target.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, l_firinit_done_flag);

    for (auto l_eq_chplt : l_eqChiplets)
    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PPM_LFIR> l_ppmFir(l_eq_chplt);

        if (l_firinit_done_flag
            == fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED)
        {
            l_ppmFir.get(p9pmFIR::REG_ERRMASK);
            l_ppmFir.saveMask();
        }
        l_ppmFir.setAllRegBits(p9pmFIR::REG_ERRMASK);
        l_ppmFir.put();
    }
    for (auto l_c_chplt : l_coreChiplets)
    {
        p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PPM_LFIR> l_cppmFir(l_c_chplt);

        if (l_firinit_done_flag
            == fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED)
        {
            l_cppmFir.get(p9pmFIR::REG_ERRMASK);
            l_cppmFir.saveMask();
        }

        l_cppmFir.setAllRegBits(p9pmFIR::REG_ERRMASK);
        l_cppmFir.put();
    }
}

fapi2::ReturnCode p9_pm_ppm_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    if(i_mode == p9pm::PM_RESET)
    {
        pm_ppm_fir_reset(i_target);
    }
    else if(i_mode == p9pm::PM_INIT)
    {
        pm_ppm_fir_init(i_target);
    }
}

fapi2::ReturnCode pm_pba_fir_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t firinit_done_flag;
    p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PBA_LFIR> l_pbaFir(i_target);

    l_pbaFir.get(p9pmFIR::REG_ALL);

    l_pbaFir.clearAllRegBits(p9pmFIR::REG_FIR);
    l_pbaFir.clearAllRegBits(p9pmFIR::REG_ACTION0);
    l_pbaFir.clearAllRegBits(p9pmFIR::REG_ACTION1);
    l_pbaFir.setAllRegBits(p9pmFIR::REG_FIRMASK);

    l_pbaFir.setRecvAttn(PBAFIR_OCI_APAR_ERR);
    l_pbaFir.mask(PBAFIR_PB_RDADRERR_FW);
    l_pbaFir.mask(PBAFIR_PB_RDDATATO_FW);
    l_pbaFir.mask(PBAFIR_PB_SUE_FW);
    l_pbaFir.setRecvAttn(PBAFIR_PB_UE_FW);
    l_pbaFir.setRecvAttn(PBAFIR_PB_CE_FW);
    l_pbaFir.setRecvAttn(PBAFIR_OCI_SLAVE_INIT);
    l_pbaFir.setRecvAttn(PBAFIR_OCI_WRPAR_ERR);
    l_pbaFir.mask(PBAFIR_SPARE);
    l_pbaFir.setRecvAttn(PBAFIR_PB_UNEXPCRESP);
    l_pbaFir.setRecvAttn(PBAFIR_PB_UNEXPDATA);
    l_pbaFir.setRecvAttn(PBAFIR_PB_PARITY_ERR);
    l_pbaFir.setRecvAttn(PBAFIR_PB_WRADRERR_FW);
    l_pbaFir.setRecvAttn(PBAFIR_PB_BADCRESP);
    l_pbaFir.mask(PBAFIR_PB_ACKDEAD_FW_RD);
    l_pbaFir.setRecvAttn(PBAFIR_PB_CRESPTO);
    l_pbaFir.mask(PBAFIR_BCUE_SETUP_ERR);
    l_pbaFir.mask(PBAFIR_BCUE_PB_ACK_DEAD);
    l_pbaFir.mask(PBAFIR_BCUE_PB_ADRERR);
    l_pbaFir.mask(PBAFIR_BCUE_OCI_DATERR);
    l_pbaFir.mask(PBAFIR_BCDE_SETUP_ERR);
    l_pbaFir.mask(PBAFIR_BCDE_PB_ACK_DEAD);
    l_pbaFir.mask(PBAFIR_BCDE_PB_ADRERR);
    l_pbaFir.mask(PBAFIR_BCDE_RDDATATO_ERR);
    l_pbaFir.mask(PBAFIR_BCDE_SUE_ERR);
    l_pbaFir.mask(PBAFIR_BCDE_UE_ERR);
    l_pbaFir.mask(PBAFIR_BCDE_CE);
    l_pbaFir.mask(PBAFIR_BCDE_OCI_DATERR);
    l_pbaFir.setRecvAttn(PBAFIR_INTERNAL_ERR);
    l_pbaFir.setRecvAttn(PBAFIR_ILLEGAL_CACHE_OP);
    l_pbaFir.setRecvAttn(PBAFIR_OCI_BAD_REG_ADDR);
    l_pbaFir.mask(PBAFIR_AXPUSH_WRERR);
    l_pbaFir.mask(PBAFIR_AXRCV_DLO_ERR);
    l_pbaFir.mask(PBAFIR_AXRCV_DLO_TO);
    l_pbaFir.mask(PBAFIR_AXRCV_RSVDATA_TO);
    l_pbaFir.mask(PBAFIR_AXFLOW_ERR);
    l_pbaFir.mask(PBAFIR_AXSND_DHI_RTYTO);
    l_pbaFir.mask(PBAFIR_AXSND_DLO_RTYTO);
    l_pbaFir.mask(PBAFIR_AXSND_RSVTO);
    l_pbaFir.mask(PBAFIR_AXSND_RSVERR);
    l_pbaFir.mask(PBAFIR_PB_ACKDEAD_FW_WR);
    l_pbaFir.mask(PBAFIR_RESERVED_41);
    l_pbaFir.mask(PBAFIR_RESERVED_42);
    l_pbaFir.mask(PBAFIR_RESERVED_43);
    l_pbaFir.mask(PBAFIR_FIR_PARITY_ERR2);
    l_pbaFir.mask(PBAFIR_FIR_PARITY_ERR);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, firinit_done_flag);
    if (firinit_done_flag)
    {
        l_pbaFir.restoreSavedMask();
    }
    l_pbaFir.put();
}

fapi2::ReturnCode pm_pba_fir_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t firinit_done_flag;
    p9pmFIR::PMFir <p9pmFIR::FIRTYPE_PBA_LFIR> l_pbaFir(i_target);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, firinit_done_flag);

    if (firinit_done_flag == fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED)
    {
        l_pbaFir.get(p9pmFIR::REG_FIRMASK);
        l_pbaFir.saveMask();
    }

    l_pbaFir.setAllRegBits(p9pmFIR::REG_FIRMASK);
    l_pbaFir.put();
}

fapi2::ReturnCode p9_pm_pba_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    if(i_mode == p9pm::PM_RESET)
    {
        pm_pba_fir_reset(i_target);
    }
    else if(i_mode == p9pm::PM_INIT)
    {
        pm_pba_fir_init(i_target);
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

fapi2::ReturnCode stop_gpe_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    uint32_t                l_timeout_in_MS = 100;
    std::vector<uint64_t> l_ppe_base_addr_list;
    std::vector< fapi2::Target<fapi2::TARGET_TYPE_EQ> > l_eq_list;

    get_functional_chiplet_info( i_target, l_ppe_base_addr_list, l_eq_list);
    l_data64.flush<0>().insertFromRight(p9hcd::HALT, 1, 3);
    putScom(i_target, PU_GPE3_PPE_XIXCR, l_data64);
    do
    {
        getScom(i_target, PU_GPE3_GPEXIXSR_SCOM, l_data64);
        fapi2::delay(SGPE_POLLTIME_MS * 1000, SGPE_POLLTIME_MCYCLES * 1000 * 1000);
    }
    while((l_data64.getBit<p9hcd::HALTED_STATE>() == 0) && (--l_timeout_in_MS != 0));
    l_data64.flush<0>().setBit<p9hcd::SGPE_ACTIVE>();
    putScom(i_target, PU_OCB_OCI_OCCFLG_CLEAR, l_data64);
}

fapi2::ReturnCode get_functional_chiplet_info(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    std::vector<uint64_t>& o_ppe_addr_list,
    std::vector< fapi2::Target<fapi2::TARGET_TYPE_EQ > >& o_eq_target_list )
{
    fapi2::buffer<uint64_t> l_qcsrBuf;
    uint8_t l_exPos = 0;
    auto l_ex_vector = i_target.getChildren<fapi2::TARGET_TYPE_EX>( fapi2::TARGET_STATE_PRESENT );
    getScom(i_target, PU_OCB_OCI_QCSR_SCOM, l_qcsrBuf);

    o_ppe_addr_list.push_back( SGPE_BASE_ADDRESS );

    for ( auto ex : l_ex_vector )
    {
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, ex, l_exPos );

        if(l_qcsrBuf.getBit( l_exPos ))
        {
            o_ppe_addr_list.push_back( getCmeBaseAddress( l_exPos ) );
            fapi2::Target< fapi2::TARGET_TYPE_EQ > l_parentEq = ex.getParent<fapi2::TARGET_TYPE_EQ>();
            std::vector< fapi2::Target< fapi2::TARGET_TYPE_EQ > >::iterator l_eq;
            l_eq = std::find ( o_eq_target_list.begin(), o_eq_target_list.end(), l_parentEq );

            if ( l_eq != o_eq_target_list.end() )
            {
                continue;
            }
            o_eq_target_list.push_back( l_parentEq );
        }
    }
}

fapi2::ReturnCode stop_gpe_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_occ_flag;
    fapi2::buffer<uint64_t> l_xcr;
    fapi2::buffer<uint64_t> l_xsr;
    fapi2::buffer<uint64_t> l_iar;
    fapi2::buffer<uint64_t> l_ir;
    fapi2::buffer<uint64_t> l_ivpr;
    fapi2::buffer<uint64_t> l_slave_cfg;
    uint32_t                l_ivpr_offset;
    uint32_t                l_timeout_in_MS = TIMEOUT_COUNT;
    std::vector<uint64_t> l_ppe_base_addr_list;
    std::vector< fapi2::Target<fapi2::TARGET_TYPE_EQ> > l_eq_list;

    get_functional_chiplet_info( i_target, l_ppe_base_addr_list, l_eq_list );
    getScom(i_target, PU_OCB_OCI_OCCFLG_SCOM, l_occ_flag);

    if (l_occ_flag.getBit<p9hcd::SGPE_ACTIVE>() == 1)
    {
        l_occ_flag.flush<0>();
        l_occ_flag.setBit<p9hcd::SGPE_ACTIVE>();
        putScom(i_target, PU_OCB_OCI_OCCFLG_CLEAR, l_occ_flag);
    }

    FAPI_ATTR_GET(fapi2::ATTR_STOPGPE_BOOT_COPIER_IVPR_OFFSET, i_target, l_ivpr_offset)

    l_ivpr.flush<0>().insertFromRight<0, 32>(l_ivpr_offset);
    putScom(i_target, PU_GPE3_GPEIVPR_SCOM, l_ivpr);

    l_xcr.flush<0>().insertFromRight(p9hcd::HARD_RESET, 1 , 3);
    putScom(i_target, PU_GPE3_PPE_XIXCR, l_xcr);
    l_xcr.flush<0>().insertFromRight(p9hcd::TOGGLE_XSR_TRH, 1 , 3);
    putScom(i_target, PU_GPE3_PPE_XIXCR, l_xcr);
    l_xcr.flush<0>().insertFromRight(p9hcd::RESUME, 1 , 3);
    putScom(i_target, PU_GPE3_PPE_XIXCR, l_xcr);

    l_occ_flag.flush<0>();
    l_xsr.flush<0>();

    do
    {
        getScom(i_target, PU_OCB_OCI_OCCFLG_SCOM, l_occ_flag);
        getScom(i_target, PU_GPE3_GPEXIXSR_SCOM, l_xsr);
        getScom(i_target, PU_GPE3_GPEXIIAR_SCOM, l_iar);
        getScom(i_target, PU_GPE3_GPEXIIR_SCOM, l_ir);
        fapi2::delay(20000ns);
    }
    while((!((l_occ_flag.getBit<p9hcd::SGPE_ACTIVE>() == 1)
    && (l_xsr.getBit<p9hcd::HALTED_STATE>() == 0)))
    && (--l_timeout_in_MS != 0));

    if(l_occ_flag.getBit<p9hcd::SGPE_ACTIVE>() == 1)
    {
        FAPI_INF("SGPE was activated successfully!!!!");
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
            auto l_functional_core_vector = i_target.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);
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
            auto l_functional_core_vector = i_target.getChildren<fapi2::TARGET_TYPE_CORE>(fapi2::TARGET_STATE_FUNCTIONAL);
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

fapi2::ReturnCode pstate_gpe_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    uint32_t                l_timeout_in_MS = 100;
    std::vector<uint64_t> l_pgpe_base_addr;
    l_pgpe_base_addr.push_back( PGPE_BASE_ADDRESS );

    auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
    for (auto l_quad_chplt : l_eqChiplets)
    {
        l_data64.flush<0>()
            .setBit<EQ_QPPM_QPMMR_ENABLE_PCB_INTR_UPON_HEARTBEAT_LOSS>();
        fapi2::putScom(l_quad_chplt, EQ_QPPM_QPMMR_CLEAR, l_data64);
    }
    l_data64.flush<0>().insertFromRight(p9hcd::HALT, 1, 3);
    putScom(i_target, PU_GPE2_PPE_XIXCR, l_data64);

    do
    {
        getScom(i_target, PU_GPE2_GPEXIXSR_SCOM, l_data64);
        fapi2::delay(20000000ns);
    }
    while((l_data64.getBit<p9hcd::HALTED_STATE>() == 0)
    && (--l_timeout_in_MS != 0));

    getScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_data64);
    l_data64.flush<0>().clearBit<p9hcd::PGPE_ACTIVE>();
    putScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_data64);
}

fapi2::ReturnCode pba_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64 = 0;
    uint8_t l_attr_pbax_groupid;
    uint8_t l_attr_pbax_chipid;
    uint8_t l_attr_pbax_broadcast_vector;
    uint8_t l_hw423589_option1;

    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW423589_OPTION1, i_target, l_hw423589_option1);
    l_data64.flush<0>();

    if (l_hw423589_option1)
    {
        l_data64.setBit<PU_PBACFG_CHSW_DIS_GROUP_SCOPE>();
    }
    fapi2::putScom(i_target, PU_PBACFG, l_data64);

    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_PBAFIR, l_data64);
    FAPI_ATTR_GET(fapi2::ATTR_PBAX_GROUPID, i_target, l_attr_pbax_groupid);
    FAPI_ATTR_GET(fapi2::ATTR_PBAX_CHIPID, i_target, l_attr_pbax_chipid);
    FAPI_ATTR_GET(fapi2::ATTR_PBAX_BRDCST_ID_VECTOR, i_target, l_attr_pbax_broadcast_vector);

    l_data64.insertFromRight<PU_PBAXCFG_RCV_GROUPID, PU_PBAXCFG_RCV_GROUPID_LEN>(l_attr_pbax_groupid);
    l_data64.insertFromRight<PU_PBAXCFG_RCV_CHIPID, PU_PBAXCFG_RCV_CHIPID_LEN>(l_attr_pbax_chipid);
    l_data64.insertFromRight<PU_PBAXCFG_RCV_BRDCST_GROUP, PU_PBAXCFG_RCV_BRDCST_GROUP_LEN>(l_attr_pbax_broadcast_vector);
    l_data64.insertFromRight<PU_PBAXCFG_RCV_DATATO_DIV, PU_PBAXCFG_RCV_DATATO_DIV_LEN>(PBAX_DATA_TIMEOUT);
    l_data64.insertFromRight<PU_PBAXCFG_SND_RETRY_COUNT_OVERCOM, 1>(PBAX_SND_RETRY_COMMIT_OVERCOMMIT);
    l_data64.insertFromRight<PU_PBAXCFG_SND_RETRY_THRESH, PU_PBAXCFG_SND_RETRY_THRESH_LEN>(PBAX_SND_RETRY_THRESHOLD);
    l_data64.insertFromRight<PU_PBAXCFG_SND_RSVTO_DIV, PU_PBAXCFG_SND_RSVTO_DIV_LEN>(PBAX_SND_TIMEOUT);

    fapi2::putScom(i_target, PU_PBAXCFG_SCOM, l_data64);
    pba_slave_setup_runtime_phase(i_target);
}

fapi2::ReturnCode
pba_slave_setup_boot_phase(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    pba_mode_t          pm;
    pba_slvctln_t       ps;

    fapi2::buffer<uint64_t>  l_data64(64);

    pm.value = 0;
    pm.fields.pba_region = PBA_OCI_REGION;
    pm.fields.bcde_ocitrans = PBA_BCE_OCI_TRANSACTION_64_BYTES;
    pm.fields.bcue_ocitrans = PBA_BCE_OCI_TRANSACTION_64_BYTES;
    pm.fields.en_marker_ack = 1;
    pm.fields.oci_marker_space = (PBA_OCI_MARKER_BASE >> 16) & 0x7;
    pm.fields.en_slv_fairness = 1;
    pm.fields.en_second_wrbuf = 1;

    l_data64 = pm.value;

    fapi2::putScom(i_target, PU_PBAMODE_SCOM, l_data64);
    ps.value = 0;
    ps.fields.enable = 1;
    ps.fields.mid_match_value = OCI_MASTER_ID_SGPE;
    ps.fields.mid_care_mask   = OCI_MASTER_ID_MASK_ALL;

    ps.fields.read_ttype = PBA_READ_TTYPE_CL_RD_NC;
    ps.fields.read_prefetch_ctl = PBA_READ_PREFETCH_NONE;
    ps.fields.buf_alloc_a = 1;
    ps.fields.buf_alloc_b = 1;
    ps.fields.buf_alloc_c = 1;
    ps.fields.buf_alloc_w = 1;

    l_data64 = ps.value;

    fapi2::putScom(i_target, PU_PBASLVCTL0_SCOM, l_data64);

    ps.value = 0;
    ps.fields.enable = 1;
    ps.fields.mid_match_value = OCI_MASTER_ID_ICU & OCI_MASTER_ID_DCU;
    ps.fields.mid_care_mask   = OCI_MASTER_ID_ICU & OCI_MASTER_ID_DCU;

    ps.fields.read_ttype = PBA_READ_TTYPE_CL_RD_NC;
    ps.fields.read_prefetch_ctl = PBA_READ_PREFETCH_NONE;
    ps.fields.write_ttype = PBA_WRITE_TTYPE_DMA_PR_WR;
    ps.fields.wr_gather_timeout = PBA_WRITE_GATHER_TIMEOUT_2_PULSES;
    ps.fields.buf_alloc_a = 1;
    ps.fields.buf_alloc_b = 1;
    ps.fields.buf_alloc_c = 1;
    ps.fields.buf_alloc_w = 1;

    l_data64 = ps.value;

    fapi2::putScom(i_target, PU_PBASLVCTL1_SCOM, l_data64);

    ps.value = 0;
    ps.fields.enable = 1;
    ps.fields.mid_match_value = OCI_MASTER_ID_PGPE;
    ps.fields.mid_care_mask   = OCI_MASTER_ID_MASK_ALL;

    ps.fields.read_ttype = PBA_READ_TTYPE_CL_RD_NC;
    ps.fields.read_prefetch_ctl = PBA_READ_PREFETCH_NONE;
    ps.fields.write_ttype = PBA_WRITE_TTYPE_DMA_PR_WR;
    ps.fields.wr_gather_timeout = PBA_WRITE_GATHER_TIMEOUT_2_PULSES;
    ps.fields.buf_alloc_a = 1;
    ps.fields.buf_alloc_b = 1;
    ps.fields.buf_alloc_c = 1;
    ps.fields.buf_alloc_w = 1;
    l_data64 = ps.value;
    fapi2::putScom(i_target, PU_PBASLVCTL2_SCOM, l_data64);
}

fapi2::ReturnCode pba_slave_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    bool                    l_poll_failure = false;
    uint32_t                l_pollCount;

    // Slave to be reset. Note: Slave 3 is not reset as it is owned by SBE
    std::vector<uint32_t> v_slave_resets = {0, 1, 2};

    for (auto sl : v_slave_resets)
    {
        l_poll_failure = true;

        for (l_pollCount = 0; l_pollCount < p9pba::MAX_PBA_RESET_POLLS;
             l_pollCount++)
        {
            l_data64.insert<0, 64>(p9pba::PBA_SLVRESETs[sl] );
            fapi2::putScom(i_target, PU_PBASLVRST_SCOM, l_data64);
            fapi2::getScom(i_target, PU_PBASLVRST_SCOM, l_data64);

            if (l_data64 & 0x0000000000000001 << (63 - ( 4 + sl)) )
            {
                fapi2::delay(1000ns);
            }
            else
            {
                break;
            }
        }
    }
}

fapi2::ReturnCode pba_bc_stop(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    bool                l_bcde_stop_complete = false;
    bool                l_bcue_stop_complete = false;
    uint32_t            l_pollCount;

    l_data64.setBit<0>();
    fapi2::putScom(i_target, PU_BCDE_CTL_SCOM, l_data64);
    fapi2::putScom(i_target, PU_BCUE_CTL_SCOM, l_data64);

    for (l_pollCount = 0;
         l_pollCount < p9pba::MAX_PBA_BC_STOP_POLLS;
         l_pollCount++)
    {
        FAPI_TRY(fapi2::getScom(i_target, PU_BCDE_STAT_SCOM, l_data64));
        if (!l_data64.getBit<p9pba::PBA_BC_STAT_RUNNING>() )
        {
            l_bcde_stop_complete = true;
        }

        fapi2::getScom(i_target, PU_BCUE_STAT_SCOM, l_data64);
        if(! l_data64.getBit<p9pba::PBA_BC_STAT_RUNNING>())
        {
            l_bcue_stop_complete = true;
        }

        if (l_bcde_stop_complete && l_bcue_stop_complete)
        {
            break;
        }
        fapi2::delay(256000ns);
    }

    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_BCDE_CTL_SCOM, l_data64);
    fapi2::putScom(i_target, PU_BCUE_CTL_SCOM, l_data64);
}

fapi2::ReturnCode pba_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{

    std::vector<uint64_t> v_pba_reset_regs =
    {
        PU_BCDE_PBADR_SCOM,
        PU_BCDE_OCIBAR_SCOM,
        PU_BCUE_CTL_SCOM,
        PU_BCUE_SET_SCOM,
        PU_BCUE_PBADR_SCOM,
        PU_BCUE_OCIBAR_SCOM,
        PU_PBAXSHBR0_SCOM,
        PU_PBAXSHBR1_SCOM,
        PU_PBAXSHCS0_SCOM,
        PU_PBAXSHCS1_SCOM,
        PU_PBASLVCTL0_SCOM,
        PU_PBASLVCTL1_SCOM,
        PU_PBASLVCTL2_SCOM,
        PU_PBAFIR,
        PU_PBAERRRPT0
    };
    fapi2::buffer<uint64_t> l_data64;
    uint8_t l_hw423589_option1;

    pba_bc_stop(i_target);
    pba_slave_reset(i_target);

    for (auto it : v_pba_reset_regs)
    {
        fapi2::putScom(i_target, it, 0);
    }
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW423589_OPTION1, i_target, l_hw423589_option1);
    l_data64.flush<0>();

    if (l_hw423589_option1)
    {
        l_data64.setBit<PU_PBACFG_CHSW_DIS_GROUP_SCOPE>();
    }
    fapi2::putScom(i_target, PU_PBACFG, l_data64);
    l_data64.flush<0>().setBit<2, 2>();
    fapi2::putScom(i_target, PU_PBAXCFG_SCOM, l_data64);
    pba_slave_setup_boot_phase(i_target);
}

fapi2::ReturnCode p9_pm_pba_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    if (i_mode == p9pm::PM_INIT)
    {
        pba_init(i_target);
    }
    else if (i_mode == p9pm::PM_RESET)
    {
        pba_reset(i_target);
    }
}

fapi2::ReturnCode pstate_gpe_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint64_t> l_occ_scratch2;
    fapi2::buffer<uint64_t> l_xcr;
    fapi2::buffer<uint64_t> l_xsr_iar;
    fapi2::buffer<uint64_t> l_ivpr;
    uint32_t                l_xsr_halt_condition = 0;
    uint32_t                l_timeout_counter = TIMEOUT_COUNT;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>      FAPI_SYSTEM;
    fapi2::ATTR_PSTATEGPE_BOOT_COPIER_IVPR_OFFSET_Type  l_ivpr_offset = 0;
    fapi2::ATTR_VDD_AVSBUS_BUSNUM_Type                  l_avsbus_number = 0;
    fapi2::ATTR_VDD_AVSBUS_RAIL_Type                    l_avsbus_rail = 0;
    fapi2::ATTR_SYSTEM_PSTATES_MODE_Type                l_pstates_mode = 0;

    FAPI_ATTR_GET(fapi2::ATTR_PSTATEGPE_BOOT_COPIER_IVPR_OFFSET, i_target, l_ivpr_offset);

    l_ivpr.flush<0>().insertFromRight<0, 32>(l_ivpr_offset);
    putScom(i_target, PU_GPE2_GPEIVPR_SCOM, l_ivpr);
    getScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_occ_scratch2);

    l_occ_scratch2.clearBit<p9hcd::PGPE_ACTIVE>();

    FAPI_ATTR_GET(fapi2::ATTR_VDD_AVSBUS_BUSNUM, i_target, l_avsbus_number);
    FAPI_ATTR_GET(fapi2::ATTR_VDD_AVSBUS_RAIL, i_target, l_avsbus_rail);

    l_occ_scratch2
        .insertFromRight<27, 1>(l_avsbus_number)
        .insertFromRight<28, 4>(l_avsbus_rail);

    putScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_occ_scratch2);

    FAPI_ATTR_GET(fapi2::ATTR_SYSTEM_PSTATES_MODE, FAPI_SYSTEM, l_pstates_mode);

    fapi2::ATTR_PSTATES_ENABLED_Type l_ps_enabled;
    FAPI_ATTR_GET(fapi2::ATTR_PSTATES_ENABLED, i_target, l_ps_enabled);

    if (l_pstates_mode != fapi2::ENUM_ATTR_SYSTEM_PSTATES_MODE_OFF
    && l_ps_enabled == fapi2::ENUM_ATTR_PSTATES_ENABLED_TRUE)
    {
        if (l_pstates_mode == fapi2::ENUM_ATTR_SYSTEM_PSTATES_MODE_AUTO)
        {
            putScom(i_target, PU_OCB_OCI_OCCFLG_SCOM2, BIT(p9hcd::PGPE_PSTATE_PROTOCOL_AUTO_ACTIVATE));
        }

        l_data64.flush<0>()
            .insertFromRight<0, 4>(0x1)
            .insertFromRight<4, 4>(0xA);
        fapi2::putScom(i_target, PU_GPE2_GPETSEL_SCOM, l_data64);

        l_data64.flush<0>()
            .setBit<p9hcd::OCCFLG2_PGPE_HCODE_FIT_ERR_INJ>()
            .setBit<p9hcd::OCCFLG2_PGPE_HCODE_PSTATE_REQ_ERR_INJ>();
        fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);

        l_xcr.flush<0>().insertFromRight(p9hcd::HARD_RESET, 1, 3);
        putScom(i_target, PU_GPE2_PPE_XIXCR, l_xcr);
        l_xcr.flush<0>().insertFromRight(p9hcd::TOGGLE_XSR_TRH, 1 , 3);
        putScom(i_target, PU_GPE2_PPE_XIXCR, l_xcr);
        l_xcr.flush<0>().insertFromRight(p9hcd::RESUME, 1, 3);
        putScom(i_target, PU_GPE2_PPE_XIXCR, l_xcr);
        l_occ_scratch2.flush<0>();
        l_xsr_iar.flush<0>();

        do
        {
            getScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_occ_scratch2);
            getScom(i_target, PU_GPE2_PPE_XIDBGPRO, l_xsr_iar);
            // does this need to be such a lone time?
            fapi2::delay(20000000ns);
        }
        while((l_occ_scratch2.getBit<p9hcd::PGPE_ACTIVE>() != 1) &&
              (l_xsr_iar.getBit<p9hcd::HALTED_STATE>() != 1) &&
              (--l_timeout_counter != 0));

        l_xsr_iar.extractToRight<uint32_t>(l_xsr_halt_condition,
                                           p9hcd::HALT_CONDITION_START,
                                           p9hcd::HALT_CONDITION_LEN);

        if (l_pstates_mode == fapi2::ENUM_ATTR_SYSTEM_PSTATES_MODE_AUTO)
        {
            do
            {
                getScom(i_target, PU_OCB_OCI_OCCS2_SCOM, l_occ_scratch2);
                getScom(i_target, PU_GPE3_PPE_XIDBGPRO, l_xsr_iar);
                // does this need to be such a lone time?
                fapi2::delay(20000000ns);
            }
            while((l_occ_scratch2.getBit<p9hcd::PGPE_PSTATE_PROTOCOL_ACTIVE>() != 1) &&
                  (l_xsr_iar.getBit<p9hcd::HALTED_STATE>() != 1) &&
                  (--l_timeout_counter != 0));

            if (l_timeout_counter != 0
            && l_occ_scratch2.getBit<p9hcd::PGPE_PSTATE_PROTOCOL_ACTIVE>() == 1
            && l_xsr_iar.getBit<p9hcd::HALTED_STATE>() != 1)
            {
                FAPI_INF("Pstate Auto Start Mode Complete!!!!");
            }
            else
            {
                FAPI_INF("Pstate GPE Protocol Auto Start timeout");
            }
        }

        const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
        auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
        fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ_Type l_attr_safe_mode_freq_mhz;
        fapi2::ATTR_FREQ_DPLL_REFCLOCK_KHZ_Type l_ref_clock_freq_khz;
        fapi2::ATTR_PROC_DPLL_DIVIDER_Type l_proc_dpll_divider;
        FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ, i_target, l_attr_safe_mode_freq_mhz);
        FAPI_ATTR_GET(fapi2::ATTR_FREQ_DPLL_REFCLOCK_KHZ, FAPI_SYSTEM, l_ref_clock_freq_khz);
        FAPI_ATTR_GET(fapi2::ATTR_PROC_DPLL_DIVIDER, i_target, l_proc_dpll_divider);

        uint32_t l_safe_mode_freq = ((l_attr_safe_mode_freq_mhz * 1000) * l_proc_dpll_divider) / l_ref_clock_freq_khz;

        for (auto l_eq_chplt : l_eqChiplets)
        {
            getScom(l_eq_chplt, EQ_QPPM_QPMMR, l_data64);
            l_data64.insertFromRight<EQ_QPPM_QPMMR_FSAFE, EQ_QPPM_QPMMR_FSAFE_LEN>(l_safe_mode_freq);

            fapi2::putScom(l_eq_chplt, EQ_QPPM_QPMMR, l_data64);
        }
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

inline bool spBaseServicesEnabled()
{
    bool spBaseServicesEnabled = false;
    TARGETING::Target * sys = NULL;
    TARGETING::targetService().getTopLevelTarget( sys );
    TARGETING::SpFunctions spfuncs;
    if( sys &&
        sys->tryGetAttr<TARGETING::ATTR_SP_FUNCTIONS>(spfuncs) &&
        spfuncs.baseServices )
    {
        spBaseServicesEnabled = true;
    }

    return spBaseServicesEnabled;
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
            return;
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

    // For each EX ciplet
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
