# 9.5 fabric_post_trainadv: Advanced post EI/EDI training
### p9_io_post_trainadv.C(called on each O and X bus target pair)
* Debug routine for IO Characterization
* Nothing in it

```
for each bus:
  if typeof(bus) == TYPE_OBUS:
    if fapi2::ATTR_MNFG_FLAGS & fapi2::ENUM_ATTR_MNFG_FLAGS_MNFG_THRESHOLDS != 0:
        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C:63
        # Setup ECC & CRC Masks
        OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_OR =
          OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK0_SL_ECC_CORRECTABLE
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_SL_ECC_CORRECTABLE
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK0_TOO_MANY_CRC_ERRORS
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_TOO_MANY_CRC_ERRORS

        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C
        # Setup Performance Counters
        OBUS_LL0_IOOL_PERF_SEL_CONFIG =
          0x1B | (0x14 << 8) | (0x1B << 16) | (0x14 << 24)

        # Setup Performance Counters
        OBUS_LL0_IOOL_PERF_TRACE_CONFIG =
             0x02
          | (0x02 << 2)
          | (0x01 << 8)
          | (0x01 << 10)
          | (0x01 << 16)
          | (0x01 << 18)
          | (0x01 << 24)
          | (0x01 << 26)

        # Setup Performance Counters
        # Make it so there are only 10 or 5 ECC errors allowed per lane or 10 or 5 CRC errors allowed
        # per link (reliability test) --- Have a flag to go between 3.5/3.7 settings
        # Ex. putscom pu 0901080F 0F000F00000000000 -bor -pall
        # Ex. putscom pu 0901080F FF8AFF8AFFFFFFFF -band -pall
        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C:116
        OBUS_LL0_IOOL_OPTICAL_CONFIG |= (0xF << 4) | (0xF << 20)

        OBUS_LL0_IOOL_OPTICAL_CONFIG |= (0x7F << 9) | (0x7F << 25)
        if fapi2::ATTR_IO_O_MNFG_ERROR_THRESHOLD == fapi2::ENUM_ATTR_IO_O_MNFG_ERROR_THRESHOLD_CORNER_MODE:
          OBUS_LL0_IOOL_OPTICAL_CONFIG &= (5 << 9) | (5 << 25)
        elif fapi2::ATTR_IO_O_MNFG_ERROR_THRESHOLD == ENUM_ATTR_IO_O_MNFG_ERROR_THRESHOLD_RELIABILITY_MODE:
          OBUS_LL0_IOOL_OPTICAL_CONFIG &= (10 << 9) | (10 << 25)
```
