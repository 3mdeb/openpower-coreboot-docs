```cpp
void TodSvc::todSetup()
{
    TodTopologyManager l_primary(); // NOOP
    iv_proc0 = TARGETING::getAllChips(l_targetList, TARGETING::TYPE_PROC, false)[0];
    TOD::buildTodDrawers();         // assgn drawers to iv_todConfig[TOD_PRIMARY].iv_todDrawerList
    l_primary.create();
    p9_tod_setup();                 // some scom's
    p9_tod_save_config();           // some scom's
}

// for each functional prcessor in each node (childs of SystemTarget)
// If Drawer is found just assign TodProc to iv_todConfig[TOD_PRIMARY].iv_todDrawerList and exit
// If Drawer for this processor does not exist, create a new one, assign, append to a list and continue
static void TodControls::buildTodDrawers()
{
    TARGETING::TargetHandleList l_funcNodeTargetList;
    TARGETING::Target* l_pSysTarget;
    TARGETING::targetService().getTopLevelTarget(l_pSysTarget);
    TOD::TodSvcUtil::getFuncNodeTargetsOnSystem(l_pSysTarget, l_funcNodeTargetList, true); // get functional childs of SystemTarget
    TARGETING::TargetHandleList l_funcProcTargetList;
    TARGETING::PredicateCTM l_procCTM(TARGETING::CLASS_CHIP,TARGETING::TYPE_PROC);
    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcProcPostfixExpr;
    l_funcProcPostfixExpr.push(&l_procCTM).push(&l_funcPred).And();

    for(uint32_t l_nodeIndex = 0; l_nodeIndex < l_funcNodeTargetList.size(); ++l_nodeIndex)
    {
        TARGETING::targetService().getAssociated(
            l_funcProcTargetList,               // child processors are returned here
            l_funcNodeTargetList[l_nodeIndex],  // node to get children from
            TARGETING::TargetService::CHILD,    // get the children
            &l_funcProcPostfixExpr);            // only functional processors

        for(uint32_t l_procIndex = 0; l_procIndex < l_funcProcTargetList.size(); ++l_procIndex)
        {
            for(TodDrawerContainer::iterator l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
                l_todDrawerIter != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end(); ++l_todDrawerIter)
            if((*l_todDrawerIter)->iv_todDrawerId == l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>())
            {
                (*l_todDrawerIter)->iv_todProcList.push_back(new TodProc(l_funcProcTargetList[l_procIndex], (*l_todDrawerIter)));
                return;
            }
            // TodDrawer constructor just initializes iv_todDrawerId, iv_parentNodeTarget and iv_isTodMaster(false)
            TodDrawer *l_pTodDrawer = new TodDrawer(
                l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>(),
                l_funcNodeTargetList[l_nodeIndex]);
            l_pTodDrawer->iv_todProcList.push_back(new TodProc(l_funcProcTargetList[l_procIndex], l_pTodDrawer));
            iv_todConfig[TOD_PRIMARY].iv_todDrawerList.push_back(l_pTodDrawer);
        }
    }
}

static void getFuncNodeTargetsOnSystem(
    TARGETING::ConstTargetHandle_t sysTarget,
    TARGETING::TargetHandleList& o_nodeList,
    const bool i_skipFuncCheck)
{
    o_nodeList.clear();
    TARGETING::PredicateCTM l_isNode(TARGETING::CLASS_ENC, TARGETING::TYPE_NODE);

    TARGETING::targetService().getAssociated(
        o_nodeList,
        sysTarget,
        TARGETING::TargetService::CHILD,
        static_cast<TARGETING::PredicateBase*>(&l_isNode);
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

static void TodTopologyManager::create()
{
    TOD::pickMdmt(); // select and assign MDMT processor to the iv_todConfig[TOD_PRIMARY].iv_mdmt
    TodProcContainer l_targetsList = iv_todConfig[TOD_PRIMARY].iv_todDrawerList[0]->iv_todProcList;
    TodProc* l_pDrawerMaster = NULL;
    iv_todConfig[TOD_PRIMARY].iv_todDrawerList[0]->findMasterProc(l_pDrawerMaster);
    TodProcContainer l_sourcesList;
    // push TOD_MASTER or DRAWER_MASTER proc as first
    l_sourcesList.push_back(l_pDrawerMaster);

    // At the start, sources contain only masterProc
    // in the loops bellow it is filled with more
    for(TodProcContainer::iterator l_sourceItr = l_sourcesList.begin(); l_sourcesList.end() != l_sourceItr; ++l_sourceItr)
    for(TodProcContainer::iterator l_targetItr = l_targetsList.begin(); l_targetItr != l_targetsList.end(); l_targetItr = l_targetsList.erase(l_targetItr))
    {
        l_sourcesList.push_back(*l_targetItr);
        if((*l_sourceItr)->iv_proc0->getAttr<TARGETING::ATTR_HUID>()
        != (*l_sourceItr)->(*l_targetItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
        {
            (*l_sourceItr)->iv_childrenList.push_back(*l_targetItr);
            (*l_sourceItr)->iv_tod_node_data->i_children.push_back(*l_targetItr->getTopologyNode());
        }
    }
}

// Find and configure correct processor to be MDMT
// amd assign it to the iv_todConfig[TOD_PRIMARY].iv_mdmt
static void TodControls ::pickMdmt()
{
    // if we alreade have MDMT (probably not)
    // but it is somehow not configured to be pickable
    // then find it in a Drawer and configure properly
    if(iv_todConfig[TOD_PRIMARY].iv_mdmt)
    {
        if(NULL == pickMdmt(iv_todConfig[l_oppConfig].iv_mdmt, iv_todConfig[l_oppConfig].iv_mdmt))
        for (const auto & drawer: iv_todConfig[TOD_PRIMARY].iv_todDrawerList)
        {
            // iterate over processors
            for (const auto & proc: iv_todProcList)
            // check if this proc in this drawer is previously picked MDMT
            if(proc->getTarget() == iv_todConfig[TOD_PRIMARY].iv_mdmt->getTarget())
            {
                // set this drawer and processor as master
                iv_todConfig[TOD_PRIMARY].iv_mdmt = proc;
                proc->setMasterType(TodProc::TOD_MASTER);
                drawer->setMasterDrawer(true);
                return;
            }
        }
    }
    // iterate over all Drawers to find processor with most cores
    // and set it as a new MDMT
    else
    {
        TodDrawer* l_pTodDrw = NULL;
        TodProc* l_newMdmt = NULL;
        TodProc* l_pTodProc;
        uint32_t l_maxCoreCount = 0;
        // iterate over Drawers
        for (const auto & l_drwItr: l_todDrawerList)
        {
            // make sure that the core count is the largest in all Drawers
            uint32_t l_coreCount;
            l_drwItr->getProcWithMaxCores(l_pTodProc, l_coreCount);
            if(l_coreCount > l_maxCoreCount)
            {
                l_maxCoreCount = l_coreCount;
                l_pTodDrw = l_drwItr;
                l_newMdmt = l_pTodProc;
            }
        }

        // if found any, assign new MDMT
        if(l_newMdmt)
        {
            iv_todConfig[TOD_PRIMARY].iv_mdmt = l_newMdmt;
            l_newMdmt->iv_tod_node_data->i_drawer_master = true;
            l_newMdmt->iv_tod_node_data->i_tod_master = true;
            l_pTodDrw->iv_isTodMaster = true;
        }
    }
}

TodProc* TodControls::pickMdmt(
    const TodProc* i_otherConfigMdmt,
    const p9_tod_setup_tod_sel& i_config)
{
    TodProc* l_pTodProc = NULL;
    TodDrawer* l_pMasterDrw = NULL;
    uint32_t l_coreCount;

    // FIRST PART
    // iterate over todDrawerList (why no for-range syntax?)
    for(TodDrawerContainer::iterator l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
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

    // SECOND PART
    // iterate over const references (no modification allowed to l_todDrawerList)
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

    // THIRD PART
    // iterate over local copies
    for(const auto l_todDrawerIter : l_todDrawerList)
    {
        l_pTodProc = NULL;
        if(iv_todConfig[l_oppConfig].iv_mdmt->getParentDrawer()->iv_todDrawerId == l_todDrawerIter->iv_todDrawerId)
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

// iterates over iv_todProcList and returns TOD or DRAWER proc master
static void TodDrawer::findMasterProc(TodProc*& i_pDrawerMaster) const
{
    i_pDrawerMaster = NULL;
    for(TodProcContainer::const_iterator l_procIter = iv_todProcList.begin();
        l_procIter != iv_todProcList.end();
        ++l_procIter)
    {
        if((*l_procIter)->iv_masterType == TodProc::TOD_MASTER
        || (*l_procIter)->iv_masterType == TodProc::DRAWER_MASTER)
        {
            i_pDrawerMaster = *l_procIter;
            return;
        }
    }
}

static void TodDrawer::getProcWithMaxCores(
    TodProc*& o_pTodProc,
    uint32_t& o_coreCount) const
{
    o_pTodProc = NULL;
    o_coreCount = 0;

    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcCorePostfixExpr;
    TARGETING::PredicateCTM l_coreCTM(TARGETING::CLASS_UNIT, TARGETING::TYPE_CORE);
    l_funcCorePostfixExpr.push(&l_coreCTM).push(&l_funcPred).And();

    for(TodProcContainer::const_iterator l_procIter = iv_todProcList.begin(); l_procIter != iv_todProcList.end(); ++l_procIter)
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

static void p9_tod_setup(void)
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

    if(iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_tod_master
    && iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->i_drawer_master)
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
```
