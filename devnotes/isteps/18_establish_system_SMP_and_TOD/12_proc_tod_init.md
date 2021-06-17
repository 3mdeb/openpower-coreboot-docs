void TodSvc::todInit()
{
    p9_tod_clear_error_reg(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data);
    init_tod_node(TOD::iv_todConfig[TOD_PRIMARY].iv_mdmt->iv_tod_node_data);
}

void p9_tod_clear_error_reg(const tod_topology_node* i_tod_node)
{
    fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_ERROR_REG, 0xFFFFFFFFFFFFFFFF);
}

void init_tod_node(const tod_topology_node* i_tod_node)
{
    // Sequence details are in TOD Workbook section 1.6.3
    // Is the current TOD being processed the master drawer master TOD?
    if (i_tod_node->i_tod_master && i_tod_node->i_drawer_master)
    {
        // TOD Step checkers enable - write TOD_TX_TTYPE_2_REG to enable
        // TOD STEP checking on all chips
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_TX_TTYPE_2_REG, PPC_BIT(PERV_TOD_TX_TTYPE_2_REG_TRIGGER));
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_LOAD_TOD_MOD_REG, PPC_BIT(PERV_TOD_LOAD_TOD_MOD_REG_FSM_TRIGGER));
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_TX_TTYPE_5_REG, PPC_BIT(PERV_TOD_TX_TTYPE_5_REG_TRIGGER));
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_LOAD_TOD_REG, PERV_TOD_LOAD_REG_LOAD_VALUE);
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_START_TOD_REG, PPC_BIT(PERV_TOD_START_TOD_REG_FSM_TRIGGER));
        fapi2::putScom(*(i_tod_node->i_target), PERV_TOD_TX_TTYPE_4_REG, PPC_BIT(PERV_TOD_TX_TTYPE_4_REG_TRIGGER));
    }

    uint32_t l_tod_init_pending_count = 0;
    while(l_tod_init_pending_count < P9_TOD_UTIL_TIMEOUT_COUNT)
    {
        fapi2::delay(P9_TOD_UTILS_HW_NS_DELAY, P9_TOD_UTILS_SIM_CYCLE_DELAY);
        uint64_t l_tod_fsm_reg;
        fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_FSM_REG, l_tod_fsm_reg);

        if (l_tod_fsm_reg & PPC_BIT(PERV_TOD_FSM_REG_IS_RUNNING))
        {
            break;
        }
        ++l_tod_init_pending_count;
    }

    fapi2::putScom(
        *(i_tod_node->i_target),
        PERV_TOD_ERROR_REG,
        PPC_BIT(PERV_TOD_ERROR_REG_RX_TTYPE_2)
      | PPC_BIT(PERV_TOD_ERROR_REG_RX_TTYPE_4)
      | PPC_BIT(PERV_TOD_ERROR_REG_RX_TTYPE_5));

    fapi2::getScom(*(i_tod_node->i_target), PERV_TOD_ERROR_REG, l_tod_err_reg);

    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiFailingProcTarget(NULL);
    // going to assert, populate pointer prior to exit
    if (l_tod_err_reg.getBit<PERV_TOD_ERROR_REG_M_PATH_0_STEP_CHECK>()
      || l_tod_err_reg.getBit<PERV_TOD_ERROR_REG_M_PATH_1_STEP_CHECK>())
    {
        *l_fapiFailingProcTarget = *(i_tod_node->i_target);
    }

    fapi2::putScom(
        *(i_tod_node->i_target),
        PERV_TOD_ERROR_MASK_REG,
        PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_0)
      | PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_1)
      | PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_2)
      | PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_3)
      | PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_4)
      | PPC_BIT(PERV_TOD_ERROR_MASK_REG_RX_TTYPE_5));
}
