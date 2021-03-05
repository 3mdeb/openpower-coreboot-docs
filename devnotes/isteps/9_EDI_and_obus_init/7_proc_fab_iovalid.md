# 9.7 proc_fab_iovalid: Lower functional fences on local SMP
### p9_fab_iovalid.C(chip target)
* Reads logical A/X link config, sets iovalid for selected links
* Only performed on trained, valid buses
* After this point a checkstop on a slave will checkstop master
* Reads the A/X link delays for later HWP to pick best link for
coherent traffic
```cpp
fapi2::ReturnCode
p9_fab_iovalid(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
               const bool True,
               const bool True,
               const bool i_manage_optical,
               std::vector<fapi2::ReturnCode>& o_obus_dl_fail_rcs)
{
  # logical link (X/A) configuration parameters
  # arrays indexed by link ID on local end
  # enable on local end
  uint8_t l_x_en[7]
  uint8_t l_a_en[4]
  # link ID on remote end
  uint8_t l_x_rem_link_id[7]
  uint8_t l_a_rem_link_id[4]
  # aggregate (local+remote) delays
  uint32_t l_x_agg_link_delay[7]
  uint32_t l_a_agg_link_delay[4]

  # seed arrays with attribute values
  l_x_en              = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
  l_x_rem_link_id     = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[target]
  l_x_agg_link_delay  = fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[target]
  l_a_en              = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]
  l_a_rem_link_id     = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[target]
  l_a_agg_link_delay  = fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[target]

  # Add delay for dd1.1+ procedure to compensate for lack of lane lock polls
  sleep(100000000ns)

  for l_link_id in range(0, 7):
    if l_x_en[l_link_id]:
      if P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
      or (i_manage_optical and (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL)):

        if P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL:
          l_rc = p9_fab_iovalid_link_validate<fapi2::TARGET_TYPE_XBUS>(
            i_target,
            P9_FBC_XBUS_LINK_CTL_ARR[l_link_id],
            P9_FBC_XBUS_LINK_CTL_ARR[l_x_rem_link_id[l_link_id]])
        else:
          l_rc = p9_fab_iovalid_link_validate<fapi2::TARGET_TYPE_OBUS>(
            i_target,
            P9_FBC_XBUS_LINK_CTL_ARR[l_link_id],
            P9_FBC_XBUS_LINK_CTL_ARR[l_x_rem_link_id[l_link_id]])

        # form data buffers for iovalid/RAS FIR mask updates
        l_iovalid_mask = 0
        if (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
            l_iovalid_mask |= 1 << P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit

        if (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
            l_iovalid_mask |= 1 << (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit + 1)

        l_fbc_cent_fir_data = PU_PB_CENT_SM0_PB_CENT_FIR_REG[i_target]
        # clear RAS FIR mask for optical link, or electrical link if not already setup by SBE
        if (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_14
        or (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13:
            # get the value of the action registers, clear the bit and write it
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1190
            PU_PB_CENT_SM1_EXTFIR_ACTION0_REG[i_target] &= ~P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit
            PU_PB_CENT_SM1_EXTFIR_ACTION1_REG[i_target] &= ~i_P9_FBC_XBUS_LINK_CTL_ARR[l_link_id]ctl.ras_fir_field_bit

            # clear associated mask bit
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1204
            PU_PB_CENT_SM1_EXTFIR_MASK_REG_AND[i_target] = 0xFFFFFFFFFFFFFFFF & ~P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

        # use AND/OR mask registers to atomically update link specific fields
        # in iovalid control register
        # REGISTER write
        P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_or_addr[i_target] = l_iovalid_mask

        # This value is probably result of a bug
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1021
        # called at
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1110
        # o_link_delay should be an output parameter,
        # but it is not a pointer nor a reference
        l_x_agg_link_delay[l_link_id] = 0x1FFE

  for l_link_id in range(0, 4):
    if l_a_en[l_link_id]:
      if  i_manage_optical
      and P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL:
        l_rc = p9_fab_iovalid_link_validate<T>(
          i_target,
          P9_FBC_ABUS_LINK_CTL_ARR[l_link_id],
          P9_FBC_ABUS_LINK_CTL_ARR[l_a_rem_link_id[l_link_id]])

        l_iovalid_mask = 0
        if (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
            l_iovalid_mask |= 1 << P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit

        if (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
            l_iovalid_mask |= 1 << (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit + 1)

        l_fbc_cent_fir_data = PU_PB_CENT_SM0_PB_CENT_FIR_REG[i_target]

        # clear RAS FIR mask for optical link, or electrical link if not already setup by SBE
        if (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_14
        or (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13:
            # get the value of the action registers, clear the bit and write it
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1190
            PU_PB_CENT_SM1_EXTFIR_ACTION0_REG[i_target] &= ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit
            PU_PB_CENT_SM1_EXTFIR_ACTION1_REG[i_target] &= ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

            # clear associated mask bit
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1204
            PU_PB_CENT_SM1_EXTFIR_MASK_REG_AND[i_target] = 0xFFFFFFFFFFFFFFFF & ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

        # use AND/OR mask registers to atomically update link specific fields
        # in iovalid control register
        # REGISTER write
        P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_or_addr[i_target] = l_iovalid_mask

        # This value is probably result of a bug
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1021
        # called at
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1110
        # o_link_delay should be an output parameter,
        # but it is not a pointer nor a reference
        l_x_agg_link_delay[l_link_id] = 0x1FFE

  # update link delay attributes
  fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[i_target] = l_x_agg_link_delay
  fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[i_target] = l_a_agg_link_delay
}

#/ @brief Validate DL/TL link layers are trained
#/
#/ @param[in]  i_target          Processor chip target
#/ @param[in]  i_loc_link_ctl    X/A link control structure for link local end
#/ @param[in]  i_rem_link_ctl    X/A link control structure for link remote end
#/
#/ @return fapi2::ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
template<fapi2::TargetType T>
fapi2::ReturnCode p9_fab_iovalid_link_validate(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9_fbc_link_ctl_t& i_loc_link_ctl,
    const p9_fbc_link_ctl_t& i_rem_link_ctl)
{
  l_dl_status_reg = None
  l_dl_trained = False
  l_dl_status_even = 0
  l_dl_prior_status_even = 0
  l_dl_fail_even = False
  l_dl_status_odd = 0
  l_dl_prior_status_odd = 0
  l_dl_fail_odd = False

  for children_target in i_target.getChildren<T>():
    if (static_cast<fapi2::TargetType>(i_loc_link_ctl.endp_type) == T)
    and (i_loc_link_ctl.endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[children_target]):
      l_loc_endp_target = children_target
      break;

  l_loc_link_train = fapi2::ATTR_LINK_TRAIN[l_loc_endp_target]

  # poll for DL trained indications
  while not l_dl_trained:
    # validate DL training state
    l_dl_fir_reg = i_loc_link_ctl.dl_fir_addr[i_target]
    l_dl_status_reg = i_loc_link_ctl.dl_status_addr[i_target]
    # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:636
    l_dl_status_even        = (l_dl_status_reg & (0x0F <<  4)) >> 4
    l_dl_prior_status_even  = (l_dl_status_reg & (0x0F << 12)) >> 12
    l_dl_status_odd         = (l_dl_status_reg & (0x0F << 28)) >> 28
    l_dl_prior_status_odd   = (l_dl_status_reg & (0x0F << 36)) >> 36

    if l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH:
      l_dl_trained = (l_dl_fir_reg & DL_FIR_LINK0_TRAINED_BIT)
                 and (l_dl_fir_reg & DL_FIR_LINK1_TRAINED_BIT)
      if not l_dl_trained:
        l_dl_fail_even =
          not ((((l_dl_status_even == 0x8) or (l_dl_prior_status_even == 0x8) or (l_dl_status_even == 0x9) or (l_dl_prior_status_even == 0x9))
          and ((l_dl_status_odd  >= 0xB) and (l_dl_status_odd  <= 0xE))) or ((l_dl_status_even == 0x2) and ((l_dl_status_odd  >= 0x8) and (l_dl_status_odd  <= 0xC))))
        l_dl_fail_odd =
          not ((((l_dl_status_odd == 0x8) or (l_dl_prior_status_odd == 0x8) or (l_dl_status_odd == 0x9) or (l_dl_prior_status_odd == 0x9))
          and ((l_dl_status_even >= 0xB) and (l_dl_status_even <= 0xE))) or ((l_dl_status_odd == 0x2) and ((l_dl_status_even >= 0x8) and (l_dl_status_even <= 0xC))))
    elif l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY:
      l_dl_trained   = (l_dl_fir_reg & (1 << DL_FIR_LINK0_TRAINED_BIT)) == 0
      l_dl_fail_even = not l_dl_trained
      l_dl_fail_odd  = True
    else:
      l_dl_trained   = (l_dl_fir_reg & (1 << DL_FIR_LINK1_TRAINED_BIT)) == 0
      l_dl_fail_even = True
      l_dl_fail_odd  = not l_dl_trained

    if not l_dl_trained:
      sleep(1000000ns)

  # OBUS DL reported trained, need to validate that no lane sparing occurred
  # in some cases, a spare may occur but not report in the FIR
  #
  # as we are not persisting bad lane information, we dont want to fail the
  # IPL directly if a single spare occurs, but can raise a FIR to indicate that the
  # spare has been consumed (MFG may choose to fail based on this criteria)
  #
  # if more than one spare is detected, mark the link as failed
  if l_dl_trained and (T == fapi2::TARGET_TYPE_OBUS):
    l_dl_fail_by_lane_status = False
    if not l_dl_fail_even and not l_dl_fail_odd:
      if not l_dl_fail_even:
        # REGISTER address
        l_dl_rx_control_addr = i_loc_link_ctl.dl_control_addr+7
        l_lane_failed     = (l_dl_rx_control_addr[i_target] & (0x7FF << 48)) >> 48
        l_lane_not_locked = (l_dl_rx_control_addr[i_target] & (0x7FF << 36)) >> 36
      # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:537
      l_lane_failed_count = count_bits_set(l_lane_failed)
      l_lane_not_locked_count = count_bits_set(l_lane_not_locked)
      if not ((l_lane_failed_count == 0) and (l_lane_not_locked_count == 0)):
        if (l_lane_failed_count <= 1) and (l_lane_not_locked_count <= 1):
          if i_even_not_odd:
            l_dl_fir |= 1 << 44
          else:
            l_dl_fir |= 1 << 45
          # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:560
          # REGISTER write
          i_loc_link_ctl.dl_fir_addr |= l_dl_fir
        else:
          l_dl_fail_by_lane_status = True
      if l_dl_fail_odd and l_dl_fail_by_lane_status:
        l_dl_trained = False
        l_dl_fail_even = True

      if not l_dl_fail_odd:
        # REGISTER address
        l_dl_rx_control_addr = i_loc_link_ctl.dl_control_addr+8)
        l_lane_failed     = (l_dl_rx_control_addr[i_target] & (0x7FF << 48)) >> 48
        l_lane_not_locked = (l_dl_rx_control_addr[i_target] & (0x7FF << 36)) >> 36
      # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:537
      l_lane_failed_count = count_bits_set(l_lane_failed)
      l_lane_not_locked_count = count_bits_set(l_lane_not_locked)
      if not ((l_lane_failed_count == 0) and (l_lane_not_locked_count == 0)):
        if (l_lane_failed_count <= 1) and (l_lane_not_locked_count <= 1):
          if i_even_not_odd:
            l_dl_fir |= 1 << 44
          else:
            l_dl_fir |= 1 << 45
          # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:560
          # REGISTER write
          i_loc_link_ctl.dl_fir_addr |= l_dl_fir
        else:
          l_dl_fail_by_lane_status = True
      if l_dl_fail_by_lane_status:
        l_dl_trained = False
        l_dl_fail_odd = True

  # control reconfig loop behavior
  if not l_dl_trained:
    if l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH
    and T == fapi2::TARGET_TYPE_OBUS
    and ((l_dl_fail_even and not l_dl_fail_odd) or (not l_dl_fail_even and l_dl_fail_odd)):
      if l_dl_fail_even:
        fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY
      else
        fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY
    else
      fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_NONE
    return
}
```
