void TodSvc::todSetup()
{
    TodTopologyManager l_primary(); // NOOP
    iv_proc0 = TARGETING::getAllChips(l_targetList, TARGETING::TYPE_PROC, false)[0];
    TOD::buildTodDrawers();
    l_primary.create();
    p9_tod_setup();
    p9_tod_save_config(); // some scom's
}

struct tod_topology_node
{
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>* i_target;
    bool i_tod_master;
    bool i_drawer_master;
    p9_tod_setup_bus i_bus_rx;
    p9_tod_setup_bus i_bus_tx;
    std::list<tod_topology_node*> i_children;
    p9_tod_setup_conf_regs o_todRegs;
    uint32_t o_int_path_delay;
};

static void TodDrawer::findMasterProc(TodProc*& l_pDrawerMaster) const
{
    l_pDrawerMaster = NULL;
    for(TodProcContainer::const_iterator l_procIter = iv_todProcList.begin();
        l_procIter != iv_todProcList.end();
        ++l_procIter)
    {
        if((*l_procIter)->iv_masterType == TodProc::TOD_MASTER
        || (*l_procIter)->iv_masterType == TodProc::DRAWER_MASTER)
        {
            l_pDrawerMaster = *l_procIter;
            return;
        }
    }
}

void TodProc::init()
{
    iv_tod_node_data = new tod_topology_node();
    iv_tod_node_data->i_target = new fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>(const_cast<TARGETING::Target*>(iv_proc0));
    iv_tod_node_data->i_tod_master = false;
    iv_tod_node_data->i_drawer_master = false;
}

void TargetService::getAssociated(
          TargetHandleList&    o_list,
    const Target* const        i_pSourceTarget,
    const ASSOCIATION_TYPE     i_type,
    const PredicateBase* const i_pPredicate) const
{
    o_list.clear();
    AbstractPointer<Target>* pDestinationTargetItr = TARGETING::theAttrRP::instance().translateAddr(i_pSourceTarget->iv_ppAssociations[i_type], i_pSourceTarget);

    while(*pDestinationTargetItr)
    {
        if((*pDestinationTargetItr).TranslationEncoded.nodeId)
        {
            pDestinationTargetItr = TARGETING::theAttrRP::instance().translateAddr(
                pDestinationTargetItr,
                (*pDestinationTargetItr).TranslationEncoded.nodeId - 1);
        }
        else
        {
            pDestinationTargetItr = TARGETING::theAttrRP::instance().translateAddr(
                pDestinationTargetItr,
                i_pSourceTarget);
        }

        if(!i_pPredicate || (*i_pPredicate)(pDestinationTargetItr))
        {
            o_list.push_back(pDestinationTargetItr);
        }

        getAssociated(
            o_list,
            pDestinationTargetItr,
            i_type,
            i_pPredicate);
        ++pDestinationTargetItr;
    }

    if (o_list.size() > 1)
    {
        std::sort(o_list.begin(), o_list.end(), compareTargetHuid);
    }
}

static void TodTopologyManager::create()
{
    TOD::pickMdmt();
    TodProcContainer l_targetsList = iv_todConfig[TOD_PRIMARY].iv_todDrawerList[0]->iv_todProcList;
    TodProc* l_pDrawerMaster = NULL;
    iv_todConfig[TOD_PRIMARY].iv_todDrawerList[0]->findMasterProc(l_pDrawerMaster);
    TodProcContainer l_sourcesList;
    l_sourcesList.push_back(l_pDrawerMaster);

    for(TodProcContainer::iterator l_sourceItr = l_sourcesList.begin();
        l_sourcesList.end() != l_sourceItr;
        ++l_sourceItr;)
    {
        for(TodProcContainer::iterator l_targetItr = l_targetsList.begin();
            l_targetItr != l_targetsList.end();)
        {
            l_sourcesList.push_back(*l_targetItr);
            if((*l_sourceItr)->iv_proc0->getAttr<TARGETING::ATTR_HUID>()
            != (*l_sourceItr)->*l_targetItr->getTarget()->getAttr<TARGETING::ATTR_HUID>())
            {
                (*l_sourceItr)->iv_childrenList.push_back(*l_targetItr);
                (*l_sourceItr)->iv_tod_node_data->i_children.push_back(*l_targetItr->getTopologyNode());
            }
            l_targetItr = l_targetsList.erase(l_targetItr);
        }
    }
}

static void calculate_node_link_delay(
    tod_topology_node* i_tod_node,
    uint32_t& o_node_delay)
{
    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        o_node_delay = 0;
        i_tod_node->o_int_path_delay = 0;
        return;
    }
    ASSERT(false);
}

TodProc* TodControls ::pickMdmt(
    const TodProc* i_otherConfigMdmt,
    const p9_tod_setup_tod_sel& i_config)
{
    TodProc *l_pTodProc = NULL;
    TodDrawer* l_pMasterDrw = NULL;
    uint32_t l_coreCount;

    for (TodDrawerContainer::iterator l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
        l_todDrawerIter != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end();
        ++l_todDrawerIter)
    {
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
            != (*l_todDrawerIter)->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>())
        {
            (*l_todDrawerIter)->getProcWithMaxCores(NULL, l_pTodProc, l_coreCount);
            if(l_coreCount > 0)
            {
                l_pMasterDrw = *l_todDrawerIter;
            }
        }
    }
    if(l_pTodProc)
    {
        iv_todConfig[TOD_PRIMARY].iv_mdmt = l_pTodProc;
        l_pTodProc->iv_tod_node_data->i_drawer_master = true;
        l_pTodProc->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
        return l_pTodProc;
    }

    for(const auto & l_todDrawerIter : l_todDrawerList)
    {
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
                == l_todDrawerIter->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
            && iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->iv_todDrawerId
                != l_todDrawerIter->iv_todDrawerId)
        {
            l_pTodProc = NULL;
            l_todDrawerIter->getProcWithMaxCores(NULL, l_pTodProc, l_coreCount);
            if(l_coreCount > 0)
            {
                l_pMasterDrw = l_todDrawerIter;
            }
        }
    }
    if(l_pTodProc)
    {
        iv_todConfig[TOD_PRIMARY].iv_mdmt = l_pTodProc;
        l_pTodProc->iv_tod_node_data->i_drawer_master = true;
        l_pTodProc->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
        return l_pTodProc;
    }

    for (const auto l_todDrawerIter : l_todDrawerList)
    {
        l_pTodProc = NULL;
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->iv_todDrawerId
           == l_todDrawerIter->iv_todDrawerId)
        {
            l_todDrawerIter->getProcWithMaxCores(
                iv_todConfig[l_oppConfig].iv_mdmt,
                l_pTodProc,
                l_coreCount);
            l_pMasterDrw = l_todDrawerIter;
            break;
        }
    }
    if(l_pTodProc)
    {
        iv_todConfig[TOD_PRIMARY].iv_mdmt = l_pTodProc;
        l_pTodProc->iv_tod_node_data->i_drawer_master = true;
        l_pTodProc->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
    }
    return l_pTodProc;
}

static void TodControls ::pickMdmt()
{
    if(iv_todConfig[TOD_PRIMARY].iv_mdmt)
    {
        if(NULL == pickMdmt(iv_todConfig[l_oppConfig].iv_mdmt, iv_todConfig[l_oppConfig].iv_mdmt))
        {
            for (const auto & drawer: iv_todConfig[TOD_PRIMARY].iv_todDrawerList)
            {
                for (const auto & proc: iv_todProcList)
                {
                    if(proc->getTarget() == iv_todConfig[TOD_PRIMARY].iv_mdmt->getTarget())
                    {
                        iv_todConfig[TOD_PRIMARY].iv_mdmt = proc;
                        proc->setMasterType(TodProc::TOD_MASTER);
                        drawer->setMasterDrawer(true);
                        break;
                    }
                }
                if(iv_todConfig[TOD_PRIMARY].iv_mdmt)
                {
                    break;
                }
            }
        }
    }
    else
    {
        TodProc* l_newMdmt;
        TodDrawer* l_pTodDrw;
        for (const auto & l_drwItr: l_todDrawerList)
        {
            TodProc* l_pTodProc;
            uint32_t l_coreCount;
            l_drwItr->getProcWithMaxCores(l_pTodProc, l_coreCount);
            if(l_coreCount > 0)
            {
                l_pTodDrw = l_drwItr;
                l_newMdmt = l_pTodProc;
            }
        }

        if(l_newMdmt)
        {
            iv_todConfig[TOD_PRIMARY].iv_mdmt = l_newMdmt;
            l_newMdmt->iv_tod_node_data->i_drawer_master = true;
            l_newMdmt->iv_tod_node_data->i_tod_master = true;
            l_pTodDrw->iv_isTodMaster = true;
        }
    }
}

static void TodDrawer::getProcWithMaxCores(
    TodProc*& o_pTodProc,
    uint32_t& o_coreCount) const
{
    o_pTodProc = NULL;
    o_coreCount = 0;

    TARGETING::PredicateCTM l_coreCTM(TARGETING::CLASS_UNIT, TARGETING::TYPE_CORE);

    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcCorePostfixExpr;
    l_funcCorePostfixExpr.push(&l_coreCTM).push(&l_funcPred).And();

    for(TodProcContainer::const_iterator l_procIter = iv_todProcList.begin();
        l_procIter != iv_todProcList.end();
        ++l_procIter)
    {
        TARGETING::TargetHandleList l_funcCoreTargetList;
        l_funcCoreTargetList.clear();
        TARGETING::targetService().getAssociated(
            l_funcCoreTargetList,
            (*l_procIter)->getTarget(),
            TARGETING::TargetService::CHILD,
            &l_funcCorePostfixExpr);

        if(l_funcCoreTargetList.size() > 0)
        {
            o_pTodProc = *l_procIter;
            o_coreCount = l_funcCoreTargetList.size();
        }
    }
}

static void TodControls::buildTodDrawers(const p9_tod_setup_tod_sel i_config)
{
    TARGETING::TargetHandleList l_funcNodeTargetList;
    TARGETING::Target* l_pSysTarget;
    TARGETING::targetService().getTopLevelTarget(l_pSysTarget);
    TOD::TodSvcUtil::getFuncNodeTargetsOnSystem(l_pSysTarget, l_funcNodeTargetList, true);
    TARGETING::TargetHandleList l_funcProcTargetList;
    TARGETING::PredicateCTM l_procCTM(TARGETING::CLASS_CHIP,TARGETING::TYPE_PROC);
    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcProcPostfixExpr;
    l_funcProcPostfixExpr.push(&l_procCTM).push(&l_funcPred).And();

    for(uint32_t l_nodeIndex = 0;
        l_nodeIndex < l_funcNodeTargetList.size();
        ++l_nodeIndex)
    {
        l_funcProcTargetList.clear();

        TARGETING::targetService().getAssociated(
            l_funcProcTargetList,
            l_funcNodeTargetList[l_nodeIndex],
            TARGETING::TargetService::CHILD,
            &l_funcProcPostfixExpr);

        for(uint32_t l_procIndex = 0;
            l_procIndex < l_funcProcTargetList.size();
            ++l_procIndex)
        {
            bool b_foundDrawer = false;

            for(TodDrawerContainer::iterator l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
                l_todDrawerIter != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end();
                ++l_todDrawerIter)
            {
                if((*l_todDrawerIter)->iv_todDrawerId == l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>())
                {
                    (*l_todDrawerIter)->iv_todProcList.push_back(new TodProc(l_funcProcTargetList[l_procIndex], (*l_todDrawerIter)));
                    b_foundDrawer = true;
                    break;
                }
            }

            if (!b_foundDrawer)
            {
                TodDrawer *l_pTodDrawer = new TodDrawer(
                    l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>(),
                    l_funcNodeTargetList[l_nodeIndex]);
                l_pTodDrawer->iv_todProcList.push_back(new TodProc(l_funcProcTargetList[l_procIndex], l_pTodDrawer));
                iv_todConfig[TOD_PRIMARY].iv_todDrawerList.push_back(l_pTodDrawer);
            }
        }
    }
}

static void TodControls::getParent(
    const TARGETING::Target *i_pTarget,
    const TARGETING::CLASS i_class,
    TARGETING::Target *& o_parent_target)
{
    bool l_parent_found = false;
    TARGETING::TargetHandleList l_list;

    l_list.clear();
    TARGETING::targetService().getAssociated(
        l_list,
        i_pTarget,
        TARGETING::TargetService::PARENT,
        TARGETING::TargetService::IMMEDIATE);
    if(l_list.size() == 1
    && (i_class == TARGETING::CLASS_NA
    || i_class == l_list[0]->getAttr<TARGETING::ATTR_CLASS>()))
    {
        o_parent_target = const_cast<TARGETING::Target *>(l_list[0]);
        l_parent_found = true;
        break;
    }
}

static void p9_tod_setup()
{
    if (fapi2::ATTR_IS_MPIPL) // Attribute of the TARGET_TYPE_SYSTEM
    {
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PSS_MSS_CTRL_REG, 0x0ULL)
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_RX_TTYPE_CTRL_REG, PPC_BIT(5) | PPC_BIT(56));
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_TX_TTYPE_5_REG, PPC_BIT(PERV_TOD_TX_TTYPE_5_REG_TRIGGER));
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_LOAD_TOD_REG, PPC_BIT(63));
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_M_PATH_CTRL_REG, M_PATH_CTRL_REG_CLEAR_VALUE);
        fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_S_PATH_CTRL_REG, S_PATH_CTRL_REG_CLEAR_VALUE);
    }

    iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay = -iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay;
    iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay += MDMT_TOD_GRID_CYCLE_STAGING_DELAY;
    write_scom_for_chiplet(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, 0);
    write_scom_for_chiplet(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, 0);
    scom_or_for_chiplet(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_S_PATH_CTRL_REG, PPC_BITMASK(26, 31));
    uint64_t l_pss_mss_ctrl_reg;
    fapi2::getScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PSS_MSS_CTRL_REG, l_pss_mss_ctrl_reg);

    if (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
    {
        l_pss_mss_ctrl_reg.setBit<1>();
        l_pss_mss_ctrl_reg.clearBit<0>();
    }
    else
    {
        l_pss_mss_ctrl_reg.clearBit<1>();
    }

    if (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
    {
        l_pss_mss_ctrl_reg.setBit<2>();
    }
    fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PSS_MSS_CTRL_REG, l_pss_mss_ctrl_reg);

    if (!(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master))
    {
        scom_and_or_for_chiplet(
            *(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target),
            PERV_TOD_S_PATH_CTRL_REG,
            ~PPC_BIT(0)
          & ~PPC_BITMASK(6, 7)
          & ~PPC_BITMASK(8, 11)
          & ~PPC_BITMASK(13, 15)
          & ~PPC_BITMASK(28, 31)
          & ~PPC_BITMASK(32, 39)
          & ~PPC_BITMASK(26, 27),
            PPC_BITMASK(8, 9)
          | PPC_BITMASK(14, 15)
          | PPC_BITMASK(28, 29)
          | PPC_BIT(37)
          | PPC_BIT(39));
    }

    scom_and_for_chiplet(
        *(i_tiv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_dataod_node->i_target),
        PERV_TOD_PRI_PORT_0_CTRL_REG,
        ~PPC_BITMASK(0, 2));

    scom_and_for_chiplet(
        *(i_tiv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_dataod_node->i_target),
        PERV_TOD_SEC_PORT_0_CTRL_REG,
        ~PPC_BITMASK(0, 2));

    uint64_t l_m_path_ctrl_reg;
    uint64_t l_root_ctrl8_reg;
    fapi2::getScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_ROOT_CTRL8_SCOM, l_root_ctrl8_reg);
    fapi2::getScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_M_PATH_CTRL_REG, l_m_path_ctrl_reg);
    if(l_root_ctrl8_reg & PPC_BIT(21))
    {
        l_m_path_ctrl_reg |= PPC_BIT(4);
    }
    else
    {
        l_m_path_ctrl_reg &= ~PPC_BIT(4);
    }
    if (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master
     && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
    {
        l_m_path_ctrl_reg &=
               ~PPC_BIT(0)
             & ~PPC_BIT(2)
             & ~PPC_BITMASK(5, 7)
             & ~PPC_BITMASK(9, 11)
             & PPC_BIT(13)
             & PPC_BITMASK(24, 25)
             | PPC_BIT(1)
             | PPC_BIT(8)
             | PPC_BITMASK(14, 15);
    }
    fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_M_PATH_CTRL_REG, l_m_path_ctrl_reg);

    uint64_t l_port_ctrl_reg;
    fapi2::getScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    l_port_ctrl_reg.insertFromRight<32, 8>(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay);
    fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);

    scom_and_or_for_chiplet(
        *(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target),
        PERV_TOD_I_PATH_CTRL_REG,
          ~PPC_BITMASK(0, 1)
        & ~PPC_BITMASK(6, 7)
        & ~PPC_BIT(13),
          PPC_BITMASK(8, 11)
        | PPC_BITMASK(14, 15));
    scom_and_or_for_chiplet(
        *(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target),
        PERV_TOD_CHIP_CTRL_REG,
          ~PPC_BITMASK(1, 3)
        & ~PPC_BIT(4)
        & ~PPC_BITMASK(7, 9)
        & ~PPC_BIT(30),
        PPC_BITMASK(10, 15));
}

static void p9_tod_save_config()
{
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_M_PATH_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_m_path_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_pri_port_0_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_1_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_pri_port_1_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_sec_port_0_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_SEC_PORT_1_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_sec_port_1_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_S_PATH_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_s_path_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_I_PATH_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_i_path_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PSS_MSS_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_pss_mss_ctrl_reg);
    fapi2::getScom(*(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_CHIP_CTRL_REG, TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_todRegs.tod_chip_ctrl_reg);
}

static void initTargeting(errlHndl_t& io_pError)
{
    bool l_isMpipl = false;
    bool l_isIstepMode = false;
    ATTR_MASTER_MBOX_SCRATCH_type l_scratch = {0,0,0,0,0,0,0,0};

    for(size_t i = 0; i < sizeof(l_scratch) / sizeof(l_scratch[0]); i++)
    {
        l_scratch[i] = Util::readScratchReg(MBOX_SCRATCH_REG1+i);
    }

    INITSERVICE::SPLESS::MboxScratch3_t l_scratch3;
    l_scratch3.data32 = l_scratch[SCRATCH_3];

    if(l_scratch3.isMpipl)
    {
        l_isMpipl = true;
#ifdef CONFIG_SUPPORT_PARTIAL_CACHE
        mm_extend(MM_EXTEND_FULL_CACHE);
#endif
    }
    if(l_scratch3.istepMode)
    {
        l_isIstepMode = true;
    }

#ifndef CONFIG_SUPPORT_PARTIAL_CACHE
    mm_extend(MM_EXTEND_FULL_CACHE);
#endif
    Singleton<AttrRP>::instance().startup(io_pError, l_isMpipl);

    if (io_pError == NULL)
    {
        TargetService& l_targetService = targetService();
        (void)l_targetService.init();
        if(l_isMpipl)
        {
            l_targetService.modifyReadOnlyPagePermissions(true);
            TARGETING::AssociationManager::reconnectSyAndNodeTargets();
        }

        initializeAttributes(l_targetService, l_isMpipl, l_isIstepMode, l_scratch);
        uint32_t l_peerTargetsAdjusted = 0;
        uint32_t l_numberMutexAttrsReset = 0;

        if(l_isMpipl)
        {
            for( auto targ_iter = l_targetService.begin();
                 targ_iter != l_targetService.end();
                 targ_iter++)
            {
                const Target* l_pTarget = *targ_iter;

                l_numberMutexAttrsReset += l_targetService.resetMutexAttributes(l_pTarget);
                if(l_targetService.updatePeerTarget(l_pTarget))
                {
                    l_peerTargetsAdjusted++;
                }
            }

            l_targetService.modifyReadOnlyPagePermissions(false);
        }
        else
        {

            for(auto targ_iter = l_targetService.begin();
                targ_iter != l_targetService.end();
                targ_iter++)
            {
                const Target* l_pTarget = *targ_iter;
                l_targetService.resetMutexAttributes(l_pTarget);
            }
        }
        checkProcessorTargeting(l_targetService);
        Target* l_pTopLevel = NULL;
        l_targetService.getTopLevelTarget(l_pTopLevel);

#ifdef CONFIG_DRTM
        const INITSERVICE::SPLESS::MboxScratch7_t scratch7 = {.data32 = l_scratch[SCRATCH_7] };
        const INITSERVICE::SPLESS::MboxScratch8_t scratch8 = {.data32 = l_scratch[SCRATCH_8] };

        errlHndl_t pError = SECUREBOOT::DRTM::discoverDrtmState(scratch7,scratch8);
#endif
        bool l_allow_attr_overrides = g_BlToHbDataManager.getAllowAttrOverrides();
        if (l_allow_attr_overrides)
        {
            l_pTopLevel->setAttr<TARGETING::ATTR_ALLOW_ATTR_OVERRIDES_IN_SECURE_MODE>(l_allow_attr_overrides);
        }
        else
        {
            l_pTopLevel->setAttr<TARGETING::ATTR_ALLOW_ATTR_OVERRIDES_IN_SECURE_MODE>(0x0);
        }

#ifndef CONFIG_VPO_COMPILE
        ERRORLOG::ErrlManager::errlResourceReady(ERRORLOG::TARG);
#endif
        Util::setIsTargetingLoaded();
#ifdef CONFIG_BMC_IPMI
        l_pTopLevel->setAttr<TARGETING::ATTR_IPMI_MAX_BUFFER_SIZE>(IPMI::max_buffer());
#endif
    }
}

errlHndl_t TargetService::modifyReadOnlyPagePermissions(bool i_allowWrites)
{
    TARGETING::AttrRP *l_pAttrRP = &TARG_GET_SINGLETON(TARGETING::theAttrRP);
    if(i_allowWrites)
    {
        l_pAttrRP->editPagePermissions(SECTION_TYPE_PNOR_RO, WRITABLE);
    }
    else
    {
        l_pAttrRP->editPagePermissions(SECTION_TYPE_PNOR_RO, READ_ONLY);
    }
}

errlHndl_t AttrRP::editPagePermissions(uint8_t i_type, uint32_t i_permission)
{
    uint32_t l_perm = i_permission;
    for (size_t i = 0; i < iv_sectionCount; ++i)
    {
        if(i_permission == DEFAULT_PERMISSIONS)
        {
            switch(iv_sections[i].type)
            {
                case SECTION_TYPE_PNOR_RO:
                    l_perm = READ_ONLY;
                    break;

                case SECTION_TYPE_PNOR_RW:
                    l_perm = WRITABLE | WRITE_TRACKED;
                    break;

                case SECTION_TYPE_HEAP_PNOR_INIT:
                    l_perm = WRITABLE;
                    break;

                case SECTION_TYPE_HEAP_ZERO_INIT:
                case SECTION_TYPE_HB_HEAP_ZERO_INIT:
                    l_perm = WRITABLE | ALLOCATE_FROM_ZERO;
                    break;
            }
        }
        if( i_type == ALL_SECTION_TYPES || i_type == iv_sections[i].type)
        {
            mm_set_permission(reinterpret_cast<void*>(iv_sections[i].vmmAddress), iv_sections[i].size, l_perm);
        }
    }
}

void* AttrRP::translateAddr(
    void* i_pAddress,
    const Target* i_pTarget)
{
    void* o_pTransAddr = i_pAddress;
    if(i_pTarget != NULL)
    {
        NODE_ID l_nodeId = iv_nodeContainer.size();
        static std::map<const Target*,NODE_ID> s_targToNodeMap;
        auto l_nodeItr = s_targToNodeMap.find(i_pTarget);
        if(l_nodeItr != s_targToNodeMap.end())
        {
            l_nodeId = l_nodeItr->second;
            return;
        }

        for(uint8_t i = 0; i < iv_nodeContainer.size(); ++i)
        {
            for(uint32_t j = 0; j < iv_nodeContainer[i].sectionCount; ++j)
            {
                if(iv_nodeContainer[i].pSections[j].type == SECTION_TYPE_PNOR_RO
                && i_pTarget >= iv_nodeContainer[i].pTargetMap
                && i_pTarget < iv_nodeContainer[i].pTargetMap + iv_nodeContainer[i].pSections[j].size)
                {
                    l_nodeId = i;
                    s_targToNodeMap[i_pTarget] = i;
                    return;
                }
            }
        }
        void* o_pTransAddr = i_pAddress & MASK_OFF_UPPER_BYTE;
        for (size_t i = 0; i < iv_nodeContainer[l_nodeId].sectionCount; ++i)
        {
            if(iv_nodeContainer[l_nodeId].pSections[i].vmmAddress
             + iv_nodeContainer[l_nodeId].pSections[i].size
            >= o_pTransAddr)
            {
                o_pTransAddr =
                    iv_nodeContainer[l_nodeId].pSections[i].pnorAddress
                  - iv_nodeContainer[l_nodeId].pSections[i].vmmAddress
                  + o_pTransAddr;
                break;
            }
        }
    }
    return o_pTransAddr;
}


void AttrRP::startup(bool i_isMpipl)
{
    iv_isMpipl = i_isMpipl;
    this->parseAttrSectHeader();
    this->createVmmSections();
    this->editPagePermissions(ALL_SECTION_TYPES, DEFAULT_PERMISSIONS);
    task_create(&AttrRP::startMsgServiceTask, this);
    msg_q_register(iv_attrSyncMsgQ, ATTRRP_ATTR_SYNC_MSG_Q);
    task_create(&AttrRP::startAttrSyncTask, this);
    if(iv_isMpipl)
    {
        populateAttrsForMpipl();
    }
}

void AttrRP::populateAttrsForMpipl()
{
    for (size_t i = 0; i < iv_sectionCount; ++i)
    {
        if((iv_sections[i].type == SECTION_TYPE_HEAP_ZERO_INIT
        || iv_sections[i].type == SECTION_TYPE_HB_HEAP_ZERO_INIT
        || iv_sections[i].type == SECTION_TYPE_PNOR_RW)
        && iv_isMpipl)
        {
            memcpy(
                iv_sections[i].vmmAddress,
                iv_sections[i].realMemAddress,
                (iv_sections[i].size));
        }

    }
}

int msg_q_register(msg_q_t q, const char* name)
{
    if (0 == strcmp(VFS_ROOT, name))
    {
        return _syscall2(MSGQ_REGISTER_ROOT, MSGQ_ROOT_VFS, q);
    }
    else
    {
        msg_q_t vfsQ = _syscall1(MSGQ_RESOLVE_ROOT, MSGQ_ROOT_VFS);
        msg_t* msg = msg_allocate();
        msg->type = VFS_MSG_REGISTER_MSGQ;
        msg->data[0] = (uint64_t) q;
        msg->extra_data = (void*) name;
        msg_sendrecv(vfsQ, msg);
        msg_free(msg);
    }
}

static void AttrRP::editPagePermissions(uint8_t i_type, uint32_t i_permission)
{
    uint32_t l_perm = i_permission;
    for (size_t i = 0; i < iv_sectionCount; ++i)
    {
        if(i_permission == DEFAULT_PERMISSIONS)
        {
            switch(iv_sections[i].type)
            {
                case SECTION_TYPE_PNOR_RO:
                    l_perm = READ_ONLY;
                    break;
                case SECTION_TYPE_PNOR_RW:
                    l_perm = WRITABLE | WRITE_TRACKED;
                    break;
                case SECTION_TYPE_HEAP_PNOR_INIT:
                    l_perm = WRITABLE;
                    break;
                case SECTION_TYPE_HEAP_ZERO_INIT:
                case SECTION_TYPE_HB_HEAP_ZERO_INIT:
                    l_perm = WRITABLE | ALLOCATE_FROM_ZERO;
                    break;
            }
        }
        if(i_type == ALL_SECTION_TYPES || i_type == iv_sections[i].type)
        {
            _syscall3(
                MM_SET_PERMISSION,
                iv_sections[i].vmmAddress,
                iv_sections[i].size,
                l_perm);
        }
    }
}

static void AttrRP::createVmmSections()
{
    iv_msgQ = msg_q_create();
    msg_q_register(iv_msgQ, ATTRRP_MSG_Q);

    for (size_t i = 0; i < iv_sectionCount; ++i)
    {
        int rc = 0;
        msg_q_t l_msgQ = iv_msgQ;

        if ( (iv_sections[i].type == SECTION_TYPE_HEAP_ZERO_INIT) ||
                (iv_sections[i].type == SECTION_TYPE_HB_HEAP_ZERO_INIT) )
        {
            l_msgQ = NULL;
        }

        mm_alloc_block(
            l_msgQ,
            iv_sections[i].vmmAddress,
            iv_sections[i].size);
        if(iv_sections[i].type == SECTION_TYPE_PNOR_RW)
        {
            INITSERVICE::registerBlock(
                reinterpret_cast<void*>(iv_sections[i].vmmAddress),
                iv_sections[i].size,ATTR_PRIORITY);
        }
    }
}

static void AttrRP::parseAttrSectHeader()
{
        #ifdef CONFIG_SECUREBOOT
        PNOR::loadSecureSection(PNOR::HB_DATA);
        #endif

        PNOR::SectionInfo_t l_pnorSectionInfo;
        TargetingHeader* l_header = nullptr;
        PNOR::getSectionInfo(PNOR::HB_DATA, l_pnorSectionInfo);

        if(!iv_isMpipl)
        {
            l_header = reinterpret_cast<TargetingHeader*>(l_pnorSectionInfo.vaddr);
        }
        else
        {
            uint64_t l_phys_attr_data_addr = 0;
            uint64_t l_attr_data_size = 0;
            uint64_t l_toc_addr = AttrRP::getHbDataTocAddr();

            Util::hbrtTableOfContents_t * l_toc_ptr =
            reinterpret_cast<Util::hbrtTableOfContents_t *>(
            mm_block_map(l_toc_addr, sizeof(Util::hbrtTableOfContents_t)));

            if(l_toc_ptr != 0)
            {
                uint64_t l_attr_data_addr = Util::hb_find_rsvd_mem_label(
                    Util::HBRT_MEM_LABEL_ATTR,
                    l_toc_ptr,
                    l_attr_data_size);

                uint64_t l_attr_offset = l_attr_data_addr - l_toc_ptr;
                l_phys_attr_data_addr = l_toc_addr + l_attr_offset;
                int l_rc = mm_block_unmap(l_toc_ptr);

                l_header = mm_block_map(
                    l_phys_attr_data_addr,
                    l_attr_data_size);
            }
            else
            {
                l_header = 0;
                l_phys_attr_data_addr = l_toc_addr;
                l_attr_data_size = sizeof(Util::hbrtTableOfContents_t);
            }
        }

        iv_sectionCount = l_header->numSections;
        iv_sections = new AttrRP_Section[iv_sectionCount];

        TargetingSection* l_section = nullptr;
        if(!iv_isMpipl)
        {
            l_section = reinterpret_cast<TargetingSection*>(
                l_pnorSectionInfo.vaddr + sizeof(TargetingHeader)
                + l_header->offsetToSections);
        }
        else
        {
            l_section = reinterpret_cast<TargetingSection*>(
                reinterpret_cast<uint64_t>(l_header)
                + sizeof(TargetingHeader) + l_header->offsetToSections);
        }
        uint64_t l_realMemOffset = 0;

        for (size_t i = 0; i < iv_sectionCount; i++, ++l_section)
        {
            iv_sections[i].type = l_section->sectionType;
            iv_sections[i].vmmAddress = static_cast<uint64_t>(
                TARG_TO_PLAT_PTR(l_header->vmmBaseAddress))
                + l_header->vmmSectionOffset*i;


            iv_sections[i].pnorAddress =
                l_pnorSectionInfo.vaddr + l_section->sectionOffset;

            #ifdef CONFIG_SECUREBOOT
            if(   l_pnorSectionInfo.secure
                && iv_sections[i].type == SECTION_TYPE_PNOR_RW)
            {
                iv_sections[i].pnorAddress -=
                    (VMM_VADDR_SPNOR_DELTA + VMM_VADDR_SPNOR_DELTA);
            }
            #endif

            if(iv_isMpipl)
            {
                iv_sections[i].realMemAddress = l_header + l_realMemOffset;
            }
            iv_sections[i].size = l_section->sectionSize;
            l_realMemOffset += iv_sections[i].size;

        }
}
uint32_t readScratchReg(uint64_t i_addr)
{
    size_t l_size = sizeof(uint64_t);
    uint64_t l_value = 0;

    deviceRead(
        TARGETING::MASTER_PROCESSOR_CHIP_TARGET_SENTINEL,
        &l_value, l_size,
        DEVICE_SCOM_ADDRESS(i_addr));

    return static_cast<uint32_t>(l_value >> 32);
}

errlHndl_t deviceRead(TARGETING::Target* i_target,
                          void* o_buffer, size_t& io_buflen,
                          AccessType i_accessType, ...)
{
    va_list args;
    va_start(args, i_accessType);
    Singleton<Associator>::instance().performOp(
            READ, i_target, o_buffer, io_buflen,
            i_accessType, args);

    va_end(args);
}

 errlHndl_t Associator::performOp(
    OperationType i_opType,
    Target* i_target,
    void* io_buffer, size_t& io_buflen,
    int64_t i_accessType, va_list i_addr)
{
    deviceOp_t l_devRoute = findDeviceRoute(i_opType, i_target->getAttr<ATTR_TYPE>(), i_accessType);
    (*l_devRoute)(i_opType, i_target, io_buffer, io_buflen, i_accessType, i_addr);
}

 deviceOp_t Associator::findDeviceRoute(
     OperationType i_opType,
     TARGETING::TYPE i_devType,
     int64_t i_accessType)
{
    // The ranges of the parameters should all be verified by the
    // compiler due to the template specializations in driverif.H.
    // e.g. i_accessType can never be negative
    // No assert-checks will be done here.

    // Function pointer found for this route request.
    deviceOp_t l_devRoute = NULL;
    const AssociationData* routeMap = iv_associations[iv_routeMap];
    const AssociationData* ops = iv_associations[routeMap[i_accessType].offset];

    // Check op type = WILDCARD registrations.
    if (0 != ops[WILDCARD].offset)
    {
        // Check access type = WILDCARD registrations.
        if (ops[WILDCARD].flag)
        {
            l_devRoute =
                iv_operations[
                    iv_associations[ops[WILDCARD].offset]->offset];
            break;
        }

        const AssociationData* targets = iv_associations[ops[WILDCARD].offset];
        if (targets[i_devType].flag)
        {
            l_devRoute = iv_operations[targets[i_devType].offset];
            break;
        }
    }

    if (0 != ops[i_opType].offset)
    {
        if(ops[i_opType].flag)
        {
            l_devRoute = iv_operations[iv_associations[ops[i_opType].offset]->offset];
            break;
        }

        const AssociationData* targets = iv_associations[ops[i_opType].offset];
        if (targets[i_devType].flag)
        {
            l_devRoute = iv_operations[targets[i_devType].offset];
            break;
        }
    }

    return l_devRoute;
}
