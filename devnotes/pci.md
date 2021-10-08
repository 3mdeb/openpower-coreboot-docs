## Terms

PEC - PCI Express Controller (probably)

PHB - PCI Express Host Bridge ("PCIe controller integrated into the CPU and
which connects the CPU with a number of PCIe lanes.")

## Initialization process

- istep 10.10 `proc_pcie_scominit` : Apply scom inits to PCIe chiplets

  * `src/usr/isteps/istep10/host_proc_pcie_scominit.C`
  * `src/usr/isteps/istep10/host_proc_pcie_scominit.H`
  * `src/usr/isteps/istep10/call_proc_pcie_scominit.C`
  * `src/import/chips/p9/procedures/hwp/nest/p9_pcie_scominit.C`
  * `src/import/chips/p9/procedures/hwp/nest/p9_pcie_scominit.H`

  Performs PCIE Phase1 init sequence via SCOM calls.

- istep 10.12 `proc_chiplet_enable_ridi` : Enable RI/DI chip wide (maybe needed)

  * `src/import/chips/p9/procedures/hwp/perv/p9_chiplet_enable_ridi.C`
  * `src/import/chips/p9/procedures/hwp/perv/p9_chiplet_enable_ridi.H`

  Updates value of `NET_CTRL0` register.

- istep 14.3 `proc_pcie_config` : Configure the PHBs

  * `src/import/chips/p9/procedures/hwp/nest/p9_pcie_config.C`

  Performs PCIE Phase2 init sequence.

- istep 14.5 `proc_setup_bars` : Setup Memory BARs

  Looks like 14.3 does this for PCIe.

## Other relevant sources

* `setup_pcie_work_around_attributes()` in
  `src/import/chips/p9/procedures/hwp/perv/p9_getecid.C`.

## What's "initfile" that appears sometimes in comments/documentation?

This seems to be a reference to source files in
`src/import/chips/p9/procedures/hwp/initfiles/` directory.  There are also two
`*.initfiles` there.

## Hostboot attributes

At least 30 PCIe-related attributes which are described in
`src/import/chips/p9/procedures/xml/attribute_info/p9_pcie_attributes.xml`:
 - 4 targets system
 - 1 targets PHB
 - 25 target PEC

Some others are also queried (for example, those related to defect workarounds).

## Discovery/enumeration

Two kinds: PECs and PHBs.

`src/import/hwpf/fapi2/include/error_info_defs.H`:
```c
MAX_PEC_PER_PROC     =  3,  //Nimbus,Cumulus,Axone
MAX_PHB_PER_PROC     =  6,  //Nimbus,Cumulus,Axone
```

`talos.xml` has:
```xml
<attribute>
        <id>PROC_PCIE_NUM_PEC</id>
        <default>3</default>
</attribute>
<attribute>
        <id>PROC_PCIE_NUM_PHB</id>
        <default>6</default>
</attribute>
```

Probably can assume that all of them are present.
