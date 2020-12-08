# 8.10 proc_xbus_scominit: Apply scom inits to Xbus

```cpp
void *call_proc_xbus_scominit(void *io_pArgs)
{
    EDI_EI_INITIALIZATION::TargetPairs_t l_XbusConnections;
    // Note:
    // i_noDuplicate parameter must be set to true because
    // one call to p9_io_xbus_scominit will handle both
    //    X0 <--> X1
    //    X1 <--> X0
    // both the xbus and the connected target are used to issue SCOMs
    EDI_EI_INITIALIZATION::PbusLinkSvc::getTheInstance().getPbusConnections(l_XbusConnections, TYPE_XBUS, true);
    for l_XbusConnection in l_XbusConnections:
    {
        // XBUS_GROUP_COUNT = 2
        for group in range(0, XBUS_GROUP_COUNT):
        {
            p9_io_xbus_scominit(l_XbusConnection.first, l_XbusConnection.second, group);
        }
    }
}

fapi2::ReturnCode p9_io_xbus_scominit(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS> &i_target,
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS> &i_connected_target,
    const uint8_t group)
{
    const uint8_t LANE_00 = 0;
    fapi2::ReturnCode rc = fapi2::FAPI2_RC_SUCCESS;
    // get system target
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> l_system_target;
    // get proc chip to pass for EC level checks in procedure
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc = i_target.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    // assert IO reset to power-up bus endpoint logic
    // read-modify-write, set single reset bit (HW auto-clears)
    // on writeback
    EDIP_RX_IORESET[i_target, group, LANE_00] = 1;
    EDIP_RX_IORESET[i_connected_target, group, LANE_00] = 1;
    sleep(50us);
    EDIP_TX_IORESET[i_target, group, LANE_00] = 1;
    EDIP_TX_IORESET[i_connected_target, group, LANE_00] = 1;
    sleep(50us);
    // Set rx master/slave attribute prior to calling the scominit procedures.
    // The scominit procedure will reference the attribute to set the register field.
    set_rx_master_mode(i_target, i_connected_target);
    // Set Msb Swap based upon attribute data.
    set_msb_swap(i_target, group);
    set_msb_swap(i_connected_target, group);
    switch (group)
    {
    case ENUM_ATTR_XBUS_GROUP_0:
        p9_xbus_g0_scom(i_target, l_system_target, l_proc);
        p9_xbus_g0_scom(i_connected_target, l_system_target, l_proc);
        break;
    case ENUM_ATTR_XBUS_GROUP_1:
        p9_xbus_g1_scom(i_target, l_system_target, l_proc);
        p9_xbus_g1_scom(i_connected_target, l_system_target, l_proc);
        break;
    }

    fapi2::buffer<uint64_t> l_scom_data;
    l_scom_data = l_proc[PU_PB_CENT_SM0_PB_CENT_FIR_REG];
    if(!l_scom_data.getBit<13>())
    {
        // XBUS_PHY_FIR_ACTION0 = 0x0000000000000000ULL;
        // XBUS_FIR_ACTION0_REG = 0x06010C06
        // XBUS_PHY_FIR_ACTION1 = 0x2068680000000000ULL;
        // XBUS_FIR_ACTION1_REG = 0x06010C07
        // XBUS_PHY_FIR_MASK    = 0xDF9797FFFFFFC000ULL;
        // XBUS_FIR_MASK_REG    = 0x06010C03

        i_target[XBUS_FIR_ACTION0_REG] = XBUS_PHY_FIR_ACTION0;
        i_target[XBUS_FIR_ACTION1_REG] = XBUS_PHY_FIR_ACTION1;
        i_target[XBUS_FIR_MASK_REG] = XBUS_PHY_FIR_MASK;
    }
}

fapi2::ReturnCode set_msb_swap(const fapi2::Target<fapi2::TARGET_TYPE_XBUS> i_tgt, const uint8_t i_grp)
{
    const uint8_t lane0   = 0;
    uint8_t tx_msb_swap = 0x0;
    uint8_t field_data  = 0x0;

    // Retrieve msb swap attribute
    tx_msb_swap = fapi2::ATTR_EI_BUS_TX_MSBSWAP[i_tgt];

    switch( i_grp )
    {
    case ENUM_ATTR_XBUS_GROUP_0:
        if( tx_msb_swap & fapi2::ENUM_ATTR_EI_BUS_TX_MSBSWAP_GROUP_0_SWAP )
        {
            field_data = 1;
        }
        break;

    case ENUM_ATTR_XBUS_GROUP_1:
        if( tx_msb_swap & fapi2::ENUM_ATTR_EI_BUS_TX_MSBSWAP_GROUP_1_SWAP )
        {
            field_data = 1;
        }
        break;
    }

    EDIP_TX_MSBSWAP[i_tgt, i_grp, lane0] = field_data;
}

fapi2::ReturnCode set_rx_master_mode(
    const fapi2::Target< fapi2::TARGET_TYPE_XBUS >& i_target,
    const fapi2::Target< fapi2::TARGET_TYPE_XBUS >& i_ctarget )
{
    uint8_t  l_primary_group_id   = 0;
    uint8_t  l_primary_chip_id    = 0;
    uint32_t l_primary_id         = 0;
    uint8_t  l_primary_attr       = 0;
    uint8_t  l_connected_group_id = 0;
    uint8_t  l_connected_chip_id  = 0;
    uint32_t l_connected_id       = 0;
    uint8_t  l_connected_attr     = 0;

    p9_get_proc_fabric_group_id(i_target,  l_primary_group_id);
    p9_get_proc_fabric_group_id(i_ctarget, l_connected_group_id);

    p9_get_proc_fabric_chip_id(i_target,  l_primary_chip_id);
    p9_get_proc_fabric_chip_id(i_ctarget, l_connected_chip_id);

    l_primary_id   = ((uint32_t)l_primary_group_id   << 8) + (uint32_t)l_primary_chip_id;
    l_connected_id = ((uint32_t)l_connected_group_id << 8) + (uint32_t)l_connected_chip_id;

    if(l_primary_id < l_connected_id) {
        fapi2::ATTR_IO_XBUS_MASTER_MODE[i_target] = fapi2::ENUM_ATTR_IO_XBUS_MASTER_MODE_TRUE;
        fapi2::ATTR_IO_XBUS_MASTER_MODE[i_ctarget] = fapi2::ENUM_ATTR_IO_XBUS_MASTER_MODE_FALSE;
    }
    else {
        fapi2::ATTR_IO_XBUS_MASTER_MODE[i_target] = fapi2::ENUM_ATTR_IO_XBUS_MASTER_MODE_FALSE;
        fapi2::ATTR_IO_XBUS_MASTER_MODE[i_ctarget] = fapi2::ENUM_ATTR_IO_XBUS_MASTER_MODE_TRUE;
    }
}

fapi2::ReturnCode p9_get_proc_fabric_group_id(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS>& i_target, uint8_t& o_group_id)
{
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc = i_target.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    // Retrieve node attribute
    o_group_id = fapi2::ATTR_PROC_FABRIC_GROUP_ID[l_proc];
}

fapi2::ReturnCode p9_get_proc_fabric_chip_id(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS>& i_target, uint8_t& o_chip_id)
{
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc = i_target.getParent<fapi2::TARGET_TYPE_PROC_CHIP>();
    // Retrieve pos ID attribute
    o_chip_id = fapi2::ATTR_PROC_FABRIC_CHIP_ID[l_proc];
}

fapi2::ReturnCode p9_xbus_g0_scom(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS> &TGT0,
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> &TGT1,
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT2)
{
    l_chip_id = fapi2::ATTR_NAME[TGT2];
    l_chip_ec = fapi2::ATTR_EC[TGT2];
    l_TGT1_ATTR_IS_SIMULATION = fapi2::ATTR_IS_SIMULATION[TGT1];
    l_TGT0_ATTR_IO_XBUS_CHAN_EQ = fapi2::ATTR_IO_XBUS_CHAN_EQ[TGT0];
    l_TGT2_ATTR_CHIP_EC_FEATURE_HW393297 = fapi2::ATTR_CHIP_EC_FEATURE_HW393297[TGT2];
    l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND = fapi2::ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND[TGT2];
    l_TGT0_ATTR_IO_XBUS_MASTER_MODE = fapi2::ATTR_IO_XBUS_MASTER_MODE[TGT0];

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000006010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000106010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000206010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000306010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000406010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000506010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000606010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000706010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000806010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000906010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000A06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000B06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000C06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000D06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000E06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000000F06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000000F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000001006010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000001006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000001106010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000001106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080006010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080106010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080206010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080306010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080406010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080506010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080606010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080706010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080806010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080906010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080A06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080B06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080C06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080D06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080E06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000080F06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000080F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000081006010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000081006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000081106010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_ON
    TGT0[0x8000081106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280106010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280206010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280306010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280406010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280506010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280606010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280706010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280806010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280906010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280A06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280B06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280C06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280D06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280E06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000280F06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000280F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000281006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000281006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000281106010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000281106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300106010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300206010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300306010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300406010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300506010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300606010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300706010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300806010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300906010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300906010C3F] = l_scom_buffer;

    l_scom_buffer = TGT0[0x8000300A06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300B06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300C06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300D06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300E06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000300F06010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000300F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000301006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000301006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000301106010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000301106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C00F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C00F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C01006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C01006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C01106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C01106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200206010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200306010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200406010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200506010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200606010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200706010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200706010C3F] = l_scom_buffer;

    l_scom_buffer = TGT0[0x8002200806010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200906010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200A06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200B06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200C06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200D06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200E06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002200F06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002200F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002201006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002201006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002201106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_ON
    TGT0[0x8002201106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00106010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf03e); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_B_0_15
    TGT0[0x8002C00106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00206010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7bc); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_C_0_15
    TGT0[0x8002C00206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00306010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c7); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_D_0_15
    TGT0[0x8002C00306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00406010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x3ef); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_E_0_15
    TGT0[0x8002C00406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00506010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f0f); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_F_0_15
    TGT0[0x8002C00506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00606010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1800); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_G_0_15
    TGT0[0x8002C00606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00706010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x9c00); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_H_0_15
    TGT0[0x8002C00706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00806010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C00806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00906010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x9c00); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_H_0_15
    TGT0[0x8002C00906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00A06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1800); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_G_0_15
    TGT0[0x8002C00A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00B06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f0f); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_F_0_15
    TGT0[0x8002C00B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00C06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x3ef); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_E_0_15
    TGT0[0x8002C00C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00D06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c7); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_D_0_15
    TGT0[0x8002C00D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00E06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7bc); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_C_0_15
    TGT0[0x8002C00E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C00F06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf03e); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_B_0_15
    TGT0[0x8002C00F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C01006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C01006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE4_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE5_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3e); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_B_16_22
    TGT0[0x8002C80106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_C_12_ACGH_16_22
    TGT0[0x8002C80206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x60); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_D_16_22
    TGT0[0x8002C80306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C80406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS3_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX0_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C80506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C80606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C80706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS2_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX0_RXPACKS_2_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C80806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C80906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C80A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C80B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS1_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX0_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C80C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x60); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_D_16_22
    TGT0[0x8002C80D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x0); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_C_12_ACGH_16_22
    TGT0[0x8002C80E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C80F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3e); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_B_16_22
    TGT0[0x8002C80F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RXPACKS0_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C81006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX0_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C81006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040206010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040306010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040406010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040506010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040606010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040706010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040806010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040906010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040A06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040B06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040C06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040D06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040E06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004040F06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004040F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE4_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004041006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004041006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0006010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0106010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0206010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0306010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0406010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0506010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0606010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0706010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0806010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0906010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0A06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0B06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0C06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0D06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0E06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C0F06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C0F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE4_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C1006010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C1006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0106010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C0106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0206010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1e); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_C_0_15
    TGT0[0x80043C0206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0306010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_D_0_15
    TGT0[0x80043C0306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0406010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_E_HALF_B_0_15
    TGT0[0x80043C0406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0506010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_F_0_15
    TGT0[0x80043C0506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0606010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xc63); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_G_0_15
    TGT0[0x80043C0606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0706010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xe73); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_H_0_15
    TGT0[0x80043C0706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0806010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C0806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0906010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xe73); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_H_0_15
    TGT0[0x80043C0906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0A06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xc63); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_G_0_15
    TGT0[0x80043C0A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0B06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_F_0_15
    TGT0[0x80043C0B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0C06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_E_HALF_B_0_15
    TGT0[0x80043C0C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0D06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_D_0_15
    TGT0[0x80043C0D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0E06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1e); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_C_0_15
    TGT0[0x80043C0E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C0F06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C0F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE4_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C1006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C1006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004440006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_B_16_22
    TGT0[0x8004440106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7b); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_C_16_22
    TGT0[0x8004440206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS0_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX0_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004440306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x5e); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_E_16_22
    TGT0[0x8004440406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x10); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_F_HALF_A_16_22
    TGT0[0x8004440506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004440606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS1_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x4e); // l_IOF1_TX_WRAP_TX0_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_H_HALF_B_16_22
    TGT0[0x8004440706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004440806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x4e); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_H_HALF_B_16_22
    TGT0[0x8004440906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004440A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS2_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x10); // l_IOF1_TX_WRAP_TX0_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_F_HALF_A_16_22
    TGT0[0x8004440B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x5e); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_E_16_22
    TGT0[0x8004440C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004440D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7b); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_C_16_22
    TGT0[0x8004440E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004440F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_B_16_22
    TGT0[0x8004440F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TXPACKS3_SLICE4_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004441006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004441006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x8008000006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_HW393297 == 0) {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PG_SPARE_MODE_1_ON
    }
    else {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PG_SPARE_MODE_1_OFF
    }
    TGT0[0x8008000006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_ID1_PG
    l_scom_buffer = TGT0[0x8008080006010C3F];
    l_scom_buffer.insert<48, 6, 58, uint64_t>(0);
    TGT0[0x8008080006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE1_EO_PG
    l_scom_buffer = TGT0[0x8008100006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_CLKDIST_PDWN_OFF
    TGT0[0x8008100006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE5_EO_PG
    l_scom_buffer = TGT0[0x8008300006010C3F];
    l_scom_buffer.insert<51, 3, 61, uint64_t>(0x5); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP5
    l_scom_buffer.insert<54, 2, 62, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP1
    TGT0[0x8008300006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE7_EO_PG
    l_scom_buffer = TGT0[0x8008400006010C3F];
    l_scom_buffer.insert<60, 4, 60, uint64_t>(0b1010);
    TGT0[0x8008400006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE23_EO_PG
    l_scom_buffer = TGT0[0x8008C00006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<48, 2, 62, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<48, 2, 62, uint64_t>(0b01);
    }
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PEAK_TUNE_OFF
    l_scom_buffer.insert<57, 2, 62, uint64_t>(0x3);
    l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DFEHISPD_EN_ON
    l_scom_buffer.insert<60, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DFE12_EN_ON
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_DIS_RX_LTE) {
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_LTE_EN_OFF
    }
    else {
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_LTE_EN_ON
    }
    TGT0[0x8008C00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE29_EO_PG
    l_scom_buffer = TGT0[0x8008D00006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0b01010000);
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b00110111);
    }
    else if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_VGA_GAIN_TARGET) {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0b01011100);
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b00111101);
    }
    else {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0x0C110);
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b01000100);
    }
    TGT0[0x8008D00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE27_EO_PG
    l_scom_buffer = TGT0[0x8009700006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL_ON
    TGT0[0x8009700006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_ID2_PG
    l_scom_buffer = TGT0[0x8009800006010C3F];
    l_scom_buffer.insert<49, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<57, 7, 57, uint64_t>(0x10);
    TGT0[0x8009800006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE1_E_PG
    l_scom_buffer = TGT0[0x8009900006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_MASTER_MODE) {
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_MASTER_MODE_MASTER
    }
    l_scom_buffer.insert<58, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PDWN_LITE_DISABLE_ON
    l_scom_buffer.insert<57, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_FENCE_FENCED
    TGT0[0x8009900006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE2_E_PG
    l_scom_buffer = TGT0[0x8009980006010C3F];
    l_scom_buffer.insert<48, 5, 59, uint64_t>(0x1);
    TGT0[0x8009980006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE3_E_PG
    l_scom_buffer = TGT0[0x8009A00006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
    TGT0[0x8009A00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE5_E_PG
    l_scom_buffer = TGT0[0x8009B00006010C3F];
    l_scom_buffer.insert<52, 4, 60, uint64_t>(0x1);
    TGT0[0x8009B00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE6_E_PG
    l_scom_buffer = TGT0[0x8009B80006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x11);
    l_scom_buffer.insert<55, 7, 57, uint64_t>(0x11);
    TGT0[0x8009B80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE8_E_PG
    l_scom_buffer = TGT0[0x8009C80006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1111);
    l_scom_buffer.insert<55, 4, 60, uint64_t>(0x5); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RPR_ERR_CNTR1_DURATION_TAP5
    l_scom_buffer.insert<61, 3, 61, uint64_t>(0x05);
    TGT0[0x8009C80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE9_E_PG
    l_scom_buffer = TGT0[0x8009D00006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7111);
    l_scom_buffer.insert<55, 4, 60, uint64_t>(0x5); // l_IOF1_RX_RX0_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RPR_ERR_CNTR2_DURATION_TAP5
    TGT0[0x8009D00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE11_E_PG
    l_scom_buffer = TGT0[0x8009E00006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0);
    TGT0[0x8009E00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE12_E_PG
    l_scom_buffer = TGT0[0x8009E80006010C3F];
    l_scom_buffer.insert<48, 8, 56, uint64_t>(0x71111);
    TGT0[0x8009E80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_GLBSM_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800A800006010C3F];
    l_scom_buffer.insert<56, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_GLBSM_REGS_RX_DESKEW_BUMP_AFTER_AFTER
    l_scom_buffer.insert<50, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX0_RXCTL_GLBSM_REGS_RX_PG_GLBSM_SPARE_MODE_2_ON
    TGT0[0x800A800006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_GLBSM_CNTL3_EO_PG
    l_scom_buffer = TGT0[0x800AE80006010C3F];
    l_scom_buffer.insert<56, 2, 62, uint64_t>(0x02);
    TGT0[0x800AE80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_GLBSM_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800AF80006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0x0C);
    l_scom_buffer.insert<52, 4, 60, uint64_t>(0x0C);
    TGT0[0x800AF80006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_DATASM_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800B800006010C3F];
    l_scom_buffer.insert<60, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX0_RXCTL_DATASM_DATASM_REGS_RX_CTL_DATASM_CLKDIST_PDWN_OFF
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<56, 4, 60, uint64_t>(0x02);
    }
    TGT0[0x800B800006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800C040006010C3F];
    l_scom_buffer.insert<56, 2, 62, uint64_t>(0);
    TGT0[0x800C040006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_ID1_PG
    l_scom_buffer = TGT0[0x800C0C0006010C3F];
    l_scom_buffer.insert<48, 6, 58, uint64_t>(0);
    TGT0[0x800C0C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800C140006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXCTL_CTL_REGS_TX_CTL_REGS_TX_CLKDIST_PDWN_OFF
    l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXCTL_CTL_REGS_TX_CTL_REGS_TX_PDWN_LITE_DISABLE_ON
    l_scom_buffer.insert<53, 5, 59, uint64_t>(0b00001);
    TGT0[0x800C140006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_MODE2_EO_PG
    l_scom_buffer = TGT0[0x800C1C0006010C3F];
    l_scom_buffer.insert<56, 7, 57, uint64_t>(0x11);
    TGT0[0x800C1C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_CNTLG1_EO_PG
    l_scom_buffer = TGT0[0x800C240006010C3F];
    l_scom_buffer.insert<48, 2, 62, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DRV_CLK_PATTERN_GCRMSG_DRV_0S
    TGT0[0x800C240006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_ID2_PG
    l_scom_buffer = TGT0[0x800C840006010C3F];
    l_scom_buffer.insert<49, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<57, 7, 57, uint64_t>(0x10);
    TGT0[0x800C840006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_MODE1_E_PG
    l_scom_buffer = TGT0[0x800C8C0006010C3F];
    l_scom_buffer.insert<55, 3, 61, uint64_t>(0x5); // l_IOF1_TX_WRAP_TX0_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP5
    l_scom_buffer.insert<58, 2, 62, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP1
    TGT0[0x800C8C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_MODE2_E_PG
    l_scom_buffer = TGT0[0x800CEC0006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0);
    TGT0[0x800CEC0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTL_MODE3_E_PG
    l_scom_buffer = TGT0[0x800CF40006010C3F];
    l_scom_buffer.insert<48, 8, 56, uint64_t>(0x71111);
    TGT0[0x800CF40006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX0_TX_CTLSM_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800D2C0006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_DIS_TX_AC_BOOST) {
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX0_TXCTL_TX_CTL_SM_REGS_TX_FFE_BOOST_EN_OFF
    }
    else {
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX0_TXCTL_TX_CTL_SM_REGS_TX_FFE_BOOST_EN_ON
    }
    TGT0[0x800D2C0006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX_IMPCAL_P_4X_PB
    l_scom_buffer = TGT0[0x800F1C0006010C3F];
    l_scom_buffer.insert<48, 5, 59, uint64_t>(0x70);
    TGT0[0x800F1C0006010C3F] = l_scom_buffer;
}

fapi2::ReturnCode p9_xbus_g1_scom(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS> &TGT0,
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> &TGT1,
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &TGT2)
{
    l_chip_id = fapi2::ATTR_NAME[TGT2];
    l_chip_ec = fapi2::ATTR_EC[TGT2];
    l_TGT1_ATTR_IS_SIMULATION = fapi2::ATTR_IS_SIMULATION[TGT1];
    l_TGT0_ATTR_IO_XBUS_CHAN_EQ = fapi2::ATTR_IO_XBUS_CHAN_EQ[TGT0];
    l_TGT2_ATTR_CHIP_EC_FEATURE_HW393297 = fapi2::ATTR_CHIP_EC_FEATURE_HW393297[TGT2];
    l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND = fapi2::ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND[TGT2];
    l_TGT0_ATTR_IO_XBUS_MASTER_MODE = fapi2::ATTR_IO_XBUS_MASTER_MODE[TGT0];
    fapi2::buffer<uint64_t> l_scom_buffer;

    // P9A_XBUS_RX1_RXPACKS0_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002006010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); //l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); //l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); //l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002106010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002206010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002306010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002406010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002506010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002606010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002706010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002806010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002906010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002A06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002B06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002C06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000002C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002D06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF);
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF);
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF);
    TGT0[0x8000002D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002E06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF);
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF);
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF);
    TGT0[0x8000002E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000002F06010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF);
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF);
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF);
    TGT0[0x8000002F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000003006010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF);
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF);
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF);
    TGT0[0x8000003006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_DATA_DAC_SPARE_MODE_PL
    l_scom_buffer = TGT0[0x8000003106010C3F];
    l_scom_buffer.insert<53, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_5_OFF
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_6_OFF
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_PL_DATA_DAC_SPARE_MODE_7_OFF
    TGT0[0x8000003106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082006010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082106010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082206010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082306010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082406010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082506010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082606010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082706010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082806010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082906010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082A06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082B06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082C06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082D06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082E06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000082F06010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000082F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000083006010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_OFF
    TGT0[0x8000083006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL1_EO_PL
    l_scom_buffer = TGT0[0x8000083106010C3F];
    l_scom_buffer.insert<54, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_3_RX_DAC_REGS_RX_DAC_REGS_RX_LANE_ANA_PDWN_ON
    TGT0[0x8000083106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282106010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282206010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282306010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282406010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282506010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282606010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282706010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282806010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282906010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282A06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282B06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282C06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282D06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282E06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000282F06010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000282F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000283006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000283006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL5_EO_PL
    l_scom_buffer = TGT0[0x8000283106010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0);
    l_scom_buffer.insert<52, 5, 59, uint64_t>(0);
    l_scom_buffer.insert<57, 5, 59, uint64_t>(0);
    TGT0[0x8000283106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302006010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302106010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302206010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302306010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302406010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302506010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302606010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302706010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302806010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302906010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302A06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302B06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302C06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302D06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302E06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000302F06010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000302F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000303006010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000303006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL6_EO_PL
    l_scom_buffer = TGT0[0x8000303106010C3F];
    if (l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_GAIN_PEAK_INITS) {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<53, 4, 60, uint64_t>(0x7);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x0C);
    }
    TGT0[0x8000303106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C02F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C02F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C03006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C03006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL9_E_PL
    l_scom_buffer = TGT0[0x8000C03106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<55, 6, 58, uint64_t>(0);
    TGT0[0x8000C03106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202206010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202306010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202406010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202506010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202606010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202706010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202806010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202906010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202A06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202B06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202C06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202D06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202E06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002202F06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002202F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002203006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_OFF
    TGT0[0x8002203006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE3_RX_BIT_MODE1_EO_PL
    l_scom_buffer = TGT0[0x8002203106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_LANE_DIG_PDWN_ON
    TGT0[0x8002203106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02106010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf03e); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_B_0_15
    TGT0[0x8002C02106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02206010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7bc); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_C_0_15
    TGT0[0x8002C02206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02306010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c7); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_D_0_15
    TGT0[0x8002C02306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02406010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x3ef); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_E_0_15
    TGT0[0x8002C02406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02506010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f0f); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_F_0_15
    TGT0[0x8002C02506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02606010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1800); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_G_0_15
    TGT0[0x8002C02606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02706010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x9c00); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_H_0_15
    TGT0[0x8002C02706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02806010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C02806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02906010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x9c00); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_H_0_15
    TGT0[0x8002C02906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02A06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1800); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_G_0_15
    TGT0[0x8002C02A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02B06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f0f); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_F_0_15
    TGT0[0x8002C02B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02C06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x3ef); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_E_0_15
    TGT0[0x8002C02C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02D06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c7); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_D_0_15
    TGT0[0x8002C02D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C02E06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7bc); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_C_0_15
    TGT0[0x8002C02E06010C3F] = l_scom_buffer;

    l_scom_buffer = TGT0[0x8002C02F06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf03e); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_B_0_15
    TGT0[0x8002C02F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x8002C03006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1000); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15
    TGT0[0x8002C03006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3e); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_B_16_22
    TGT0[0x8002C82106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_C_12_ACGH_16_22
    TGT0[0x8002C82206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS0_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x60); // l_IOF1_RX_RX1_RXPACKS_0_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_D_16_22
    TGT0[0x8002C82306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C82406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C82506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C82606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS1_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX1_RXPACKS_1_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C82706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C82806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C82906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS2_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3); // l_IOF1_RX_RX1_RXPACKS_2_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_GH_16_22
    TGT0[0x8002C82A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE3_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_3_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C82B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE1_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x40); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_1_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_EF_16_22
    TGT0[0x8002C82C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE2_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x60); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_2_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_D_16_22
    TGT0[0x8002C82D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE0_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x0); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_0_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_C_12_ACGH_16_22
    TGT0[0x8002C82E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE4_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C82F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x3e); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_4_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_B_16_22
    TGT0[0x8002C82F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RXPACKS3_SLICE5_RX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8002C83006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x42); // l_IOF1_RX_RX1_RXPACKS_3_RXPACK_RD_SLICE_5_RD_RX_BIT_REGS_RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22
    TGT0[0x8002C83006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042106010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042206010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042206010C3F] = l_scom_buffer;

    l_scom_buffer = TGT0[0x8004042306010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042406010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042506010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042606010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042706010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042806010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042906010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042A06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042B06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE0_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042C06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE1_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042D06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE2_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042E06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE3_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004042F06010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004042F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE4_TX_MODE1_PL
    l_scom_buffer = TGT0[0x8004043006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_LANE_PDWN_ENABLED
    TGT0[0x8004043006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2006010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2106010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2106010C3F] = l_scom_buffer;

    l_scom_buffer = TGT0[0x80040C2206010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2306010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2406010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2506010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2606010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2706010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2806010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2906010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2A06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2B06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE0_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2C06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE1_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2D06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE2_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2E06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE3_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C2F06010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C2F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE4_TX_MODE2_PL
    l_scom_buffer = TGT0[0x80040C3006010C3F];
    l_scom_buffer.insert<62, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_CAL_LANE_SEL_ON
    TGT0[0x80040C3006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2106010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C2106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2206010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1e); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_C_0_15
    TGT0[0x80043C2206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2306010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_D_0_15
    TGT0[0x80043C2306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2406010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_E_HALF_B_0_15
    TGT0[0x80043C2406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2506010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_F_0_15
    TGT0[0x80043C2506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2606010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xc63); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_G_0_15
    TGT0[0x80043C2606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2706010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xe73); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_H_0_15
    TGT0[0x80043C2706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2806010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C2806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2906010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xe73); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_H_0_15
    TGT0[0x80043C2906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2A06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xc63); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_G_0_15
    TGT0[0x80043C2A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2B06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_F_0_15
    TGT0[0x80043C2B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE0_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2C06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0xf); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_E_HALF_B_0_15
    TGT0[0x80043C2C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE1_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2D06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1f); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_D_0_15
    TGT0[0x80043C2D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE2_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2E06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x1e); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_C_0_15
    TGT0[0x80043C2E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE3_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C2F06010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C2F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE4_TX_BIT_MODE1_E_PL
    l_scom_buffer = TGT0[0x80043C3006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15
    TGT0[0x80043C3006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004442006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442106010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_B_16_22
    TGT0[0x8004442106010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442206010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7b); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_C_16_22
    TGT0[0x8004442206010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS0_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442306010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX1_TXPACKS_0_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004442306010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442406010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x5e); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_E_16_22
    TGT0[0x8004442406010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442506010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x10); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_F_HALF_A_16_22
    TGT0[0x8004442506010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442606010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004442606010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS1_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442706010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x4e); // l_IOF1_TX_WRAP_TX1_TXPACKS_1_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_H_HALF_B_16_22
    TGT0[0x8004442706010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442806010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004442806010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442906010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x4e); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_H_HALF_B_16_22
    TGT0[0x8004442906010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442A06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004442A06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS2_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442B06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x10); // l_IOF1_TX_WRAP_TX1_TXPACKS_2_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_F_HALF_A_16_22
    TGT0[0x8004442B06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE0_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442C06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x5e); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_0_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_E_16_22
    TGT0[0x8004442C06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE1_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442D06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0xc); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_1_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_DG_16_22
    TGT0[0x8004442D06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE2_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442E06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7b); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_2_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_C_16_22
    TGT0[0x8004442E06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE3_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004442F06010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7c); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_3_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_B_16_22
    TGT0[0x8004442F06010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TXPACKS3_SLICE4_TX_BIT_MODE2_E_PL
    l_scom_buffer = TGT0[0x8004443006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXPACKS_3_TXPACK_DD_SLICE_4_DD_TX_BIT_REGS_TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22
    TGT0[0x8004443006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x8008002006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_HW393297 == 0) {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PG_SPARE_MODE_1_ON
    }
    else {
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PG_SPARE_MODE_1_OFF
    }
    TGT0[0x8008002006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_ID1_PG
    l_scom_buffer = TGT0[0x8008082006010C3F];
    l_scom_buffer.insert<48, 6, 58, uint64_t>(1);
    TGT0[0x8008082006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE1_EO_PG
    l_scom_buffer = TGT0[0x8008102006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_CLKDIST_PDWN_OFF
    TGT0[0x8008102006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE5_EO_PG
    l_scom_buffer = TGT0[0x8008302006010C3F];
    l_scom_buffer.insert<51, 3, 61, uint64_t>(0x5); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP5
    l_scom_buffer.insert<54, 2, 62, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP1
    TGT0[0x8008302006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE7_EO_PG
    l_scom_buffer = TGT0[0x8008402006010C3F];
    l_scom_buffer.insert<60, 4, 60, uint64_t>(0b1010);
    TGT0[0x8008402006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX0_RX_CTL_MODE23_EO_PG
    l_scom_buffer = TGT0[0x8008C00006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<48, 2, 62, uint64_t>(0);
    }
    else {
        l_scom_buffer.insert<48, 2, 62, uint64_t>(0b01);
    }
    TGT0[0x8008C00006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE23_EO_PG
    l_scom_buffer = TGT0[0x8008C02006010C3F];
    l_scom_buffer.insert<55, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PEAK_TUNE_OFF
    l_scom_buffer.insert<57, 2, 62, uint64_t>(0x3);
    l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DFEHISPD_EN_ON
    l_scom_buffer.insert<60, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DFE12_EN_ON
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_DIS_RX_LTE) {
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_LTE_EN_OFF
    }
    else {
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_LTE_EN_ON
    }
    TGT0[0x8008C02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE29_EO_PG
    l_scom_buffer = TGT0[0x8008D02006010C3F];
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0b01010000);
    }
    else if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_VGA_GAIN_TARGET) {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0b01011100);
    }
    else {
        l_scom_buffer.insert<48, 8, 56, uint64_t>(0x0C110);
    }
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b00110111);
    }
    else if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_LOWER_VGA_GAIN_TARGET) {
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b00111101);
    }
    else {
        l_scom_buffer.insert<56, 8, 56, uint64_t>(0b01000100);
    }
    TGT0[0x8008D02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE27_EO_PG
    l_scom_buffer = TGT0[0x8009702006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL_ON
    TGT0[0x8009702006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_ID2_PG
    l_scom_buffer = TGT0[0x8009802006010C3F];
    l_scom_buffer.insert<49, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<57, 7, 57, uint64_t>(0x10);
    TGT0[0x8009802006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE1_E_PG
    l_scom_buffer = TGT0[0x8009902006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_MASTER_MODE) {
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_MASTER_MODE_MASTER
    }
    l_scom_buffer.insert<58, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_PDWN_LITE_DISABLE_ON
    l_scom_buffer.insert<57, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_FENCE_FENCED
    TGT0[0x8009902006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE2_E_PG
    l_scom_buffer = TGT0[0x8009982006010C3F];
    l_scom_buffer.insert<48, 5, 59, uint64_t>(0x01);
    TGT0[0x8009982006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE3_E_PG
    l_scom_buffer = TGT0[0x8009A02006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0xB);
    TGT0[0x8009A02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE5_E_PG
    l_scom_buffer = TGT0[0x8009B02006010C3F];
    l_scom_buffer.insert<52, 4, 60, uint64_t>(0x1);
    TGT0[0x8009B02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE6_E_PG
    l_scom_buffer = TGT0[0x8009B82006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x11);
    l_scom_buffer.insert<55, 7, 57, uint64_t>(0x11);
    TGT0[0x8009B82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE8_E_PG
    l_scom_buffer = TGT0[0x8009C82006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x1111);
    l_scom_buffer.insert<55, 4, 60, uint64_t>(0x5); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RPR_ERR_CNTR1_DURATION_TAP5
    l_scom_buffer.insert<61, 3, 61, uint64_t>(0x05);
    TGT0[0x8009C82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE9_E_PG
    l_scom_buffer = TGT0[0x8009D02006010C3F];
    l_scom_buffer.insert<48, 7, 57, uint64_t>(0x7111);
    l_scom_buffer.insert<55, 4, 60, uint64_t>(0x5); // l_IOF1_RX_RX1_RXCTL_CTL_REGS_RX_CTL_REGS_RX_DYN_RPR_ERR_CNTR2_DURATION_TAP5
    TGT0[0x8009D02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE11_E_PG
    l_scom_buffer = TGT0[0x8009E02006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0);
    TGT0[0x8009E02006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_CTL_MODE12_E_PG
    l_scom_buffer = TGT0[0x8009E82006010C3F];
    l_scom_buffer.insert<48, 8, 56, uint64_t>(0x71111);
    TGT0[0x8009E82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_GLBSM_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800A802006010C3F];
    l_scom_buffer.insert<56, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_GLBSM_REGS_RX_DESKEW_BUMP_AFTER_AFTER
    l_scom_buffer.insert<50, 1, 63, uint64_t>(0x1); // l_IOF1_RX_RX1_RXCTL_GLBSM_REGS_RX_PG_GLBSM_SPARE_MODE_2_ON
    TGT0[0x800A802006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_GLBSM_CNTL3_EO_PG
    l_scom_buffer = TGT0[0x800AE82006010C3F];
    l_scom_buffer.insert<56, 2, 62, uint64_t>(0x02);
    TGT0[0x800AE82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_GLBSM_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800AF82006010C3F];
    l_scom_buffer.insert<48, 4, 60, uint64_t>(0x0C);
    l_scom_buffer.insert<52, 4, 60, uint64_t>(0x0C);
    TGT0[0x800AF82006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_RX1_RX_DATASM_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800B802006010C3F];
    l_scom_buffer.insert<60, 1, 63, uint64_t>(0x0); // l_IOF1_RX_RX1_RXCTL_DATASM_DATASM_REGS_RX_CTL_DATASM_CLKDIST_PDWN_OFF
    if(l_TGT2_ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND) {
        l_scom_buffer.insert<56, 4, 60, uint64_t>(0x02);
    }
    TGT0[0x800B802006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_SPARE_MODE_PG
    l_scom_buffer = TGT0[0x800C042006010C3F];
    l_scom_buffer.insert<56, 2, 62, uint64_t>(0);
    TGT0[0x800C042006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_ID1_PG
    l_scom_buffer = TGT0[0x800C0C2006010C3F];
    l_scom_buffer.insert<48, 6, 58, uint64_t>(1);
    TGT0[0x800C0C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800C142006010C3F];
    l_scom_buffer.insert<48, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXCTL_CTL_REGS_TX_CTL_REGS_TX_CLKDIST_PDWN_OFF
    l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXCTL_CTL_REGS_TX_CTL_REGS_TX_PDWN_LITE_DISABLE_ON
    l_scom_buffer.insert<53, 5, 59, uint64_t>(0b00001);
    TGT0[0x800C142006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_MODE2_EO_PG
    l_scom_buffer = TGT0[0x800C1C2006010C3F];
    l_scom_buffer.insert<56, 7, 57, uint64_t>(0x11);
    TGT0[0x800C1C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_CNTLG1_EO_PG
    l_scom_buffer = TGT0[0x800C242006010C3F];
    l_scom_buffer.insert<48, 2, 62, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DRV_CLK_PATTERN_GCRMSG_DRV_0S
    TGT0[0x800C242006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_ID2_PG
    l_scom_buffer = TGT0[0x800C842006010C3F];
    l_scom_buffer.insert<49, 7, 57, uint64_t>(0);
    l_scom_buffer.insert<57, 7, 57, uint64_t>(0x10);
    TGT0[0x800C842006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_MODE1_E_PG
    l_scom_buffer = TGT0[0x800C8C2006010C3F];
    l_scom_buffer.insert<55, 3, 61, uint64_t>(0x5); // l_IOF1_TX_WRAP_TX1_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP5
    l_scom_buffer.insert<58, 2, 62, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXCTL_CTL_REGS_TX_CTL_REGS_TX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP1
    TGT0[0x800C8C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_MODE2_E_PG
    l_scom_buffer = TGT0[0x800CEC2006010C3F];
    l_scom_buffer.insert<48, 16, 48, uint64_t>(0);
    TGT0[0x800CEC2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTL_MODE3_E_PG
    l_scom_buffer = TGT0[0x800CF42006010C3F];
    l_scom_buffer.insert<48, 8, 56, uint64_t>(0x71111);
    TGT0[0x800CF42006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX1_TX_CTLSM_MODE1_EO_PG
    l_scom_buffer = TGT0[0x800D2C2006010C3F];
    if(l_TGT0_ATTR_IO_XBUS_CHAN_EQ & ENUM_ATTR_IO_XBUS_CHAN_EQ_DIS_TX_AC_BOOST) {
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0x0); // l_IOF1_TX_WRAP_TX1_TXCTL_TX_CTL_SM_REGS_TX_FFE_BOOST_EN_OFF
    }
    else {
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0x1); // l_IOF1_TX_WRAP_TX1_TXCTL_TX_CTL_SM_REGS_TX_FFE_BOOST_EN_ON
    }
    TGT0[0x800D2C2006010C3F] = l_scom_buffer;

    // P9A_XBUS_0_TX_IMPCAL_P_4X_PB
    l_scom_buffer = TGT0[0x800F1C0006010C3F];
    l_scom_buffer.insert<48, 5, 59, uint64_t>(0x70);
    TGT0[0x800F1C0006010C3F] = l_scom_buffer;
}
```
