# What this document is about?
This document is an attempt to gather information about undocumented registers
and registers that code analysis proofed to work diffrently than described.

**P9N2_MCS_PORT02_MCPERF0 0x5010823**
| Bit range | Name      | Assigned value | Description |
| --------- | --------- | -------------- | ----------- |
| 22-27     | AMO_LIMIT | 0x20           |             |

**P9N2_MCS_PORT02_MCPERF2 0x5010824**
| Bit range | Name                      | Assigned value                             | Description                                                                                 |
| --------- | ------------------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------- |
| 0-2       | PF_DROP_VALUE0            | 1                                          |                                                                                             |
| 3-5       | PF_DROP_VALUE1            | 3                                          |                                                                                             |
| 6-8       | PF_DROP_VALUE2            | 5                                          |                                                                                             |
| 9-11      | PF_DROP_VALUE3            | 7                                          |                                                                                             |
| 13-15     | REFRESH_BLOCK_CONFIG      |                                            | if has only one DIMM in MCA: <br> 0b000 : if master ranks = 1 <br> 0b001 : if master ranks = 2 <br> 0b100 : if master ranks = 4 <br> Per allowable DIMM mixing rules, we cannot mix different number of ranks on any single port <br> if has both DIMMs in MCA: <br> 0b010 : if master ranks = 1 <br> 0b011 : if master ranks = 2 <br> 0b100 : if master ranks = 4 // 4 mranks is the same for one and two DIMMs in MCA|
| 16        | ENABLE_REFRESH_BLOCK_SQ   |                                            | Always same value as [17]                                                                   |
| 17        | ENABLE_REFRESH_BLOCK_NSQ  |                                            | 1 : if (1 < (DIMM0 + DIMM1 logical ranks) <= 8 && not (one DIMM, 4 mranks, 2H 3DS)          |
|           |                           |                                            | 0 : otherwise                                                                               |
| 18        | ENABLE_REFRESH_BLOCK_DISP | 0                                          |                                                                                             |
| 28-31     | SQ_LFSR_CNTL              | 0b0100                                     |                                                                                             |
| 50-54     | NUM_RMW_BUF               | 0b11100                                    |                                                                                             |
| 61        | EN_ALT_ECR_ERR            | ATTR_ENABLE_MEM_EARLY_DATA_SCOM probably 0 |                                                                                             |

**P9N2_MCS_PORT02_MCAMOC 0x5010825**
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 1         | FORCE_PF_DROP0                    | 0              |             |
| 4-28      | WRTO_AMO_COLLISION_RULES          | 0x19FFFFF      |             |
| 29-31     | AMO_SIZE_SELECT, 128B_RW_64B_DATA | 1              |             |

**P9N2_MCS_PORT02_MCEPSQ 0x5010826** \
Note: ATTR_PROC_EPS_READ_CYCLES_T* are calculated in istep 8.6
| Bit range | Name                 | Assigned value                         | Description |
| --------- | -------------------- | -------------------------------------- | ----------- |
| 0-7       | JITTER_EPSILON       | 1                                      |             |
| 8-15      | LOCAL_NODE_EPSILON   | (ATTR_PROC_EPS_READ_CYCLES_T0 + 6) / 4 |             |
| 16-23     | NEAR_NODAL_EPSILON   | (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4 |             |
| 24-31     | GROUP_EPSILON        | (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4 |             |
| 32-39     | REMOTE_NODAL_EPSILON | (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4 |             |
| 40-47     | VECTOR_GROUP_EPSILON | (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4 |             |

**P9N2_MCS_PORT02_MCBUSYQ 0x5010827**
| Bit range | Name                       | Assigned value | Description          |
| --------- | -------------------------- | -------------- | -------------------- |
| 0         | ENABLE_BUSY_COUNTERS       | 1              |                      |
| 1-3       | BUSY_COUNTER_WINDOW_SELECT | 1              | 1024 cycles selected |
| 4-13      | BUSY_COUNTER_THRESHOLD0    | 38             |                      |
| 14-23     | BUSY_COUNTER_THRESHOLD1    | 51             |                      |
| 24-33     | BUSY_COUNTER_THRESHOLD2    | 64             |                      |

**P9N2_MCS_PORT02_MCPERF3 0x501082B**
| Bit range | Name                        | Assigned value                   | Description |
| --------- | --------------------------- | -------------------------------- | ----------- |
| 31        | ENABLE_CL0                  | 1                                |             |
| 41        | ENABLE_AMO_MSI_RMW_ONLY     | 1                                |             |
| 43        | ENABLE_CP_M_MDI0_LOCAL_ONLY | !ATTR_ENABLE_MEM_EARLY_DATA_SCOM | !0 = 1?     |
| 44        | DISABLE_WRTO_IG             | 1                                |             |
| 45        | AMO_LIMIT_SEL               | 1                                |             |

**MCS_MCFIRACT1 0x5010807**
| Bit range | Name                                    | Assigned value | Description |
| --------- | --------------------------------------- | -------------- | ----------- |
| 0         | MCS_MCFIR_MC_INTERNAL_RECOVERABLE_ERROR | 1              |             |
| 8         | MCS_MCFIR_COMMAND_LIST_TIMEOUT          | 1              |             |
| 17        | MCS_MCFIR_MS_WAT_DEBUG_CONFIG_REG_ERROR |                |             |

**MCS_MCFIRMASK_AND 0x5010804**
| Bit range | Name                                          | Assigned value | Description |
| --------- | --------------------------------------------- | -------------- | ----------- |
| 0         | MCS_MCFIR_MC_INTERNAL_RECOVERABLE_ERROR       | 0              |             |
| 1         | MCS_MCFIR_MC_INTERNAL_NONRECOVERABLE_ERROR    | 0              |             |
| 2         | MCS_MCFIR_POWERBUS_PROTOCOL_ERROR             | 0              |             |
| 3         | MCS_MCFIR_INBAND_BAR_HIT_WITH_INCORRECT_TTYPE | 1              |             |
| 4         | MCS_MCFIR_MULTIPLE_BAR                        | 0              |             |
| 5         | MCS_MCFIR_INVALID_ADDRESS                     | 0              |             |
| 8         | MCS_MCFIR_COMMAND_LIST_TIMEOUT                | 0              |             |
| 17        | MCS_MCFIR_MS_WAT_DEBUG_CONFIG_REG_ERROR       | 1              |             |

**MCS_MCFGP 0x501080A**
| Bit range | Name                                            | Assigned value | Description |
| --------- | ----------------------------------------------- | -------------- | ----------- |
| 0         | MCS_MCFGP_VALID                                 |                |             |
| 1-4       | MCS_MCFGP_MC_CHANNELS_PER_GROUP                 |                |             |
| 5-7       | MCS_MCFGP_CHANNEL_0_GROUP_MEMBER_IDENTIFICATION |                |             |
| 8-10      | MCS_MCFGP_CHANNEL_1_GROUP_MEMBER_IDENTIFICATION |                |             |
| 13-23     | MCS_MCFGP_GROUP_SIZE                            |                |             |
| 24-47     | MCS_MCFGP_GROUP_BASE_ADDRESS                    |                | Group base address (bits 24:47) 0b000000000000000000000001 = 4GB<br> 000000001 (base addr of 4GB)<br> 000000010 (base addr of 8GB)<br> 000000100 (base addr of 16GB)<br> 000001000 (base addr of 32GB)<br> 000010000 (base addr of 64GB)<br> 000100000 (base addr of 128GB)<br> 001000000 (base addr of 256GB) |

**MCS_MCFGPM 0x501080C**
| Bit range | Name                          | Assigned value | Description |
| --------- | ----------------------------- | -------------- | ----------- |
| 0         | MCS_MCFGPM_VALID              | 1              |             |
| 13-23     | MCS_MCFGPM_GROUP_SIZE         | 1              |             |
| 24-47     | MCS_MCFGPM_GROUP_BASE_ADDRESS |                | Group base address (bits 24:47), 0b000000000000000000000001 = 4GB<br>000000001 (base addr of 4GB)<br>000000010 (base addr of 8GB)<br>000000100 (base addr of 16GB)<br>000001000 (base addr of 32GB)<br>000010000 (base addr of 64GB)<br>000100000 (base addr of 128GB)<br>001000000 (base addr of 256GB) |

**MCS_MCFGPA 0x501080B**

NOTE: some of the bit-fields overlap

<a name="hole_description">
HOLE1 and SMF cannot be both valid. Hostboot asserts in that case only now, after calculating both ranges and
passing both sets of variables. This could be checked all the way back in 7.4
</a>

| Bit range | Name                                              | Assigned value | Description        |
| --------- | ------------------------------------------------- | -------------- | ------------------ |
| 0         | MCS_MCFGPA_HOLE0_VALID                            |                |                    |
| 2-11      | MCS_MCFGPA_HOLE0_LOWER_ADDRESS                    |                | 0b0000000001 = 4GB |
| 14-23     | MCS_MCFGPA_HOLE0_UPPER_ADDRESS                    |                | 0b0000000001 = 4GB |
| 24        | MCS_MCFGPA_HOLE1_VALID                            |                |                    |
| 26-35     | MCS_MCFGPA_HOLE1_LOWER_ADDRESS                    |                | 0b0000000001 = 4GB |
| 38-47     | MCS_MCFGPA_HOLE1_UPPER_ADDRESS                    |                | 0b0000000001 = 4GB |
| 28        | P9N2_MCS_MCFGPA_SMF_VALID                         |                |                    |
| 29        | P9N2_MCS_MCFGPA_SMF_UPPER_ADDRESS_AT_END_OF_RANGE |                |                    |
| 30-43     | P9N2_MCS_MCFGPA_SMF_LOWER_ADDRESS                 |                |                    |
| 44-57     | P9N2_MCS_MCFGPA_SMF_UPPER_ADDRESS                 |                |                    |

**MCS_MCFGPMA 0x501080D**

NOTE: some of the bit-fields overlap
You can find more information [here](#hole_description).

| Bit range | Name                                               | Assigned value | Description           |
| --------- | -------------------------------------------------- | -------------- | --------------------- |
| 0         | MCS_MCFGPMA_HOLE0_VALID                            |                |                       |
| 2-11      | MCS_MCFGPMA_HOLE0_LOWER_ADDRESS                    |                | // 0b0000000001 = 4GB |
| 14-23     | MCS_MCFGPMA_HOLE0_UPPER_ADDRESS                    |                | // 0b0000000001 = 4GB |
| 24        | MCS_MCFGPMA_HOLE1_VALID                            |                |                       |
| 26-35     | MCS_MCFGPMA_HOLE1_LOWER_ADDRESS                    |                | // 0b0000000001 = 4GB |
| 38-47     | MCS_MCFGPMA_HOLE1_UPPER_ADDRESS                    |                | // 0b0000000001 = 4GB |
| 28        | P9N2_MCS_MCFGPMA_SMF_VALID                         |                |                       |
| 29        | P9N2_MCS_MCFGPMA_SMF_UPPER_ADDRESS_AT_END_OF_RANGE |                |                       |
| 30-43     | P9N2_MCS_MCFGPMA_SMF_LOWER_ADDRESS                 |                |                       |
| 44-57     | P9N2_MCS_MCFGPMA_SMF_UPPER_ADDRESS                 |                |                       |

**PU_NXCQ_PB_MODE_REG 0x2011095**
| Bit range | Description |
| --------- | ----------- |
| 56-63     | Documentation says this range is read-only and constant to 0, but hostboot is writing to it in [p9_nx_scom.C:668](https://github.com/open-power/hostboot/blob/3e6bf45bea9b61ef6b3da1df9f7e63e0b8ec5403/src/import/chips/p9/procedures/hwp/initfiles/p9_nx_scom.C#L668) |


**MCS_MCMODE0 0x5010811**
| Bit range | Name                             | Assigned value | Description |
| --------- | -------------------------------- | -------------- | ----------- |
| 2         | ENABLE_CENTAUR_PERFMON_COMMAND   | 1              |             |
| 5         | SYNC_MODE                        |                |             |
| 6         | ASYNC_MODE                       |                |             |
| 8         | ENABLE_EMERGENCY_THROTTLE        | 1              |             |
| 9         | ENABLE_CENTAUR_CHECKSTOP_COMMAND | 1              |             |
| 10        | ENABLE_CENTAUR_TRACESTOP_COMMAND | 1              |             |
| 12        | DISABLE_MC_SYNC                  | 1              |             |
| 13        | DISABLE_MC_PAIR_SYNC             | 1              |             |
| 21        | ENABLE_EMERGENCY_THROTTLE        |                |             |
| 22        | ENABLE_CENTAUR_CHECKSTOP_COMMAND |                |             |
| 23        | ENABLE_CENTAUR_TRACESTOP_COMMAND |                |             |
| 25        | ENABLE_SELECT_ERROR_LOG_SOURCE   |                |             |
| 27        | DISABLE_MC_SYNC                  |                |             |
| 28        | DISABLE_MC_PAIR_SYNC             |                |             |
| 48        | ENABLE_CENTAUR_PERFMON_COMMAND   |                |             |

**MCS_MCSYNC 0x5010815**
| Bit range | Name              | Assigned value | Description |
| --------- | ----------------- | -------------- | ----------- |
| 0-7       | CHANNEL_SELECT    |                |             |
| 8-15      | SYNC_TYPE         |                |             |
| 16        | SYNC_GO_CH0       |                |             |
| 17        | SYNC_GO_CH1       |                |             |
| 18-24     | SYNC_REPLAY_COUNT |                |             |
| 25-27     | SYNC_RESERVED     |                |             |


**XBUS_RX0_RXPACKS3_SLICE4_RX_DATA_DAC_SPARE_MODE_PL 0x8000000006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_DATA_DAC_SPARE_MODE_PL 0x8000000106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000000206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000000306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000000406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000000506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000000606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000000706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000000806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000000906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000000A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000000B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000000C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000000D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000000E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000000F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000001006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000001106010C3F**
| Bit range | Name                        | Assigned value | Description |
| --------- | --------------------------- | -------------- | ----------- |
| 53        | RX_PL_DATA_DAC_SPARE_MODE_5 |                |             |
| 54        | RX_PL_DATA_DAC_SPARE_MODE_6 | 0              |             |
| 55        | RX_PL_DATA_DAC_SPARE_MODE_7 | 0              |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL1_EO_PL 0x8000080006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL1_EO_PL 0x8000080106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000080206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000080306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000080406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000080506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000080606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000080706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000080806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000080906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000080A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000080B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000080C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000080D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000080E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000080F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000081006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000081106010C3F**
| Bit range | Name             | Assigned value | Description |
| --------- | ---------------- | -------------- | ----------- |
| 54        | RX_LANE_ANA_PDWN | 1              |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL5_EO_PL 0x8000280006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL5_EO_PL 0x8000280106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000280206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000280306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000280406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000280506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000280606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000280706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000280806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000280906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000280A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000280B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000280C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000280D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000280E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000280F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000281006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000281106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 0              |             |
| 52-56     |      | 0              |             |
| 57-61     |      | 0              |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL6_EO_PL 0x8000300006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL6_EO_PL 0x8000300106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000300206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000300306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000300406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000300506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000300606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000300706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000300806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000300906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000300A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000300B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000300C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000300D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000300E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000300F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000301006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000301106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-52     |      |                |             |
| 53-56     |      |                |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_DAC_CNTL9_E_PL 0x8000C00006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_DAC_CNTL9_E_PL 0x8000C00106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C00206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C00306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C00406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C00506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C00606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C00706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C00806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C00906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C00A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C00B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C00C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C00D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C00E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C00F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C01006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C01106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-54     |      | 0              |             |
| 55-60     |      | 0              |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_BIT_MODE1_EO_PL 0x8002200006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_BIT_MODE1_EO_PL 0x8002200106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_BIT_MODE1_EO_PL 0x8002200206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_BIT_MODE1_EO_PL 0x8002200306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_BIT_MODE1_EO_PL 0x8002200406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_BIT_MODE1_EO_PL 0x8002200506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_BIT_MODE1_EO_PL 0x8002200606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_BIT_MODE1_EO_PL 0x8002200706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_BIT_MODE1_EO_PL 0x8002200806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_BIT_MODE1_EO_PL 0x8002200906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_BIT_MODE1_EO_PL 0x8002200A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_BIT_MODE1_EO_PL 0x8002200B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_BIT_MODE1_EO_PL 0x8002200C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_BIT_MODE1_EO_PL 0x8002200D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_BIT_MODE1_EO_PL 0x8002200E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_BIT_MODE1_EO_PL 0x8002200F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_BIT_MODE1_EO_PL 0x8002201006010C3F** \
**XBUS_RX0_RXPACKS2_SLICE1_RX_BIT_MODE1_EO_PL 0x8002201106010C3F**
| Bit range | Name             | Assigned value | Description |
| --------- | ---------------- | -------------- | ----------- |
| 48        | RX_LANE_DIG_PDWN | 1              |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_BIT_MODE1_E_PL 0x8002C00006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_BIT_MODE1_E_PL 0x8002C00106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_BIT_MODE1_E_PL 0x8002C00206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_BIT_MODE1_E_PL 0x8002C00306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_BIT_MODE1_E_PL 0x8002C00406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_BIT_MODE1_E_PL 0x8002C00506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_BIT_MODE1_E_PL 0x8002C00606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_BIT_MODE1_E_PL 0x8002C00706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_BIT_MODE1_E_PL 0x8002C00806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_BIT_MODE1_E_PL 0x8002C00906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_BIT_MODE1_E_PL 0x8002C00A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_BIT_MODE1_E_PL 0x8002C00B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_BIT_MODE1_E_PL 0x8002C00C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_BIT_MODE1_E_PL 0x8002C00D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_BIT_MODE1_E_PL 0x8002C00E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_BIT_MODE1_E_PL 0x8002C00F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_BIT_MODE1_E_PL 0x8002C01006010C3F**
| Bit range | Name                                      | Assigned value | Description |
| --------- | ----------------------------------------- | -------------- | ----------- |
| 48-63     | RX_PRBS_SEED_VALUE_0_15_PATTERN_24_A_0_15 | 0x1000         |             |

**XBUS_RX0_RXPACKS3_SLICE4_RX_BIT_MODE2_E_PL 0x8002C80006010C3F** \
**XBUS_RX0_RXPACKS3_SLICE5_RX_BIT_MODE2_E_PL 0x8002C80106010C3F** \
**XBUS_RX0_RXPACKS3_SLICE1_RX_BIT_MODE2_E_PL 0x8002C80206010C3F** \
**XBUS_RX0_RXPACKS3_SLICE3_RX_BIT_MODE2_E_PL 0x8002C80306010C3F** \
**XBUS_RX0_RXPACKS3_SLICE0_RX_BIT_MODE2_E_PL 0x8002C80406010C3F** \
**XBUS_RX0_RXPACKS3_SLICE2_RX_BIT_MODE2_E_PL 0x8002C80506010C3F** \
**XBUS_RX0_RXPACKS2_SLICE2_RX_BIT_MODE2_E_PL 0x8002C80606010C3F** \
**XBUS_RX0_RXPACKS2_SLICE0_RX_BIT_MODE2_E_PL 0x8002C80706010C3F** \
**XBUS_RX0_RXPACKS2_SLICE3_RX_BIT_MODE2_E_PL 0x8002C80806010C3F** \
**XBUS_RX0_RXPACKS1_SLICE1_RX_BIT_MODE2_E_PL 0x8002C80906010C3F** \
**XBUS_RX0_RXPACKS1_SLICE3_RX_BIT_MODE2_E_PL 0x8002C80A06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE0_RX_BIT_MODE2_E_PL 0x8002C80B06010C3F** \
**XBUS_RX0_RXPACKS1_SLICE2_RX_BIT_MODE2_E_PL 0x8002C80C06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE0_RX_BIT_MODE2_E_PL 0x8002C80D06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE2_RX_BIT_MODE2_E_PL 0x8002C80E06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE1_RX_BIT_MODE2_E_PL 0x8002C80F06010C3F** \
**XBUS_RX0_RXPACKS0_SLICE3_RX_BIT_MODE2_E_PL 0x8002C81006010C3F**
| Bit range | Name                                        | Assigned value | Description |
| --------- | ------------------------------------------- | -------------- | ----------- |
| 48-54     | RX_PRBS_SEED_VALUE_16_22_PATTERN_24_A_16_22 | 0x42           |             |

**XBUS_TX0_TXPACKS0_SLICE0_TX_MODE1_PL 0x8004040006010C3F** \
**XBUS_TX0_TXPACKS0_SLICE1_TX_MODE1_PL 0x8004040106010C3F** \
**XBUS_TX0_TXPACKS0_SLICE2_TX_MODE1_PL 0x8004040206010C3F** \
**XBUS_TX0_TXPACKS0_SLICE3_TX_MODE1_PL 0x8004040306010C3F** \
**XBUS_TX0_TXPACKS1_SLICE0_TX_MODE1_PL 0x8004040406010C3F** \
**XBUS_TX0_TXPACKS1_SLICE1_TX_MODE1_PL 0x8004040506010C3F** \
**XBUS_TX0_TXPACKS1_SLICE2_TX_MODE1_PL 0x8004040606010C3F** \
**XBUS_TX0_TXPACKS1_SLICE3_TX_MODE1_PL 0x8004040706010C3F** \
**XBUS_TX0_TXPACKS2_SLICE0_TX_MODE1_PL 0x8004040806010C3F** \
**XBUS_TX0_TXPACKS2_SLICE1_TX_MODE1_PL 0x8004040906010C3F** \
**XBUS_TX0_TXPACKS2_SLICE2_TX_MODE1_PL 0x8004040A06010C3F** \
**XBUS_TX0_TXPACKS2_SLICE3_TX_MODE1_PL 0x8004040B06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE0_TX_MODE1_PL 0x8004040C06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE1_TX_MODE1_PL 0x8004040D06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE2_TX_MODE1_PL 0x8004040E06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE3_TX_MODE1_PL 0x8004040F06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE4_TX_MODE1_PL 0x8004041006010C3F**
| Bit range | Name         | Assigned value | Description |
| --------- | ------------ | -------------- | ----------- |
| 48        | TX_LANE_PDWN | 0              |             |

**XBUS_TX0_TXPACKS0_SLICE0_TX_MODE2_PL 0x80040C0006010C3F** \
**XBUS_TX0_TXPACKS0_SLICE1_TX_MODE2_PL 0x80040C0106010C3F** \
**XBUS_TX0_TXPACKS0_SLICE2_TX_MODE2_PL 0x80040C0206010C3F** \
**XBUS_TX0_TXPACKS0_SLICE3_TX_MODE2_PL 0x80040C0306010C3F** \
**XBUS_TX0_TXPACKS1_SLICE0_TX_MODE2_PL 0x80040C0406010C3F** \
**XBUS_TX0_TXPACKS1_SLICE1_TX_MODE2_PL 0x80040C0506010C3F** \
**XBUS_TX0_TXPACKS1_SLICE2_TX_MODE2_PL 0x80040C0606010C3F** \
**XBUS_TX0_TXPACKS1_SLICE3_TX_MODE2_PL 0x80040C0706010C3F** \
**XBUS_TX0_TXPACKS2_SLICE0_TX_MODE2_PL 0x80040C0806010C3F** \
**XBUS_TX0_TXPACKS2_SLICE1_TX_MODE2_PL 0x80040C0906010C3F** \
**XBUS_TX0_TXPACKS2_SLICE2_TX_MODE2_PL 0x80040C0A06010C3F** \
**XBUS_TX0_TXPACKS2_SLICE3_TX_MODE2_PL 0x80040C0B06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE0_TX_MODE2_PL 0x80040C0C06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE1_TX_MODE2_PL 0x80040C0D06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE2_TX_MODE2_PL 0x80040C0E06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE3_TX_MODE2_PL 0x80040C0F06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE4_TX_MODE2_PL 0x80040C1006010C3F**
| Bit range | Name            | Assigned value | Description |
| --------- | --------------- | -------------- | ----------- |
| 62        | TX_CAL_LANE_SEL | 1              |             |

**XBUS_TX0_TXPACKS0_SLICE0_TX_BIT_MODE1_E_PL 0x80043C0006010C3F** \
**XBUS_TX0_TXPACKS0_SLICE1_TX_BIT_MODE1_E_PL 0x80043C0106010C3F** \
**XBUS_TX0_TXPACKS0_SLICE2_TX_BIT_MODE1_E_PL 0x80043C0206010C3F** \
**XBUS_TX0_TXPACKS0_SLICE3_TX_BIT_MODE1_E_PL 0x80043C0306010C3F** \
**XBUS_TX0_TXPACKS1_SLICE0_TX_BIT_MODE1_E_PL 0x80043C0406010C3F** \
**XBUS_TX0_TXPACKS1_SLICE1_TX_BIT_MODE1_E_PL 0x80043C0506010C3F** \
**XBUS_TX0_TXPACKS1_SLICE2_TX_BIT_MODE1_E_PL 0x80043C0606010C3F** \
**XBUS_TX0_TXPACKS1_SLICE3_TX_BIT_MODE1_E_PL 0x80043C0706010C3F** \
**XBUS_TX0_TXPACKS2_SLICE0_TX_BIT_MODE1_E_PL 0x80043C0806010C3F** \
**XBUS_TX0_TXPACKS2_SLICE1_TX_BIT_MODE1_E_PL 0x80043C0906010C3F** \
**XBUS_TX0_TXPACKS2_SLICE2_TX_BIT_MODE1_E_PL 0x80043C0A06010C3F** \
**XBUS_TX0_TXPACKS2_SLICE3_TX_BIT_MODE1_E_PL 0x80043C0B06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE0_TX_BIT_MODE1_E_PL 0x80043C0C06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE1_TX_BIT_MODE1_E_PL 0x80043C0D06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE2_TX_BIT_MODE1_E_PL 0x80043C0E06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE3_TX_BIT_MODE1_E_PL 0x80043C0F06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE4_TX_BIT_MODE1_E_PL 0x80043C1006010C3F**
| Bit range | Name                                              | Assigned value | Description |
| --------- | ------------------------------------------------- | -------------- | ----------- |
| 48-63     | TX_PRBS_SEED_VALUE_0_15_PATTERN_TX_AB_HALF_A_0_15 | 0              |             |

**XBUS_TX0_TXPACKS0_SLICE0_TX_BIT_MODE2_E_PL 0x8004440006010C3F** \
**XBUS_TX0_TXPACKS0_SLICE1_TX_BIT_MODE2_E_PL 0x8004440106010C3F** \
**XBUS_TX0_TXPACKS0_SLICE2_TX_BIT_MODE2_E_PL 0x8004440206010C3F** \
**XBUS_TX0_TXPACKS0_SLICE3_TX_BIT_MODE2_E_PL 0x8004440306010C3F** \
**XBUS_TX0_TXPACKS1_SLICE0_TX_BIT_MODE2_E_PL 0x8004440406010C3F** \
**XBUS_TX0_TXPACKS1_SLICE1_TX_BIT_MODE2_E_PL 0x8004440506010C3F** \
**XBUS_TX0_TXPACKS1_SLICE2_TX_BIT_MODE2_E_PL 0x8004440606010C3F** \
**XBUS_TX0_TXPACKS1_SLICE3_TX_BIT_MODE2_E_PL 0x8004440706010C3F** \
**XBUS_TX0_TXPACKS2_SLICE0_TX_BIT_MODE2_E_PL 0x8004440806010C3F** \
**XBUS_TX0_TXPACKS2_SLICE1_TX_BIT_MODE2_E_PL 0x8004440906010C3F** \
**XBUS_TX0_TXPACKS2_SLICE2_TX_BIT_MODE2_E_PL 0x8004440A06010C3F** \
**XBUS_TX0_TXPACKS2_SLICE3_TX_BIT_MODE2_E_PL 0x8004440B06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE0_TX_BIT_MODE2_E_PL 0x8004440C06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE1_TX_BIT_MODE2_E_PL 0x8004440D06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE2_TX_BIT_MODE2_E_PL 0x8004440E06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE3_TX_BIT_MODE2_E_PL 0x8004440F06010C3F** \
**XBUS_TX0_TXPACKS3_SLICE4_TX_BIT_MODE2_E_PL 0x8004441006010C3F**
| Bit range | Name                                        | Assigned value | Description |
| --------- | ------------------------------------------- | -------------- | ----------- |
| 48-54     | TX_PRBS_SEED_VALUE_16_22_PATTERN_TX_A_16_22 | 1              |             |

**XBUS_RX0_RX_SPARE_MODE_PG 0x8008000006010C3F**
| Bit range | Name                           | Assigned value | Description |
| --------- | ------------------------------ | -------------- | ----------- |
| 49        | RX_CTL_REGS_RX_PG_SPARE_MODE_1 |                |             |

**XBUS_RX0_RX_ID1_PG 0x8008080006010C3F**
NOTE: No known register bits

**XBUS_RX0_RX_CTL_MODE1_EO_PG 0x8008100006010C3F**
| Bit range | Name                        | Assigned value | Description |
| --------- | --------------------------- | -------------- | ----------- |
| 48        | RX_CTL_REGS_RX_CLKDIST_PDWN | 0              |             |

**XBUS_RX0_RX_CTL_MODE5_EO_PG 0x8008300006010C3F**
| Bit range | Name                                    | Assigned value | Description |
| --------- | --------------------------------------- | -------------- | ----------- |
| 51-53     | RX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP   | 5              |             |
| 54-55     | RX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP | 1              |             |

**XBUS_RX0_RX_CTL_MODE7_EO_PG 0x8008400006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 60-63     |      | 10             |             |

**XBUS_RX0_RX_CTL_MODE23_EO_PG 0x8008C00006010C3F**
| Bit range | Name                       | Assigned value | Description |
| --------- | -------------------------- | -------------- | ----------- |
| 48-49     |                            |                |             |
| 55        | RX_CTL_REGS_RX_PEAK_TUNE   | 0              |             |
| 56        | RX_CTL_REGS_RX_LTE_EN      |                |             |
| 57-58     |                            | 3              |             |
| 59        | RX_CTL_REGS_RX_DFEHISPD_EN | 1              |             |
| 60        | RX_DFE12_EN                | 1              |             |

**XBUS_RX0_RX_CTL_MODE29_EO_PG 0x8008D00006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-55     |      |                |             |
| 56-63     |      |                |             |

**XBUS_RX0_RX_CTL_MODE27_EO_PG 0x8009700006010C3F**
| Bit range | Name                                                  | Assigned value | Description |
| --------- | ----------------------------------------------------- | -------------- | ----------- |
| 48        | RX_CTL_REGS_RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL_ON | 1              |             |

**XBUS_RX0_RX_ID2_PG 0x8009800006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 49-56     |      | 0              |             |
| 57-63     |      | 16             |             |

**XBUS_RX0_RX_CTL_MODE1_E_PG 0x8009900006010C3F**
| Bit range | Name                 | Assigned value | Description |
| --------- | -------------------- | -------------- | ----------- |
| 48        | MASTER_MODE          |                |             |
| 57        | RX_FENCE             | 1              |             |
| 58        | RX_PDWN_LITE_DISABLE | 1              |             |

**XBUS_RX0_RX_CTL_MODE2_E_PG 0x8009980006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-52     |      | 1              |             |

**XBUS_RX0_RX_CTL_MODE3_E_PG 0x8009A00006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 0xB            |             |

**XBUS_RX0_RX_CTL_MODE5_E_PG 0x8009B00006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 52-55     |      | 1              |             |

**XBUS_RX0_RX_CTL_MODE6_E_PG 0x8009B80006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-54     |      | 0x11           |             |
| 55-61     |      | 0x11           |             |

**XBUS_RX0_RX_CTL_MODE8_E_PG 0x8009C80006010C3F**
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 48-54     |                                   | 15             |             |
| 55-58     | RX_DYN_RPR_ERR_CNTR1_DURATION_TAP | 5              |             |
| 61-63     |                                   | 5              |             |

**XBUS_RX0_RX_CTL_MODE9_E_PG 0x8009D00006010C3F**
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 48-54     |                                   | 63             |             |
| 55-58     | RX_DYN_RPR_ERR_CNTR2_DURATION_TAP | 5              |             |

**XBUS_RX0_RX_CTL_MODE11_E_PG 0x8009E00006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 0              |             |

**XBUS_RX0_RX_CTL_MODE12_E_PG 0x8009E80006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 127            |             |

**XBUS_RX0_RX_GLBSM_SPARE_MODE_PG 0x800A800006010C3F**
| Bit range | Name                       | Assigned value | Description |
| --------- | -------------------------- | -------------- | ----------- |
| 50        | RX_DESKEW_BUMP_AFTER_AFTER | 1              |             |
| 56        | RX_PG_GLBSM_SPARE_MODE_2   | 1              |             |

**XBUS_RX0_RX_GLBSM_CNTL3_EO_PG 0x800AE80006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-57     |      | 2              |             |

**XBUS_RX0_RX_GLBSM_MODE1_E_PG 0x800AF80006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 12             |             |
| 52-55     |      | 12             |             |

**XBUS_RX0_RX_DATASM_SPARE_MODE_PG 0x800B800006010C3F**
| Bit range | Name                       | Assigned value | Description |
| --------- | -------------------------- | -------------- | ----------- |
| 56        |                            |                |             |
| 60        | RX_CTL_DATASM_CLKDIST_PDWN | 0              |             |

**XBUS_TX0_TX_SPARE_MODE_PG 0x800C040006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-57     |      | 0              |             |

**XBUS_TX0_TX_ID1_PG 0x800C0C0006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-53     |      | 0              |             |

**XBUS_TX0_TX_CTL_MODE1_EO_PG 0x800C140006010C3F**
| Bit range | Name                             | Assigned value | Description |
| --------- | -------------------------------- | -------------- | ----------- |
| 48        | TX_CLKDIST_PDWN                  | 0              |             |
| 53-57     | TX_CTL_REGS_TX_PDWN_LITE_DISABLE | 1              |             |
| 59        |                                  | 1              |             |

**XBUS_TX0_TX_CTL_MODE2_EO_PG 0x800C1C0006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-62     |      | 17             |             |

**XBUS_TX0_TX_CTL_CNTLG1_EO_PG 0x800C240006010C3F**
| Bit range | Name                          | Assigned value | Description |
| --------- | ----------------------------- | -------------- | ----------- |
| 48-49     | TX_DRV_CLK_PATTERN_GCRMSG_DRV | 0              |             |

**XBUS_TX0_TX_ID2_PG 0x800C840006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 49-55     |      | 0              |             |
| 57-63     |      | 16             |             |

**XBUS_TX0_TX_CTL_MODE1_E_PG 0x800C8C0006010C3F**
| Bit range | Name                                    | Assigned value | Description |
| --------- | --------------------------------------- | -------------- | ----------- |
| 55-57     | TX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP   | 5              |             |
| 58-59     | TX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP | 1              |             |

**XBUS_TX0_TX_CTL_MODE2_E_PG 0x800CEC0006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 0              |             |

**XBUS_TX0_TX_CTL_MODE3_E_PG 0x800CF40006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-55     |      | 127            |             |

**XBUS_TX0_TX_CTLSM_MODE1_EO_PG 0x800D2C0006010C3F**
| Bit range | Name            | Assigned value | Description |
| --------- | --------------- | -------------- | ----------- |
| 59        | TX_FFE_BOOST_EN |                |             |

**XBUS_TX_IMPCAL_P_4X_PB 0x800F1C0006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-52     |      | 14             |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000002006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000002106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000002206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000002306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000002406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000002506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000002606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000002706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000002806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000002906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000002A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000002B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_DATA_DAC_SPARE_MODE_PL 0x8000002C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_DATA_DAC_SPARE_MODE_PL 0x8000002D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_DATA_DAC_SPARE_MODE_PL 0x8000002E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_DATA_DAC_SPARE_MODE_PL 0x8000002F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_DATA_DAC_SPARE_MODE_PL 0x8000003006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_DATA_DAC_SPARE_MODE_PL 0x8000003106010C3F**
| Bit range | Name                        | Assigned value | Description |
| --------- | --------------------------- | -------------- | ----------- |
| 53        | RX_PL_DATA_DAC_SPARE_MODE_5 | 0              |             |
| 54        | RX_PL_DATA_DAC_SPARE_MODE_6 | 0              |             |
| 55        | RX_PL_DATA_DAC_SPARE_MODE_7 | 0              |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000082006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000082106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000082206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000082306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000082406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000082506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000082606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000082706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000082806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000082906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000082A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000082B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL1_EO_PL 0x8000082C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL1_EO_PL 0x8000082D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL1_EO_PL 0x8000082E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL1_EO_PL 0x8000082F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL1_EO_PL 0x8000083006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL1_EO_PL 0x8000083106010C3F**
| Bit range | Name             | Assigned value | Description |
| --------- | ---------------- | -------------- | ----------- |
| 54        | RX_LANE_ANA_PDWN | 0              |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000282006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000282106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000282206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000282306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000282406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000282506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000282606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000282706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000282806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000282906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000282A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000282B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL5_EO_PL 0x8000282C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL5_EO_PL 0x8000282D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL5_EO_PL 0x8000282E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL5_EO_PL 0x8000282F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL5_EO_PL 0x8000283006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL5_EO_PL 0x8000283106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 0              |             |
| 52-56     |      | 0              |             |
| 57-61     |      | 0              |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000302006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000302106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000302206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000302306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000302406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000302506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000302606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000302706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000302806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000302906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000302A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000302B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL6_EO_PL 0x8000302C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL6_EO_PL 0x8000302D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL6_EO_PL 0x8000302E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL6_EO_PL 0x8000302F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL6_EO_PL 0x8000303006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL6_EO_PL 0x8000303106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 53-56     |      |                |             |
| 48-52     |      |                |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C02006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C02106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C02206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C02306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C02406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C02506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C02606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C02706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C02806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C02906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C02A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C02B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_DAC_CNTL9_E_PL 0x8000C02C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_DAC_CNTL9_E_PL 0x8000C02D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_DAC_CNTL9_E_PL 0x8000C02E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_DAC_CNTL9_E_PL 0x8000C02F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_DAC_CNTL9_E_PL 0x8000C03006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_DAC_CNTL9_E_PL 0x8000C03106010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-54     |      | 0              |             |
| 55-60     |      | 0              |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_BIT_MODE1_EO_PL 0x8002202006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_BIT_MODE1_EO_PL 0x8002202106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_BIT_MODE1_EO_PL 0x8002202206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_BIT_MODE1_EO_PL 0x8002202306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_BIT_MODE1_EO_PL 0x8002202406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_BIT_MODE1_EO_PL 0x8002202506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_BIT_MODE1_EO_PL 0x8002202606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_BIT_MODE1_EO_PL 0x8002202706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_BIT_MODE1_EO_PL 0x8002202806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_BIT_MODE1_EO_PL 0x8002202906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_BIT_MODE1_EO_PL 0x8002202A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_BIT_MODE1_EO_PL 0x8002202B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_BIT_MODE1_EO_PL 0x8002202C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_BIT_MODE1_EO_PL 0x8002202D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_BIT_MODE1_EO_PL 0x8002202E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_BIT_MODE1_EO_PL 0x8002202F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_BIT_MODE1_EO_PL 0x8002203006010C3F** \
**XBUS_RX1_RXPACKS2_SLICE3_RX_BIT_MODE1_EO_PL 0x8002203106010C3F**
| Bit range | Name             | Assigned value | Description |
| --------- | ---------------- | -------------- | ----------- |
| 48        | RX_LANE_DIG_PDWN |                |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_BIT_MODE1_E_PL 0x8002C02006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_BIT_MODE1_E_PL 0x8002C02106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_BIT_MODE1_E_PL 0x8002C02206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_BIT_MODE1_E_PL 0x8002C02306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_BIT_MODE1_E_PL 0x8002C02406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_BIT_MODE1_E_PL 0x8002C02506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_BIT_MODE1_E_PL 0x8002C02606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_BIT_MODE1_E_PL 0x8002C02706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_BIT_MODE1_E_PL 0x8002C02806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_BIT_MODE1_E_PL 0x8002C02906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_BIT_MODE1_E_PL 0x8002C02A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_BIT_MODE1_E_PL 0x8002C02B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_BIT_MODE1_E_PL 0x8002C02C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_BIT_MODE1_E_PL 0x8002C02D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_BIT_MODE1_E_PL 0x8002C02E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_BIT_MODE1_E_PL 0x8002C02F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_BIT_MODE1_E_PL 0x8002C03006010C3F**
| Bit range | Name               | Assigned value | Description |
| --------- | ------------------ | -------------- | ----------- |
| 48-63     | RX_PRBS_SEED_VALUE |                |             |

**XBUS_RX1_RXPACKS0_SLICE2_RX_BIT_MODE2_E_PL 0x8002C82006010C3F** \
**XBUS_RX1_RXPACKS0_SLICE0_RX_BIT_MODE2_E_PL 0x8002C82106010C3F** \
**XBUS_RX1_RXPACKS0_SLICE3_RX_BIT_MODE2_E_PL 0x8002C82206010C3F** \
**XBUS_RX1_RXPACKS0_SLICE1_RX_BIT_MODE2_E_PL 0x8002C82306010C3F** \
**XBUS_RX1_RXPACKS1_SLICE3_RX_BIT_MODE2_E_PL 0x8002C82406010C3F** \
**XBUS_RX1_RXPACKS1_SLICE1_RX_BIT_MODE2_E_PL 0x8002C82506010C3F** \
**XBUS_RX1_RXPACKS1_SLICE2_RX_BIT_MODE2_E_PL 0x8002C82606010C3F** \
**XBUS_RX1_RXPACKS1_SLICE0_RX_BIT_MODE2_E_PL 0x8002C82706010C3F** \
**XBUS_RX1_RXPACKS2_SLICE0_RX_BIT_MODE2_E_PL 0x8002C82806010C3F** \
**XBUS_RX1_RXPACKS2_SLICE2_RX_BIT_MODE2_E_PL 0x8002C82906010C3F** \
**XBUS_RX1_RXPACKS2_SLICE1_RX_BIT_MODE2_E_PL 0x8002C82A06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE3_RX_BIT_MODE2_E_PL 0x8002C82B06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE1_RX_BIT_MODE2_E_PL 0x8002C82C06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE2_RX_BIT_MODE2_E_PL 0x8002C82D06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE0_RX_BIT_MODE2_E_PL 0x8002C82E06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE4_RX_BIT_MODE2_E_PL 0x8002C82F06010C3F** \
**XBUS_RX1_RXPACKS3_SLICE5_RX_BIT_MODE2_E_PL 0x8002C83006010C3F**
| Bit range | Name               | Assigned value | Description |
| --------- | ------------------ | -------------- | ----------- |
| 48-63     | RX_PRBS_SEED_VALUE |                |             |

**XBUS_TX1_TXPACKS0_SLICE0_TX_MODE1_PL 0x8004042006010C3F** \
**XBUS_TX1_TXPACKS0_SLICE1_TX_MODE1_PL 0x8004042106010C3F** \
**XBUS_TX1_TXPACKS0_SLICE2_TX_MODE1_PL 0x8004042206010C3F** \
**XBUS_TX1_TXPACKS0_SLICE3_TX_MODE1_PL 0x8004042306010C3F** \
**XBUS_TX1_TXPACKS1_SLICE0_TX_MODE1_PL 0x8004042406010C3F** \
**XBUS_TX1_TXPACKS1_SLICE1_TX_MODE1_PL 0x8004042506010C3F** \
**XBUS_TX1_TXPACKS1_SLICE2_TX_MODE1_PL 0x8004042606010C3F** \
**XBUS_TX1_TXPACKS1_SLICE3_TX_MODE1_PL 0x8004042706010C3F** \
**XBUS_TX1_TXPACKS2_SLICE0_TX_MODE1_PL 0x8004042806010C3F** \
**XBUS_TX1_TXPACKS2_SLICE1_TX_MODE1_PL 0x8004042906010C3F** \
**XBUS_TX1_TXPACKS2_SLICE2_TX_MODE1_PL 0x8004042A06010C3F** \
**XBUS_TX1_TXPACKS2_SLICE3_TX_MODE1_PL 0x8004042B06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE0_TX_MODE1_PL 0x8004042C06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE1_TX_MODE1_PL 0x8004042D06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE2_TX_MODE1_PL 0x8004042E06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE3_TX_MODE1_PL 0x8004042F06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE4_TX_MODE1_PL 0x8004043006010C3F**
| Bit range | Name         | Assigned value | Description |
| --------- | ------------ | -------------- | ----------- |
| 48        | TX_LANE_PDWN | 0              |             |

**XBUS_TX1_TXPACKS0_SLICE0_TX_MODE2_PL 0x80040C2006010C3F** \
**XBUS_TX1_TXPACKS0_SLICE1_TX_MODE2_PL 0x80040C2106010C3F** \
**XBUS_TX1_TXPACKS0_SLICE2_TX_MODE2_PL 0x80040C2206010C3F** \
**XBUS_TX1_TXPACKS0_SLICE3_TX_MODE2_PL 0x80040C2306010C3F** \
**XBUS_TX1_TXPACKS1_SLICE0_TX_MODE2_PL 0x80040C2406010C3F** \
**XBUS_TX1_TXPACKS1_SLICE1_TX_MODE2_PL 0x80040C2506010C3F** \
**XBUS_TX1_TXPACKS1_SLICE2_TX_MODE2_PL 0x80040C2606010C3F** \
**XBUS_TX1_TXPACKS1_SLICE3_TX_MODE2_PL 0x80040C2706010C3F** \
**XBUS_TX1_TXPACKS2_SLICE0_TX_MODE2_PL 0x80040C2806010C3F** \
**XBUS_TX1_TXPACKS2_SLICE1_TX_MODE2_PL 0x80040C2906010C3F** \
**XBUS_TX1_TXPACKS2_SLICE2_TX_MODE2_PL 0x80040C2A06010C3F** \
**XBUS_TX1_TXPACKS2_SLICE3_TX_MODE2_PL 0x80040C2B06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE0_TX_MODE2_PL 0x80040C2C06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE1_TX_MODE2_PL 0x80040C2D06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE2_TX_MODE2_PL 0x80040C2E06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE3_TX_MODE2_PL 0x80040C2F06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE4_TX_MODE2_PL 0x80040C3006010C3F**
| Bit range | Name            | Assigned value | Description |
| --------- | --------------- | -------------- | ----------- |
| 62        | TX_CAL_LANE_SEL |                |             |

**XBUS_TX1_TXPACKS0_SLICE0_TX_BIT_MODE1_E_PL 0x80043C2006010C3F** \
**XBUS_TX1_TXPACKS0_SLICE1_TX_BIT_MODE1_E_PL 0x80043C2106010C3F** \
**XBUS_TX1_TXPACKS0_SLICE2_TX_BIT_MODE1_E_PL 0x80043C2206010C3F** \
**XBUS_TX1_TXPACKS0_SLICE3_TX_BIT_MODE1_E_PL 0x80043C2306010C3F** \
**XBUS_TX1_TXPACKS1_SLICE0_TX_BIT_MODE1_E_PL 0x80043C2406010C3F** \
**XBUS_TX1_TXPACKS1_SLICE1_TX_BIT_MODE1_E_PL 0x80043C2506010C3F** \
**XBUS_TX1_TXPACKS1_SLICE2_TX_BIT_MODE1_E_PL 0x80043C2606010C3F** \
**XBUS_TX1_TXPACKS1_SLICE3_TX_BIT_MODE1_E_PL 0x80043C2706010C3F** \
**XBUS_TX1_TXPACKS2_SLICE0_TX_BIT_MODE1_E_PL 0x80043C2806010C3F** \
**XBUS_TX1_TXPACKS2_SLICE1_TX_BIT_MODE1_E_PL 0x80043C2906010C3F** \
**XBUS_TX1_TXPACKS2_SLICE2_TX_BIT_MODE1_E_PL 0x80043C2A06010C3F** \
**XBUS_TX1_TXPACKS2_SLICE3_TX_BIT_MODE1_E_PL 0x80043C2B06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE0_TX_BIT_MODE1_E_PL 0x80043C2C06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE1_TX_BIT_MODE1_E_PL 0x80043C2D06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE2_TX_BIT_MODE1_E_PL 0x80043C2E06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE3_TX_BIT_MODE1_E_PL 0x80043C2F06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE4_TX_BIT_MODE1_E_PL 0x80043C3006010C3F**
| Bit range | Name               | Assigned value | Description |
| --------- | ------------------ | -------------- | ----------- |
| 48-63     | TX_PRBS_SEED_VALUE |                |             |

**XBUS_TX1_TXPACKS0_SLICE0_TX_BIT_MODE2_E_PL 0x8004442006010C3F** \
**XBUS_TX1_TXPACKS0_SLICE1_TX_BIT_MODE2_E_PL 0x8004442106010C3F** \
**XBUS_TX1_TXPACKS0_SLICE2_TX_BIT_MODE2_E_PL 0x8004442206010C3F** \
**XBUS_TX1_TXPACKS0_SLICE3_TX_BIT_MODE2_E_PL 0x8004442306010C3F** \
**XBUS_TX1_TXPACKS1_SLICE0_TX_BIT_MODE2_E_PL 0x8004442406010C3F** \
**XBUS_TX1_TXPACKS1_SLICE1_TX_BIT_MODE2_E_PL 0x8004442506010C3F** \
**XBUS_TX1_TXPACKS1_SLICE2_TX_BIT_MODE2_E_PL 0x8004442606010C3F** \
**XBUS_TX1_TXPACKS1_SLICE3_TX_BIT_MODE2_E_PL 0x8004442706010C3F** \
**XBUS_TX1_TXPACKS2_SLICE0_TX_BIT_MODE2_E_PL 0x8004442806010C3F** \
**XBUS_TX1_TXPACKS2_SLICE1_TX_BIT_MODE2_E_PL 0x8004442906010C3F** \
**XBUS_TX1_TXPACKS2_SLICE2_TX_BIT_MODE2_E_PL 0x8004442A06010C3F** \
**XBUS_TX1_TXPACKS2_SLICE3_TX_BIT_MODE2_E_PL 0x8004442B06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE0_TX_BIT_MODE2_E_PL 0x8004442C06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE1_TX_BIT_MODE2_E_PL 0x8004442D06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE2_TX_BIT_MODE2_E_PL 0x8004442E06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE3_TX_BIT_MODE2_E_PL 0x8004442F06010C3F** \
**XBUS_TX1_TXPACKS3_SLICE4_TX_BIT_MODE2_E_PL 0x8004443006010C3F**
| Bit range | Name               | Assigned value | Description |
| --------- | ------------------ | -------------- | ----------- |
| 48-54     | TX_PRBS_SEED_VALUE |                |             |

**XBUS_RX1_RX_SPARE_MODE_PG 0x8008002006010C3F**
| Bit range | Name               | Assigned value | Description |
| --------- | ------------------ | -------------- | ----------- |
| 49        | RX_PG_SPARE_MODE_1 |                |             |

**XBUS_RX1_RX_ID1_PG 0x8008082006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-53     |      | 1              |             |

**XBUS_RX1_RX_CTL_MODE1_EO_PG 0x8008102006010C3F**
| Bit range | Name            | Assigned value | Description |
| --------- | --------------- | -------------- | ----------- |
| 48        | RX_CLKDIST_PDWN | 0              |             |

**XBUS_RX1_RX_CTL_MODE5_EO_PG 0x8008302006010C3F**
| Bit range | Name                                    | Assigned value | Description |
| --------- | --------------------------------------- | -------------- | ----------- |
| 51-53     | RX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP   | 5              |             |
| 54-55     | RX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP | 1              |             |

**XBUS_RX1_RX_CTL_MODE7_EO_PG 0x8008402006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 60-63     |      | 10             |             |

**XBUS_RX0_RX_CTL_MODE23_EO_PG 0x8008C00006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-49     |      |                |             |

**XBUS_RX1_RX_CTL_MODE23_EO_PG 0x8008C02006010C3F**
| Bit range | Name                     | Assigned value | Description |
| --------- | ------------------------ | -------------- | ----------- |
| 55        | RX_CTL_REGS_RX_PEAK_TUNE | 0              |             |
| 57-58     |                          | 3              |             |
| 59        | RX_DFEHISPD_EN           | 1              |             |
| 60        | RX_DFE12_EN              | 1              |             |

**XBUS_RX1_RX_CTL_MODE29_EO_PG 0x8008D02006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-55     |      |                |             |
| 56-63     |      |                |             |

**XBUS_RX1_RX_CTL_MODE27_EO_PG 0x8009702006010C3F**
| Bit range | Name                                   | Assigned value | Description |
| --------- | -------------------------------------- | -------------- | ----------- |
| 48        | RX_RC_ENABLE_CTLE_1ST_LATCH_OFFSET_CAL | 1              |             |

**XBUS_RX1_RX_ID2_PG 0x8009802006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 49-55     |      | 0              |             |
| 57-63     |      | 16             |             |

**XBUS_RX1_RX_CTL_MODE1_E_PG 0x8009902006010C3F**
| Bit range | Name                 | Assigned value | Description |
| --------- | -------------------- | -------------- | ----------- |
| 48        | RX_MASTER_MODE       |                |             |
| 57        | RX_PDWN_LITE_DISABLE | 1              |             |
| 58        | RX_FENCE             | 1              |             |

**XBUS_RX1_RX_CTL_MODE2_E_PG 0x8009982006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-52     |      | 1              |             |

**XBUS_RX1_RX_CTL_MODE3_E_PG 0x8009A02006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 11             |             |

**XBUS_RX1_RX_CTL_MODE5_E_PG 0x8009B02006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |

**XBUS_RX1_RX_CTL_MODE6_E_PG 0x8009B82006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 52-55     |      | 1              |             |

**XBUS_RX1_RX_CTL_MODE8_E_PG 0x8009C82006010C3F**
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 48-54     | RX_DYN_RPR_ERR_CNTR1_DURATION_TAP | 15             |             |
| 55-58     |                                   | 5              |             |
| 61-63     |                                   | 5              |             |

**XBUS_RX1_RX_CTL_MODE9_E_PG 0x8009D02006010C3F**
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 48-54     |                                   | 63             |             |
| 55-58     | RX_DYN_RPR_ERR_CNTR2_DURATION_TAP | 5              |             |

**XBUS_RX1_RX_CTL_MODE11_E_PG 0x8009E02006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |

**XBUS_RX1_RX_CTL_MODE12_E_PG 0x8009E82006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 0              |             |

**XBUS_RX1_RX_GLBSM_SPARE_MODE_PG 0x800A802006010C3F**
| Bit range | Name                     | Assigned value | Description |
| --------- | ------------------------ | -------------- | ----------- |
| 50        | RX_DESKEW_BUMP_AFTER     | 1              |             |
| 56        | RX_PG_GLBSM_SPARE_MODE_2 | 1              |             |

**XBUS_RX1_RX_GLBSM_CNTL3_EO_PG 0x800AE82006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-57     |      | 2              |             |

**XBUS_RX1_RX_GLBSM_MODE1_E_PG 0x800AF82006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-51     |      | 12             |             |
| 52-55     |      | 12             |             |

**XBUS_RX1_RX_DATASM_SPARE_MODE_PG 0x800B802006010C3F**
| Bit range | Name                       | Assigned value | Description |
| --------- | -------------------------- | -------------- | ----------- |
| 56-57     | RX_CTL_DATASM_CLKDIST_PDWN | 0              |             |
| 60        |                            |                |             |

**XBUS_TX1_TX_SPARE_MODE_PG 0x800C042006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-57     |      | 0              |             |

**XBUS_TX1_TX_ID1_PG 0x800C0C2006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-53     |      | 1              |             |

**XBUS_TX1_TX_CTL_MODE1_EO_PG 0x800C142006010C3F**
| Bit range | Name                        | Assigned value | Description |
| --------- | --------------------------- | -------------- | ----------- |
| 48        | TX_CLKDIST_PDWN             | 0              |             |
| 53-57     | TX_CTL_REGS_TX_CLKDIST_PDWN | 1              |             |
| 59        | TX_PDWN_LITE_DISABLE        | 1              |             |

**XBUS_TX1_TX_CTL_MODE2_EO_PG 0x800C1C2006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 56-62     |      | 17             |             |

**XBUS_TX1_TX_CTL_CNTLG1_EO_PG 0x800C242006010C3F**
| Bit range | Name                          | Assigned value | Description |
| --------- | ----------------------------- | -------------- | ----------- |
| 48-49     | TX_DRV_CLK_PATTERN_GCRMSG_DRV | 0              |             |

**XBUS_TX1_TX_ID2_PG 0x800C842006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 49-55     |      | 0              |             |
| 57-63     |      | 16             |             |

**XBUS_TX1_TX_CTL_MODE1_E_PG 0x800C8C2006010C3F**
| Bit range | Name                                    | Assigned value | Description |
| --------- | --------------------------------------- | -------------- | ----------- |
| 55-57     | TX_DYN_RECAL_INTERVAL_TIMEOUT_SEL_TAP   | 5              |             |
| 58-59     | TX_DYN_RECAL_STATUS_RPT_TIMEOUT_SEL_TAP | 1              |             |

**XBUS_TX1_TX_CTL_MODE2_E_PG 0x800CEC2006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 0              |             |

**XBUS_TX1_TX_CTL_MODE3_E_PG 0x800CF42006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-63     |      | 127            |             |

**XBUS_TX1_TX_CTLSM_MODE1_EO_PG 0x800D2C2006010C3F**
| Bit range | Name            | Assigned value | Description |
| --------- | --------------- | -------------- | ----------- |
| 59        | TX_FFE_BOOST_EN |                |             |

**XBUS_TX_IMPCAL_P_4X_PB 0x800F1C0006010C3F**
| Bit range | Name | Assigned value | Description |
| --------- | ---- | -------------- | ----------- |
| 48-52     |      | 14             |             |
