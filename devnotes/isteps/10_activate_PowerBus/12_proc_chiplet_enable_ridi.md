From "POWER9 IPL flow" document:

> 10.12  proc_chiplet_enable_ridi	: Enable RI/DI chip wide
> a p9_chiplet_enable_ridi.C
>  * Drop RI/DI for all chiplets being used (A, O, PCIe, DMI)
>  * Any other chip wide RI/DI

Source files:

- `src/import/chips/p9/procedures/hwp/perv/p9_chiplet_enable_ridi.C`
- `src/import/chips/p9/procedures/hwp/perv/p9_chiplet_enable_ridi.H`

Source code:

```cpp
/// @brief Drop RI/DI for O, PCIE, MC
///
/// @param[in]     i_target_chip   Reference to TARGET_TYPE_PROC_CHIP target
/// @return  FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_chiplet_enable_ridi(const
	fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target_chip)
{
	for(auto l_target_cplt : i_target_chip.getChildren<fapi2::TARGET_TYPE_PERV>
	(static_cast<fapi2::TargetFilter>(fapi2::TARGET_FILTER_ALL_MC |
					  fapi2::TARGET_FILTER_ALL_PCI |
					  fapi2::TARGET_FILTER_ALL_OBUS),
	 fapi2::TARGET_STATE_FUNCTIONAL))
	{
		p9_chiplet_enable_ridi_net_ctrl_action_function(l_target_cplt);
	}
}

/// @brief Enable Drivers/Recievers of O, PCIE, MC chiplets
///
/// @param[in]	   i_target_chiplet   Reference to TARGET_TYPE_PERV target
/// @return  FAPI2_RC_SUCCESS if success, else error code.
static fapi2::ReturnCode p9_chiplet_enable_ridi_net_ctrl_action_function(
	const fapi2::Target<fapi2::TARGET_TYPE_PERV>& i_target_chiplet)
{
	bool l_read_reg = false;
	fapi2::buffer<uint64_t> l_data64;

	// Check for chiplet enable
	// Getting NET_CTRL0 register value
	fapi2::getScom(i_target_chiplet, PERV_NET_CTRL0, l_data64);
	l_read_reg = l_data64.getBit<0>();	//l_read_reg = NET_CTRL0.CHIPLET_ENABLE

	if ( l_read_reg )
	{
		// Enable Recievers, Drivers DI1 & DI2
		// Setting NET_CTRL0 register value
		l_data64.flush<0>();
		l_data64.setBit<19>();	//NET_CTRL0.RI_N = 1
		l_data64.setBit<20>();	//NET_CTRL0.DI1_N = 1
		l_data64.setBit<21>();	//NET_CTRL0.DI2_N = 1
		fapi2::putScom(i_target_chiplet, PERV_NET_CTRL0_WOR, l_data64);
	}
}
```
