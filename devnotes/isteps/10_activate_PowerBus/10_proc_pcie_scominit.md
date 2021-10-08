From "POWER9 IPL flow" document:

> 10.10 proc_pcie_scominit : Apply scom inits to PCIe chiplets
> a p9_pcie_scominit.C
>  * Initfiles in procedure defined on VBU ENGD wiki
>  * Perform the PCIe Phase 1 Inits 1-8
>  * Sets the lane config based on MRW attributes
>  * Sets the swap bits based on MRW attributes
>  * Sets valid PHBs, remove from reset
>  * Performs any needed overrides (should flush correctly) – this is where initfile may be used
>  * Set the IOP program complete bit
>  * This is where the dSMP versus PCIE is selected in the PHY Link Layer

Source files:

- `src/usr/isteps/istep10/host_proc_pcie_scominit.C`
- `src/usr/isteps/istep10/host_proc_pcie_scominit.H`
- `src/usr/isteps/istep10/call_proc_pcie_scominit.C`
- `src/import/chips/p9/procedures/hwp/nest/p9_pcie_scominit.C`
- `src/import/chips/p9/procedures/hwp/nest/p9_pcie_scominit.H`

Analisys assumptions:

* `#undef CONFIG_DYNAMIC_BIFURCATION`
* `INITSERVICE::spBaseServicesEnabled() == false` (from the log).

Source code:

```cpp
void*    call_proc_pcie_scominit( void    *io_pArgs )
{
	errlHndl_t          l_errl      =   NULL;

	//
	//  get a list of all the procs in the system
	//
	TARGETING::TargetHandleList l_cpuTargetList;
	getAllChips(l_cpuTargetList, TYPE_PROC);

	for (const auto & l_cpu_target: l_cpuTargetList)
	{
		// Compute the PCIE attribute config on all systems
		l_errl = computeProcPcieConfigAttrs(l_cpu_target);
		if(l_errl != NULL)
		{
			// Any failure to configure PCIE that makes it to this handler
			// implies a firmware bug that should be fixed, everything else
			// is tolerated internally (usually as disabled PHBs)
		}

		const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2_proc_target(
																 l_cpu_target);

		//  call the HWP with each fapi2::Target
		p9_pcie_scominit(l_fapi2_proc_target);
	} // end of looping through all processors
}

/**
 *  @brief Enum indicating lane width (units = "number of lanes")
 */
enum LaneWidth
{
	LANE_WIDTH_NC  = 0,
	LANE_WIDTH_4X  = 4,
	LANE_WIDTH_8X  = 8,
	LANE_WIDTH_16X = 16
};

/**
 *  @brief Enumeration of lane mask values
 */
enum LaneMask
{
	LANE_MASK_X16     = 0xFFFF,
	LANE_MASK_X8_GRP0 = 0xFF00,
	LANE_MASK_X8_GRP1 = 0x00FF,
	LANE_MASK_X4_GRP0 = 0x00F0,
	LANE_MASK_X4_GRP1 = 0x000F,
};

/**
 *  @brief Enum giving bitmask values for enabled PHBs
 */
enum PhbActiveMask
{
	PHB_MASK_NA = 0x00, ///< Sentinel mask (loop terminations)
	PHB0_MASK   = 0x80, ///< PHB0 enabled
	PHB1_MASK   = 0x40, ///< PHB1 enabled
	PHB2_MASK   = 0x20, ///< PHB2 enabled
	PHB3_MASK   = 0x10, ///< PHB3 enabled
	PHB4_MASK   = 0x08, ///< PHB4 enabled
	PHB5_MASK   = 0x04, ///< PHB5 enabled
};

/**
 *  @brief Struct for PCIE lane properties within IOP configuration tables
 */
struct LaneSet
{
	uint8_t width; // Width of each PCIE lane set (0, 4, 8, or 16)
};

/**
 *  @brief Lane groups per PEC
 */
static const size_t MAX_LANE_GROUPS_PER_PEC = 4;

/**
 *  @brief Struct for each row in PCIE IOP configuration table.
 *      Used by code to compute the IOP config and PHBs active mask.
 */
struct laneConfigRow
{
	// Grouping of lanes under one IOP
	LaneSet laneSet[MAX_LANE_GROUPS_PER_PEC];

	// IOP config value from PCIE IOP configuration table
	uint8_t laneConfig;

	// PHB active mask (see PhbActiveMask enum)
	// PHB0 = 0x80
	// PHB1 = 0x40
	// PHB2 = 0x20
	// PHB3 = 0x10
	// PHB4 = 0x08
	// PHB5 = 0x04
	uint8_t phbActive;
	uint16_t phb_to_pcieMAC;
};

/**
 *  @brief Computes PCIE configuration attributes based on MRW values
 *
 *  @param i_pProcChipTarget: Proc chip to compute the PCIE Configuration
 *      attributes for.
 */
errlHndl_t computeProcPcieConfigAttrs(TARGETING::Target * i_pProcChipTarget)
{
	// Currently there are three PEC config tables for procs with 48 usable PCIE
	// lanes. In general, the code accumulates the current configuration of
	// the PECs from the MRW and other dynamic information(such as bifurcation)
	// then matches that config to one of the rows in the table.  Once a match
	// is discovered, the PEC config value is  pulled from the matching row and
	// set in the attributes.
	//
	// Each PEC can control up to 16 lanes:
	// - PEC0 can give 16 lanes to PHB0
	// - PEC1 can split 16 lanes between PHB1 & PHB2
	// - PEC2 can split 16 lanes between PHB3, PHB4 & PHB5
	const laneConfigRow pec0_laneConfigTable[] =
		{{{LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB_MASK_NA,
		   PHB_X16_MAC_MAP},

		 {{LANE_WIDTH_16X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB0_MASK,
		   PHB_X16_MAC_MAP},
		};

	const laneConfigRow pec1_laneConfigTable[] =
		{{{LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB_MASK_NA,
		   PHB_X8_X8_MAC_MAP},

		 {{LANE_WIDTH_8X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_8X,
		   LANE_WIDTH_NC},
		   0x00,PHB1_MASK|PHB2_MASK,
		   PHB_X8_X8_MAC_MAP},

		 {{LANE_WIDTH_8X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB1_MASK,
		   PHB_X8_X8_MAC_MAP},

		 {{LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_8X,
		   LANE_WIDTH_NC},
		   0x00,PHB2_MASK,
		   PHB_X8_X8_MAC_MAP},
		};

	const laneConfigRow pec2_laneConfigTable[] =
		{{{LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB_MASK_NA,
		   PHB_X16_MAC_MAP},

		 {{LANE_WIDTH_16X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_NC},
		   0x00,PHB3_MASK,
		   PHB_X16_MAC_MAP},

		 {{LANE_WIDTH_8X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_8X,
		   LANE_WIDTH_NC},
		   0x10,PHB3_MASK|PHB4_MASK,
		   PHB_X8_X8_MAC_MAP},

		 {{LANE_WIDTH_8X,
		   LANE_WIDTH_NC,
		   LANE_WIDTH_4X,
		   LANE_WIDTH_4X},
		   0x20,PHB3_MASK|PHB4_MASK|PHB5_MASK,
		   PHB_X8_X4_X4_MAC_MAP},
		};

	const laneConfigRow* pec0_end = pec0_laneConfigTable +
		(  sizeof(pec0_laneConfigTable)
		 / sizeof(pec0_laneConfigTable[0]));

	const laneConfigRow* pec1_end = pec1_laneConfigTable +
		(  sizeof(pec1_laneConfigTable)
		 / sizeof(pec1_laneConfigTable[0]));

	const laneConfigRow* pec2_end = pec2_laneConfigTable +
		(  sizeof(pec2_laneConfigTable)
		 / sizeof(pec2_laneConfigTable[0]));

	const laneConfigRow* pLaneConfigTableBegin = nullptr;
	const laneConfigRow* pLaneConfigTableEnd = nullptr;
	TARGETING::ATTR_PROC_PCIE_PHB_ACTIVE_type procPhbActiveMask = 0;

	assert((i_pProcChipTarget != NULL) && "computeProcPcieConfigs was "
		"passed in a NULL processor target");

	const TARGETING::ATTR_CLASS_type targetClass
		= i_pProcChipTarget->getAttr<TARGETING::ATTR_CLASS>();
	const TARGETING::ATTR_TYPE_type targetType
		= i_pProcChipTarget->getAttr<TARGETING::ATTR_TYPE>();
	const bool targetPresent =
		i_pProcChipTarget->getAttr<TARGETING::ATTR_HWAS_STATE>()
			.present;

	assert(targetClass == TARGETING::CLASS_CHIP
		   && targetType == TARGETING::TYPE_PROC
		   && targetPresent && "computeProcPcieConfigs - input either not a "
		   "processor chip or not present");

	// Set up vector of functional PECs under this processor
	TargetHandleList l_pecList;
	(void)TARGETING::getChildChiplets(
						 l_pecList, i_pProcChipTarget, TARGETING::TYPE_PEC);

	// Even if the list is empty we still want to go to the bottom of the
	// function and set PROC_PHB_ACTIVE_MASK

	// Iterate over every PEC to find its config, swap, reversal and
	// bifurcation attributes
	for(auto l_pec : l_pecList)
	{
		// Get the PEC id
		uint8_t l_pecID = l_pec->getAttr<TARGETING::ATTR_CHIP_UNIT>();

		// Select the correct PEC config table
		if      (l_pecID == 0)
		{
			pLaneConfigTableBegin = pec0_laneConfigTable;
			pLaneConfigTableEnd = pec0_end;
		}
		else if (l_pecID == 1)
		{
			pLaneConfigTableBegin = pec1_laneConfigTable;
			pLaneConfigTableEnd = pec1_end;
		}
		else if (l_pecID == 2)
		{
			pLaneConfigTableBegin = pec2_laneConfigTable;
			pLaneConfigTableEnd = pec2_end;
		}
		else
		{
			// Code bug! Unsupported PEC ID attribute for
			// processor.  Expected 0,1 or 2.
			break;
		}

		TARGETING::ATTR_PROC_PCIE_LANE_MASK_type effectiveLaneMask = {0};

		//Only attempt to determine the lane config on FSP-less systems
		//On FSP based systems it has already been determined
		{
			TARGETING::ATTR_PEC_PCIE_LANE_MASK_NON_BIFURCATED_type
			  laneMaskNonBifurcated = {0};
			assert(l_pec->tryGetAttr<
					 TARGETING::ATTR_PEC_PCIE_LANE_MASK_NON_BIFURCATED>(
					 laneMaskNonBifurcated) && "Failed to get "
					 "ATTR_PEC_PCIE_LANE_MASK_NON_BIFURCATED attribute");

			memcpy(effectiveLaneMask,laneMaskNonBifurcated,
				   sizeof(effectiveLaneMask));

			l_pec->setAttr<TARGETING::ATTR_PROC_PCIE_LANE_MASK>(effectiveLaneMask);
		}

		TARGETING::ATTR_PROC_PCIE_PHB_ACTIVE_type pecPhbActiveMask = 0;
		TARGETING::ATTR_PROC_PCIE_IOP_CONFIG_type iopConfig = 0;
		TARGETING::ATTR_PROC_PCIE_REFCLOCK_ENABLE_type refEnable = 0;
		TARGETING::ATTR_PROC_PCIE_PCS_SYSTEM_CNTL_type macCntl = 0;

		laneConfigRow effectiveConfig =
			  {{LANE_WIDTH_NC,
				LANE_WIDTH_NC,
				LANE_WIDTH_NC,
				LANE_WIDTH_NC},
				0x00,PHB_MASK_NA,
				PHB_X16_MAC_MAP,
			  };

		// Transform effective config to match lane config table format
		for(size_t laneGroup = 0;
			laneGroup < MAX_LANE_GROUPS_PER_PEC;
			++laneGroup)
		{
			effectiveConfig.laneSet[laneGroup].width
				= _laneMaskToLaneWidth(effectiveLaneMask[laneGroup]);
		}

		const laneConfigRow* laneConfigItr =
			std::find(
				pLaneConfigTableBegin,
				pLaneConfigTableEnd,
				effectiveConfig);

		if(laneConfigItr != pLaneConfigTableEnd)
		{
			iopConfig = laneConfigItr->laneConfig;
			refEnable = 0x1;
			macCntl = laneConfigItr->phb_to_pcieMAC;
			pecPhbActiveMask = laneConfigItr->phbActive;

			// If we find a valid config, and the PHB_MASK is still NA
			// that means all PHB's on this PEC will be disabled. Lets
			// trace something out just so someone knows.
			if(pecPhbActiveMask == PHB_MASK_NA)
			{
				 // Valid configuration found for a PEC, but no PHBs behind
				 // it will be functional
			}
			else
			{
				 // Valid configuration found for a PEC
			}
		}
		else
		{
				// Code bug! PEC PCIE IOP configuration not found.
				// Continuing with no PHBs active on PEC.
		}

		// Add the PEC phb mask to the overall Proc PHB mask
		procPhbActiveMask |= pecPhbActiveMask;

		l_pec->setAttr<
			TARGETING::ATTR_PROC_PCIE_IOP_CONFIG>(iopConfig);
		l_pec->setAttr<
			  TARGETING::ATTR_PROC_PCIE_REFCLOCK_ENABLE>(refEnable);
		l_pec->setAttr<
			  TARGETING::ATTR_PROC_PCIE_PCS_SYSTEM_CNTL>(macCntl);
	} // PEC loop

	// Deconfigure the PHBs once we have the phbActiveMask attribute
	// set up for the whole processor.  Also, update the phbActiveMask
	// attribute to include gard'd PHBs.
	(void)_deconfigPhbsBasedOnPhbMask(
		 i_pProcChipTarget,
		 procPhbActiveMask);

	// Set the procPhbActiveMask
	i_pProcChipTarget->setAttr<
		TARGETING::ATTR_PROC_PCIE_PHB_ACTIVE>(procPhbActiveMask);

	// setup ATTR_PROC_PCIE_IOVALID_ENABLE for this processor
	// This has to be done after the PHB's are disabled
	setup_pcie_iovalid_enable(i_pProcChipTarget);
}

inline bool operator==(
	const laneConfigRow& i_lhs,
	const laneConfigRow& i_rhs)
	{
		return ( memcmp(i_lhs.laneSet,i_rhs.laneSet,
			sizeof(i_lhs.laneSet)) == 0);
	}

LaneWidth _laneMaskToLaneWidth(const uint16_t i_laneMask)
{
	LaneWidth laneWidth = LANE_WIDTH_NC;
	if(i_laneMask == LANE_MASK_X16)
	{
		laneWidth = LANE_WIDTH_16X;
	}
	else if(   (i_laneMask == LANE_MASK_X8_GRP0)
			|| (i_laneMask == LANE_MASK_X8_GRP1))
	{
		laneWidth = LANE_WIDTH_8X;
	}
	else if(   (i_laneMask == LANE_MASK_X4_GRP0)
			|| (i_laneMask == LANE_MASK_X4_GRP1))
	{
		laneWidth = LANE_WIDTH_4X;
	}

	return laneWidth;
}

void _deconfigPhbsBasedOnPhbMask(
	TARGETING::ConstTargetHandle_t const       i_procTarget,
	TARGETING::ATTR_PROC_PCIE_PHB_ACTIVE_type& io_phbActiveMask)
{
	uint8_t l_phbNum = 0;
	errlHndl_t l_err = NULL;

	// PHB mask bits start at the left most bit - so we must shift the bits
	// right in order to get the correct masks. This number below should
	// always be 7.
	const size_t bitsToRightShift =
		(sizeof(io_phbActiveMask) * BITS_PER_BYTE) - 1;

	// Get every PEC under the Proc
	TARGETING::TargetHandleList l_pecList;
	(void)TARGETING::getChildChiplets(
		l_pecList, i_procTarget, TARGETING::TYPE_PEC, false);

	for (auto const & l_pec : l_pecList)
	{
		// Get pec chip's PHB units
		TARGETING::TargetHandleList l_phbList;
		(void)TARGETING::getChildChiplets(
			l_phbList, l_pec, TARGETING::TYPE_PHB, false);

		// io_phbActiveMask is a bitmask whose leftmost bit corresponds to
		// PHB0, followed by bits for PHB1, PHB2, PHB3, PHB4, and PBH5. The
		// remaining bits are ignored. We need to compare each PHB mask to
		// its respective PHB and deconfigure it if needed.
		for (auto const & l_phb : l_phbList)
		{
			// Get the PHB unit number
			l_phbNum = l_phb->getAttr<TARGETING::ATTR_CHIP_UNIT>();

			// Subtract the PHB unit number from the constant bitsToRightShift
			// in order to get the correct amount of bits to shift right.
			// e.g. for PHB0, the unit number is 0, bitsToRightShift-0 = 7.
			// We will shift io_phbActiveMask 7 bits right, which means we are
			// examining bit 0 of the PHB mask. If it is not set - deconfigure
			// the respective PHB
			if (!((io_phbActiveMask >> (bitsToRightShift - l_phbNum)) & 1))
			{
				l_err = HWAS::theDeconfigGard().deconfigureTarget(
					 *l_phb, HWAS::DeconfigGard::DECONFIGURED_BY_PHB_DECONFIG);

				if (l_err)
				{
					// Try to deconfigure any other PHBs
				}
			}
			else
			{
				// PHB is marked active, check if it is non-functional.
				// if so, then mark it inactive in the phbActiveMask.
				if (!l_phb->getAttr<ATTR_HWAS_STATE>().functional)
				{
					io_phbActiveMask &= ~(1 >> (bitsToRightShift - l_phbNum));
				}
			}
		} // PHB loop
	} // PEC loop
}

/******************************************************************
 * setup_pcie_iovalid_enable
 *
 * Setup ATTR_PROC_PCIE_IOVALID_ENABLE on i_procTarget's PEC children
 *
 *******************************************************************/
void setup_pcie_iovalid_enable(const TARGETING::Target * i_procTarget)
{
	// Get list of PEC chiplets downstream from the given proc chip
	TARGETING::TargetHandleList l_pecList;

	getChildAffinityTargetsByState( l_pecList,
									i_procTarget,
									TARGETING::CLASS_NA,
									TARGETING::TYPE_PEC,
									TARGETING::UTIL_FILTER_ALL);

	for (auto l_pecTarget : l_pecList)
	{
		// Get list of PHB chiplets downstream from the given PEC chiplet
		TARGETING::TargetHandleList l_phbList;

		getChildAffinityTargetsByState( l_phbList,
				   const_cast<TARGETING::Target*>(l_pecTarget),
				   TARGETING::CLASS_NA,
				   TARGETING::TYPE_PHB,
				   TARGETING::UTIL_FILTER_ALL);

		// default to all invalid
		ATTR_PROC_PCIE_IOVALID_ENABLE_type l_iovalid = 0;

		// arrange phb targets from largest to smallest based on unit
		// ex.  PHB5, PHB4, PHB3
		std::sort(l_phbList.begin(),l_phbList.end(),compareChipUnits);
		for(uint32_t k = 0; k<l_phbList.size(); ++k)
		{
			const fapi2::Target<fapi2::TARGET_TYPE_PHB> l_fapi_phb_target(l_phbList[k]);

			if(l_fapi_phb_target.isFunctional())
			{
				// PHB functional

				// filled in bitwise, largest PHB unit on the right to smallest
				// leftword. ex. l_iovalid = 0b00000110 : PHB3, PHB4 functional
				// PHB5 not
				l_iovalid |= (1<<k);
			}
			else
			{
				// PHB not functional
			}
		}

		l_pecTarget->setAttr<TARGETING::ATTR_PROC_PCIE_IOVALID_ENABLE>(l_iovalid);
	}
}

#define SET_REG_RMW_WITH_SINGLE_ATTR_8(in_attr_name, in_reg_name, in_start_bit, in_bit_count)\
	FAPI_ATTR_GET(in_attr_name, l_pec_chiplets, l_attr_8); \
	fapi2::getScom(l_pec_chiplets, in_reg_name, l_buf); \
	l_buf.insertFromRight(l_attr_8, in_start_bit, in_bit_count); \
	fapi2::putScom(l_pec_chiplets, in_reg_name, l_buf);

#define SET_REG_RMW_WITH_SINGLE_ATTR_16(in_attr_name, in_reg_name, in_start_bit, in_bit_count)\
	FAPI_ATTR_GET(in_attr_name, l_pec_chiplets, l_attr_16); \
	fapi2::getScom(l_pec_chiplets, in_reg_name, l_buf); \
	l_buf.insertFromRight(l_attr_16, in_start_bit, in_bit_count); \
	fapi2::putScom(l_pec_chiplets, in_reg_name, l_buf);

#define SET_REG_WR_WITH_SINGLE_ATTR_16(in_attr_name, in_reg_name, in_start_bit, in_bit_count)\
	l_buf = 0; \
	FAPI_ATTR_GET(in_attr_name, l_pec_chiplets, l_attr_16); \
	l_buf.insertFromRight(l_attr_16, in_start_bit, in_bit_count); \
	fapi2::putScom(l_pec_chiplets, in_reg_name, l_buf);

#define SET_REG_RMW(in_value, in_reg_name, in_start_bit, in_bit_count)\
	fapi2::getScom(l_pec_chiplets, in_reg_name, l_buf); \
	l_buf.insertFromRight(in_value, in_start_bit, in_bit_count); \
	fapi2::putScom(l_pec_chiplets, in_reg_name, l_buf);

const uint64_t PCI_IOP_FIR_ACTION0_REG = 0x0000000000000000ULL;
const uint64_t PCI_IOP_FIR_ACTION1_REG = 0xE000000000000000ULL;
const uint64_t PCI_IOP_FIR_MASK_REG    = 0x1FFFFFFFF8000000ULL;

const uint8_t NUM_PCS_CONFIG = 4;
const uint8_t NUM_PCIE_LANES = 16;
const uint8_t NUM_M_CONFIG = 4;
const uint8_t PEC0_IOP_CONFIG_START_BIT = 13;
const uint8_t PEC1_IOP_CONFIG_START_BIT = 14;
const uint8_t PEC2_IOP_CONFIG_START_BIT = 10;
const uint8_t PEC0_IOP_BIT_COUNT = 1;
const uint8_t PEC1_IOP_BIT_COUNT = 2;
const uint8_t PEC2_IOP_BIT_COUNT = 3;
const uint8_t PEC0_IOP_SWAP_START_BIT = 12;
const uint8_t PEC1_IOP_SWAP_START_BIT = 12;
const uint8_t PEC2_IOP_SWAP_START_BIT = 7;
const uint8_t PEC0_IOP_IOVALID_ENABLE_START_BIT = 4;
const uint8_t PEC1_IOP_IOVALID_ENABLE_START_BIT = 4;
const uint8_t PEC2_IOP_IOVALID_ENABLE_START_BIT = 4;
const uint8_t PEC_IOP_IOVALID_ENABLE_STACK0_BIT = 4;
const uint8_t PEC_IOP_IOVALID_ENABLE_STACK1_BIT = 5;
const uint8_t PEC_IOP_IOVALID_ENABLE_STACK2_BIT = 6;
const uint8_t PEC_IOP_REFCLOCK_ENABLE_START_BIT = 32;
const uint8_t PEC_IOP_PMA_RESET_START_BIT = 29;
const uint8_t PEC_IOP_PIPE_RESET_START_BIT = 28;
const uint8_t PEC_IOP_HSS_PORT_READY_START_BIT = 58;

const uint64_t PEC_IOP_PLLA_VCO_COURSE_CAL_REGISTER1 = 0x800005010D010C3F;
const uint64_t PEC_IOP_PLLB_VCO_COURSE_CAL_REGISTER1 = 0x800005410D010C3F;
const uint64_t PEC_IOP_RX_DFE_FUNC_REGISTER1 = 0x8000049F0D010C3F;
const uint64_t PEC_IOP_RX_DFE_FUNC_REGISTER2 = 0x800004A00D010C3F;

const uint64_t RX_VGA_CTRL3_REGISTER[NUM_PCIE_LANES] =
{
	0x8000008D0D010C3F,
	0x800000CD0D010C3F,
	0x8000018D0D010C3F,
	0x800001CD0D010C3F,
	0x8000028D0D010C3F,
	0x800002CD0D010C3F,
	0x8000038D0D010C3F,
	0x800003CD0D010C3F,
	0x8000088D0D010C3F,
	0x800008CD0D010C3F,
	0x8000098D0D010C3F,
	0x800009CD0D010C3F,
	0x80000A8D0D010C3F,
	0x80000ACD0D010C3F,
	0x80000B8D0D010C3F,
	0x80000BCD0D010C3F,
};

const uint64_t RX_LOFF_CNTL_REGISTER[NUM_PCIE_LANES] =
{
	0x800000A60D010C3F,
	0x800000E60D010C3F,
	0x800001A60D010C3F,
	0x800001E60D010C3F,
	0x800002A60D010C3F,
	0x800002E60D010C3F,
	0x800003A60D010C3F,
	0x800003E60D010C3F,
	0x800008A60D010C3F,
	0x800008E60D010C3F,
	0x800009A60D010C3F,
	0x800009E60D010C3F,
	0x80000AA60D010C3F,
	0x80000AE60D010C3F,
	0x80000BA60D010C3F,
	0x80000BE60D010C3F,
};

const uint32_t PCS_CONFIG_MODE0 = 0xA006;
const uint32_t PCS_CONFIG_MODE1 = 0xA805;
const uint32_t PCS_CONFIG_MODE2 = 0xB071;
const uint32_t PCS_CONFIG_MODE3 = 0xB870;

const uint32_t MAX_NUM_POLLS = 100; //Maximum number of iterations (So, 400ns * 100 = 40us before timeout)
const uint64_t PMA_RESET_NANO_SEC_DELAY = 400; //400ns to wait for PMA RESET to go through
const uint64_t PMA_RESET_CYC_DELAY = 400; //400ns to wait for PMA RESET to go through

/// @brief This function configures a buffer with respect to different pec id
///
/// @param[in]         in_target The target
/// @param[in/out]     io_buf  The buffer
/// @param[in]         in_pec_id The PEC id
/// @param[in]         in_attr The attribute value to be set
/// @param[in]         in_pec[0-2]_s The start bit for pec[0-2]
/// @param[in]         in_pec[0-2]_c The bit count for pec[0-2]
///
/// @return  FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode set_buf(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& in_target,
						  fapi2::buffer<uint64_t>& io_buf,
						  const int in_pec_id,
						  const uint8_t in_attr,
						  const uint32_t in_pec0_s, const uint32_t in_pec0_c,
						  const uint32_t in_pec1_s, const uint32_t in_pec1_c,
						  const uint32_t in_pec2_s, const uint32_t in_pec2_c)
{
	switch(in_pec_id)
	{
		case 0:
			io_buf.insertFromRight(in_attr, in_pec0_s, in_pec0_c);
			break;

		case 1:
			io_buf.insertFromRight(in_attr, in_pec1_s, in_pec1_c);
			break;

		case 2:
			io_buf.insertFromRight(in_attr, in_pec2_s, in_pec2_c);
			break;

		default:
			assert(false && "Unknown PEC ID!");
			break;
	}
}

/// @brief Perform PCIE Phase1 init sequence
/// @param[in] i_target => P9 chip target
/// @return FAPI_RC_SUCCESS if the setup completes successfully,
//
fapi2::ReturnCode p9_pcie_scominit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
	uint8_t l_attr_proc_pcie_iop_config = 0;
	uint8_t l_attr_proc_pcie_iop_swap = 0;
	uint8_t l_attr_proc_pcie_iovalid_enable = 0;
	uint8_t l_attr_proc_pcie_refclock_enable = 0;
	fapi2::buffer<uint64_t> l_buf = 0;
	fapi2::buffer<uint64_t> l_buf2 = 0;
	unsigned char l_pec_id = 0;
	auto l_pec_chiplets_vec = i_target.getChildren<fapi2::TARGET_TYPE_PEC>();
	uint32_t l_pcs_config_mode[NUM_PCS_CONFIG] = {PCS_CONFIG_MODE0, PCS_CONFIG_MODE1, PCS_CONFIG_MODE2, PCS_CONFIG_MODE3};
	uint8_t l_pcs_cdr_gain[NUM_PCS_CONFIG] = {0};
	uint8_t l_pcs_pk_init[NUM_PCS_CONFIG][NUM_PCIE_LANES] = {0};
	uint8_t l_pcs_init_gain[NUM_PCS_CONFIG][NUM_PCIE_LANES] = {0};
	uint8_t l_pcs_sigdet_lvl[NUM_PCS_CONFIG] = {0};
	uint16_t l_pcs_m_cntl[NUM_M_CONFIG] = {0};
	uint8_t l_pcs_rot_cntl_cdr_lookahead = 0;
	uint8_t l_pcs_rot_cntl_cdr_ssc = 0;
	uint8_t l_pcs_rot_cntl_extel = 0;
	uint8_t l_pcs_rot_cntl_rst_fw = 0;
	uint8_t l_pcs_rx_dfe_fddc = 0;
	uint8_t l_attr_8 = 0;
	uint16_t l_attr_16 = 0;
	uint32_t l_poll_counter; //Number of iterations while polling for PLLA and PLLB Port Ready Status

	FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414759, i_target, l_hw414759);

	for (auto l_pec_chiplets : l_pec_chiplets_vec)
	{
		// Get the pec id
		FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_pec_chiplets, l_pec_id);

		// Phase1 init step 1 (get VPD, no operation here)

		// Phase1 init step 2a
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_IOP_CONFIG, l_pec_chiplets,
					  l_attr_proc_pcie_iop_config);

		l_buf = 0;
		set_buf(i_target, l_buf, l_pec_id, l_attr_proc_pcie_iop_config,
				PEC0_IOP_CONFIG_START_BIT, PEC0_IOP_BIT_COUNT * 2,
				PEC1_IOP_CONFIG_START_BIT, PEC1_IOP_BIT_COUNT * 2,
				PEC2_IOP_CONFIG_START_BIT, PEC2_IOP_BIT_COUNT * 2);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_OR, l_buf);

		// Phase1 init step 2b
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_IOP_SWAP, l_pec_chiplets,
					  l_attr_proc_pcie_iop_swap);

		l_buf = 0;
		set_buf(i_target, l_buf, l_pec_id, l_attr_proc_pcie_iop_swap,
				PEC0_IOP_SWAP_START_BIT, PEC0_IOP_BIT_COUNT,
				PEC1_IOP_SWAP_START_BIT, PEC1_IOP_BIT_COUNT,
				PEC2_IOP_SWAP_START_BIT, PEC2_IOP_BIT_COUNT);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_OR, l_buf);

		// Phase1 init step 3a
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_IOVALID_ENABLE, l_pec_chiplets,
					  l_attr_proc_pcie_iovalid_enable);

		l_buf = 0;
		set_buf(i_target, l_buf, l_pec_id, l_attr_proc_pcie_iovalid_enable,
				PEC0_IOP_IOVALID_ENABLE_START_BIT, PEC0_IOP_BIT_COUNT,
				PEC1_IOP_IOVALID_ENABLE_START_BIT, PEC1_IOP_BIT_COUNT,
				PEC2_IOP_IOVALID_ENABLE_START_BIT, PEC2_IOP_BIT_COUNT);

		// Set IOVALID for base PHB if PHB2, or PHB4, or PHB5 are set (SW417485)
		if (l_buf.getBit(PEC_IOP_IOVALID_ENABLE_STACK1_BIT) || l_buf.getBit(PEC_IOP_IOVALID_ENABLE_STACK2_BIT))
		{
			l_buf.setBit<PEC_IOP_IOVALID_ENABLE_STACK0_BIT>();
			l_buf.setBit<PEC_IOP_IOVALID_ENABLE_STACK1_BIT>();
		}

		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_OR, l_buf);

		// Phase1 init step 3b (enable clock)
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_REFCLOCK_ENABLE, l_pec_chiplets, l_attr_proc_pcie_refclock_enable);

		l_buf = 0;
		l_buf.insertFromRight(l_attr_proc_pcie_refclock_enable, PEC_IOP_REFCLOCK_ENABLE_START_BIT, 1);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CTRL0_OR, l_buf);

		// Phase1 init step 4 (PMA reset)
		l_buf = 0;
		l_buf.insertFromRight(1, PEC_IOP_PMA_RESET_START_BIT, 1);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_CLEAR, l_buf);
		fapi2::delay(PMA_RESET_NANO_SEC_DELAY, PMA_RESET_CYC_DELAY);

		l_buf = 0;
		l_buf.insertFromRight(1, PEC_IOP_PMA_RESET_START_BIT, 1);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_OR, l_buf);
		fapi2::delay(PMA_RESET_NANO_SEC_DELAY, PMA_RESET_CYC_DELAY);

		l_buf = 0;
		l_buf.insertFromRight(1, PEC_IOP_PMA_RESET_START_BIT, 1);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_CLEAR, l_buf);

		// Poll for PRTREADY status on PLLA and PLLB

		l_poll_counter = 0; //Reset poll counter

		while (l_poll_counter < MAX_NUM_POLLS)
		{
			l_poll_counter++;
			fapi2::delay(PMA_RESET_NANO_SEC_DELAY, PMA_RESET_CYC_DELAY);

			//Read PLLA VCO Course Calibration Register into l_buf
			fapi2::getScom(l_pec_chiplets, PEC_IOP_PLLA_VCO_COURSE_CAL_REGISTER1, l_buf);

			//Read PLLB VCO Course Calibration Register into l_buf
			fapi2::getScom(l_pec_chiplets, PEC_IOP_PLLB_VCO_COURSE_CAL_REGISTER1, l_buf2);

			//Check PRTEADY PLLA and PLLB status bit
			if (l_buf.getBit(PEC_IOP_HSS_PORT_READY_START_BIT) || l_buf2.getBit(PEC_IOP_HSS_PORT_READY_START_BIT))
			{
				// HSS Port is ready
				break;
			}
		}

		assert(l_poll_counter < MAX_NUM_POLLS && "IOP HSS Port Ready status is not set!");

		// Phase1 init step 5 (Set IOP FIR action0)
		fapi2::putScom(l_pec_chiplets, PEC_FIR_ACTION0_REG, PCI_IOP_FIR_ACTION0_REG);

		// Phase1 init step 6 (Set IOP FIR action1)
		fapi2::putScom(l_pec_chiplets, PEC_FIR_ACTION1_REG, PCI_IOP_FIR_ACTION1_REG);

		// Phase1 init step 7 (Set IOP FIR mask)
		fapi2::putScom(l_pec_chiplets, PEC_FIR_MASK_REG, PCI_IOP_FIR_MASK_REG);

		// Phase1 init step 8-11 (Config 0 - 3)
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_CDR_GAIN, l_pec_chiplets, l_pcs_cdr_gain);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_INIT_GAIN, l_pec_chiplets, l_pcs_init_gain);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_PK_INIT, l_pec_chiplets, l_pcs_pk_init);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_SIGDET_LVL, l_pec_chiplets, l_pcs_sigdet_lvl);

		for (int i = 0; i < NUM_PCS_CONFIG; i++)
		{
			// RX Config Mode
			l_buf = 0;
			l_buf.insertFromRight(l_pcs_config_mode[i], 48, 16);
			fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_CONFIG_MODE_REG, l_buf);

			// RX CDR GAIN
			fapi2::getScom(l_pec_chiplets, PEC_PCS_RX_CDR_GAIN_REG, l_buf);
			l_buf.insertFromRight(l_pcs_cdr_gain[i], 56, 8);
			fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_CDR_GAIN_REG, l_buf);

			for  (int l_lane = 0; l_lane < NUM_PCIE_LANES; l_lane++)
			{
				// RX INITGAIN
				fapi2::getScom(l_pec_chiplets, RX_VGA_CTRL3_REGISTER[l_lane], l_buf);
				l_buf.insertFromRight(l_pcs_init_gain[i][l_lane], 48, 5);
				fapi2::putScom(l_pec_chiplets, RX_VGA_CTRL3_REGISTER[l_lane], l_buf);

				// RX PKINIT
				fapi2::getScom(l_pec_chiplets, RX_LOFF_CNTL_REGISTER[l_lane], l_buf);
				l_buf.insertFromRight(l_pcs_pk_init[i][l_lane], 58, 6);
				fapi2::putScom(l_pec_chiplets, RX_LOFF_CNTL_REGISTER[l_lane], l_buf);
			}

			// RX SIGDET LVL
			fapi2::getScom(l_pec_chiplets, PEC_PCS_RX_SIGDET_CONTROL_REG, l_buf);
			l_buf.insertFromRight(l_pcs_sigdet_lvl[i], 59, 5);
			fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_SIGDET_CONTROL_REG, l_buf);
		}

		// Phase1 init step 12 (RX Rot Cntl CDR Lookahead Disabled,SSC Disabled)
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_ROT_CDR_LOOKAHEAD, l_pec_chiplets,
					  l_pcs_rot_cntl_cdr_lookahead);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_ROT_CDR_SSC, l_pec_chiplets,
					  l_pcs_rot_cntl_cdr_ssc);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_ROT_EXTEL, l_pec_chiplets,
					  l_pcs_rot_cntl_extel);
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_ROT_RST_FW, l_pec_chiplets,
					  l_pcs_rot_cntl_rst_fw);

		fapi2::getScom(l_pec_chiplets, PEC_PCS_RX_ROT_CNTL_REG, l_buf);
		l_buf.insertFromRight(l_pcs_rot_cntl_cdr_lookahead, 55, 1);
		l_buf.insertFromRight(l_pcs_rot_cntl_cdr_ssc, 63, 1);
		l_buf.insertFromRight(l_pcs_rot_cntl_extel, 59, 1);
		l_buf.insertFromRight(l_pcs_rot_cntl_rst_fw, 62, 1);
		fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_ROT_CNTL_REG, l_buf);

		// Phase1 init step 13 (RX Config Mode Enable External Config Control)
		l_buf = 0;
		l_buf.insertFromRight(0x8600, 48, 16);
		fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_CONFIG_MODE_REG, l_buf);

		// Phase1 init step 14 (PCLCK Control Register - PLLA)
		SET_REG_RMW_WITH_SINGLE_ATTR_8(fapi2::ATTR_PROC_PCIE_PCS_PCLCK_CNTL_PLLA,
									   PEC_PCS_PCLCK_CNTL_PLLA_REG,
									   56, 8);

		// Phase1 init step 15 (PCLCK Control Register - PLLB)
		SET_REG_RMW_WITH_SINGLE_ATTR_8(fapi2::ATTR_PROC_PCIE_PCS_PCLCK_CNTL_PLLB,
									   PEC_PCS_PCLCK_CNTL_PLLB_REG,
									   56, 8);

		// Phase1 init step 16 (TX DCLCK Rotator Override)
		SET_REG_WR_WITH_SINGLE_ATTR_16(fapi2::ATTR_PROC_PCIE_PCS_TX_DCLCK_ROT,
									   PEC_PCS_TX_DCLCK_ROTATOR_REG,
									   48, 16);

		// Phase1 init step 17 (TX PCIe Receiver Detect Control Register 1)
		SET_REG_WR_WITH_SINGLE_ATTR_16(fapi2::ATTR_PROC_PCIE_PCS_TX_PCIE_RECV_DETECT_CNTL_REG1,
									   PEC_PCS_TX_PCIE_REC_DETECT_CNTL1_REG,
									   48, 16);

		// Phase1 init step 18 (TX PCIe Receiver Detect Control Register 2)
		SET_REG_WR_WITH_SINGLE_ATTR_16(fapi2::ATTR_PROC_PCIE_PCS_TX_PCIE_RECV_DETECT_CNTL_REG2,
									   PEC_PCS_TX_PCIE_REC_DETECT_CNTL2_REG,
									   48, 16);

		// Phase1 init step 19 (TX Power Sequence Enable)
		SET_REG_RMW_WITH_SINGLE_ATTR_8(fapi2::ATTR_PROC_PCIE_PCS_TX_POWER_SEQ_ENABLE,
									   PEC_PCS_TX_POWER_SEQ_ENABLE_REG,
									   56, 7);

		// Phase1 init step 20 (RX VGA Control Register 1)
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_VGA_CNTL_REG1,
					  l_pec_chiplets,
					  l_attr_16);
		// l_buf isn't reset to 0?  Probably a bug in Hostboot.
		l_buf.insertFromRight(l_attr_16, 48, 16);

		/* Becase ATTR_CHIP_EC_FEATURE_HW414759 = 1 */
		l_buf.setBit<PEC_SCOM0X0B_EDMOD, PEC_SCOM0X0B_EDMOD_LEN>();

		fapi2::putScom(l_pec_chiplets, PEC_PCS_RX_VGA_CONTROL1_REG, l_buf);

		// Phase1 init step 21 (RX VGA Control Register 2)
		SET_REG_WR_WITH_SINGLE_ATTR_16(fapi2::ATTR_PROC_PCIE_PCS_RX_VGA_CNTL_REG2,
									   PEC_PCS_RX_VGA_CONTROL2_REG,
									   48, 16);

		// Phase1 init step 22 (RX DFE Func Control Register 1)
		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_RX_DFE_FDDC, l_pec_chiplets, l_pcs_rx_dfe_fddc);
		fapi2::getScom(l_pec_chiplets, PEC_IOP_RX_DFE_FUNC_REGISTER1, l_buf);
		l_buf.insertFromRight(l_pcs_rx_dfe_fddc, 50, 1);
		fapi2::putScom(l_pec_chiplets, PEC_IOP_RX_DFE_FUNC_REGISTER1, l_buf);

		// Phase1 init step 23 (PCS System Control)
		SET_REG_RMW_WITH_SINGLE_ATTR_16(fapi2::ATTR_PROC_PCIE_PCS_SYSTEM_CNTL,
										PEC_PCS_SYS_CONTROL_REG,
										55, 9);

		FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_PCS_M_CNTL, l_pec_chiplets, l_pcs_m_cntl);


		// Phase1 init step 24 (PCS M1 Control)
		SET_REG_RMW(l_pcs_m_cntl[0],
					PEC_PCS_M1_CONTROL_REG,
					55, 9);

		// Phase1 init step 25 (PCS M2 Control)
		SET_REG_RMW(l_pcs_m_cntl[1],
					PEC_PCS_M1_CONTROL_REG,
					55, 9);

		// Phase1 init step 26 (PCS M3 Control)
		SET_REG_RMW(l_pcs_m_cntl[2],
					PEC_PCS_M1_CONTROL_REG,
					55, 9);

		// Phase1 init step 27 (PCS M4 Control)
		SET_REG_RMW(l_pcs_m_cntl[3],
					PEC_PCS_M1_CONTROL_REG,
					55, 9);

		//Delay a minimum of 200ns to allow prior SCOM programming to take effect
		fapi2::delay(PMA_RESET_NANO_SEC_DELAY, PMA_RESET_CYC_DELAY);

		// Phase1 init step 28
		l_buf = 0;
		l_buf.insertFromRight(1, PEC_IOP_PIPE_RESET_START_BIT, 1);
		fapi2::putScom(l_pec_chiplets, PEC_CPLT_CONF1_CLEAR, l_buf);

		// Delay a minimum of 300ns for reset to complete.
		// Inherent delay before deasserting PCS PIPE Reset is enough here.
	}
}
```
