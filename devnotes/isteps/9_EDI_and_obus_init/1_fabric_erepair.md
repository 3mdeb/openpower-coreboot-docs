# 9.1 fabric_erepair: Restore Fabric Bus eRepair data
### p9_io_restore_erepair.C(O, X bus target pairs)

* Restore/preset bad lanes on electrical O and X buses from VPD
(in drawer)
* Applies powerbus repair data from module vpd (#ER keyword in VRML VWML)
* Runtime detected fails that were written to VPD are restored here
* NOOP for Cronus

```python
For each lane_to_restore:
    if is_rx(lane_to_restore):
        # mark lines as disabled
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_restore_erepair.C:143
        if(lane_index < IO_GCR_REG_WIDTH):
            EDIP_RX_LANE_BAD_VEC_0_15 |= 0x8000 >> lane_to_restore
        else:
            EDIP_RX_LANE_BAD_VEC_16_23 |= 0x8000 >> lane_to_restore

        # power down digital and analog recieve lines
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_pdwn_lanes.C:139
        EDIP_RX_LANE_DIG_PDWN[lane_to_restore] = 1
        EDIP_RX_LANE_ANA_PDWN[lane_to_restore] = 1
    else:
        # power down transmit lines
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_pdwn_lanes.C:209
        EDIP_TX_LANE_PDWN[lane_to_restore] = 1
```
