void TodSvc::todSetup()
{
    errlHndl_t l_errHdl = NULL;
    bool l_isTodRunning = false;
    TodTopologyManager l_primary();
    l_primary.iv_topologyType = TOD_PRIMARY;
    TOD::destroy(TOD_PRIMARY);
    TOD::destroy(TOD_SECONDARY);
    //Build the list of garded TOD targets
    TOD::buildGardedTargetsList();
    //Build a set of datastructures to setup creation of the TOD topology
    //We're going to setup TOD for this IPL
    //1) Build a set of datastructures to setup creation of the TOD
    //topologies.
    TOD::buildTodDrawers(TOD_PRIMARY);
    l_primary.iv_topologyType = TOD_PRIMARY
    //2) Ask the topology manager to setup the primary topology
    l_primary.create();
    l_primary.dumpTopology();
    //3) Call hardware procedures to configure the TOD hardware logic for
    //the primary topology and to fill up the TOD regs.
    todSetupHwp(TOD_PRIMARY);
    todSaveRegsHwp(TOD_PRIMARY);
    //Primary successfully configured
    TOD::setConfigStatus(TOD_PRIMARY,true);
    //Build datastructures for secondary topology
    TOD::buildTodDrawers(TOD_SECONDARY);
    //4) Ask the topology manager to setup the secondary topology
    TodTopologyManager l_secondary();
    l_secondary.iv_topologyType = TOD_SECONDARY;
    l_secondary.create();
    l_secondary.dumpTopology();
    //5) Call hardware procedures to configure the TOD hardware logic for
    //the secondary topology and to fill up the TOD regs.
    todSetupHwp(TOD_SECONDARY);
    //Secondary successfully configured
    TOD::setConfigStatus(TOD_SECONDARY,true);
    //Need to call this again if the secondary topology got set up,
    //that would have updated more regs.
    todSaveRegsHwp(TOD_PRIMARY);
    //Done with TOD setup

    l_primary.dumpTodRegs();
    //If we are here then atleast Primary or both configurations were
    //successfully setup. If both were successfuly setup then we can use
    //writeTodProcData for either of them else we should call
    //writeTodProcData for only primary.
    //Ultimately it should be good enough to call the method for Primary
    TOD::writeTodProcData(TOD_PRIMARY);
    TOD::clearGardedTargetsList();
}

void clearGardedTargetsList()
{
    iv_gardedTargets.clear();
    iv_gardListInitialized = false;
}

errlHndl_t TodControls :: writeTodProcData(
        const p9_tod_setup_tod_sel i_config)
{
    //As per the requirement specified by PHYP/HDAT, TOD needs to fill
    //data for every chip that can be installed on the system.
    //It is also required that chip ID match the index of the entry in the
    //array so we can possibly have valid chip data at different indexes in
    //the array and the intermittent locations filled with the chip entries
    //that does not exist on the system. All such entires will have default
    //non-significant values

    TodChipData blank;
    uint32_t l_maxProcCount = TOD::TodSvcUtil::getMaxProcsOnSystem();
    Target* l_target = NULL;
    iv_todChipDataVector.assign(l_maxProcCount,blank);

    TARGETING::ATTR_ORDINAL_ID_type l_ordId = 0x0;
    //Ordinal Id of the processors that form the topology

    //Fill the TodChipData structures with the actual value in the
    //ordinal order
    for(TodDrawerContainer::iterator l_itr =
            iv_todConfig[i_config].iv_todDrawerList.begin();
            l_itr != iv_todConfig[i_config].iv_todDrawerList.end();
            ++l_itr)
    {
        const TodProcContainer&  l_procs =
            (*l_itr)->getProcs();

        for(TodProcContainer::const_iterator
                l_procItr = l_procs.begin();
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
            (*l_procItr )->setTodChipData(iv_todChipDataVector[l_ordId]);
            //Set flags to indicate if the proc chip is an MDMT
            //See if the current proc chip is MDMT of the primary
            //topology
            if(getConfigStatus(TOD_PRIMARY) && getMDMT(TOD_PRIMARY))
            {
                if (getMDMT(TOD_PRIMARY)->getTarget()->getAttr<TARGETING::ATTR_HUID>()
                     == (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
                {
                    iv_todChipDataVector[l_ordId].header.flags |= TOD_PRI_MDMT;
                }
            }
            //See if the current proc chip is MDMT of the secondary
            //network
            //Note: The chip can be theoretically both primary and
            //secondary MDMDT
            if (getConfigStatus(TOD_SECONDARY) && getMDMT(TOD_SECONDARY))
            {
                if (getMDMT(TOD_SECONDARY)->getTarget()->getAttr<TARGETING::ATTR_HUID>()
                    == (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
                {
                    iv_todChipDataVector[l_ordId].header.flags |= TOD_SEC_MDMT;
                }
            }

            ATTR_TOD_CPU_DATA_type l_tod_array;
            memcpy(l_tod_array, &iv_todChipDataVector[l_ordId],sizeof(TodChipData));
            l_target = const_cast<Target *>((*l_procItr)->getTarget());
            l_target->setAttr<ATTR_TOD_CPU_DATA>(l_tod_array);
        }
    }
}

void setConfigStatus(const p9_tod_setup_tod_sel i_config, const bool i_isConfigured )
{
    iv_todConfig[i_config].iv_isConfigured = i_isConfigured;
}

errlHndl_t todSetupHwp(const p9_tod_setup_tod_sel i_topologyType)
{

    errlHndl_t l_errHdl = NULL;

    //Get the MDMT
    TodProc* l_pMDMT = TOD::getMDMT(i_topologyType);
    p9_tod_setup_osc_sel l_selectedOsc = TOD_OSC_0;

    //Invoke the HWP by passing the topology tree (rooted at MDMT)
    FAPI_INVOKE_HWP(l_errHdl,
            p9_tod_setup,
            l_pMDMT->getTopologyNode(),
            i_topologyType,
            l_selectedOsc);
}

fapi2::ReturnCode p9_tod_setup(
    tod_topology_node* i_tod_node,
    const p9_tod_setup_tod_sel i_tod_sel,
    const p9_tod_setup_osc_sel i_osc_sel)
{
    fapi2::ATTR_IS_MPIPL_Type l_is_mpipl = 0x00;

    FAPI_ATTR_GET(fapi2::ATTR_IS_MPIPL, fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>(), l_is_mpipl);

    if (l_is_mpipl && ( i_tod_sel == TOD_PRIMARY))
    {
        // Put the TOD in reset state, and clear the register
        // PERV_TOD_PSS_MSS_CTRL_REG, we do it before the primary
        // topology is configured and not repeat it to prevent overwriting
        // the configuration.
        mpipl_clear_tod_node(i_tod_node, i_tod_sel);
    }

    // Start configuring each node
    // configure_tod_node will recurse on each child
    calculate_node_delays(i_tod_node);

    display_tod_nodes(i_tod_node, 0);

    // If there is a previous topology, it needs to be cleared
    clear_tod_node(i_tod_node, i_tod_sel, l_is_mpipl);

    // Start configuring each node
    // configure_tod_node will recurse on each child
    configure_tod_node(i_tod_node, i_tod_sel, i_osc_sel);

fapi_try_exit:
    return fapi2::current_err;
}

void TodTopologyManager::dumpTodRegs() const
{

    static const char* topologynames[2] = {0};
    topologynames[TOD_PRIMARY] = TOD_PRIMARY_TOPOLOGY;
    topologynames[TOD_SECONDARY] = TOD_SECONDARY_TOPOLOGY;

    fapi2::variable_buffer l_regData(64);
    //Get the TOD drawers
    TodDrawerContainer l_todDrwList;
    TOD::getDrawers(iv_topologyType, l_todDrwList);
    TodDrawerContainer::const_iterator l_drwItr = l_todDrwList.begin();
    while(l_todDrwList.end() != l_drwItr)
    {
        //Get the procs on this drawer
        TodProcContainer l_procList = (*l_drwItr)->getProcs();
        TodProcContainer::const_iterator l_procItr =
                                                        l_procList.begin();
        while(l_procList.end() != l_procItr)
        {
            p9_tod_setup_conf_regs l_todRegs;
            (*l_procItr)->getTodRegs(l_todRegs);
            l_regData.set(l_todRegs.tod_m_path_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_pri_port_0_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_pri_port_1_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_sec_port_0_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_sec_port_1_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_s_path_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_i_path_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_pss_mss_ctrl_reg(), 0);
            l_regData.set(l_todRegs.tod_chip_ctrl_reg(), 0);
            ++l_procItr;
        }
        ++l_drwItr;
    }
}

void TodTopologyManager::dumpTopology() const
{

    static const char* busnames[BUS_MAX+1] = {0};
    busnames[NONE] = NO_BUS;
    busnames[XBUS0] = X_BUS_0;
    busnames[XBUS1] = X_BUS_1;
    busnames[XBUS2] = X_BUS_2;

    static const char* topologynames[2] = {0};
    topologynames[TOD_PRIMARY] = TOD_PRIMARY_TOPOLOGY;
    topologynames[TOD_SECONDARY] = TOD_SECONDARY_TOPOLOGY;

    //Get the TOD drawers
    TodDrawerContainer l_todDrwList;
    TOD::getDrawers(iv_topologyType, l_todDrwList);
    TodDrawerContainer::const_iterator l_drwItr = l_todDrwList.begin();
    while(l_todDrwList.end() != l_drwItr)
    {
        //Get the procs on this drawer
        TodProcContainer l_procList = (*l_drwItr)->getProcs();

        TodProcContainer::const_iterator l_procItr = l_procList.begin();
        //FIX_ME_BEFORE_PRODUCTION_TASK32
        //bool l_ecmdTargetFound = false;
        while(l_procList.end() != l_procItr)
        {


            TodProcContainer l_childList;
            (*l_procItr)->getChildren(l_childList);
            TodProcContainer::const_iterator l_childItr = l_childList.begin();
            while(l_childList.end() != l_childItr)
            {
                ++l_childItr;
            }
            ++l_procItr;
        }
        ++l_drwItr;
    }
}

errlHndl_t TodControls ::pickMdmt(const p9_tod_setup_tod_sel i_config)
{

   TOD_ENTER("Input config is 0x%.2X", i_config);

   errlHndl_t l_errHdl = NULL;

   //MDMT is the master processor that drives TOD signals to all the remaining
   //processors on the system, as such wherever possible algorithm will try to
   //ensure that primary and secondary topology provides redundancy of MDMT.

    p9_tod_setup_tod_sel l_oppConfig =
        (i_config == TOD_PRIMARY) ? TOD_SECONDARY : TOD_PRIMARY;
    TodProc* l_otherConfigMdmt = iv_todConfig[l_oppConfig].iv_mdmt;
    TodDrawerContainer l_todDrawerList =
        iv_todConfig[i_config].iv_todDrawerList;
    TodProcContainer l_procList;

    if(l_otherConfigMdmt)
    {
        TOD_INF("MDMT(0x%.8X) is configured for the "
                "opposite config(0x%.2X)",
                GETHUID(l_otherConfigMdmt->getTarget()),
                l_oppConfig);
        if(NULL == pickMdmt(l_otherConfigMdmt, i_config))
        {
            TOD_INF("For config 0x%.2X, the only option for the MDMT "
                    "is the MDMT(0x%.8X) chosen for "
                    "the opposite config 0x%.2X",
                    i_config,
                    GETHUID(l_otherConfigMdmt->getTarget()),
                    l_oppConfig);

            //Get the TodProc pointer to l_otherConfigMdmt from this
            //config's data structures.
            for (const auto & l_drwItr: l_todDrawerList)
            {
                //This call will filter out GARDed/blacklisted chips
                l_drwItr->getPotentialMdmts(l_procList);
                //Now we check if l_otherConfigMdmt is still good.
                for (const auto & l_procItr: l_procList)
                {
                    if(l_procItr->getTarget() == iv_todConfig[l_oppConfig].iv_mdmt->getTarget())
                    {
                        //Found l_otherConfigMdmt pointer
                        setMdmt(i_config, l_procItr, l_drwItr);
                        break;
                    }
                }
                if(iv_todConfig[i_config].iv_mdmt)
                {
                    break;
                }
            }
        }
    }
    else
    {
        uint32_t l_coreCount = 0;
        uint32_t l_maxCoreCount = 0;
        TodProc* l_newMdmt = NULL;
        TodProc* l_pTodProc = NULL;
        TodDrawer* l_pTodDrw = NULL;

        //No MDMT configured yet. Our criteria to pick one is to
        //look at TOD drawers and pick the one with max no of cores

        for (const auto & l_drwItr: l_todDrawerList)
        {
            l_pTodProc = NULL;
            l_coreCount = 0;
            //Get the list of procs on this TOD drawer that have oscillator
            //input. Each of them is a potential MDMT, choose the one with
            //max no. of cores
            l_drwItr->getPotentialMdmts(l_procList);
            l_drwItr->getProcWithMaxCores(NULL, l_pTodProc, l_coreCount, &l_procList);
            if(l_coreCount > l_maxCoreCount)
            {
                l_maxCoreCount = l_coreCount;
                l_pTodDrw = l_drwItr;
                l_newMdmt = l_pTodProc;
            }
        }

        if(l_newMdmt)
        {
            // If new MDMT, we set the todConfig
            setMdmt(i_config, l_newMdmt, l_pTodDrw);
        }
    }
    return l_errHdl;
}

void TodDrawer::getPotentialMdmts(
        TodProcContainer& o_procList) const
{
    TOD_ENTER("TodDrawer::getPotentialMdmts");
    bool l_isGARDed = false;
    errlHndl_t l_errHdl = NULL;

    const TARGETING::Target* l_procTarget = NULL;

    for(const auto & l_procItr : iv_todProcList)
    {

        l_procTarget = l_procItr->getTarget();

        //Check if the target is not black listed
        if ( !(TOD::isProcBlackListed(l_procTarget)) )
        {
            //Check if the target is not garded
            l_errHdl = TOD::checkGardStatusOfTarget(l_procTarget,
                        l_isGARDed);


            TARGETING::ATTR_HWAS_STATE_type l_state =
                l_procTarget->getAttr<TARGETING::ATTR_HWAS_STATE>();

            if(!l_isGARDed
            || l_state.deconfiguredByEid == HWAS::DeconfigGard::CONFIGURED_BY_RESOURCE_RECOVERY)
            {
                o_procList.push_back(l_procItr);
            }
        }
        l_isGARDed = false;
    }
}

void TodControls::checkGardStatusOfTarget(
        TARGETING::ConstTargetHandle_t i_target,
        bool&  o_isTargetGarded )
{
    o_isTargetGarded =
        iv_gardListInitialized
        && iv_gardedTargets.end() != std::find(
            iv_gardedTargets.begin(),
            iv_gardedTargets.end(),
            (GETHUID(i_target)))
}

void getDrawers(const p9_tod_setup_tod_sel i_config,
                    TodDrawerContainer& o_drawerList) const
{
    o_drawerList = iv_todConfig[i_config].iv_todDrawerList;
}

void TodTopologyManager::create()
{
    //The topology creation algorithm goes as follows :
    //1)Pick the MDMT.
    //2)In the master TOD drawer (the one in which MDMT lies),
    //wire the procs together.
    //3)Connect the MDMT to one processor in each of the slave TOD drawers
    //(the TOD drawers other than the master TOD drawer)
    //4)Wire the procs in the slave TOD drawers.
    //1) Pick the MDMT.
    TOD::pickMdmt(iv_topologyType);

    //Get the TOD drawers
    TodDrawerContainer l_todDrwList;
    TOD::getDrawers(iv_topologyType, l_todDrwList);
    //Find the TOD system master drawer (the one in which the MDMT lies)
    TodDrawer* l_pMasterDrawer = NULL;
    for(TodDrawerContainer::const_iterator l_itr = l_todDrwList.begin();
        l_itr != l_todDrwList.end();
        ++l_itr)
    {
        if((*l_itr)->isMaster())
        {
            l_pMasterDrawer = *l_itr;
            break;
        }
    }

    //2)In the master TOD drawer, wire the procs together.
    wireProcs(l_pMasterDrawer);

    //3)Connect the MDMT to one processor in each of the TOD drawers
    //other than the master TOD drawer.
    for(TodDrawerContainer::iterator l_itr = l_todDrwList.begin();
        l_itr != l_todDrwList.end();
        ++l_itr)
    {
        if((*l_itr)->isMaster())
        {
            //This is the master TOD drawer, we are connecting other
            //TOD drawers to this.
            continue;
        }
        wireTodDrawer(*l_itr);
    }

    //4)Wire the procs in the other TOD drawers (i.e other than the master)
    for(TodDrawerContainer::const_iterator l_itr =
        l_todDrwList.begin();
        l_itr != l_todDrwList.end();
        ++l_itr)
    {
        if((*l_itr)->isMaster())
        {
            //We've done this already for the master TOD drawer
            continue;
        }
        wireProcs(*l_itr);
    }
}

errlHndl_t TodTopologyManager::wireTodDrawer(TodDrawer* i_pTodDrawer)
{

    //The algorithm to wire the slave TOD drawers to the mater TOD
    //drawer (to the MDMT to be specific) goes as follows :
    /*
    For each slave TOD drawer "d"
        For each proc "p" in d
            If MDMT connects p via A bus
                we are done, exit
    */

    //Get the MDMT
    TodProc* l_pMDMT = TOD::getMDMT(iv_topologyType);

    //Get the procs in this TOD drawer
    TodProcContainer l_procs = i_pTodDrawer->getProcs();

    //Find a proc which connects to the MDMT via A bus
    bool l_connected = false;
    TodProcContainer::iterator l_itr;
    for(l_itr = l_procs.begin();
        l_itr != l_procs.end();
        ++l_itr)
    {
        l_pMDMT->connect(*l_itr, TARGETING::TYPE_ABUS, l_connected);
        if(l_connected)
        {
            //Found a proc, designate this as the SDMT for this TOD drawer.
            l_pMDMT->addChild(*l_itr);
            (*l_itr)->setMasterType(TodProc::DRAWER_MASTER);
            break;
        }
    }
}

errlHndl_t TodTopologyManager::wireProcs(const TodDrawer* i_pTodDrawer)
{
    //The algorithm to wire procs in a TOD drawer goes as follows :
    /*
    Have a "sources" list which initially has only the drawer master
    Have a "targets" list which has all the other procs.
    While the sources list isn't empty, for each source "s" in sources
        For each target "t" in targets
            If s connects t via X bus
                Remove t from targets and add it to sources,
                since it's now a potential source
    If targets isn't empty, we couldn't wire one or more procs
    */

    //Get the targets
    TodProcContainer l_targetsList = i_pTodDrawer->getProcs();

    //Push the drawer master onto the sources list
    TodProc* l_pDrawerMaster = NULL;
    l_errHdl = i_pTodDrawer->findMasterProc(l_pDrawerMaster);
    TodProcContainer l_sourcesList;
    l_sourcesList.push_back(l_pDrawerMaster);

    //Start connecting targets to sources
    TodProcContainer::iterator l_sourceItr = l_sourcesList.begin();
    TodProcContainer::iterator l_targetItr;
    while(NULL == l_errHdl && l_sourcesList.end() != l_sourceItr)
    {
        for(l_targetItr = l_targetsList.begin();
            l_targetItr != l_targetsList.end();)
        {
            bool l_connected = false;
            (*l_sourceItr)->connect(*l_targetItr, TARGETING::TYPE_XBUS, l_connected);
            if(l_connected)
            {
                //Prefer push_back to push_front since in case of multiple
                //X bus path alternatives, the paths and hence the TOD
                //delays will be shorter.
                l_sourcesList.push_back(*l_targetItr);
                (*l_sourceItr)->addChild(*l_targetItr);
                l_targetItr = l_targetsList.erase(l_targetItr);
            }
            else
            {
                ++l_targetItr;
            }
        }
        ++l_sourceItr;
    }
}

void TodControls::buildTodDrawers(const p9_tod_setup_tod_sel i_config)
{
    errlHndl_t l_errHdl = NULL;

    TARGETING::TargetHandleList l_funcNodeTargetList;

    //Get the system pointer
    TARGETING::Target* l_pSysTarget = NULL;
    (void)TARGETING::targetService().getTopLevelTarget(l_pSysTarget);

    //Build the list of functional nodes
    TOD::TodSvcUtil::getFuncNodeTargetsOnSystem(l_pSysTarget, l_funcNodeTargetList, true);

    //For each node target find the prcessor chip on it
    TARGETING::TargetHandleList l_funcProcTargetList;
    TARGETING::PredicateCTM l_procCTM(TARGETING::CLASS_CHIP,TARGETING::TYPE_PROC);

    TARGETING::PredicateHwas l_funcPred;
    l_funcPred.functional(true);
    TARGETING::PredicatePostfixExpr l_funcProcPostfixExpr;
    l_funcProcPostfixExpr.push(&l_procCTM).push(&l_funcPred).And();

    TodDrawerContainer& l_todDrawerList = iv_todConfig[i_config].iv_todDrawerList;
    TodDrawerContainer::iterator l_todDrawerIter;

    for(uint32_t l_nodeIndex = 0;
        l_nodeIndex < l_funcNodeTargetList.size();
        ++l_nodeIndex)
    {
        l_funcProcTargetList.clear();

        //Find the funcational Proc targets on the system
        TARGETING::targetService().getAssociated(
            l_funcProcTargetList,
            l_funcNodeTargetList[l_nodeIndex],
            TARGETING::TargetService::CHILD,
            TARGETING::TargetService::ALL,
            &l_funcProcPostfixExpr);

        //Go over the list of procs and insert them in respective TOD drawer
        for(uint32_t l_procIndex = 0;
            l_procIndex < l_funcProcTargetList.size();
            ++l_procIndex)
        {
            bool b_foundDrawer = false;

            //Get the ordinal id of the parent node and find if there is an
            //existing TOD drawer whose id matches with the ordinal id of
            //this node
            for(l_todDrawerIter = l_todDrawerList.begin();
                l_todDrawerIter != l_todDrawerList.end();
                ++l_todDrawerIter)
            {
                if((*l_todDrawerIter)->getId() ==
                        l_funcNodeTargetList[l_nodeIndex]->
                        getAttr<TARGETING::ATTR_ORDINAL_ID>())
                {
                    //Add the proc to this TOD drawer, such that
                    //TodProc has the target pointer and the pointer to
                    //the TOD drawer to which it belongs
                    TodProc *l_procPtr = new TodProc(l_funcProcTargetList[l_procIndex], (*l_todDrawerIter));
                    (*l_todDrawerIter)->addProc(l_procPtr);
                    l_procPtr = NULL;
                    b_foundDrawer = true;
                    break;
                }

            }

            if (!b_foundDrawer)
            {
                //Create a new TOD drawer and add it to the TOD drawer list
                //Create a TOD drawer with the drawer id same as parent
                //node's id , and the pointer to the node target to which
                //the TOD drawer belongs
                TodDrawer *l_pTodDrawer = new TodDrawer(
                    l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>(),
                    l_funcNodeTargetList[l_nodeIndex]);

                //Create a TodProc passing the target pointer and the
                //pointer of TodDrawer to which this processor belongs
                TodProc *l_pTodProc = new TodProc(l_funcProcTargetList[l_procIndex], l_pTodDrawer);

                //push the processor ( TodProc ) , into the TOD drawer
                l_pTodDrawer->addProc(l_pTodProc);

                //push the Tod drawer ( TodDrawer ) , into the
                //TodControls
                l_todDrawerList.push_back(l_pTodDrawer);
                //Delete the pointers after transfering the ownership
                l_pTodDrawer = NULL;
                l_pTodProc = NULL;
            }
        }
    }
}

void TodControls ::destroy(const p9_tod_setup_tod_sel i_config)
{
    for(TodDrawerContainer::iterator l_itr = iv_todConfig[i_config].iv_todDrawerList.begin();
        l_itr != iv_todConfig[i_config].iv_todDrawerList.end();
        ++l_itr)
    {
        if(*l_itr)
        {
            delete (*l_itr);
        }
    }
    iv_todConfig[i_config].iv_todDrawerList.clear();
    iv_todConfig[i_config].iv_mdmt = NULL;
    iv_todConfig[i_config].iv_isConfigured = false;
    iv_todChipDataVector.clear();
    iv_BlackListedProcs.clear();
    iv_gardedTargets.clear();
}

void TodControls::buildGardedTargetsList()
{
    errlHndl_t l_errHdl = NULL;
    iv_gardListInitialized = false;
    clearGardedTargetsList();

    TARGETING::Target* l_pSystemTarget = NULL;
    TARGETING::targetService().getTopLevelTarget(l_pSystemTarget);
    GardedUnitList_t l_gardedUnitList;
    gardGetGardedUnits(l_pSystemTarget,l_gardedUnitList);

    for(const auto & l_iter : l_gardedUnitList)
    {
        //Push the HUID to the set of garded targets
        iv_gardedTargets.push_back(l_iter.iv_huid);
    }
    iv_gardListInitialized = true;
}

void TodControls::gardGetGardedUnits(
    const TARGETING::Target* const i_pTarget,
    GardedUnitList_t &o_gardedUnitList)
{
    o_gardedUnitList.clear();
    HWAS::DeconfigGard::GardRecords_t l_gardRecords;
    HWAS::theDeconfigGard().getGardRecords(i_pTarget, l_gardRecords);

    for(const auto & l_iter : l_gardRecords)
    {
        TARGETING::Target * l_pTarget = NULL;

        // getTargetFromPhysicalPath will either succeed or assert
        getTargetFromPhysicalPath(l_iter.iv_targetId, l_pTarget);

        GardedUnit_t l_gardedUnit;
        memset(&l_gardedUnit, 0, sizeof(GardedUnit_t));
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
        l_gardedUnit.iv_errlogId = l_iter.iv_errlogEid;
        l_gardedUnit.iv_errType = static_cast<HWAS::GARD_ErrorType>(l_iter.iv_errorType);
        l_gardedUnit.iv_domain = l_pTarget->getAttr<TARGETING::ATTR_CDM_DOMAIN>();
        l_gardedUnit.iv_type = l_pTarget->getAttr<TARGETING::ATTR_TYPE>();
        l_gardedUnit.iv_class = l_pTarget->getAttr<TARGETING::ATTR_CLASS>();

        o_gardedUnitList.push_back(l_gardedUnit);
    }
}
