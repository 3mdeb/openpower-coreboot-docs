# What this document is about?
This document is an attempt to gather information about undocumented registers
and registers that code analysis proofed to work diffrently than described.

**P9N2_MCS_PORT02_MCPERF0 0x05010823** \
| Bit range | Name      | Assigned value | Description |
| --------- | --------- | -------------- | ----------- |
| 22-27     | AMO_LIMIT | 0x20           |             |

**P9N2_MCS_PORT02_MCPERF2 0x05010824** \
| Bit range | Name                      | Assigned value                             | Description                                                                                 |
| --------- | ------------------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------- |
| 0-2       | PF_DROP_VALUE0            | 1                                          |                                                                                             |
| 3-5       | PF_DROP_VALUE1            | 3                                          |                                                                                             |
| 6-8       | PF_DROP_VALUE2            | 5                                          |                                                                                             |
| 9-11      | PF_DROP_VALUE3            | 7                                          |                                                                                             |
| 13-15     | REFRESH_BLOCK_CONFIG      |                                            | if has only one DIMM in MCA:                                                                |
|           |                           |                                            |   0b000 : if master ranks = 1                                                               |
|           |                           |                                            |   0b001 : if master ranks = 2                                                               |
|           |                           |                                            |   0b100 : if master ranks = 4                                                               |
|           |                           |                                            | Per allowable DIMM mixing rules, we cannot mix different number of ranks on any single port |
|           |                           |                                            | if has both DIMMs in MCA:                                                                   |
|           |                           |                                            |   0b010 : if master ranks = 1                                                               |
|           |                           |                                            |   0b011 : if master ranks = 2                                                               |
|           |                           |                                            |   0b100 : if master ranks = 4 // 4 mranks is the same for one and two DIMMs in MCA          |
| 16        | ENABLE_REFRESH_BLOCK_SQ   |                                            | Always same value as [17]                                                                   |
| 17        | ENABLE_REFRESH_BLOCK_NSQ  |                                            | 1 : if (1 < (DIMM0 + DIMM1 logical ranks) <= 8 && not (one DIMM, 4 mranks, 2H 3DS)          |
|           |                           |                                            | 0 : otherwise                                                                               |
| 18        | ENABLE_REFRESH_BLOCK_DISP | 0                                          |                                                                                             |
| 28-31     | SQ_LFSR_CNTL              | 0b0100                                     |                                                                                             |
| 50-54     | NUM_RMW_BUF               | 0b11100                                    |                                                                                             |
| 61        | EN_ALT_ECR_ERR            | ATTR_ENABLE_MEM_EARLY_DATA_SCOM probably 0 |                                                                                             |

**P9N2_MCS_PORT02_MCAMOC 0x05010825** \
| Bit range | Name                              | Assigned value | Description |
| --------- | --------------------------------- | -------------- | ----------- |
| 1         | FORCE_PF_DROP0                    | 0              |             |
| 4-28      | WRTO_AMO_COLLISION_RULES          | 0x19fffff      |             |
| 29-31     | AMO_SIZE_SELECT, 128B_RW_64B_DATA | 1              |             |

**P9N2_MCS_PORT02_MCEPSQ 0x05010826** \
Note: ATTR_PROC_EPS_READ_CYCLES_T* are calculated in istep 8.6
| Bit range | Name                 | Assigned value                         | Description |
| --------- | -------------------- | -------------------------------------- | ----------- |
| 0-7       | JITTER_EPSILON       | 1                                      |             |
| 8-15      | LOCAL_NODE_EPSILON   | (ATTR_PROC_EPS_READ_CYCLES_T0 + 6) / 4 |             |
| 16-23     | NEAR_NODAL_EPSILON   | (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4 |             |
| 24-31     | GROUP_EPSILON        | (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4 |             |
| 32-39     | REMOTE_NODAL_EPSILON | (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4 |             |
| 40-47     | VECTOR_GROUP_EPSILON | (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4 |             |

**P9N2_MCS_PORT02_MCBUSYQ 0x05010827** \
| Bit range | Name                       | Assigned value | Description          |
| --------- | -------------------------- | -------------- | -------------------- |
| 0         | ENABLE_BUSY_COUNTERS       | 1              |                      |
| 1-3       | BUSY_COUNTER_WINDOW_SELECT | 1              | 1024 cycles selected |
| 4-13      | BUSY_COUNTER_THRESHOLD0    | 38             |                      |
| 14-23     | BUSY_COUNTER_THRESHOLD1    | 51             |                      |
| 24-33     | BUSY_COUNTER_THRESHOLD2    | 64             |                      |

**P9N2_MCS_PORT02_MCPERF3 0x0501082B** \
| Bit range | Name                        | Assigned value                   | Description |
| --------- | --------------------------- | -------------------------------- | ----------- |
| 31        | ENABLE_CL0                  | 1                                |             |
| 41        | ENABLE_AMO_MSI_RMW_ONLY     | 1                                |             |
| 43        | ENABLE_CP_M_MDI0_LOCAL_ONLY | !ATTR_ENABLE_MEM_EARLY_DATA_SCOM | !0 = 1?     |
| 44        | DISABLE_WRTO_IG             | 1                                |             |
| 45        | AMO_LIMIT_SEL               | 1                                |             |
