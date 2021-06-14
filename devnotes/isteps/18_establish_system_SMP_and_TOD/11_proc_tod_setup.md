void TodSvc::todSetup()
{
    TodTopologyManager l_primary();
    TOD::buildGardedTargetsList();
    TOD::buildTodDrawers(TOD_PRIMARY);
    l_primary.create();
    todSetupHwp(TOD_PRIMARY);
    todSaveRegsHwp(TOD_PRIMARY);
    iv_todConfig[TOD_PRIMARY].iv_isConfigured = true;
    todSaveRegsHwp(TOD_PRIMARY);
    TOD::writeTodProcData(TOD_PRIMARY);
}

const TodProcContainer& getProcs() const
{
    return iv_todProcList;
}

static void TodControls :: writeTodProcData(const p9_tod_setup_tod_sel i_config)
{
    //As per the requirement specified by PHYP/HDAT, TOD needs to fill
    //data for every chip that can be installed on the system.
    //It is also required that chip ID match the index of the entry in the
    //array so we can possibly have valid chip data at different indexes in
    //the array and the intermittent locations filled with the chip entries
    //that does not exist on the system. All such entires will have default
    //non-significant values

    TARGETING::ATTR_ORDINAL_ID_type l_ordId = 0;
    //Ordinal Id of the processors that form the topology

    //Fill the TodChipData structures with the actual value in the
    //ordinal order
    for(TodDrawerContainer::iterator l_itr =
        iv_todConfig[i_config].iv_todDrawerList.begin();
        l_itr != iv_todConfig[i_config].iv_todDrawerList.end();
        ++l_itr)
    {
        const TodProcContainer& l_procs = (*l_itr)->getProcs();

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
            if(getConfigStatus(TOD_PRIMARY)
            && iv_todConfig[TOD_PRIMARY].iv_mdmt
            && iv_todConfig[TOD_PRIMARY].iv_mdmt->getTarget()->getAttr<TARGETING::ATTR_HUID>()
                == (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
            {
                iv_todChipDataVector[l_ordId].header.flags |= TOD_PRI_MDMT;
            }
            //See if the current proc chip is MDMT of the secondary
            //network
            //Note: The chip can be theoretically both primary and
            //secondary MDMDT
            if(getConfigStatus(TOD_SECONDARY)
            && iv_todConfig[TOD_SECONDARY].iv_mdmt
            && iv_todConfig[TOD_SECONDARY].iv_mdmt->getTarget()->getAttr<TARGETING::ATTR_HUID>()
                == (*l_procItr)->getTarget()->getAttr<TARGETING::ATTR_HUID>())
            {
                iv_todChipDataVector[l_ordId].header.flags |= TOD_SEC_MDMT;
            }

            ATTR_TOD_CPU_DATA_type l_tod_array;
            memcpy(l_tod_array, &iv_todChipDataVector[l_ordId],sizeof(TodChipData));
            Target* l_target = const_cast<Target *>((*l_procItr)->getTarget());
            l_target->setAttr<ATTR_TOD_CPU_DATA>(l_tod_array);
        }
    }
}

static void todSetupHwp(const p9_tod_setup_tod_sel i_topologyType)
{
    //Get the MDMT
    TodProc* l_pMDMT = iv_todConfig[i_topologyType].iv_mdmt;
    p9_tod_setup(l_pMDMT->getTopologyNode(), i_topologyType);
}

tod_topology_node* getTopologyNode()
{
    return iv_tod_node_data;
}

static void p9_tod_setup(
    tod_topology_node* i_tod_node,
    const p9_tod_setup_tod_sel i_tod_sel)
{
    fapi2::ATTR_IS_MPIPL_Type l_is_mpipl;
    FAPI_ATTR_GET(fapi2::ATTR_IS_MPIPL, fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>(), l_is_mpipl);

    if (l_is_mpipl)
    {
        // Put the TOD in reset state, and clear the register
        // PERV_TOD_PSS_MSS_CTRL_REG, we do it before the primary
        // topology is configured and not repeat it to prevent overwriting
        // the configuration.
        mpipl_clear_tod_node(i_tod_node);
    }
    calculate_node_delays(i_tod_node);
    clear_tod_node(i_tod_node, i_tod_sel, l_is_mpipl);
    configure_tod_node(i_tod_node, i_tod_sel);
}

static void configure_tod_node(tod_topology_node* i_tod_node)
{
    configure_pss_mss_ctrl_reg(i_tod_node);
    if (!(i_tod_node->i_tod_master && i_tod_node->i_drawer_master))
    {
        configure_s_path_ctrl_reg(i_tod_node);
    }
    configure_port_ctrl_regs(i_tod_node);
    configure_m_path_ctrl_reg(i_tod_node);
    configure_i_path_ctrl_reg(i_tod_node);
    scom_and_or_for_chiplet(
        *(i_tod_node->i_target),
        PERV_TOD_CHIP_CTRL_REG,
        ~PPC_BITMASK(1, 3) & ~PPC_BIT(4) & ~PPC_BITMASK(7, 9) & ~PPC_BIT(30),
        PPC_BITMASK(10, 15));

    // TOD is configured for this node; if it has children, start their
    // configuration
    for (auto l_child = (i_tod_node->i_children).begin();
         l_child != (i_tod_node->i_children).end();
         ++l_child)
    {
        configure_tod_node(*l_child);
    }
}

static void configure_m_path_ctrl_reg(tod_topology_node* i_tod_node)
{
    uint64_t l_m_path_ctrl_reg;
    uint64_t l_root_ctrl8_reg;

    fapi2::getScom(*(i_tod_node->i_target), PERV_ROOT_CTRL8_SCOM, l_root_ctrl8_reg);
    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_M_PATH_CTRL_REG, l_m_path_ctrl_reg);
    if(l_root_ctrl8_reg & PPC_BIT(21))
    {
        l_m_path_ctrl_reg |= PPC_BIT(4);
    }
    else
    {
        l_m_path_ctrl_reg &= ~PPC_BIT(4);
    }
    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        l_m_path_ctrl_reg &=
            ~PPC_BIT(0)
            & ~PPC_BIT(2)
            & ~PPC_BIT(13)
            & ~PPC_BITMASK(5, 7)
            & ~PPC_BITMASK(9, 11)
            & ~PPC_BITMASK(24, 25)
            | PPC_BIT(1)
            | PPC_BIT(8)
            | PPC_BITMASK(14, 15);
    }
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_M_PATH_CTRL_REG, l_m_path_ctrl_reg);
}

static void configure_port_ctrl_regs(tod_topology_node* i_tod_node)
{
    uint64_t l_port_ctrl_reg;
    uint64_t l_port_ctrl_check_reg;
    uint32_t l_port_rx_select_val;
    uint32_t l_path_sel;

    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, l_port_ctrl_check_reg);

    switch (i_tod_node->i_bus_rx)
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

    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        l_path_sel = TOD_PORT_CTRL_REG_M_PATH_0;
    }
    else
    {
        l_path_sel = TOD_PORT_CTRL_REG_S_PATH_0;
    }

    for (auto l_child = (i_tod_node->i_children).begin();
         l_child != (i_tod_node->i_children).end();
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
        }
    }
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_SEC_PORT_0_CTRL_REG, l_port_ctrl_check_reg);
}

static void configure_s_path_ctrl_reg(tod_topology_node* i_tod_node)
{
    scom_and_or_for_chiplet(
        *(i_tod_node->i_target),
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
      | PPC_BIT(37) | PPC_BIT(39))
}

static void configure_pss_mss_ctrl_reg(tod_topology_node* i_tod_node)
{
    uint64_t l_pss_mss_ctrl_reg;
    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_PSS_MSS_CTRL_REG, l_pss_mss_ctrl_reg);

    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        l_pss_mss_ctrl_reg.setBit<1>();
        l_pss_mss_ctrl_reg.clearBit<0>();
    }
    else
    {
        l_pss_mss_ctrl_reg.clearBit<1>();
    }

    if (i_tod_node->i_drawer_master)
    {
        l_pss_mss_ctrl_reg.setBit<2>();
    }
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_PSS_MSS_CTRL_REG, l_pss_mss_ctrl_reg);
}

static void clear_tod_node(
    tod_topology_node* i_tod_node,
    const p9_tod_setup_tod_sel i_tod_sel,
    const fapi2::ATTR_IS_MPIPL_Type i_is_mpipl)
{
    uint32_t l_port_ctrl_reg;
    uint32_t l_port_ctrl_check_reg;

    if (i_tod_sel == TOD_PRIMARY)
    {
        l_port_ctrl_reg = PERV_TOD_PRI_PORT_0_CTRL_REG;
        l_port_ctrl_check_reg = PERV_TOD_SEC_PORT_0_CTRL_REG;
    }
    else // (i_tod_sel==TOD_SECONDARY)
    {
        l_port_ctrl_reg = PERV_TOD_SEC_PORT_1_CTRL_REG;
        l_port_ctrl_check_reg = PERV_TOD_PRI_PORT_1_CTRL_REG;
    }

    fapi2::putScom(*(i_tod_node->i_target), l_port_ctrl_reg, 0);
    fapi2::putScom(*(i_tod_node->i_target), l_port_ctrl_check_reg, 0);

    if (i_tod_sel == TOD_PRIMARY)
    {
        // Workaround for HW480181: Init remote sync checker tolerance to maximum;
        // will be closed down by configure_tod_node later.
        fapi2::buffer<uint64_t> l_data;
        fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_S_PATH_CTRL_REG, l_data);
        l_data.insertFromRight<PERV_TOD_S_PATH_CTRL_REG_REMOTE_SYNC_CHECK_CPS_DEVIATION,
                               PERV_TOD_S_PATH_CTRL_REG_REMOTE_SYNC_CHECK_CPS_DEVIATION_LEN>
                               (STEP_CHECK_CPS_DEVIATION_93_75_PCENT);
        l_data.insertFromRight<PERV_TOD_S_PATH_CTRL_REG_REMOTE_SYNC_CHECK_CPS_DEVIATION_FACTOR,
                               PERV_TOD_S_PATH_CTRL_REG_REMOTE_SYNC_CHECK_CPS_DEVIATION_FACTOR_LEN>
                               (STEP_CHECK_CPS_DEVIATION_FACTOR_8);
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_S_PATH_CTRL_REG, l_data);
    }


    // TOD is cleared for this node; if it has children, start clearing
    // their registers
    for (auto l_child = (i_tod_node->i_children).begin();
         l_child != (i_tod_node->i_children).end();
         ++l_child)
    {
        clear_tod_node(*l_child, i_tod_sel, i_is_mpipl);
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


    for(auto l_child = (i_tod_node->i_children).begin();
        l_child != (i_tod_node->i_children).end();
        ++l_child)
    {
        mpipl_clear_tod_node(*l_child);
    }
}

static void calculate_node_delays(tod_topology_node* i_tod_node)
{
    uint32_t l_longest_delay = 0;

    // Find the most-delayed path in the topology; this is the MDMT's delay
    calculate_longest_topolopy_delay(i_tod_node, l_longest_delay);
    set_topology_delays(i_tod_node, l_longest_delay);

    // Finally, the MDMT delay must include additional TOD-grid-cycles to
    // account for staging latches in slaves
    i_tod_node->o_int_path_delay += MDMT_TOD_GRID_CYCLE_STAGING_DELAY;
}

static void calculate_longest_topolopy_delay(
    tod_topology_node* i_tod_node,
    uint32_t& o_longest_delay)
{
    uint32_t l_node_delay = 0;
    uint32_t l_current_longest_delay = 0;

    calculate_node_link_delay(i_tod_node, l_node_delay);
    o_longest_delay = l_node_delay;

    for (auto l_child = (i_tod_node->i_children).begin();
         l_child != (i_tod_node->i_children).end();
         ++l_child)
    {
        tod_topology_node* l_tod_node = *l_child;
        calculate_longest_topolopy_delay(l_tod_node, l_node_delay);
        if (l_node_delay > l_current_longest_delay)
        {
            l_current_longest_delay = l_node_delay;
        }
    }
    o_longest_delay += l_current_longest_delay;
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
        i_tod_node->o_int_path_delay = o_node_delay;
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

static void configure_i_path_ctrl_reg(tod_topology_node* i_tod_node)
{
    uint64_t l_port_ctrl_reg;

    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);
    l_port_ctrl_reg.insertFromRight<32, 8>(i_tod_node->o_int_path_delay);
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_PRI_PORT_0_CTRL_REG, l_port_ctrl_reg);

    scom_and_or_for_chiplet(
        *(i_tod_node->i_target),
        PERV_TOD_I_PATH_CTRL_REG,
        ~PPC_BITMASK(0, 1) & ~PPC_BITMASK(6, 7) & ~PPC_BIT(13),
        PPC_BITMASK(8, 11) | PPC_BITMASK(14, 15));
}

static void set_topology_delays(
    tod_topology_node* i_tod_node,
    const uint32_t i_longest_delay)
{
    // Retrieve saved node_delay from calculate_node_link_delay instead of
    // making a second call
    i_tod_node->o_int_path_delay = i_longest_delay - i_tod_node->o_int_path_delay;

    // Recurse on downstream nodes
    for (auto l_child = (i_tod_node->i_children).begin();
         l_child != (i_tod_node->i_children).end();
         ++l_child)
    {
        set_topology_delays(*l_child, i_tod_node->o_int_path_delay);
    }
}

static void TodControls ::pickMdmt(const p9_tod_setup_tod_sel i_config)
{
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
        if(NULL == pickMdmt(l_otherConfigMdmt, i_config))
        {

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
}

static void TodDrawer::getPotentialMdmts(TodProcContainer& o_procList) const
{
    const TARGETING::Target* l_procTarget = NULL;

    for(const auto & l_procItr : iv_todProcList)
    {

        l_procTarget = l_procItr->getTarget();

        //Check if the target is not black listed
        if (!TOD::isProcBlackListed(l_procTarget))
        {
            //Check if the target is not garded
            TARGETING::ATTR_HWAS_STATE_type l_state = l_procTarget->getAttr<TARGETING::ATTR_HWAS_STATE>();

            if(!(iv_gardedTargets.end() != std::find(
                    iv_gardedTargets.begin(),
                    iv_gardedTargets.end(),
                    GETHUID(l_procTarget)))
            || l_state.deconfiguredByEid == HWAS::DeconfigGard::CONFIGURED_BY_RESOURCE_RECOVERY)
            {
                o_procList.push_back(l_procItr);
            }
        }
    }
}

static void TodTopologyManager::create()
{
    //The topology creation algorithm goes as follows :
    //1)Pick the MDMT.
    //2)In the master TOD drawer (the one in which MDMT lies),
    //wire the procs together.
    //3)Connect the MDMT to one processor in each of the slave TOD drawers
    //(the TOD drawers other than the master TOD drawer)
    //4)Wire the procs in the slave TOD drawers.
    //1) Pick the MDMT.
    TOD::pickMdmt(TOD_PRIMARY);

    //Get the TOD drawers
    TodDrawerContainer l_todDrwList;
    l_todDrwList = iv_todConfig[TOD_PRIMARY].iv_todDrawerList;
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
    for(TodDrawerContainer::const_iterator l_itr = l_todDrwList.begin();
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

static void TodTopologyManager::wireTodDrawer(TodDrawer* i_pTodDrawer)
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
    TodProc* l_pMDMT = iv_todConfig[TOD_PRIMARY].iv_mdmt;

    //Get the procs in this TOD drawer
    TodProcContainer l_procs = i_pTodDrawer->getProcs();

    //Find a proc which connects to the MDMT via A bus
    for(TodProcContainer::iterator l_itr = l_procs.begin();
        l_itr != l_procs.end();
        ++l_itr)
    {
        bool l_connected;
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

static void TodDrawer::findMasterProc(TodProc*& o_drawerMaster) const
{
    o_drawerMaster = NULL;
    for(TodProcContainer::const_iterator l_procIter = iv_todProcList.begin();
        l_procIter != iv_todProcList.end();
        ++l_procIter)
    {
        if(iv_masterType == TodProc::TOD_MASTER
        || iv_masterType == TodProc::DRAWER_MASTER)
        {
            o_drawerMaster = *l_procIter;
            return;
        }
    }
}

static void TodTopologyManager::wireProcs(const TodDrawer* i_pTodDrawer)
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
    TodProc* l_pDrawerMaster;
    i_pTodDrawer->findMasterProc(l_pDrawerMaster);
    TodProcContainer l_sourcesList;
    l_sourcesList.push_back(l_pDrawerMaster);

    //Start connecting targets to sources
    for(TodProcContainer::iterator l_sourceItr = l_sourcesList.begin();
        l_sourcesList.end() != l_sourceItr;
        ++l_sourceItr;)
    {
        for(TodProcContainer::iterator l_targetItr = l_targetsList.begin();
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
    }
}

static void TodProc::connect(
        TodProc* i_destination,
        const TARGETING::TYPE i_busChipUnitType,
        bool& o_isConnected)
{
    o_isConnected = false;

    if(iv_procTarget->getAttr<TARGETING::ATTR_HUID>() == i_destination->getTarget()->getAttr<TARGETING::ATTR_HUID>())
    {
        o_isConnected = true;
        break;
    }

    TARGETING::TargetHandleList* l_pBusList;

    //Check whether we've to connect over X or A bus
    if(TARGETING::TYPE_XBUS == i_busChipUnitType)
    {
        l_pBusList = &iv_xbusTargetList;
    }
    else if(TARGETING::TYPE_ABUS == i_busChipUnitType)
    {
        l_pBusList = &iv_abusTargetList;
    }

    //From this proc (iv_procTarget), find if destination(i_destination)
    //has a connection via the bus type i_busChipUnitType :

    //Step 1: Sequentially pick buses from either iv_xbusTargetList or
    //iv_abusTargetList depending on the bus type specified as input
    //Step 2: Get the parent of peer target of the bus target found in the
    //previous step. If it matches with the destination, we got a connection
    //Step 3: If a match is found then fill the i_bus_tx and i_bus_rx
    //attributes for the destination proc and return

    //Predicates tp look for a functional proc who's HUID is same as
    //that of i_destination. This predicate will be applied as a result
    //filter to getPeerTargets to determine the connected proc.
    TARGETING::PredicateCTM l_procFilter(TARGETING::CLASS_CHIP,TARGETING::TYPE_PROC);
    TARGETING::PredicateIsFunctional l_funcFilter;
    TARGETING::PredicateAttrVal<TARGETING::ATTR_HUID>l_huidFilter(i_destination->getTarget()->getAttr<TARGETING::ATTR_HUID>());
    TARGETING::PredicatePostfixExpr l_resFilter;
    l_resFilter.push(&l_procFilter).push(&l_funcFilter).And().push(&l_huidFilter).And();

    TARGETING::TargetHandleList l_procList;
    TARGETING::TargetHandleList l_busList;

    for(TARGETING::TargetHandleList::iterator l_busIter = (*l_pBusList).begin(); l_busIter != (*l_pBusList).end() ;++l_busIter)
    {
        l_procList.clear();
        l_busList.clear();

        //Need this call without result filter to get a handle to the
        //peer (bus). I need to access its HUID and chip unit.
        TARGETING::getPeerTargets(l_busList, *l_busIter, NULL, NULL);

        //The call below is to determine the connected proc
        TARGETING::getPeerTargets(l_procList, *l_busIter, NULL, &l_resFilter);

        if(l_procList.size())
        {
            //Determine the bus type as per our format, for eg XBUS0
            //ATTR_CHIP_UNIT gives the instance number of
            //the bus and it has direct correspondance to
            //the port no.
            //For instance a processor has two A buses
            //then the one with ATTR_CHIP_UNIT 0 will be A0
            //and the one with ATTR_CHIP_UNIT 1 will be A1
            p9_tod_setup_bus l_busOut = NONE;
            p9_tod_setup_bus l_busIn = NONE;
            getBusPort(
                i_busChipUnitType,
                (*l_busIter)->getAttr<TARGETING::ATTR_CHIP_UNIT>(),
                l_busOut);
            getBusPort(
                i_busChipUnitType,
                l_busList[0]->getAttr<TARGETING::ATTR_CHIP_UNIT>(),
                l_busIn);

            //Set the bus connections for i_destination :
            //Bus out from this proc = l_busOut;
            //Bus in to destination = l_busIn;
            i_destination->setConnections(l_busOut, l_busIn);
            o_isConnected = true;
        }
    }
}

static void getPeerTargets(
          TARGETING::TargetHandleList& o_peerTargetList,
    const Target*                      i_pSrcTarget,
    const PredicateBase*               i_pPeerFilter,
    const PredicateBase*               i_pResultFilter)
{
    Target* l_pPeerTarget = NULL;

    // Clear the list
    o_peerTargetList.clear();
    // List to maintain all child targets which are found by get associated
    // from the Src target with i_pPeerFilter predicate
    TARGETING::TargetHandleList l_pSrcTarget_list;

    // Create input master predicate here by taking in the i_pPeerFilter
    TARGETING::PredicatePostfixExpr l_superPredicate;
    TARGETING::PredicateAttrVal<TARGETING::ATTR_PEER_TARGET> l_notNullPeerExist(NULL, true);
    l_superPredicate.push(&l_notNullPeerExist);
    if(i_pPeerFilter)
    {
        l_superPredicate.push(i_pPeerFilter).And();
    }

    // Check if the i_srcTarget is the leaf node
    if(i_pSrcTarget->tryGetAttr<TARGETING::ATTR_PEER_TARGET>(l_pPeerTarget))
    {
        if(l_superPredicate(i_pSrcTarget))
        {
            // Exactly one Peer Target to Cross
            // Put this to input target list
            l_pSrcTarget_list.push_back(const_cast<TARGETING::Target*>(i_pSrcTarget));
        }
    }
    // Not a leaf node, find out all leaf node with valid PEER Target
    else
    {
        (void) TARGETING::targetService().getAssociated(
            l_pSrcTarget_list,
            i_pSrcTarget,
            TARGETING::TargetService::CHILD,
            TARGETING::TargetService::ALL,
            &l_superPredicate);
    }

    // Now we have a list of input targets on which we have to find the peer
    // Check if we have a result predicate filter to apply
    if(i_pResultFilter == NULL)
    {
        // Simply get the Peer Target for all Src target in the list and
        // return
        for(TARGETING::TargetHandleList::const_iterator pTargetIt = l_pSrcTarget_list.begin();
            pTargetIt != l_pSrcTarget_list.end();
            ++pTargetIt)
        {
            TARGETING::Target* l_pPeerTgt = (*pTargetIt)->getAttr<TARGETING::ATTR_PEER_TARGET>();
            o_peerTargetList.push_back(l_pPeerTgt);
        }
    }
    // Result predicate filter is not NULL, we need to apply this predicate
    // on each of the PEER Target found on the input target list
    else
    {
        for(TARGETING::TargetHandleList::const_iterator pTargetIt = l_pSrcTarget_list.begin();
            pTargetIt != l_pSrcTarget_list.end();
            ++pTargetIt)
        {
            TARGETING::TargetHandleList l_peerTarget_list;
            TARGETING::Target* l_pPeerTgt = (*pTargetIt)->getAttr<TARGETING::ATTR_PEER_TARGET>();

            // Check whether this target matches the filter criteria
            // or we have to look for ALL Parents matching the criteria.
            if((*i_pResultFilter)(l_pPeerTgt))
            {
                o_peerTargetList.push_back(l_pPeerTgt);
            }
            else
            {
                (void) TARGETING::targetService().getAssociated(
                    l_peerTarget_list,
                    l_pPeerTgt,
                    TARGETING::TargetService::PARENT,
                    TARGETING::TargetService::ALL,
                    i_pResultFilter);
                if(!l_peerTarget_list.empty())
                {
                    // Insert the first one only.
                    o_peerTargetList.push_back(l_peerTarget_list.front());
                }
            }
        }
    }
    // If target vector contains more than one element, sorty by HUID
    if (o_peerTargetList.size() > 1)
    {
        std::sort(o_peerTargetList.begin(),o_peerTargetList.end(), compareTargetHuid);
    }
}

static void setConnections(const p9_tod_setup_bus i_parentBusOut,
                const p9_tod_setup_bus i_thisBusIn)
{
    iv_tod_node_data->i_bus_tx = i_parentBusOut;
    iv_tod_node_data->i_bus_rx = i_thisBusIn;
}

static void TodProc::getBusPort(
    const TARGETING::TYPE  i_busChipUnitType,
    const uint32_t  i_busPort,
    p9_tod_setup_bus& o_busPort) const
{
    if(TARGETING::TYPE_XBUS == i_busChipUnitType)
    {
        switch(i_busPort)
        {
            case 0:
                o_busPort = XBUS0;
                break;
            case 1:
                o_busPort = XBUS1;
                break;
            case 2:
                o_busPort = XBUS2;
                break;
            case 7:
                o_busPort = XBUS7;
                break;
        }
    }
}

static void TodControls::buildTodDrawers(const p9_tod_setup_tod_sel i_config)
{
    TARGETING::TargetHandleList l_funcNodeTargetList;

    //Get the system pointer
    TARGETING::Target* l_pSysTarget;
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
                if((*l_todDrawerIter)->getId() == l_funcNodeTargetList[l_nodeIndex]->getAttr<TARGETING::ATTR_ORDINAL_ID>())
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

static void TodControls::buildGardedTargetsList()
{
    iv_gardedTargets.clear();

    TARGETING::Target* SystemTarget = NULL;
    TARGETING::targetService().getTopLevelTarget(SystemTarget);
    GardedUnitList_t l_gardedUnitList;
    gardGetGardedUnits(SystemTarget, l_gardedUnitList);

    for(const auto & l_iter : l_gardedUnitList)
    {
        //Push the HUID to the set of garded targets
        iv_gardedTargets.push_back(l_iter.iv_huid);
    }
    iv_gardListInitialized = true;
}

bool PNOR::isInhibitedSection(const uint32_t i_section)
{
#ifdef CONFIG_SECUREBOOT
    bool retVal = false;

    if ((i_section == ATTR_PERM ||
         i_section == ATTR_TMP  ||
         i_section == RINGOVD )
         && SECUREBOOT::enabled() )
    {
        // Default to these sections not being allowed in secure mode
        retVal = true;


#ifndef __HOSTBOOT_RUNTIME
        // This is the scenario where a section might be inhibited so check
        // global struct from bootloader for this setting
        retVal = !g_BlToHbDataManager.getAllowAttrOverrides();
#else
        // This is the scenario where a section might be inhibited so check
        // attribute to determine if these sections are allowed
        if (Util::isTargetingLoaded())
        {
            TARGETING::TargetService& tS = TARGETING::targetService();
            TARGETING::Target* sys = nullptr;
            (void) tS.getTopLevelTarget(sys);
            retVal = !sys->getAttr<TARGETING::ATTR_ALLOW_ATTR_OVERRIDES_IN_SECURE_MODE>();
       }
#endif
    }
    return retVal;
#else
    return false;
#endif
}

static void PnorRP::getSectionInfo(PNOR::SectionId i_section = PNOR::GUARD_DATA, PNOR::SectionInfo_t& o_info )
{
    // inhibit any attempt to getSectionInfo on any attribute override
    // sections if secureboot is enabled
    bool l_inhibited = isInhibitedSection(PNOR::GUARD_DATA);

    // copy my data into the external format
    o_info.id = iv_TOC[PNOR::GUARD_DATA].id;
    o_info.name = "GUARD";

#ifdef CONFIG_SECUREBOOT
    o_info.secure = iv_TOC[PNOR::GUARD_DATA].secure;
    o_info.size = iv_TOC[PNOR::GUARD_DATA].size;
    o_info.secureProtectedPayloadSize = 0; // for non secure sections
                                            // the protected payload size
                                            // defaults to zero
    // If a secure section and has a secure header handle secure
    // sections in SPnorRP's address space
    if (o_info.secure)
    {
        uint8_t* l_vaddr = reinterpret_cast<uint8_t*>(iv_TOC[PNOR::GUARD_DATA].virtAddr);
        // By adding VMM_VADDR_SPNOR_DELTA twice we can translate a pnor
        // address into a secure pnor address, since pnor, temp, and spnor
        // spaces are equidistant.
        // See comments in SPnorRP::verifySections() method in spnorrp.C
        // and the definition of VMM_VADDR_SPNOR_DELTA in vmmconst.h
        // for specifics.
        o_info.vaddr = reinterpret_cast<uint64_t>(l_vaddr) + VMM_VADDR_SPNOR_DELTA + VMM_VADDR_SPNOR_DELTA;

        // Get size of the secured payload for the secure section
        // Note: the payloadSize we get back is untrusted because
        // we are parsing the header in pnor (non secure space).
        size_t payloadTextSize = 0;
        // Do an existence check on the container to see if it's non-empty
        // and has valid beginning bytes. For optional Secure PNOR sections.

        SECUREBOOT::ContainerHeader l_conHdr;
        l_errhdl = l_conHdr.setHeader(l_vaddr);
        if (l_errhdl)
        {
            break;
        }
        payloadTextSize = l_conHdr.payloadTextSize();

        // skip secure header for secure sections at this point in time
        o_info.vaddr += PAGESIZE;
        // now that we've skipped the header we also need to adjust the
        // size of the section to reflect that.
        // Note: For unsecured sections, the header skip and size decrement
        // was done previously in pnor_common.C
        o_info.size -= PAGESIZE;

        // Need to change size to accommodate for hash table
        if (l_conHdr.sb_flags()->sw_hash)
        {
            o_info.vaddr += payloadTextSize;
            // Hash page table needs to use containerSize as the base
            // and subtract off header and hash table size
            o_info.size = l_conHdr.totalContainerSize() - PAGE_SIZE - payloadTextSize;
            o_info.hasHashTable = true;
        }

        // cache the value in SectionInfo struct so that we can
        // parse the container header less often
        o_info.secureProtectedPayloadSize = payloadTextSize;
    }
    else
#endif
    {
        o_info.size = iv_TOC[PNOR::GUARD_DATA].size;
        o_info.vaddr = iv_TOC[PNOR::GUARD_DATA].virtAddr;
    }

    o_info.flashAddr     = iv_TOC[PNOR::GUARD_DATA].flashAddr;
    o_info.eccProtected  = iv_TOC[PNOR::GUARD_DATA].integrity & FFS_INTEG_ECC_PROTECT ? true : false;
    o_info.sha512Version = iv_TOC[PNOR::GUARD_DATA].version & FFS_VERS_SHA512         ? true : false;
    o_info.sha512perEC   = iv_TOC[PNOR::GUARD_DATA].version & FFS_VERS_SHA512_PER_EC  ? true : false;
    o_info.readOnly      = iv_TOC[PNOR::GUARD_DATA].misc & FFS_MISC_READ_ONLY         ? true : false;
    o_info.reprovision   = iv_TOC[PNOR::GUARD_DATA].misc & FFS_MISC_REPROVISION       ? true : false;
    o_info.Volatile      = iv_TOC[PNOR::GUARD_DATA].misc & FFS_MISC_VOLATILE          ? true : false;
    o_info.clearOnEccErr = iv_TOC[PNOR::GUARD_DATA].misc & FFS_MISC_CLR_ECC_ERR       ? true : false;
}

static void _GardRecordIdSetup( void *&io_platDeconfigGard)
{
    // Get the PNOR Guard information
    PNOR::SectionInfo_t l_section;
    PNOR::getSectionInfo(PNOR::GUARD_DATA, l_section);
    // Check if guard section exists, as certain configs ignore the above
    // error (e.g. golden side has no GARD section)
    if (l_section.size == 0)
    {
        break;
    }

    // allocate our memory and set things up
    io_platDeconfigGard = malloc(sizeof(HBDeconfigGard));
    HBDeconfigGard *l_hbDeconfigGard = (HBDeconfigGard *)io_platDeconfigGard;

    l_hbDeconfigGard->iv_pGardRecords = reinterpret_cast<DeconfigGard::GardRecord *> (l_section.vaddr);
    l_hbDeconfigGard->iv_maxGardRecords = l_section.size / sizeof(DeconfigGard::GardRecord);
    l_hbDeconfigGard->iv_nextGardRecordId = 0;

    DeconfigGard::GardRecord *l_pGardRecords = (DeconfigGard::GardRecord *)l_hbDeconfigGard->iv_pGardRecords;
    for (uint32_t i = 0; i < l_hbDeconfigGard->iv_maxGardRecords; i++)
    {
        // if this gard record is already filled out
        if (l_pGardRecords[i].iv_recordId != EMPTY_GARD_RECORDID
        && l_pGardRecords[i].iv_recordId > l_hbDeconfigGard->iv_nextGardRecordId)
        {
            l_hbDeconfigGard->iv_nextGardRecordId = l_pGardRecords[i].iv_recordId;
        }
    }

    // next record will start after the highest Id we found
    l_hbDeconfigGard->iv_nextGardRecordId++;
}

static void DeconfigGard::platGetGardRecords(
    const Target * const SystemTarget,
    GardRecords_t &o_records)
{
    o_records.clear();

    _GardRecordIdSetup(iv_platDeconfigGard);
    if (iv_platDeconfigGard)
    {
        HBDeconfigGard *l_hbDeconfigGard = (HBDeconfigGard *)iv_platDeconfigGard;
        DeconfigGard::GardRecord * l_pGardRecords = (DeconfigGard::GardRecord *)l_hbDeconfigGard->iv_pGardRecords;
        for (uint32_t i = 0; i < l_hbDeconfigGard->iv_maxGardRecords; i++)
        {
            if(l_pGardRecords[i].iv_recordId != EMPTY_GARD_RECORDID
            && l_pGardRecords[i].iv_targetId == 0)
            {
                o_records.push_back(l_pGardRecords[i]);
                break;
            }
        }
    }
}

static void TodControls::gardGetGardedUnits(
    const TARGETING::Target* const SystemTarget,
    GardedUnitList_t &o_gardedUnitList)
{
    o_gardedUnitList.clear();
    HWAS::DeconfigGard::GardRecords_t l_gardRecords;
    platGetGardRecords(SystemTarget, l_gardRecords);

    for(const auto & l_iter : l_gardRecords)
    {
        TARGETING::Target * l_pTarget = NULL;

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
        l_gardedUnit.iv_errlogId = l_iter.iv_errlogEid;
        l_gardedUnit.iv_errType = static_cast<HWAS::GARD_ErrorType>(l_iter.iv_errorType);
        l_gardedUnit.iv_domain = l_pTarget->getAttr<TARGETING::ATTR_CDM_DOMAIN>();
        l_gardedUnit.iv_type = l_pTarget->getAttr<TARGETING::ATTR_TYPE>();
        l_gardedUnit.iv_class = l_pTarget->getAttr<TARGETING::ATTR_CLASS>();

        o_gardedUnitList.push_back(l_gardedUnit);
    }
}
