```cpp
void TodSvc::todSetup()
{
    TodTopologyManager l_primary(); // NOOP
    iv_proc0 = TARGETING::getAllChips(l_targetList, TARGETING::TYPE_PROC, false)[0];
    // this function saves information about targets for later scom access
    TOD::buildTodDrawers();         // assgn drawers to iv_todConfig[TOD_PRIMARY].iv_todDrawerList
    // connections are not used
    // l_primary.create();
    TOD::pickMdmt();
    p9_tod_setup();                 // some scom's
    p9_tod_save_config();           // some scom's
}

// for each functional prcessor in each node (childs of SystemTarget)
// If Drawer is found just assign TodProc to iv_todConfig[TOD_PRIMARY].iv_todDrawerList
//    where iv_todProcList is initialized with TodProc's and exit
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
        // returns <id>p9_proc_s</id>
        TARGETING::targetService().getAssociated(
            l_funcProcTargetList,               // child processors are returned here
            l_funcNodeTargetList[l_nodeIndex],  // node to get children from
            TARGETING::TargetService::CHILD,    // get all of the children
            TARGETING::TargetService::ALL,
            &l_funcProcPostfixExpr);            // only functional processors

        for(uint32_t l_procIndex = 0; l_procIndex < l_funcProcTargetList.size(); ++l_procIndex)
        {
            // for each available drawer check if it's id is equal to node id and if it is push TodProc to it
            for(TodDrawerContainer::iterator l_todDrawerIter = iv_todConfig[TOD_PRIMARY].iv_todDrawerList.begin();
                l_todDrawerIter != iv_todConfig[TOD_PRIMARY].iv_todDrawerList.end(); ++l_todDrawerIter)
            if((*l_todDrawerIter)->iv_todDrawerId == l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>())
            {
                // first arg to TodProc (i_procTarget) is later used for scom access
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
        TARGETING::TargetService::IMMEDIATE,
        static_cast<TARGETING::PredicateBase*>(&l_isNode);
}

struct tod_topology_node
{
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>* i_target;
    bool i_tod_master;
    bool i_drawer_master;
    p9_tod_setup_bus i_bus_rx; // unused
    p9_tod_setup_bus i_bus_tx; // unused
    std::list<tod_topology_node*> i_children;
    p9_tod_setup_conf_regs o_todRegs;
    uint32_t o_int_path_delay;
};

static void TodTopologyManager::create()
{
    TOD::pickMdmt();    // select and assign MDMT processor to the iv_todConfig[TOD_PRIMARY].iv_mdmt
                        // proc with max amount of cores will be our MDMT

    // connects each processor with each other but not used anywhere
    // This is removed from analysis, but this could also create TOD tree
    // in case, multiple processors are enabled
    // (look at hostboot code src/usr/isteps/tod/TodTopologyManager.C:TodTopologyManager::wireTodDrawer)
}

// Find and configure correct processor to be MDMT
// amd assign it to the iv_todConfig[TOD_PRIMARY].iv_mdmt
static void TodControls ::pickMdmt()
{
    // iterate over all Drawers to find processor with most cores
    // and set it as a new MDMT
    TodDrawer* l_pTodDrw = NULL;
    TodProc* l_newMdmt = NULL;
    TodProc* l_pTodProc;
    uint32_t l_maxCoreCount = 0;
    // find proc with max cores in all drawers
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

// just get the processor with max amount of cores
void TodDrawer::getProcWithMaxCores(
                TodProc*& o_pTodProc,
                uint32_t& o_coreCount) const
{
    o_pTodProc =  NULL;
    o_coreCount = 0;

    //List of functional cores
    TARGETING::TargetHandleList l_funcCoreTargetList;
    TARGETING::PredicateCTM
        l_coreCTM(TARGETING::CLASS_UNIT,TARGETING::TYPE_CORE);

    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcCorePostfixExpr;
    l_funcCorePostfixExpr.push(&l_coreCTM).push(&l_funcPred).And();

    TodProc* l_pSelectedTarget = NULL;
    uint32_t l_maxCores = 0;

    const TodProcContainer &l_procList = iv_todProcList;
    for(TodProcContainer::const_iterator l_procIter = l_procList.begin(); l_procIter != l_procList.end(); ++l_procIter)
    {
        l_funcCoreTargetList.clear();
        //Find the funcational core targets on this proc
        TARGETING::targetService().getAssociated(l_funcCoreTargetList,
                (*l_procIter)->getTarget(),
                TARGETING::TargetService::CHILD,
                TARGETING::TargetService::ALL,
                &l_funcCorePostfixExpr);

        if (l_funcCoreTargetList.size() > l_maxCores)
        {
            l_pSelectedTarget = *l_procIter;
            l_maxCores = l_funcCoreTargetList.size();
        }
    }
    if (l_maxCores > 0)
    {
        o_pTodProc = l_pSelectedTarget;
        o_coreCount = l_maxCores;
    }
}

static void calculate_node_delays(tod_topology_node* i_tod_node)
{
    uint32_t l_longest_delay;
    calculate_longest_topolopy_delay(i_tod_node, l_longest_delay);
    i_tod_node->o_int_path_delay = l_longest_delay - i_tod_node->o_int_path_delay;
    i_tod_node->o_int_path_delay += MDMT_TOD_GRID_CYCLE_STAGING_DELAY;
}

static void calculate_longest_topolopy_delay(
    tod_topology_node* i_tod_node,
    uint32_t& o_longest_delay)
{
    uint32_t l_node_delay = 0;
    uint32_t l_current_longest_delay = 0;
    calculate_node_link_delay(i_tod_node, l_node_delay);
    o_longest_delay = l_node_delay + l_current_longest_delay;
}

static void calculate_node_link_delay(
    tod_topology_node* i_tod_node,
    uint32_t& o_node_delay)
{
    // MDMT is originator and therefore has no node delay
    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        o_node_delay = 0;
        i_tod_node->o_int_path_delay = o_node_delay;
        return;
    }

    uint64_t l_rt_delay_ctl_reg = 0;
    uint64_t l_bus_mode_reg;
    uint32_t l_bus_delay_even;
    uint32_t l_bus_delay_odd;

    l_rt_delay_ctl_reg.setBit<PB_ELINK_RT_DELAY_CTL_SET_LINK_2>().setBit<PB_ELINK_RT_DELAY_CTL_SET_LINK_3>();
    fapi2::putScom(*(i_tod_node->i_target), PU_PB_ELINK_RT_DELAY_CTL_REG, l_rt_delay_ctl_reg);
    fapi2::getScom(*(i_tod_node->i_target), PU_PB_ELINK_DLY_0123_REG, l_bus_mode_reg);
    l_bus_mode_reg.extractToRight(l_bus_delay_even, PU_PB_ELINK_DLY_0123_REG_FMR2_LINK_DELAY, PB_EOLINK_DLY_FMR_LINK_DELAY_LEN);
    l_bus_mode_reg.extractToRight(l_bus_delay_odd, PU_PB_ELINK_DLY_0123_REG_FMR3_LINK_DELAY, PB_EOLINK_DLY_FMR_LINK_DELAY_LEN);

    // By default, the TOD grid runs at 400ps; TOD counts its delay based on this
    // Example: Bus round trip delay is 35 cycles and the bus is running at 4800MHz
    //            - Divide by 2 to get one-way delay time
    //            - Divide by 4800 * 10^6 to get delay in seconds
    //            - Multiply by 10^12 to get delay in picoseconds
    //            - Divide by 400ps to get TOD-grid-cycles
    // This is not the final internal path delay, only saved so two calls aren't needed to calculate_node_link_delay
    i_tod_node->o_int_path_delay = (uint32_t)(((double)(l_bus_delay_even + l_bus_delay_odd) / 4 / (double)FREQ_X_MHZ  * 8000000 / TOD_GRID_PS) + 1);
}

static void p9_tod_setup(void)
{
    calculate_node_delays(MDMT_node);
    iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay =
        MDMT_TOD_GRID_CYCLE_STAGING_DELAY
      - iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data->o_int_path_delay;
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
