## host_enable_memvolt: Enable the VDDR3 Voltage Rail (13.7)

> a) Bring power to dram rails VDDR and VPP. VPP must be enabled prior to VDDR
>     - BMC based systems - this is a no-op
>     - Send message to FSP to turn on voltages
>       - Message must have accounted for voltage/current tweaking based on number of plugged dimms (Dynamic VID)
>       - Pulled from HWPF attributes per voltage rail
>       - FSP
>         - Trigger voltage ramp to DPSS via I2C
>         - Wait for min 200 ms ramp, must be stable 500us after DPSS claims Pgood
>     - Wait for ack message from FSP - confirms that voltage is on and ready
