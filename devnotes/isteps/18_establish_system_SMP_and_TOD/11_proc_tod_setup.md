void TodSvc::todSetup()
{
    TodTopologyManager l_primary(); // NOOP
    iv_procTarget = TARGETING::getAllChips(l_targetList, TARGETING::TYPE_PROC, false)[0];
    TOD::buildTodDrawers();
    l_primary.create();
    p9_tod_setup();
    p9_tod_save_config();
    TOD::writeTodProcData();
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
    iv_tod_node_data->i_target = new fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>(const_cast<TARGETING::Target*>(iv_procTarget));
    iv_tod_node_data->i_tod_master = false;
    iv_tod_node_data->i_drawer_master = false;
    iv_tod_node_data->i_bus_rx = NONE;
    iv_tod_node_data->i_bus_tx = NONE;

    iv_xbusTargetList.clear();
    iv_abusTargetList.clear();

    TARGETING::PredicateCTM l_xbusCTM(TARGETING::CLASS_UNIT,TARGETING::TYPE_XBUS);
    TARGETING::PredicateCTM l_abusCTM(TARGETING::CLASS_UNIT,TARGETING::TYPE_ABUS);

    TARGETING::PredicatePostfixExpr l_funcAndXbusFilter;
    TARGETING::PredicatePostfixExpr l_funcAndAbusFilter;
    TARGETING::PredicateIsFunctional l_func;
    TARGETING::TargetHandleList l_xbusTargetList;
    TARGETING::TargetHandleList l_abusTargetList;
    l_funcAndXbusFilter.push(&l_xbusCTM).push(&l_func).And();
    l_funcAndAbusFilter.push(&l_abusCTM).push(&l_func).And();

    TARGETING::targetService().getAssociated(
        l_xbusTargetList,
        iv_procTarget,
        TARGETING::TargetService::CHILD,
        &l_funcAndXbusFilter);

    for(uint32_t l_index = 0 ; l_index < l_xbusTargetList.size(); ++l_index)
    {
        iv_xbusTargetList.push_back(l_xbusTargetList[l_index]);
    }

    TARGETING::targetService().getAssociated(
        l_abusTargetList,
        iv_procTarget,
        TARGETING::TargetService::CHILD,
        &l_funcAndAbusFilter);
    //Push the A bus targets found to the iv_abusTargetList
    for(uint32_t l_index = 0 ; l_index < l_abusTargetList.size(); ++l_index)
    {
        iv_abusTargetList.push_back(l_abusTargetList[l_index]);
    }
}

void TargetService::getAssociated(
          TargetHandleList&    o_list,
    const Target* const        i_pTarget,
    const ASSOCIATION_TYPE     i_type,
    const PredicateBase* const i_pPredicate) const
{
    o_list.clear();

    _getAssociationsViaDfs(o_list, i_pTarget, i_type, i_pPredicate);

    if (o_list.size() > 1)
    {
        std::sort(o_list.begin(), o_list.end(), compareTargetHuid);
    }
}

void TargetService::_getAssociationsViaDfs(
          TargetHandleList&    o_list,
    const Target* const        i_pSourceTarget,
    const ASSOCIATION_TYPE     i_type,
    const PredicateBase* const i_pPredicate) const
{
    AbstractPointer<Target>* pDestinationTargetItr =
        TARGETING::theAttrRP::instance().translateAddr(i_pSourceTarget->iv_ppAssociations[i_type], i_pSourceTarget);

    while(*pDestinationTargetItr)
    {
        if(!(*pDestinationTargetItr).TranslationEncoded.nodeId)
        {
            pDestinationTargetItr = TARGETING::theAttrRP::instance().translateAddr(
                pDestinationTargetItr,
                i_pSourceTarget);
        }
        else
        {
            pDestinationTargetItr = TARGETING::theAttrRP::instance().translateAddr(
                pDestinationTargetItr,
                (*pDestinationTargetItr).TranslationEncoded.nodeId - 1);
        }

        if(!i_pPredicate || (*i_pPredicate)(pDestinationTargetItr))
        {
            o_list.push_back(pDestinationTargetItr);
        }

        _getAssociationsViaDfs(
            o_list,
            pDestinationTargetItr,
            i_type,
            i_pPredicate);

        ++pDestinationTargetItr;
    }
}

void* AttrRP::translateAddr(
    void* i_pAddress,
    const Target* i_pTarget)
{
    void* o_pTransAddr = i_pAddress;
    if(i_pTarget != NULL)
    {
        NODE_ID l_nodeId;
        getNodeId(i_pTarget, l_nodeId);
        void* o_pTransAddr = i_pAddress & MASK_OFF_UPPER_BYTE;
        for (size_t i = 0; i < iv_nodeContainer[l_nodeId].sectionCount; ++i)
        {
            if ((iv_nodeContainer[l_nodeId].pSections[i].vmmAddress
                + iv_nodeContainer[l_nodeId].pSections[i].size)
                >= o_pTransAddr)
            {
                o_pTransAddr =
                        iv_nodeContainer[l_nodeId].pSections[i].pnorAddress
                        + o_pTransAddr
                        - iv_nodeContainer[l_nodeId].pSections[i].vmmAddress;
                break;
            }
        }
    }
    return o_pTransAddr;
}

static void AttrRP::getNodeId(const Target* i_pTarget, NODE_ID& o_nodeId) const
{
    o_nodeId = INVALID_NODE_ID;

    static std::map<const Target*,NODE_ID> s_targToNodeMap;
    auto l_nodeItr = s_targToNodeMap.find(i_pTarget);
    if(l_nodeItr != s_targToNodeMap.end())
    {
        o_nodeId = l_nodeItr->second;
        return;
    }

    for(uint8_t i = 0; i < INVALID_NODE_ID; ++i) // INVALID_NODE_ID = iv_nodeContainer.size()
    {
        for(uint32_t j = 0; j < iv_nodeContainer[i].sectionCount; ++j)
        {
            if(iv_nodeContainer[i].pSections[j].type == SECTION_TYPE_PNOR_RO
            && i_pTarget >= iv_nodeContainer[i].pTargetMap
            && i_pTarget < iv_nodeContainer[i].pTargetMap + iv_nodeContainer[i].pSections[j].size)
            {
                o_nodeId = i;
                s_targToNodeMap[i_pTarget] = i;
                return;
            }
        }
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
            if((*l_sourceItr)->iv_procTarget->getAttr<TARGETING::ATTR_HUID>() != (*l_sourceItr)->*l_targetItr->getTarget()->getAttr<TARGETING::ATTR_HUID>())
            {
                (*l_sourceItr)->iv_childrenList.push_back(*l_targetItr);
                (*l_sourceItr)->iv_tod_node_data->i_children.push_back(*l_targetItr->getTopologyNode());
            }
            l_targetItr = l_targetsList.erase(l_targetItr);
        }
    }
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

static void TodControls :: writeTodProcData()
{
    TARGETING::ATTR_ORDINAL_ID_type l_ordId;
    for(TodDrawerContainer::iterator l_itr = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
        l_itr != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end();
        ++l_itr)
    {
        const TodProcContainer& l_procs = (*l_itr)->iv_todProcList;
        for(TodProcContainer::const_iterator l_procItr = l_procs.begin();
            l_procItr != l_procs.end();
            ++l_procItr)
        {
            l_ordId = (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_ORDINAL_ID>();
            //Clear the default flag for this chip, defaults to
            //NON_FUNCTIONAL however this is a functional chip
            iv_todChipDataVector[l_ordId].header.flags = TOD_NONE;

            //Fill the todChipData structure at position l_ordId
            //inside iv_todChipDataVector with TOD register data
            //values
            (*l_procItr)->setTodChipData(iv_todChipDataVector[l_ordId]);
            //Set flags to indicate if the proc chip is an MDMT
            //See if the current proc chip is MDMT of the primary
            //topology
            if(iv_todConfig[TOD_PRIMARY].iv_isConfigured
            && iv_todConfig[TOD_PRIMARY].iv_mdmt
            && iv_todConfig[TOD_PRIMARY].iv_mdmt->getTarget()->getAttr<TARGETING::ATTR_HUID>()
               == (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
            {
                iv_todChipDataVector[l_ordId].header.flags |= TOD_PRI_MDMT;
            }
            (*l_procItr)->getTarget()->setAttr<ATTR_TOD_CPU_DATA>(iv_todChipDataVector[l_ordId]);
        }
    }
}

static void mpipl_clear_tod_node(tod_topology_node* i_tod_node)
{
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_PSS_MSS_CTRL_REG, 0x0ULL)
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_RX_TTYPE_CTRL_REG, PPC_BIT(5) | PPC_BIT(56));
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_TX_TTYPE_5_REG, PPC_BIT(PERV_TOD_TX_TTYPE_5_REG_TRIGGER));
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_LOAD_TOD_REG, PPC_BIT(63));
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_M_PATH_CTRL_REG, M_PATH_CTRL_REG_CLEAR_VALUE);
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_S_PATH_CTRL_REG, S_PATH_CTRL_REG_CLEAR_VALUE);
}

static void calculate_node_link_delay(
    tod_topology_node* i_tod_node,
    uint32_t& o_node_delay)
{
    uint64_t l_rt_delay_ctl_reg = 0;
    uint64_t l_bus_mode_reg = 0;
    uint32_t l_rt_delay_ctl_addr = 0;
    uint32_t l_bus_mode_addr = 0;
    uint32_t l_bus_mode_sel_even = 0;
    uint32_t l_bus_mode_sel_odd = 0;
    uint32_t l_bus_freq = 0;
    uint32_t l_bus_delay_even = 0;
    uint32_t l_bus_delay_odd = 0;

    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        o_node_delay = 0;
        i_tod_node->o_int_path_delay = 0;
        return;
    }

    switch (i_tod_node->i_bus_rx)
    {
        case (XBUS0):
            l_bus_freq = 16000;
            l_rt_delay_ctl_reg = PPC_BITMASK(0, 1);
            l_rt_delay_ctl_addr = PU_PB_ELINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_PB_ELINK_DLY_0123_REG;
            l_bus_mode_sel_even = 4;
            l_bus_mode_sel_odd = 20;
            break;

        case (XBUS1):
            l_bus_freq = 16000;
            l_rt_delay_ctl_reg = PPC_BITMASK(2, 3);
            l_rt_delay_ctl_addr = PU_PB_ELINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_PB_ELINK_DLY_0123_REG;
            l_bus_mode_sel_even = 36;
            l_bus_mode_sel_odd = 52;
            break;

        case (XBUS2):
            l_bus_freq = 16000;
            l_rt_delay_ctl_reg = PPC_BITMASK(4, 5);
            l_rt_delay_ctl_addr = PU_PB_ELINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_PB_ELINK_DLY_45_REG;
            l_bus_mode_sel_even = 4;
            l_bus_mode_sel_odd = 20;
            break;

        case (OBUS0):
            l_bus_freq = 102400;
            l_rt_delay_ctl_reg = PPC_BITMASK(0, 1);
            l_rt_delay_ctl_addr = PU_IOE_PB_OLINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_IOE_PB_OLINK_DLY_0123_REG;
            l_bus_mode_sel_even = 4;
            l_bus_mode_sel_odd = 20;
            break;

        case (OBUS1):
            l_bus_freq = 102400;
            l_rt_delay_ctl_reg = PPC_BITMASK(2, 3);
            l_rt_delay_ctl_addr = PU_IOE_PB_OLINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_IOE_PB_OLINK_DLY_0123_REG;
            l_bus_mode_sel_even = 36;
            l_bus_mode_sel_odd = 52;
            break;

        case (OBUS2):
            l_bus_freq = 102400;
            l_rt_delay_ctl_reg = PPC_BITMASK(4, 5);
            l_rt_delay_ctl_addr = PU_IOE_PB_OLINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_IOE_PB_OLINK_DLY_4567_REG;
            l_bus_mode_sel_even = 4;
            l_bus_mode_sel_odd = 20;
            break;

        case (OBUS3):
            l_bus_freq = 102400;
            l_rt_delay_ctl_reg = PPC_BITMASK(6, 7);
            l_rt_delay_ctl_addr = PU_IOE_PB_OLINK_RT_DELAY_CTL_REG;
            l_bus_mode_addr = PU_IOE_PB_OLINK_DLY_4567_REG;
            l_bus_mode_sel_even = 36;
            l_bus_mode_sel_odd = 52;
            break;
    }

    fapi2::putScom(*(i_tod_node->i_target), l_rt_delay_ctl_addr, l_rt_delay_ctl_reg);
    fapi2::getScom(*(i_tod_node->i_target), l_bus_mode_addr, l_bus_mode_reg);
    l_bus_mode_reg.extractToRight(l_bus_delay_even, l_bus_mode_sel_even, 12);
    l_bus_mode_reg.extractToRight(l_bus_delay_odd, l_bus_mode_sel_odd, 12);

    o_node_delay = (uint32_t)(((double)(l_bus_delay_even + l_bus_delay_odd) / 4 / (double)l_bus_freq  * 2500) + 1);
    i_tod_node->o_int_path_delay = o_node_delay;
}

TodProc* TodControls ::pickMdmt(
    const TodProc* i_otherConfigMdmt,
    const p9_tod_setup_tod_sel& i_config)
{
    TodProc* l_newMdmt = NULL;
    TodProc *l_pTodProc = NULL;
    TodDrawer* l_pMasterDrw = NULL;
    TodDrawerContainer l_todDrawerList = iv_todConfig[TOD_PRIMARY].iv_todDrawerList;
    TodProcContainer l_procList;
    uint32_t l_maxCoreCount;

    //1.MDMT will be chosen from a node other than the node on which
    //this MDMT exists, in case of multinode systems
    //Iterate the list of TOD drawers
    for (TodDrawerContainer::iterator l_todDrawerIter = l_todDrawerList.begin();
        l_todDrawerIter != l_todDrawerList.end();
        ++l_todDrawerIter)
    {
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
            != (*l_todDrawerIter)->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>())
        {
            l_pTodProc = NULL;
            l_procList.clear();
            //Get the list of procs on this TOD drawer that have oscillator
            //input. Each of them is a potential MDMT, choose the one with
            //max no. of cores
            uint32_t l_coreCount;
            (*l_todDrawerIter)->getPotentialMdmts(l_procList);
            (*l_todDrawerIter)->getProcWithMaxCores(
                NULL,
                l_pTodProc,
                l_coreCount,
                &l_procList);
            if(l_coreCount > l_maxCoreCount)
            {
                l_newMdmt = l_pTodProc;
                l_maxCoreCount = l_coreCount;
                l_pMasterDrw = *l_todDrawerIter;
            }
        }
    }
    if(l_newMdmt)
    {
         iv_todConfig[TOD_PRIMARY].iv_mdmt = l_newMdmt;
        l_newMdmt->iv_tod_node_data->i_drawer_master = true;
        l_newMdmt->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
        return l_newMdmt;
    }

    //2.Try to find MDMT on a TOD drawer that is on the same physical
    //node as the possible opposite MDMT but on different TOD drawer
    l_maxCoreCount = 0;
    for(const auto & l_todDrawerIter : l_todDrawerList)
    {
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
                == l_todDrawerIter->getParentNodeTarget()->getAttr<TARGETING::ATTR_HUID>()
            && iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->iv_todDrawerId
                != l_todDrawerIter->iv_todDrawerId)
        {
            l_pTodProc = NULL;
            l_coreCount = 0;
            l_procList.clear();
            //Get the list of procs on this TOD drawer that have oscillator
            //input. Each of them is a potential MDMT, choose the one with
            //max no. of cores
            l_todDrawerIter->getPotentialMdmts(l_procList);
            l_todDrawerIter->getProcWithMaxCores(NULL, l_pTodProc, l_coreCount, &l_procList);
            if(l_coreCount > l_maxCoreCount)
            {
                l_newMdmt = l_pTodProc;
                l_maxCoreCount = l_coreCount;
                l_pMasterDrw = l_todDrawerIter;
            }
        }
    }
    if(l_newMdmt)
    {
        iv_todConfig[TOD_PRIMARY].iv_mdmt = l_newMdmt;
        l_newMdmt->iv_tod_node_data->i_drawer_master = true;
        l_newMdmt->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
        return l_newMdmt;
    }

    //3.Try to find MDMT on the same TOD drawer as the TOD Drawer of
    //opposite MDMT
    l_maxCoreCount = 0;
    for (const auto l_todDrawerIter : l_todDrawerList)
    {
        l_pTodProc = NULL;
        l_coreCount = 0;
        l_procList.clear();
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->iv_todDrawerId == l_todDrawerIter->iv_todDrawerId)
        {
            //This  is the TOD drawer on which opposite MDMT exists,
            //try to avoid processor chip of opposite MDMT while
            //getting the proc with max cores
            //Get the list of procs on this TOD drawer that have oscillator
            //input. Each of them is a potential MDMT, choose the one with
            //max no. of cores
            l_todDrawerIter->getPotentialMdmts(l_procList);
            l_todDrawerIter->getProcWithMaxCores(
                iv_todConfig[l_oppConfig].iv_mdmt,
                l_pTodProc,
                l_coreCount,
                &l_procList);
            l_newMdmt = l_pTodProc;
            l_pMasterDrw = l_todDrawerIter;
            break;
        }
    }
    if(l_newMdmt)
    {
        iv_todConfig[TOD_PRIMARY].iv_mdmt = l_newMdmt;
        l_newMdmt->iv_tod_node_data->i_drawer_master = true;
        l_newMdmt->iv_tod_node_data->i_tod_master = true;
        l_pTodDrw->iv_isTodMaster = true;
        l_pMasterDrw = NULL;
    }
    return l_newMdmt;
}

static void TodControls ::pickMdmt()
{
    if(iv_todConfig[TOD_PRIMARY].iv_mdmt)
    {
        if(NULL == pickMdmt(iv_todConfig[l_oppConfig].iv_mdmt, iv_todConfig[l_oppConfig].iv_mdmt))
        {
            //Get the TodProc pointer to l_otherConfigMdmt from this
            //config's data structures.
            for (const auto & drawer: iv_todConfig[TOD_PRIMARY].iv_todDrawerList)
            {
                //This call will filter out GARDed/blacklisted chips
                TodProcContainer l_procList;
                drawer->getPotentialMdmts(l_procList);
                //Now we check if l_otherConfigMdmt is still good.
                for (const auto & proc: l_procList)
                {
                    if(proc->getTarget() == iv_todConfig[TOD_PRIMARY].iv_mdmt->getTarget())
                    {
                        setMdmt(TOD_PRIMARY, proc, drawer);
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

        //No MDMT configured yet. Our criteria to pick one is to
        //look at TOD drawers and pick the one with max no of cores

        for (const auto & l_drwItr: l_todDrawerList)
        {
            TodProc* l_pTodProc;
            uint32_t l_coreCount;
            uint32_t l_maxCoreCount;
            //Get the list of procs on this TOD drawer that have oscillator
            //input. Each of them is a potential MDMT, choose the one with
            //max no. of cores
            l_drwItr->getPotentialMdmts(l_procList);
            l_drwItr->getProcWithMaxCores(l_pTodProc, l_coreCount, &l_procList);
            if(l_coreCount > l_maxCoreCount)
            {
                l_maxCoreCount = l_coreCount;
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
    uint32_t& o_coreCount,
    TodProcContainer* i_pProcList) const
{
    o_pTodProc = NULL;
    o_coreCount = 0;

    TARGETING::PredicateCTM l_coreCTM(TARGETING::CLASS_UNIT,TARGETING::TYPE_CORE);

    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcCorePostfixExpr;
    l_funcCorePostfixExpr.push(&l_coreCTM).push(&l_funcPred).And();

    const TodProcContainer &l_procList = i_pProcList ? *i_pProcList : iv_todProcList;
    for(TodProcContainer::const_iterator l_procIter = l_procList.begin();
        l_procIter != l_procList.end();
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

static void TodDrawer::getPotentialMdmts(TodProcContainer& o_procList) const
{
    for(const auto & l_procItr : iv_todProcList)
    {
        const TARGETING::Target* l_procTarget = l_procItr->getTarget();
        TARGETING::ATTR_HWAS_STATE_type l_state = l_procTarget->getAttr<TARGETING::ATTR_HWAS_STATE>();

        if(iv_gardedTargets.end() == std::find(
            iv_gardedTargets.begin(),
            iv_gardedTargets.end(),
            l_procTarget->getAttr<TARGETING::ATTR_HUID>())
        || l_state.deconfiguredByEid == HWAS::DeconfigGard::CONFIGURED_BY_RESOURCE_RECOVERY)
        {
            o_procList.push_back(l_procItr);
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
    TodDrawerContainer::iterator l_todDrawerIter;

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

            for(l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
                l_todDrawerIter != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end();
                ++l_todDrawerIter)
            {
                if((*l_todDrawerIter)->iv_todDrawerId == l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>())
                {
                    TodProc *l_procPtr = new TodProc(l_funcProcTargetList[l_procIndex], (*l_todDrawerIter));
                    (*l_todDrawerIter)->iv_todProcList.push_back(l_procPtr);
                    b_foundDrawer = true;
                    break;
                }
            }

            if (!b_foundDrawer)
            {
                TodDrawer *l_pTodDrawer = new TodDrawer(
                    l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>(),
                    l_funcNodeTargetList[l_nodeIndex]);
                TodProc *l_pTodProc = new TodProc(l_funcProcTargetList[l_procIndex], l_pTodDrawer);
                l_pTodDrawer->iv_todProcList.push_back(l_pTodProc);
                iv_todConfig[TOD_PRIMARY].iv_todDrawerList.push_back(l_pTodDrawer);
            }
        }
    }
}

static void TodControls::buildGardedTargetsList()
{
    iv_gardedTargets.clear();

    GardedUnitList_t l_gardedUnitList;
    gardGetGardedUnits(l_gardedUnitList);

    for(const auto & l_iter : l_gardedUnitList)
    {
        iv_gardedTargets.push_back(l_iter.iv_huid);
    }
    iv_gardListInitialized = true;
}

static void DeconfigGard::platGetGardRecords(GardRecords_t &o_records)
{
    o_records.clear();

    iv_platDeconfigGard = malloc(sizeof(HBDeconfigGard));
    iv_platDeconfigGard->iv_pGardRecords = iv_TOC[PNOR::GUARD_DATA].virtAddr;
    iv_platDeconfigGard->iv_maxGardRecords = iv_TOC[PNOR::GUARD_DATA].size / sizeof(DeconfigGard::GardRecord);
    iv_platDeconfigGard->iv_nextGardRecordId = 0;

    for (uint32_t i = 0; i < iv_platDeconfigGard->iv_maxGardRecords; i++)
    {
        if(iv_platDeconfigGard->iv_pGardRecords[i].iv_recordId != EMPTY_GARD_RECORDID
        && iv_platDeconfigGard->iv_pGardRecords[i].iv_targetId == 0)
        {
            o_records.push_back(iv_platDeconfigGard->iv_pGardRecords[i]);
            break;
        }
    }
}

static void TodControls::gardGetGardedUnits(GardedUnitList_t &o_gardedUnitList)
{
    o_gardedUnitList.clear();
    HWAS::DeconfigGard::GardRecords_t l_gardRecords;
    platGetGardRecords(l_gardRecords);

    for(const auto & gardRecord : l_gardRecords)
    {
        TARGETING::Target * l_pTarget;

        l_pTarget = TARGETING::targetService().toTarget(i_path);

        GardedUnit_t l_gardedUnit = 0;
        l_gardedUnit.iv_huid = l_pTarget->getAttr<TARGETING::ATTR_HUID>();
        TARGETING::Target *l_pNodeTarget = NULL;
        if(l_pTarget->getAttr<TARGETING::ATTR_CLASS>() == TARGETING::CLASS_ENC
        && l_pTarget->getAttr<TARGETING::ATTR_TYPE>() == TARGETING::TYPE_NODE)
        {
            l_pNodeTarget = l_pTarget;
        }
        else
        {
            getParent(l_pTarget, TARGETING::CLASS_ENC, l_pNodeTarget);
        }
        l_gardedUnit.iv_nodeHuid = l_pNodeTarget->getAttr<TARGETING::ATTR_HUID>();
        l_gardedUnit.iv_errlogId = gardRecord.iv_errlogEid;
        l_gardedUnit.iv_errType = static_cast<HWAS::GARD_ErrorType>(gardRecord.iv_errorType);
        l_gardedUnit.iv_domain = l_pTarget->getAttr<TARGETING::ATTR_CDM_DOMAIN>();
        l_gardedUnit.iv_type = l_pTarget->getAttr<TARGETING::ATTR_TYPE>();
        l_gardedUnit.iv_class = l_pTarget->getAttr<TARGETING::ATTR_CLASS>();

        o_gardedUnitList.push_back(l_gardedUnit);
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
        mpipl_clear_tod_node(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data);
    }
    uint32_t l_node_delay = 0;
    uint32_t l_current_longest_delay = 0;

    calculate_node_link_delay(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data, l_node_delay);
    iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay = l_node_delay - iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay;
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
          | PPC_BIT(37) | PPC_BIT(39));
    }
    uint64_t l_port_ctrl_reg;
    uint64_t l_port_ctrl_check_reg;
    uint32_t l_port_rx_select_val;
    uint32_t l_path_sel;

    fapi2::getScom(*(i_tiv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_dataod_node->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    fapi2::getScom(*(i_tiv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_dataod_node->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, l_port_ctrl_check_reg);

    switch (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_bus_rx)
    {
        case (XBUS0):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X0_SEL;
            break;
        case (XBUS1):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X1_SEL;
            break;
        case (XBUS2):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X2_SEL;
            break;
        case (OBUS0):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X3_SEL;
            break;
        case (OBUS1):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X4_SEL;
            break;
        case (OBUS2):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X5_SEL;
            break;
        case (OBUS3):
            l_port_rx_select_val = TOD_PORT_CTRL_REG_RX_X6_SEL;
            break;
    }

    l_port_ctrl_reg.insertFromRight<0, 3>(l_port_rx_select_val);
    l_port_ctrl_check_reg.insertFromRight<0, 3>(l_port_rx_select_val);

    if (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
    {
        l_path_sel = TOD_PORT_CTRL_REG_M_PATH_0;
    }
    else
    {
        l_path_sel = TOD_PORT_CTRL_REG_S_PATH_0;
    }

    for (auto l_child = (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_children).begin();
         l_child != (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_children).end();
         ++l_child)
    {
        switch ((*l_child)->i_bus_tx)
        {
            case (XBUS0):
                l_port_ctrl_reg.insertFromRight<4, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<20>();
                l_port_ctrl_check_reg.insertFromRight<4, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<20>();
                break;
            case (XBUS1):
                l_port_ctrl_reg.insertFromRight<6, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<21>();
                l_port_ctrl_check_reg.insertFromRight<6, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<21>();
                break;
            case (XBUS2):
                l_port_ctrl_reg.insertFromRight<8, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<22>();
                l_port_ctrl_check_reg.insertFromRight<8, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<22>();
                break;
            case (OBUS0):
                l_port_ctrl_reg.insertFromRight<10, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<23>();
                l_port_ctrl_check_reg.insertFromRight<10, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<23>();
                break;
            case (OBUS1):
                l_port_ctrl_reg.insertFromRight<12, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<24>();
                l_port_ctrl_check_reg.insertFromRight<12, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<24>();
                break;
            case (OBUS2):
                l_port_ctrl_reg.insertFromRight<14, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<25>();
                l_port_ctrl_check_reg.insertFromRight<14, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<25>();
                break;
            case (OBUS3):
                l_port_ctrl_reg.insertFromRight<16, 2>(l_path_sel);
                l_port_ctrl_reg.setBit<26>();
                l_port_ctrl_check_reg.insertFromRight<16, 2>(l_path_sel);
                l_port_ctrl_check_reg.setBit<26>();
                break;
        }i_tod_node
    }
    fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    fapi2::putScom(*(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, l_port_ctrl_check_reg);

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
    if (iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
    {
        l_m_path_ctrl_reg &=i_tod_node
            & ~PPC_BITMASK(5, 7)
            & ~PPC_BITMASK(9, 11)
            & ~PPC_BITMASK(24, 25)
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
        ~PPC_BITMASK(0, 1) & ~PPC_BITMASK(6, 7) & ~PPC_BIT(13),
        PPC_BITMASK(8, 11) | PPC_BITMASK(14, 15));
    scom_and_or_for_chiplet(
        *(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_target),
        PERV_TOD_CHIP_CTRL_REG,
        ~PPC_BITMASK(1, 3) & ~PPC_BIT(4) & ~PPC_BITMASK(7, 9) & ~PPC_BIT(30),
        PPC_BITMASK(10, 15));
}
