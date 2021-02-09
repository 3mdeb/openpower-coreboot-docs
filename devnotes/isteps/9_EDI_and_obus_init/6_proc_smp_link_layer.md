# 9.6 proc_smp_link_layer: Start SMP link layer
### p9_smp_link_layer.C(called on processor chip)
* Reads logical A/X link configuration attributes, trains the
DL/TL layers of selected links
* Set scom on both sides of the bus to trigger Data link layer training
* DLL sends training packets, sets link up FIR bit when done
* FIR done bit launches the Transaction Layer (TL)
* FIR bit in nest domain to indicate training done
* After this point the mailbox register are available to communicate
- Xstop would prevent mailbox communication
* Bus is NOT part of the SMP coherency
* Only performed on trained, valid buses

```python
# logical link (X/A) configuration parameters
# enable on local end
uint8_t l_x_en[7]
uint8_t l_a_en[4]

# process set of enabled links
l_x_en = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
l_a_en = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]

for link_index in range(0, 7):
  link = l_x_en[link_index]
  if link != 0:
    # defined in src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:98
    # first 3 are electrical, any else is optical
    if P9_FBC_XBUS_LINK_CTL_ARR[link_index].endpoint_type == ELECTRICAL:
      if (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
      or (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
        # REGISTER!!!!!!
        # update control register
        # register in a structure src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:81
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr |= 1 << 1

      if (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
      or (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
        # REGISTER!!!!!!
        # update control register
        # register in a structure src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:81
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr |= 1 << 33
    elif link.endpoint_type == OPTICAL:
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:328
      # p9_smp_link_layer_train_link_optical()
      even = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
          or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_EVEN_ONLY)
      odd  = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
          or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_ODD_ONLY)

      # find local endpoint target associated with this link
      for each target.getChildrens(fapi2::TARGET_TYPE_OBUS):
        if (fapi2::ATTR_CHIP_UNIT_POS[target] == P9_FBC_XBUS_LINK_CTL_ARR[link_index].endp_unit_id):
          local_target = target
          break
      remote_target = local_target.getOtherEnd()

      if even:
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
          (1 << 0) | (1 << 1)
      if odd:
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
          (1 << 32) | (1 << 33)
      # CQ: HW453889 :: MFG Abus Stress >>>
      # Use rx_pr_data_a_offset to shift the offset by +1/2 or +2/4
      if even and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target] != 0):
        for lane_index in range(0, 11):
          # set PHY TX lane address, start at:
          # - PHY lane 0 for even (work up)
          # - PHY lane 23 for odd (work down)

          # REGISTER address!!!
          # OBUS_RX0_RXPACKS0_SLICE0_RX_BIT_CNTL3_EO_PL
          # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
          l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f
          l_phy_rx_bit_cntl3_eo_pl_addr |= (lane_index << 32)

          fapi2::buffer<uint64_t> l_phy_rx_bit_cntl3_eo_pl
          # REGISTER read
          l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
          l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
          l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 48)
                                    | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 54)
          # REGISTER write
          l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
      if odd and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target] != 0):
        for lane_index in range(0, 11):
          # set PHY TX lane address, start at:
          # - PHY lane 0 for even (work up)
          # - PHY lane 23 for odd (work down)

          # REGISTER address!!!
          l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | ((23-lane_index) << 32)

          fapi2::buffer<uint64_t> l_phy_rx_bit_cntl3_eo_pl
          # REGISTER read
          l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
          l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
          l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 48)
                                    | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 54)
          # REGISTER write
          l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
for link_index in range(0, 4):
  # all are optical
  link = l_a_en[link_index]
  # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:328
  # p9_smp_link_layer_train_link_optical()
  even = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
      or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_EVEN_ONLY)
  odd  = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
      or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_ODD_ONLY)

  # find local endpoint target associated with this link
  for each target.getChildrens(fapi2::TARGET_TYPE_OBUS):
    if (fapi2::ATTR_CHIP_UNIT_POS[target] == P9_FBC_ABUS_LINK_CTL_ARR[link_index].endp_unit_id):
      local_target = target
      break
  remote_target = local_target.getOtherEnd()

  if even:
    P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
      (1 << 0) | (1 << 1)
  if odd:
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
        (1 << 32) | (1 << 1)
  # CQ: HW453889 :: MFG Abus Stress >>>
  # Use rx_pr_data_a_offset to shift the offset by +1/2 or +2/4
  if even and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target] != 0)
    for lane_index in range(0, 11):
      # set PHY TX lane address, start at:
      # - PHY lane 0 for even (work up)
      # - PHY lane 23 for odd (work down)

      # REGISTER address!!!
      # OBUS_RX0_RXPACKS0_SLICE0_RX_BIT_CNTL3_EO_PL
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
      l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | (lane_index << 32)

      # REGISTER read!!!
      l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
      l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
      l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 48)
                                | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 54)
      # REGISTER write!!!
      l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
  if odd and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target] != 0):
    for lane_index in range(0, 11):
      # set PHY TX lane address, start at:
      # - PHY lane 0 for even (work up)
      # - PHY lane 23 for odd (work down)

      # REGISTER address!!!
      # OBUS_TX0_TXPACKS0_SLICE0_TX_MODE1_PL
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
      l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | ((23-lane_index) << 32)

      # REGISTER read
      l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
      l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
      l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 48)
                                | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 54)
      # REGISTER write
      l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
```
