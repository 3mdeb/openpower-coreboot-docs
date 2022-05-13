Below is a list of feature attributes from
`src/import/chips/p9/procedures/xml/attribute_info/chip_ec_attributes.xml` in
Hostboot which are not identical across DD2.\*.

Grouping is rather approximate as descriptions are non-existent or short and
cryptic. With some effort grouping can probably be improved a bit.

Most of the attributes are not mentioned in the code, those which appear in code
or init-files are marked in the corresponding column.

| Feature \ DD                    | 2.0 | 2.1 | 2.2 | 2.3 | 2.4+ | HB uses |
| ------------                    | --- | --- | --- | --- | ---- | ------- |
| **CPU parts/instructions:**
| CORE_COMPATIBILITY_MODE         |     |     |     | +   |      |
| CORE_NDD23_CDD11_LOGIC          |     |     |     | +   | +    |
| CORE_NDD23_CDD12_LOGIC          |     |     |     | +   | +    |
| CORE_NDD23_CDD13_LOGIC          |     |     |     | +   | +    |
| DISABLE_CP_ME                   | +   |     |     |     |      |
| DIS_COUNT_CACHE                 |     |     | +   | +   | +    |
| DIS_PATTERN_CACHE_RL            |     |     | +   | +   | +    |
| FLUSH_L1D_TRIG                  |     |     | +   | +   | +    |
| HW403111                        | +   | +   | +   |     |      |
| HW407187                        | +   | +   | +   |     |      |
| HW407330_DD2                    | +   |     |     |     |      |
| HW410503                        |     | +   | +   | +   | +    |
| HW413853                        | +   |     |     |     |      |
| HW416934_HW419818               | +   | +   |     |     |      |
| HW420860                        | +   | +   |     |     |      |
| HW421347                        | +   | +   |     |     |      |
| HW421426                        | +   | +   |     |     |      |
| PREVENT_Q0_HV_ACCESS            |     |     | +   | +   | +    |
| PRE_CACHE_DD21_SETTINGS         | +   |     |     |     |      |
| SERIALIZE_INDIRECT_BRANCHES     | +   | +   |     |     |      |
| SERIALIZE_INDIRECT_BRANCHES_RL  |     |     | +   | +   | +    |
| WOF_NOT_SUPPORTED               | +   |     |     |     |      |
| **Inits:**
| CORE_NDD23_COMPATIBILITY        |     |     |     | +   |      |
| **FIRs:**
| HW411637                        | +   |     |     |     |      | +
| HW414700                        | +   |     |     |     |      | +
| **Power management:**
| BLOCK_REG_WAKEUP_DISABLE        |     |     | +   | +   | +    |
| **Security:**
| CORE_CDD13_NDD23_ADDXX_SECURITY |     |     |     | +   |      |
| CORE_NIMDD22_SECURITY           |     |     | +   |     |      |
| NDD23_SEC_BLOCK_ISSUE           |     |     |     | +   | +    |
| SEC_BLOCK_ISSUE                 |     |     |     | +   | +    |
| **Memory:**
| CORE_SMF_SETUP                  |     |     |     | +   |      |
| CORE_SMF_SETUP_RL               |     |     |     | +   |      |
| NPU_SMF_NIMBUS_CUMULUS          |     |     | +   | +   | +    |
| SMF_SUPPORTED                   |     |     | +   | +   | +    |
| **Scom:**
| CORE_TRACE_NOT_SCOMABLE         | +   | +   | +   | +   | +    |
| HW404391_SCAN                   |     | +   | +   | +   | +    |
| HW404391_SCOM                   | +   |     |     |     |      |
| **XBus:**
| HW407123                        | +   |     |     |     |      | +
| HW418117                        | +   | +   |     |     |      |
| **OBus:**
| HW422471                        | +   |     |     |     |      | +
| HW422471_HW446964               | +   |     |     |     |      | +
| **PCIe:**
| HW410625                        | +   |     |     |     |      | +
| HW414759                        | +   |     |     |     |      | +
| SW430383                        |     |     |     | +   | +    |
| **HOMER/Pstate:**
| HW408892                        | +   |     |     |     |      | +
| VDM_NOT_SUPPORTED               | +   |     |     |     |      | +
| VDM_POUNDW_SUPPRESS_ERROR       | +   |     |     |     |      | +
| **Something else:**
| DIS_241                         | +   | +   | +   |     |      |
| EMQ_DIS_TRACKER_ROUND2          |     |     |     | +   | +    |
| EN_FULL_DERAT                   |     |     | +   | +   | +    |
| HW367017                        | +   |     |     |     |      |
| HW371047_HW415528_HW420575      | +   | +   |     |     |      |
| HW379562_HW419742               | +   | +   |     |     |      |
| HW407165                        | +   | +   | +   |     |      |
| HW407165_DD23                   |     |     |     | +   | +    |
| HW408891                        | +   |     |     |     |      |
| HW408917                        | +   |     |     |     |      |
| HW409270                        | +   |     |     |     |      |
| HW413799                        | +   |     |     |     |      |
| HW413917                        | +   |     |     |     |      |
| HW413922                        |     |     | +   | +   | +    |
| HW414249                        | +   | +   |     |     |      |
| HW414249_ROUND2                 |     |     | +   | +   | +    |
| HW414375                        | +   |     |     |     |      |
| HW414829                        | +   |     |     |     |      |
| HW415013                        | +   |     |     |     |      |
| HW415236                        | +   |     |     |     |      |
| HW416161                        |     | +   |     |     |      |
| HW416317                        |     | +   |     |     |      |
| HW417233                        | +   | +   |     |     |      |
| HW417630                        |     |     | +   | +   | +    |
| HW419082                        |     |     | +   | +   | +    |
| HW420130                        | +   |     |     |     |      |
| HW420171                        |     |     | +   | +   | +    |
| HW420489                        |     |     | +   | +   | +    |
| HW420948                        | +   | +   |     |     |      |
| HW421831                        | +   | +   |     |     |      |
| HW422533                        |     |     | +   | +   | +    |
| HW422629                        |     | +   | +   | +   | +    |
| HW423358                        |     |     | +   | +   | +    |
| HW423532                        |     | +   | +   | +   | +    |
| HW423535                        |     |     | +   | +   | +    |
| HW425526_ROUND2                 |     |     |     | +   | +    |
| HW426420                        |     |     | +   |     |      |
| HW430233                        |     |     | +   | +   | +    |
| HW430233_ROUND2                 |     |     |     | +   | +    |
| HW430944_DISFIX                 |     |     |     | +   | +    |
| HW430944_ROUND2                 |     |     | +   | +   | +    |
| HW431323                        |     |     |     | +   | +    |
| HW433038                        |     |     | +   | +   | +    |
| HW433125                        |     |     | +   | +   | +    |
| HW435395                        |     |     | +   | +   | +    |
| HW436858                        |     |     | +   | +   | +    |
| HW437436_DISFIX                 |     |     |     | +   | +    |
| HW437820                        |     |     | +   | +   | +    |
| HW443669                        |     |     |     | +   | +    |
| HW443982                        | +   | +   | +   |     |      |
| HW446453                        |     |     |     | +   | +    |
| HW447585                        | +   | +   | +   |     |      |
| HW447585_CDD13_NDD23            |     |     |     | +   | +    |
| HW447589                        | +   | +   | +   |     |      |
| HW499047                        |     |     | +   | +   | +    |
| MIXED_CORE_XLATE                |     |     | +   | +   | +    |
| NEW_TM_MODE                     |     |     | +   | +   | +    |
| NMMU_NOT_ISS734                 | +   |     |     |     |      |
| NMMU_PDE_EN_DD2                 | +   |     |     |     |      |
| NMMU_PWC_DIS_DD2                | +   |     |     |     |      |
| P9N_INT_DD20                    | +   |     |     |     |      | +
| P9N_INT_DD21                    |     | +   | +   | +   | +    | +
