# Introduction]

This document gathers informations about bugs found in Hostboot.

### Rounding up not working

[p9_pstate_parameter_block.C#L1803](https://github.com/open-power/hostboot/blob/4689d6d20fccc4587aea2cdfa843dc9881ff6482/src/import/chips/p9/procedures/hwp/pm/p9_pstate_parameter_block.C#L1803)

```cpp
l_vdd = (l_vdd << 1) + 1;
l_vdd = l_vdd >> 1;
```

1.
    The first line moves all bits to the left, discarding the most significant one
    and setting the least significant to '1'.
2.
    The second line moves all bits to the right, discarding the least significant
    bit that was just set in the first line.

### Pair contains four elements

[rank.H#L111](https://github.com/3mdeb/talos-hostboot/blob/a2ddbf3150e2c02ccc904b25d6650c9932a8a841/src/import/chips/p9/procedures/hwp/memory/lib/dimm/rank.H#L111)

```cpp
enum
{
    NUM_RANK_PAIR_REGS = 2,
    NUM_RANKS_IN_PAIR = 4,
};
```

### Comments describing different operation than code implements

Comment describing what will be executed presents different operation than
the actual code implements.

[timing.H#L329](https://github.com/3mdeb/talos-hostboot/blob/a2ddbf3150e2c02ccc904b25d6650c9932a8a841/src/import/chips/p9/procedures/hwp/memory/lib/eff_config/timing.H#L329)\
[timing.H#L344](https://github.com/3mdeb/talos-hostboot/blob/a2ddbf3150e2c02ccc904b25d6650c9932a8a841/src/import/chips/p9/procedures/hwp/memory/lib/eff_config/timing.H#L344)

```cpp
// 12 + std::max((twldqsen - tmod), (twlo - twlow))
l_twlo_twloe = 12 + std::max( (l_twldqsen + tmod(i_target)), (l_wlo_ck + l_wloe_ck) ) + l_dq_ck + l_dqs_ck;
```

### Documentation with different dates of publication

The same version of documentation, accessible on the IBM web page and
Raptor Computing Systems Wiki has the same version, but different date
of publication.

[IBM version](https://ibm.ent.box.com/s/ddcdl3g0otdzyiajhkfe3jjh2oy5p3mt)\
[Raptor version](https://wiki.raptorcs.com/w/images/0/04/POWER9_Registers_vol1_version1.1_pub.pdf)

### Overcomplicated modulo 2 calculation

In C++ this could be done using simple modulo operation.

[p9_mss_setup_bars.C#L269](https://github.com/open-power/hostboot/blob/4689d6d20fccc4587aea2cdfa843dc9881ff6482/src/import/chips/p9/procedures/hwp/nest/p9_mss_setup_bars.C#L269)

```cpp
///
/// @brief Get the port number (with respect to the MC, 0 or 1) for the
///        input PORT_ID
///
///        PORT_ID 0 --> MCS port 0
///        PORT_ID 1 --> MCS port 1
///        PORT_ID 2 --> MCS port 0
///        PORT_ID 3 --> MCS port 1
///        PORT_ID 4 --> MCS port 0
///        PORT_ID 5 --> MCS port 1
///        PORT_ID 6 --> MCS port 0
///        PORT_ID 7 --> MCS port 1
///
/// @param[in]  i_portID      PortID
/// @return port num
///
uint8_t getMCPortNum(uint8_t i_portID)
{
    uint8_t l_mcPos = getMCPosition(i_portID);
    return (i_portID - (2 * l_mcPos));
}
```

### No information about changed SCOM addresses

SCOM addresses has changed between DD1 and DD2 nit there is no information about
it in the documentation, Raptor Wiki or IBM web page.\
[p9_setup_bars.C#L939](https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/nest/p9_setup_bars.C#L939)

### isPstateModeEnabled returns false for Enabled

[p9_pstate_parameter_block.C#L4472](https://github.com/open-power/hostboot/blob/4689d6d20fccc4587aea2cdfa843dc9881ff6482/src/import/chips/p9/procedures/hwp/pm/p9_pstate_parameter_block.C#L4472)
```cpp
bool PlatPmPPB::isPstateModeEnabled()
{
    return((iv_attrs.attr_pstate_mode == fapi2::ENUM_ATTR_SYSTEM_PSTATES_MODE_OFF) ?
            true : false);
} //end of isPstateModeEnabled
```

### Different names for the same attribute

Hostboot attributes often have an alias that is used in `FAPI`/`HWPF`.
For example Hostboot attribute `MIN_FREQ_MHZ` is also named `ATTR_FREQ_CORE_FLOOR_MHZ`.
The first name is used everywhere except `src/import/*`, the second one
in the `src/import/*` and code created from `src/usr/targeting` that
maps the names between each other.\
[attribute_types.xml#n3583](https://git.raptorcs.com/git/talos-hostboot/tree/src/usr/targeting/common/xmltohb/attribute_types.xml#n3583)

### Istep order different than in the documentation

Istep 15.1 is swapped with 15.2 and 15.3 is swapped with 15.4.
The execution order of these isteps is different in the code
and in the documentation.\
[istep15list.H#L111](https://github.com/open-power/hostboot/blob/4689d6d20fccc4587aea2cdfa843dc9881ff6482/src/include/usr/isteps/istep15list.H#L111)\
[P9 IPL Flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)

### Istep missing in documentation

[P9 IPL Flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)\
has no information about istep 8.12.\
[istep08list.H#L257](https://github.com/open-power/hostboot/blob/4689d6d20fccc4587aea2cdfa843dc9881ff6482/src/include/usr/isteps/istep08list.H#L257)

### Pstates inconsistencies and VpdOperatingPoint units

[readme.txt](https://github.com/3mdeb/coreboot/blob/09bc0efd57f59dea97790dedf0ae224ab661fad6/src/soc/ibm/power9/pstates_include/readme.txt)

The comment in the code mentions units of 500mA.\
[p9_pstates_occ.h#L164](https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/lib/p9_pstates_occ.h#L164)

```cpp
/// Operating points
///
/// VPD operating points are stored without load-line correction.  Frequencies
/// are in MHz, voltages are specified in units of 5mV, and currents are
/// in units of 500mA.
VpdOperatingPoint operating_points[NUM_OP_POINTS];
```
[p9_pstates_common.h#L173](https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/lib/p9_pstates_common.h#L173)\
In the included file, units of 100mA are mentioned.
```cpp
/// A VPD operating point
///
/// VPD operating points are stored without load-line correction.  Frequencies
/// are in MHz, voltages are specified in units of 1mV, and characterization
/// currents are specified in units of 100mA.
///
typedef struct
{
    uint32_t vdd_mv;
    uint32_t vcs_mv;
    uint32_t idd_100ma;
    uint32_t ics_100ma;
    uint32_t frequency_mhz;
    uint8_t  pstate;        // Pstate of this VpdOperating
    uint8_t  pad[3];        // Alignment padding
} VpdOperatingPoint;
```
Probably that's why hostboot prints only values, without units.
