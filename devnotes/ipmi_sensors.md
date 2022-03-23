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

# Sensors in Hostboot

Targets with IPMI sensors have `ATTR_IPMI_SENSORS` attribute.

`ATTR_IPMI_SENSORS` is a fixed-size array of type `uint16_t[16][2]`. Single
array entry consists of a sensor name and its IPMI number.  IPMI number is
just one byte, which makes it easy to read value of the attribute.  Entries of
the array are sorted by sensor name and are looked up via binary search.
Unused entries look like `0xFFFF, 0xFF` and are thus located at the end of the
array.

Sensor name looks like `0xAABB` where `AA` is its major type and `BB` its minor
type.  Minor type (also called sub-type) corresponds to entity ID in IPMI
specification.

From `obj/genfiles/attributeenums.H`:

```cpp
/**
 *  @brief Enumeration defining the offsets into the IPMI_SENSORS array.
 */
enum IPMI_SENSOR_ARRAY
{
    IPMI_SENSOR_ARRAY_NAME_OFFSET   = 0x00000000,
    IPMI_SENSOR_ARRAY_NUMBER_OFFSET = 0x00000001,
};

/**
 *  @brief Enumeration indicating the IPMI sensor name, which will be used
 *  by hostboot when determining the sensor number to return. The
 *  sensor name consists of one byte of sensor type plus one byte of
 *  sub-type, to differentiate similar sensors under the same target.
 *  Our implementaion uses the IPMI defined entity ID as the sub-type.
 */
enum SENSOR_NAME
{
    SENSOR_NAME_PROC_TEMP                = 0x00000103,
    SENSOR_NAME_DIMM_TEMP                = 0x00000120,
    SENSOR_NAME_CORE_TEMP                = 0x000001D0,
    SENSOR_NAME_STATE                    = 0x00000500,
    SENSOR_NAME_MEMBUF_TEMP              = 0x000001D1,
    SENSOR_NAME_GPU_TEMP                 = 0x000001D8,
    SENSOR_NAME_GPU_MEM_TEMP             = 0x000001D9,
    SENSOR_NAME_VRM_VDD_TEMP             = 0x000001DA,
    SENSOR_NAME_GPU_STATE                = 0x000017D8,
    SENSOR_NAME_PROC_STATE               = 0x00000703,
    SENSOR_NAME_CORE_STATE               = 0x000007D0,
    SENSOR_NAME_HOST_AUTO_REBOOT_CONTROL = 0x00000921,
    SENSOR_NAME_DIMM_STATE               = 0x00000C20,
    SENSOR_NAME_HB_VOLATILE              = 0x00000C21,
    SENSOR_NAME_MEMBUF_STATE             = 0x00000CD1,
    SENSOR_NAME_FW_BOOT_PROGRESS         = 0x00000F22,
    SENSOR_NAME_SYSTEM_EVENT             = 0x00001201,
    SENSOR_NAME_OS_BOOT                  = 0x00001F23,
    SENSOR_NAME_HOST_STATUS              = 0x00002223,
    SENSOR_NAME_OCC_ACTIVE               = 0x000007D2,
    SENSOR_NAME_CORE_FREQ                = 0x0000C1D0,
    SENSOR_NAME_APSS_CHANNEL             = 0x0000C2D7,
    SENSOR_NAME_PCI_ACTIVE               = 0x0000C423,
    SENSOR_NAME_REBOOT_COUNT             = 0x0000C322,
    SENSOR_NAME_FAULT                    = 0x0000C700,
    SENSOR_NAME_BACKPLANE_FAULT          = 0x0000C707,
    SENSOR_NAME_REF_CLOCK_FAULT          = 0x0000C7D4,
    SENSOR_NAME_PCI_CLOCK_FAULT          = 0x0000C7D5,
    SENSOR_NAME_TOD_CLOCK_FAULT          = 0x0000C7D6,
    SENSOR_NAME_APSS_FAULT               = 0x0000C7D7,
    SENSOR_NAME_VRM_VDD_FAULT            = 0x0000C707,
    SENSOR_NAME_DERATING_FACTOR          = 0x0000C815,
    SENSOR_NAME_REDUNDANT_PS_POLICY      = 0x0000CA22,
    SENSOR_NAME_TURBO_ALLOWED            = 0x0000CB03,
    SENSOR_NAME_TPM_REQUIRED             = 0x0000CC03,
    SENSOR_NAME_PCI_BIFURCATED           = 0x0000CD03,
};
```
