# 9.8 host_fbc_eff_config_aggregate: Pick link(s) for coherency
### p9_fbc_eff_config_aggregate.C(chip target)
* Reads attributes from previous HWP and determines per-link address/data
capabilities
* Sets up attributes for build SMP

```python
foreach target in processorChips:
  # read attributes for this chip
  l_x_en                = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
  l_x_rem_link_id       = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[target]
  l_x_rem_fbc_chip_id   = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[target]
  l_x_agg_link_delay    = fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[target]
  l_x_addr_dis          = fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[target]
  l_x_aggregate         = fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[target]
  l_a_en                = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]
  l_a_rem_link_id       = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[target]
  l_a_rem_fbc_group_id  = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_ID[target]
  l_a_agg_link_delay    = fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[target]
  l_a_addr_dis          = fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[target]
  l_a_aggregate         = fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[target]

  l_loc_fbc_chip_id   = fapi2::ATTR_PROC_FABRIC_CHIP_ID[target]
  l_loc_fbc_group_id  = fapi2::ATTR_PROC_FABRIC_GROUP_ID[target]
  l_pump_mode         = fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM]

  # calculate aggregate configuration
  # p9_fbc_eff_config_aggregate_link_setup()
  # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config_aggregate.C:61
  #
  # mark number of links targeting each fabric ID
  # set output defaults to disable aggregate mode (all links carry coherent traffic)
  l_fbc_id_active_count[8] = []

  for l_loc_id in range(0, 7):
    # if link is valid, bump fabric ID usage count
    if l_x_en[l_loc_link_id]:
      l_fbc_id_active_count[l_x_rem_fbc_chip_id[l_loc_link_id]]++
    l_x_addr_dis[l_loc_link_id] = 0

  l_x_aggregate = 0

  # set aggregate mode if more than one link is pointed at the same remote
  # fabric ID
  for l_rem_fbc_id in range(0, 8)
    if l_fbc_id_active_count[l_rem_fbc_id] > 1:
      l_x_aggregate = 1

      # flip default value for link address disable
      for l_loc_link_id in range(0, 7)
      if(l_x_en[l_loc_link_id])
        l_x_addr_dis[l_loc_link_id] = 1
      else:
        l_x_addr_dis[l_loc_link_id] = 0

      # scan link delays for smallest value
      # looks for minimal value
      uint32_t l_loc_coherent_link_delay = 0xFFFFFFFF
      for l_loc_link_id in range(0, 7):
        if l_x_en[l_loc_link_id]
        and l_x_agg_link_delay[l_loc_link_id] < l_loc_coherent_link_delay:
          l_loc_coherent_link_delay = l_x_agg_link_delay[l_loc_link_id]

      # determine if more than one link matches the minimum delay
      l_matches = 0
      l_loc_coherent_link_id = 0xFF
      for l_loc_link_id in range(0, 7):
        if l_x_en[l_loc_link_id]
        and (l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
          l_matches++
          l_loc_coherent_link_id = l_loc_link_id

      # ties must be broken consistenty on both connected chips (i.e., we
      # need to pick both ends of the same link to carry coherency
      # select link with lowest link ID number on chip with smaller fabric ID
      if l_matches != 1:
        if (fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE and l_loc_fbc_chip_id  < l_rem_fbc_id)
        or (fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE and l_loc_fbc_group_id < l_rem_fbc_id)
          # local fabric ID is smaller than remote
          # looks for minimal value
          for l_loc_link_id in range(0, 7):
            if l_x_en[l_loc_link_id]
            and l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay:
              l_loc_coherent_link_id = l_loc_link_id
              break
        else
          # remote fabric ID is smaller than local
          # looks for minimal value
          l_rem_coherent_link_id = 0xFF
          for l_loc_link_id in range(0, 7)
            if l_x_en[l_loc_link_id]
            and l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay
            and l_x_rem_link_id[l_loc_link_id] < l_rem_coherent_link_id:
              l_rem_coherent_link_id = l_x_rem_link_id[l_loc_link_id]
              l_loc_coherent_link_id = l_loc_link_id
      l_x_addr_dis[l_loc_coherent_link_id] = 0

  # calculate aggregate configuration
  # p9_fbc_eff_config_aggregate_link_setup()
  # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config_aggregate.C:61
  #
  # mark number of links targeting each fabric ID
  # set output defaults to disable aggregate mode (all links carry coherent traffic)
  l_fbc_id_active_count[8] = []

  for l_loc_id in range(0, 4):
    # if link is valid, bump fabric ID usage count
    if l_a_en[l_loc_link_id]:
      l_fbc_id_active_count[l_a_rem_fbc_group_id[l_loc_link_id]]++
    l_a_addr_dis[l_loc_link_id] = 0

  l_a_aggregate = 0

  # set aggregate mode if more than one link is pointed at the same remote
  # fabric ID
  for l_rem_fbc_id in range(0, 8)
    if l_fbc_id_active_count[l_rem_fbc_id] > 1:
      l_a_aggregate = 1

      # flip default value for link address disable
      for l_loc_link_id in range(0, 4)
        if l_a_en[l_loc_link_id]:
          l_a_addr_dis[l_loc_link_id] = 1
        else:
          l_a_addr_dis[l_loc_link_id] = 0

      # scan link delays for smallest value
      # looks for minimal value
      uint32_t l_loc_coherent_link_delay = 0xFFFFFFFF
      for l_loc_link_id in range(0, 4):
        if l_a_en[l_loc_link_id]
        and l_a_agg_link_delay[l_loc_link_id] < l_loc_coherent_link_delay:
          l_loc_coherent_link_delay = l_a_agg_link_delay[l_loc_link_id]

      # determine if more than one link matches the minimum delay
      l_matches = 0
      l_loc_coherent_link_id = 0xFF
      for l_loc_link_id in range(0, 4):
        if l_a_en[l_loc_link_id]
        and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
          l_matches++
          l_loc_coherent_link_id = l_loc_link_id

      # ties must be broken consistenty on both connected chips (i.e., we
      # need to pick both ends of the same link to carry coherency
      # select link with lowest link ID number on chip with smaller fabric ID
      if l_matches != 1:
        if l_loc_fbc_group_id < l_rem_fbc_id:
          # local fabric ID is smaller than remote
          # looks for minimal value
          for l_loc_link_id in range(0, 4):
            if l_a_en[l_loc_link_id]
            and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
              l_loc_coherent_link_id = l_loc_link_id
              break
        else
          # remote fabric ID is smaller than local
          # looks for minimal value
          l_rem_coherent_link_id = 0xFF
          for l_loc_link_id in range(0, 4)
            if l_a_en[l_loc_link_id]
            and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay)
            and (l_a_rem_link_id[l_loc_link_id] < l_rem_coherent_link_id):
              l_rem_coherent_link_id = l_a_rem_link_id[l_loc_link_id]
              l_loc_coherent_link_id = l_loc_link_id
      l_a_addr_dis[l_loc_coherent_link_id] = 0

  # set attributes
  fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[target]  = l_x_addr_dis
  fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[target] = l_x_aggregate
  fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[target]  = l_a_addr_dis
  fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[target] = l_a_aggregate
```
