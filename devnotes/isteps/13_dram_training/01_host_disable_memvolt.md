## host_disable_memvolt: Disable VDDR on Warm Reboots (13.1)

> a) Power off dram - VDDR and vPP. Must drop VDDR first, then VPP.
>    - Turned off here to handle reconfig loop for dimm failure
>    - Only really issued if  VDDR/VPP is on

No-op for Nimbus (why IPL list doesn't say so?), enable pins driven by FPGA, not
configurable.
