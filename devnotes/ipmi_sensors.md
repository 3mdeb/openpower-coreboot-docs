# IPMI sensor types

Based on [talos.xml](https://git.raptorcs.com/git/talos-xml/plain/talos.xml),
sorted by `reg` of `/bmc/sensors/sensor@...` nodes:

  `reg`  | Name in talos.xml            | IPMI sensor type | Notes
---------|------------------------------|------------------|------
0x01     | `host_status_sensor`         | 0x22             |
0x02     | `fw_boot_sensor`             | 0x0F             |
0x03-0x04| `occ_active_sensor`          | 0x07             | One per CPU, only if CPU is installed
0x05     | not used                     |                  |
0x06-0x07| `cpu_temp_sensor`            | 0x01             | One per CPU, only if CPU is installed
0x08-0x09| `cpu_func_sensor`            | 0x07             | One per CPU, only if CPU is installed
0x0A     | `dimm_freq_sensor`           | 0xC1             | Only one (does it limit DIMM population rules?)
0x0B-0x1A| `dimm_func_sensor`           | 0x0C             | One per DIMM, `reg`s for empty slots are skipped
0x1B-0x2A| `dimm_temp_sensor`           | 0x01             | One per DIMM, `reg`s for empty slots are skipped
0x2B-0x5A| `cpucore_func_sensor`        | 0x07             | One per core, not filled by Hostboot
0x5B-0x8A| `cpucore_temp_sensor`        | 0x01             | One per core, not filled by Hostboot
0x8B     | `boot_count_sensor`          | 0xC3             |
0x8C     | `motherboard_fault_sensor`   | 0xC7             |
0x8D     | `ref_clk_sensor`             | 0xC7             |
0x8E     | `pci_clk_sensor`             | 0xC7             |
0x8F     | `tod_clk_sensor`             | 0xC7             |
0x90     | `system_event_sensor`        | 0x12             |
0x91     | `os_boot_sensor`             | 0x1F             |
0x92     | `pci_link_sensor`            | 0xC4             |
0x93     | `apss_fault_sensor`          | 0xC7             |
0x94     | `power_cap_sensor`           | 0xC2             |
0x95     | `ps_redundancy_state_sensor` | 0xCA             |
0x96     | `ps_derating_sensor`         | 0xC8             |
0x97     | `power_limit_sensor`         | 0xC6             |
0x98-0x9F| not used                     |                  |
0xA0-0xCF| `cpucore_freq_sensor`        | 0xC1             | One per core, not filled by Hostboot
0xD0-0xFF| not used                     |                  |
