```cpp
void* call_proc_pcie_config (void *io_pArgs)
{
    TARGETING::TargetHandleList l_procChips;
    getAllChips(l_procChips, TYPE_PROC);
    for (const auto & l_procChip: l_procChips)
    {
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapi_cpu_target(l_procChip);
        //  call the HWP with each fapi::Target
        p9_pcie_config(l_fapi_cpu_target);
    }
}

fapi2::ReturnCode p9_pcie_config(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::ATTR_PROC_PCIE_MMIO_BAR0_BASE_ADDR_OFFSET_Type l_mmio_bar0_offsets;
    fapi2::ATTR_PROC_PCIE_MMIO_BAR1_BASE_ADDR_OFFSET_Type l_mmio_bar1_offsets;
    fapi2::ATTR_PROC_PCIE_REGISTER_BAR_BASE_ADDR_OFFSET_Type l_register_bar_offsets;
    fapi2::ATTR_PROC_PCIE_BAR_SIZE_Type l_bar_sizes;
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_fabric_pump_mode;
    fapi2::ATTR_PROC_PCIE_CACHE_INJ_MODE_Type l_cache_inject_mode;
    fapi2::ATTR_PROC_PCIE_CACHE_INJ_THROTTLE_Type l_cache_inject_throttle;
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    fapi2::buffer<uint64_t> l_buf = 0;
    uint8_t pec0_iovalid_bits = 0;
    uint8_t pec1_iovalid_bits = 0;
    uint8_t pec2_iovalid_bits = 0;
    uint8_t l_attr_proc_pcie_iovalid_enable = 0;
    std::vector<uint64_t> l_base_addr_nm0, l_base_addr_nm1, l_base_addr_m;
    uint64_t l_base_addr_mmio;

    auto l_pec_chiplets_vec = i_target.getChildren<fapi2::TARGET_TYPE_PEC>(
                                  fapi2::TARGET_STATE_FUNCTIONAL);
    auto l_phb_chiplets_vec = i_target.getChildren<fapi2::TARGET_TYPE_PHB>(
                                  fapi2::TARGET_STATE_FUNCTIONAL);

    fapi2::ATTR_EC_Type l_chip_ec;
    fapi2::ATTR_NAME_Type l_chip_id;
    l_chip_id = fapi2::ATTR_NAME[TGT1];
    l_chip_ec = fapi2::ATTR_EC[TGT1];



    // read system level BAR MMIO offset/size attributes
    FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_MMIO_BAR0_BASE_ADDR_OFFSET, FAPI_SYSTEM, l_mmio_bar0_offsets);
    FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_MMIO_BAR1_BASE_ADDR_OFFSET, FAPI_SYSTEM, l_mmio_bar1_offsets);
    FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_REGISTER_BAR_BASE_ADDR_OFFSET, FAPI_SYSTEM, l_register_bar_offsets);
    FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_BAR_SIZE, FAPI_SYSTEM, l_bar_sizes);

    // Grab attribute to determine fabric pump mode for cache inject setup
    // fapi2::ATTR_PROC_FABRIC_PUMP_MODE = 1 by default
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_PUMP_MODE, FAPI_SYSTEM, l_fabric_pump_mode);

    // determine base address of chip MMIO range
    p9_fbc_utils_get_chip_base_address(
        i_target, l_base_addr_nm0, l_base_addr_nm1, l_base_addr_m, l_base_addr_mmio);
    // initialize functional PEC chiplets
    for (auto l_pec_chiplet : l_pec_chiplets_vec)
    {
        uint8_t l_pec_id = 0;
        // Get the PEC unit id
        // fapi2::ATTR_CHIP_UNIT_POS = 0 by default
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_pec_chiplet, l_pec_id);
        // Grab the IOVALID attribute to determine if PEC is bifurcated or not.
        FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_IOVALID_ENABLE, l_pec_chiplet, l_attr_proc_pcie_iovalid_enable);
        if (l_pec_id == 0)
        {
            pec0_iovalid_bits = l_attr_proc_pcie_iovalid_enable;
        }
        if (l_pec_id == 1)
        {
            pec1_iovalid_bits = l_attr_proc_pcie_iovalid_enable;
        }
        if (l_pec_id == 2)
        {
            pec2_iovalid_bits = l_attr_proc_pcie_iovalid_enable;
        }
        // configure extended addressing facility
        uint8_t l_addr_extension_group_id;
        uint8_t l_addr_extension_chip_id;

        // P9N2_PEC_ADDREXTMASK_REG = 0x4010c05
        fapi2::getScom(l_pec_chiplet, P9N2_PEC_ADDREXTMASK_REG, l_buf);
        // fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID = 0 probably?
        // P9N2_PEC_ADDREXTMASK_REG_PE = 0
        // P9N2_PEC_ADDREXTMASK_REG_PE_LEN = 7
        l_buf.insertFromRight<
            P9N2_PEC_ADDREXTMASK_REG_PE,
            P9N2_PEC_ADDREXTMASK_REG_PE_LEN>(
            (fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID << 3) | fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID);
        fapi2::putScom(l_pec_chiplet, P9N2_PEC_ADDREXTMASK_REG, l_buf);

        // Phase2 init step 1
        // NestBase+0x00
        // Set bits 00:03 = 0b0001 Set hang poll scale
        // Set bits 04:07 = 0b0001 Set data scale
        // Set bits 08:11 = 0b0001 Set hang pe scale
        // Set bit 22 = 0b1 Disable out­of­order store behavior
        // Set bit 33 = 0b1 Enable Channel Tag streaming behavior
        // Set bits 34:35 = 0b11 Set P9 Style cache-inject behavior
        // Set bits 46:48 = 0b011 Set P9 Style cache-inject rate, 1/16 cycles
        // Set bit 60 = 0b1 only if PEC is bifurcated or trifurcated.
        // if HW423589_option1, set Disable Group Scope (r/w) and Use Vg(sys) at Vg scope

        // PEC_PBCQHWCFG_REG = 0x4010c00
        fapi2::getScom(l_pec_chiplet, PEC_PBCQHWCFG_REG, l_buf);
        // PEC_PBCQHWCFG_REG_HANG_POLL_SCALE = 0
        // PEC_PBCQHWCFG_REG_HANG_POLL_SCALE_LEN = 4
        // PEC_PBCQ_HWCFG_HANG_POLL_SCALE = 1
        l_buf.insertFromRight<
            PEC_PBCQHWCFG_REG_HANG_POLL_SCALE,
            PEC_PBCQHWCFG_REG_HANG_POLL_SCALE_LEN>(
            PEC_PBCQ_HWCFG_HANG_POLL_SCALE);
        // PEC_PBCQHWCFG_REG_HANG_DATA_SCALE = 4
        // PEC_PBCQHWCFG_REG_HANG_DATA_SCALE_LEN = 4
        // PEC_PBCQ_HWCFG_DATA_POLL_SCALE = 1
        l_buf.insertFromRight<
            PEC_PBCQHWCFG_REG_HANG_DATA_SCALE,
            PEC_PBCQHWCFG_REG_HANG_DATA_SCALE_LEN>(
            PEC_PBCQ_HWCFG_DATA_POLL_SCALE);
        // PEC_PBCQHWCFG_REG_HANG_PE_SCALE = 8
        // PEC_PBCQHWCFG_REG_HANG_PE_SCALE_LEN = 4
        // PEC_PBCQ_HWCFG_HANG_PE_SCALE = 1
        l_buf.insertFromRight<
            PEC_PBCQHWCFG_REG_HANG_PE_SCALE,
            PEC_PBCQHWCFG_REG_HANG_PE_SCALE_LEN>(
            PEC_PBCQ_HWCFG_HANG_PE_SCALE);
        // PEC_PBCQHWCFG_REG_PE_DISABLE_OOO_MODE = 0x16
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_OOO_MODE>();
        // PEC_PBCQHWCFG_REG_PE_CHANNEL_STREAMING_EN = 0x21
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_CHANNEL_STREAMING_EN>();

        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_WR_VG>();               // 41
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_WR_SCOPE_GROUP>();      // 42
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_INTWR_VG>();            // 43
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_INTWR_SCOPE_GROUP>();   // 44
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_RD_VG>();               // 54
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_RD_SCOPE_GROUP>();      // 51
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_TCE_SCOPE_GROUP>();     // 56
        l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_TCE_VG>();              // 59

        //Attribute to control the cache inject mode.
        // DISABLE_CI = 0x0 - Disable cache inject completely. (Reset value default)
        // P7_STYLE_CI = 0x1 - Use cache inject design from Power7.
        // PCITLP_STYLE_CI = 0x2 - Use PCI TLP Hint bits in packet to perform the cache inject.
        // P9_STYLE_CI = 0x3 - Initial attempt as cache inject. Power9 style. (Attribute default)
        //
        // Different cache inject modes will affect DMA write performance. The attribute default was
        // selected based on various workloads and was to be the most optimal settings for Power9.
        // fapi2::ATTR_PROC_PCIE_CACHE_INJ_MODE = 3 by default
        FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_CACHE_INJ_MODE, l_pec_chiplet, l_cache_inject_mode);

        //Attribute to control the cache inject throttling when cache inject is enable.
        // DISABLE   = 0x0 - Disable cache inject throttling. (Reset value default)
        // 16_CYCLES = 0x1 - Perform 1 cache inject every 16 clock cycles.
        // 32_CYCLES = 0x3 - Perform 1 cache inject every 32 clock cycles. (Attribute default)
        // 64_CYCLES = 0x7 - Perform 1 cache inject every 32 clock cycles.

        // Different throttle rates will affect DMA write performance. The attribute default settings
        // were optimal settings found across various workloads.
        FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_CACHE_INJ_THROTTLE, l_pec_chiplet, l_cache_inject_throttle);

        // Disable P9 Style cache injects if chip is node.
        // CHIP_IS_NODE  = 0x01
        // CHIP_IS_GROUP = 0x02
        if ((l_fabric_pump_mode != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE)
        && (l_cache_inject_mode != fapi2::ENUM_ATTR_PROC_PCIE_CACHE_INJ_MODE_DISABLE_CI))
        {
            // PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_MODE = 0x22
            // PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_MODE_LEN = 3
            l_buf.insertFromRight<PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_MODE, PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_MODE_LEN>
            (l_cache_inject_mode);

            if((l_chip_id == 0x5 && l_chip_ec == 0x21)
            || (l_chip_id == 0x5 && l_chip_ec == 0x22)
            || (l_chip_id == 0x5 && l_chip_ec == 0x23)
            {
                // PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_RATE = 0x2e
                // PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_RATE_LEN = 3
                l_buf.insertFromRight<PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_RATE, PEC_PBCQHWCFG_REG_PE_WR_CACHE_INJECT_RATE_LEN>
                (l_cache_inject_throttle);
            }
        }

        if (( l_pec_id == 1) || ((l_pec_id == 2) && (l_attr_proc_pcie_iovalid_enable != 0x4)))
        {
            // PEC_PBCQHWCFG_REG_PE_DISABLE_TCE_ARBITRATION = 0x3c
            l_buf.setBit<PEC_PBCQHWCFG_REG_PE_DISABLE_TCE_ARBITRATION>();
        }

        // Phase2 init step 2
        // NestBase+0x01
        // N/A Modify Drop Priority Control Register (DrPriCtl)

        // Phase2 init step 3
        // NestBase+0x03
        // Set bits 00:03 = 0b1001 Enable trace, and select
        // Inbound operations with addr information
        // PEC_NESTTRC_REG = 0x4010c03
        fapi2::getScom(l_pec_chiplet, PEC_NESTTRC_REG, l_buf);
        // PEC_NESTTRC_REG_TRACE_MUX_SEL_A = 0
        // PEC_NESTTRC_REG_TRACE_MUX_SEL_A_LEN = 4
        // PEC_PBCQ_NESTTRC_SEL_A = 9
        l_buf.insertFromRight<
            PEC_NESTTRC_REG_TRACE_MUX_SEL_A,
            PEC_NESTTRC_REG_TRACE_MUX_SEL_A_LEN>(
            PEC_PBCQ_NESTTRC_SEL_A);
        fapi2::putScom(l_pec_chiplet, PEC_NESTTRC_REG, l_buf);

        // Phase2 init step 4
        // NestBase+0x05
        // N/A For use of atomics/asb_notify

        // Phase2 init step 5
        // NestBase+0x06
        // N/A To override scope prediction

        // Phase2 init step 6
        // PCIBase +0x00
        // Set bits 30 = 0b1 Enable Trace
        l_buf.flush<0>();
        // PEC_PBAIBHWCFG_REG_PE_PCIE_CLK_TRACE_EN = 0x1e
        l_buf.setBit<PEC_PBAIBHWCFG_REG_PE_PCIE_CLK_TRACE_EN>();
        // PEC_PBAIBHWCFG_REG_PE_OSMB_HOL_BLK_CNT = 0x28
        // PEC_PBAIBHWCFG_REG_PE_OSMB_HOL_BLK_CNT_LEN = 3
        // PEC_AIB_HWCFG_OSBM_HOL_BLK_CNT = 7
        l_buf.insertFromRight<
            PEC_PBAIBHWCFG_REG_PE_OSMB_HOL_BLK_CNT,
            PEC_PBAIBHWCFG_REG_PE_OSMB_HOL_BLK_CNT_LEN>(
            PEC_AIB_HWCFG_OSBM_HOL_BLK_CNT);
        // PEC_PBAIBHWCFG_REG = 0xd010800
        fapi2::putScom(l_pec_chiplet, PEC_PBAIBHWCFG_REG, l_buf);
    }

    // initialize functional PHB chiplets
    for (auto l_phb_chiplet : l_phb_chiplets_vec)
    {
        fapi2::ATTR_PROC_PCIE_BAR_ENABLE_Type l_bar_enables;
        fapi2::buffer<uint64_t> l_mmio0_bar = l_base_addr_mmio;
        fapi2::buffer<uint64_t> l_mmio1_bar = l_base_addr_mmio;
        fapi2::buffer<uint64_t> l_register_bar = l_base_addr_mmio;
        uint8_t l_phb_id = 0;

        // Get the PHB id
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_phb_chiplet, l_phb_id);
        // Initialize PHBs with IOVALID set (SW417485)
        // PEC0_IOP_IOVALID_PHB0_MASK = 1
        if ((l_phb_id == 0) && !(pec0_iovalid_bits & PEC0_IOP_IOVALID_PHB0_MASK))
        {
            continue;
        }
        // PEC1_IOP_IOVALID_PHB1_MASK = 2
        if ((l_phb_id == 1) && !(pec1_iovalid_bits & PEC1_IOP_IOVALID_PHB1_MASK))
        {
            continue;
        }
        // PEC1_IOP_IOVALID_PHB2_MASK = 1
        if ((l_phb_id == 2) && !(pec1_iovalid_bits & PEC1_IOP_IOVALID_PHB2_MASK))
        {
            continue;
        }
        // PEC2_IOP_IOVALID_PHB3_MASK = 4
        if ((l_phb_id == 3) && !(pec2_iovalid_bits & PEC2_IOP_IOVALID_PHB3_MASK))
        {
            continue;
        }
        // PEC2_IOP_IOVALID_PHB4_MASK = 2
        if ((l_phb_id == 4) && !(pec2_iovalid_bits & PEC2_IOP_IOVALID_PHB4_MASK))
        {
            continue;
        }
        // PEC2_IOP_IOVALID_PHB5_MASK = 1
        if ((l_phb_id == 5) && !(pec2_iovalid_bits & PEC2_IOP_IOVALID_PHB5_MASK))
        {
            continue;
        }
        // Phase2 init step 12_a
        // NestBase+StackBase+0xA
        // 0xFFFFFFFF_FFFFFFFF
        // Clear any spurious cerr_rpt0 bits (cerr_rpt0)
        l_buf.flush<1>();
        // PHB_CERR_RPT0_REG = 0x4010c4a
        fapi2::putScom(l_phb_chiplet, PHB_CERR_RPT0_REG, l_buf);
        // Phase2 init step 12_b
        // NestBase+StackBase+0xB
        // 0xFFFFFFFF_FFFFFFFF
        // Clear any spurious cerr_rpt1 bits (cerr_rpt1)
        l_buf.flush<1>();
        // PHB_CERR_RPT1_REG = 0x4010c4b
        fapi2::putScom(l_phb_chiplet, PHB_CERR_RPT1_REG, l_buf);
        // Phase2 init step 7_c
        // NestBase+StackBase+0x0
        // 0x00000000_00000000
        // Clear any spurious FIR
        // bits (NFIR)NFIR
        l_buf.flush<0>();
        // PHB_NFIR_REG = 0x4010c40
        fapi2::putScom(l_phb_chiplet, PHB_NFIR_REG, l_buf);
        // Phase2 init step 8
        // NestBase+StackBase+0x8
        // 0x00000000_00000000
        // Clear any spurious WOF
        // bits (NFIRWOF)
        // PHB_NFIRWOF_REG = 0x4010c48
        fapi2::putScom(l_phb_chiplet, PHB_NFIRWOF_REG, l_buf);
        // Phase2 init step 9
        // NestBase+StackBase+0x6
        // Set the per FIR Bit Action 0 register
        // PHB_NFIRACTION0_REG = 0x4010c46
        // PCI_NFIR_ACTION0_REG = 0x5b0f81e000000000
        fapi2::putScom(l_phb_chiplet, PHB_NFIRACTION0_REG, PCI_NFIR_ACTION0_REG);
        // Phase2 init step 10
        // NestBase+StackBase+0x7
        // Set the per FIR Bit Action 1 register
        // PHB_NFIRACTION1_REG = 0x4010c47
        // PCI_NFIR_ACTION1_REG = 0x7f0f81e000000000
        fapi2::putScom(l_phb_chiplet, PHB_NFIRACTION1_REG, PCI_NFIR_ACTION1_REG);
        // Phase2 init step 11
        // NestBase+StackBase+0x3
        // Set FIR Mask Bits to allow errors (NFIRMask)
        // PHB_NFIRMASK_REG = 0x4010c43
        // PCI_NFIR_MASK_REG = 0x30001c00000000
        fapi2::putScom(l_phb_chiplet, PHB_NFIRMASK_REG, PCI_NFIR_MASK_REG);
        // Phase2 init step 12
        // NestBase+StackBase+0x15
        // 0x00000000_00000000
        // Set Data Freeze Type Register for SUE handling (DFREEZE)
        // PHB_PE_DFREEZE_REG = 0x4010c55
        fapi2::putScom(l_phb_chiplet, PHB_PE_DFREEZE_REG, l_buf);
        // Phase2 init step 13_a
        // PCIBase+StackBase+0xB
        // 0x00000000_00000000
        // Clear any spurious pbaib_cerr_rpt bits
        // PHB_PBAIB_CERR_RPT_REG = 0xd01084b
        fapi2::putScom(l_phb_chiplet, PHB_PBAIB_CERR_RPT_REG, l_buf);
        // Phase2 init step 13_b
        // PCIBase+StackBase+0x0
        // 0x00000000_00000000
        // Clear any spurious FIR
        // bits (PFIR)PFIR
        // PHB_PFIR_REG = 0xd010840
        fapi2::putScom(l_phb_chiplet, PHB_PFIR_REG, l_buf);
        // Phase2 init step 14
        // PCIBase+StackBase+0x8
        // 0x00000000_00000000
        // Clear any spurious WOF
        // bits (PFIRWOF)
        // PHB_PFIRWOF_REG = 0xd010848
        fapi2::putScom(l_phb_chiplet, PHB_PFIRWOF_REG, l_buf);
        // Phase2 init step 15
        // PCIBase+StackBase+0x6
        // Set the per FIR Bit Action 0 register
        // PHB_PFIRACTION0_REG = 0xd010846
        // PCI_PFIR_ACTION0_REG = 0xb000000000000000
        fapi2::putScom(l_phb_chiplet, PHB_PFIRACTION0_REG, PCI_PFIR_ACTION0_REG);
        // Phase2 init step 16
        // PCIBase+StackBase+0x7
        // Set the per FIR Bit Action 1 register
        // PHB_PFIRACTION1_REG = 0xd010847
        // PCI_PFIR_ACTION1_REG = 0xb000000000000000
        fapi2::putScom(l_phb_chiplet, PHB_PFIRACTION1_REG, PCI_PFIR_ACTION1_REG);
        // Phase2 init step 17
        // PCIBase+StackBase+0x3
        // Set FIR Mask Bits to allow errors (PFIRMask)
        // PHB_PFIRMASK_REG = 0xd010843
        // PCI_PFIR_MASK_REG = 0xe00000000000000
        fapi2::putScom(l_phb_chiplet, PHB_PFIRMASK_REG, PCI_PFIR_MASK_REG);
        // Get the BAR enable attribute
        FAPI_ATTR_GET(fapi2::ATTR_PROC_PCIE_BAR_ENABLE, l_phb_chiplet, l_bar_enables);

        // step 18: NestBase+StackBase+0xE<software programmed>Set MMIO Base
        // Address Register 0 (MMIOBAR0)
        l_mmio0_bar += l_mmio_bar0_offsets[l_phb_id];
        // P9_PCIE_CONFIG_BAR_SHIFT = 8
        l_mmio0_bar = l_mmio0_bar << P9_PCIE_CONFIG_BAR_SHIFT;
        // PHB_MMIOBAR0_REG = 0x4010c4e
        fapi2::putScom(l_phb_chiplet, PHB_MMIOBAR0_REG, l_mmio0_bar);

        // step 19: NestBase+StackBase+0xF<software programmed>Set MMIO BASE
        // Address Register Mask 0 (MMIOBAR0_MASK)
        // PHB_MMIOBAR0_MASK_REG = 0x4010c4f
        fapi2::putScom(l_phb_chiplet, PHB_MMIOBAR0_MASK_REG, l_bar_sizes[0]);

        // step 20: NestBase+StackBase+0x10<software programmed>Set MMIO Base
        // Address Register 1 (MMIOBAR1)
        l_mmio1_bar += l_mmio_bar1_offsets[l_phb_id];
        // P9_PCIE_CONFIG_BAR_SHIFT = 8
        l_mmio1_bar = l_mmio1_bar << P9_PCIE_CONFIG_BAR_SHIFT;
        // PHB_MMIOBAR1_REG = 0x4010c50
        fapi2::putScom(l_phb_chiplet, PHB_MMIOBAR1_REG, l_mmio1_bar);

        // step 21: NestBase+StackBase+0x11<software programmed>Set MMIO Base
        // Address Register Mask 1 (MMIOBAR1_MASK)
        fapi2::putScom(l_phb_chiplet, PHB_MMIOBAR1_MASK_REG, l_bar_sizes[1]);

        // step 22: NestBase+StackBase+0x12<software programmed>Set PHB
        // Regsiter Base address Register (PHBBAR)
        l_register_bar += l_register_bar_offsets[l_phb_id];
        // P9_PCIE_CONFIG_BAR_SHIFT = 8
        l_register_bar = l_register_bar << P9_PCIE_CONFIG_BAR_SHIFT;
        // PHB_PHBBAR_REG = 0x4010c52
        fapi2::putScom(l_phb_chiplet, PHB_PHBBAR_REG, l_register_bar);

        // step 23: NestBase+StackBase+0x14<software programmed>Set Base
        // address Enable Register (BARE)
        l_buf.flush<0>();

        if (l_bar_enables[0])
        {
            // PHB_BARE_REG_PE_MMIO_BAR0_EN = 0
            l_buf.setBit<PHB_BARE_REG_PE_MMIO_BAR0_EN>(); // bit 0 for BAR0
        }

        if (l_bar_enables[1])
        {
            // PHB_BARE_REG_PE_MMIO_BAR1_EN = 1
            l_buf.setBit<PHB_BARE_REG_PE_MMIO_BAR1_EN>(); // bit 1 for BAR1
        }

        if (l_bar_enables[2])
        {
            // PHB_BARE_REG_PE_PHB_BAR_EN = 2
            l_buf.setBit<PHB_BARE_REG_PE_PHB_BAR_EN>();  // bit 2 for PHB
        }
        // PHB_BARE_REG = 0x4010c54
        fapi2::putScom(l_phb_chiplet, PHB_BARE_REG, l_buf);

        // Phase2 init step 24
        // PCIBase+StackBase +0x0A
        // 0x00000000_00000000
        // Remove ETU/AIB bus from reset (PHBReset)
        l_buf.flush<0>();
        // PHB_PHBRESET_REG = 0xd01084a
        fapi2::putScom(l_phb_chiplet, PHB_PHBRESET_REG, l_buf);
        // Configure ETU FIR (all masked)
        // PHB_ACT0_REG = 0xd01090e
        fapi2::putScom(l_phb_chiplet, PHB_ACT0_REG, l_buf);
        // PHB_ACTION1_REG = 0xd01090f
        fapi2::putScom(l_phb_chiplet, PHB_ACTION1_REG, l_buf);
        l_buf.flush<1>();
        // PHB_MASK_REG = 0xd01090b
        fapi2::putScom(l_phb_chiplet, PHB_MASK_REG, l_buf);
    }
}

fapi2::ReturnCode p9_fbc_utils_get_chip_base_address(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    std::vector<uint64_t>& o_base_address_nm0,
    std::vector<uint64_t>& o_base_address_nm1,
    std::vector<uint64_t>& o_base_address_m,
    uint64_t& o_base_address_mmio)
{
    uint64_t l_base_address_nm0 = 0;
    uint64_t l_base_address_nm1 = 0;
    uint64_t l_base_address_m = 0;
    uint8_t l_addr_extension_group_id;
    uint8_t l_addr_extension_chip_id;
    fapi2::buffer<uint64_t> l_addr_extension_enable = 0;
    uint8_t l_regions_per_msel = 1;
    std::vector<uint8_t> l_alias_bit_positions;
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    // --------
    // p9_fbc_utils_get_chip_base_address_no_aliases(i_target, l_base_address_nm0, l_base_address_nm1, l_base_address_m, o_base_address_mmio);
    uint8_t l_fabric_group_id = 0;
    uint8_t l_fabric_chip_id = 0;
    fapi2::buffer<uint64_t> l_base_address;

    // leave chip ID=0 for the purposes of establishing drawer base address
    // apply system ID
    // occupies one field for large system map
    // FABRIC_ADDR_LS_SYSTEM_ID_START_BIT = 8
    // FABRIC_ADDR_LS_SYSTEM_ID_END_BIT = 12
    // fapi2::ATTR_PROC_FABRIC_SYSTEM_ID = 0
    l_base_address.insertFromRight<
        FABRIC_ADDR_LS_SYSTEM_ID_START_BIT,
        (FABRIC_ADDR_LS_SYSTEM_ID_END_BIT-FABRIC_ADDR_LS_SYSTEM_ID_START_BIT+1)>(
        fapi2::ATTR_PROC_FABRIC_SYSTEM_ID);
    // apply group ID
    // FABRIC_ADDR_LS_GROUP_ID_START_BIT = 15
    // FABRIC_ADDR_LS_GROUP_ID_END_BIT = 18
    // fapi2::ATTR_PROC_EFF_FABRIC_GROUP_ID = 0 by default
    l_base_address.insertFromRight<
        FABRIC_ADDR_LS_GROUP_ID_START_BIT,
        (FABRIC_ADDR_LS_GROUP_ID_END_BIT-FABRIC_ADDR_LS_GROUP_ID_START_BIT+1)>(
        fapi2::ATTR_PROC_EFF_FABRIC_GROUP_ID);
    // apply chip ID (relevant for large system map only)
    // FABRIC_ADDR_LS_CHIP_ID_START_BIT = 19
    // FABRIC_ADDR_LS_CHIP_ID_END_BIT = 21
    // fapi2::ATTR_PROC_EFF_FABRIC_CHIP_ID = 0 by default
    l_base_address.insertFromRight<
        FABRIC_ADDR_LS_CHIP_ID_START_BIT,
        (FABRIC_ADDR_LS_CHIP_ID_END_BIT-FABRIC_ADDR_LS_CHIP_ID_START_BIT+1)>(
        fapi2::ATTR_PROC_EFF_FABRIC_CHIP_ID);
    // set output addresses based on application of msel
    // nm = 0b00/01, m = 0b10, mmio = 0b11
    o_base_address_nm0 = l_base_address;               // 00
    // FABRIC_ADDR_MSEL_END_BIT = 14
    l_base_address.setBit<FABRIC_ADDR_MSEL_END_BIT>();
    o_base_address_nm1 = l_base_address;               // 01
    // FABRIC_ADDR_MSEL_START_BIT = 13
    l_base_address.setBit<FABRIC_ADDR_MSEL_START_BIT>();
    // FABRIC_ADDR_MSEL_END_BIT = 14
    l_base_address.clearBit<FABRIC_ADDR_MSEL_END_BIT>();
    o_base_address_m = l_base_address;                 // 10
    // FABRIC_ADDR_MSEL_END_BIT = 14
    l_base_address.setBit(FABRIC_ADDR_MSEL_END_BIT);
    o_base_address_mmio = l_base_address;              // 11
    // --------

    // align to RA
    // FABRIC_ADDR_LS_GROUP_ID_START_BIT = 15
    // FABRIC_ADDR_LS_GROUP_ID_END_BIT = 18
    l_addr_extension_enable.insertFromRight<
        FABRIC_ADDR_LS_GROUP_ID_START_BIT,
        FABRIC_ADDR_LS_GROUP_ID_END_BIT-FABRIC_ADDR_LS_GROUP_ID_START_BIT+1>(
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_GROUP_ID);
    // FABRIC_ADDR_LS_CHIP_ID_START_BIT = 19
    // FABRIC_ADDR_LS_CHIP_ID_END_BIT = 21
    // fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID = 0 probably?
    l_addr_extension_enable.insertFromRight<
        FABRIC_ADDR_LS_CHIP_ID_START_BIT,
        FABRIC_ADDR_LS_CHIP_ID_END_BIT-FABRIC_ADDR_LS_CHIP_ID_START_BIT+1>(
        fapi2::ATTR_FABRIC_ADDR_EXTENSION_CHIP_ID & 0x01);

    // walk across bits set in enable bit field, count number of bits set
    // to determine permutations
    if (l_addr_extension_enable != 0)
    {
        // FABRIC_ADDR_LS_GROUP_ID_START_BIT = 15
        // FABRIC_ADDR_LS_CHIP_ID_END_BIT = 21
        for (uint8_t ii = FABRIC_ADDR_LS_GROUP_ID_START_BIT; ii <= FABRIC_ADDR_LS_CHIP_ID_END_BIT; ii++)
        {
            if (l_addr_extension_enable.getBit(ii))
            {
                l_regions_per_msel *= 2;
                l_alias_bit_positions.push_back(ii);
            }
        }
    }
    for (uint8_t l_region = 0; l_region < l_regions_per_msel; l_region++)
    {
        fapi2::buffer<uint64_t> l_alias_mask = 0;
        if (l_region)
        {
            uint8_t l_value = l_region;
            for (int jj = l_alias_bit_positions.size()-1; jj >= 0; jj--)
            {
                l_alias_mask.writeBit(l_value & 1, l_alias_bit_positions[jj]);
                l_value = l_value >> 1;
            }
        }
        // hide region reserved for GPU LPC
        o_base_address_nm0.push_back(l_base_address_nm0 | l_alias_mask());
        // first half
        o_base_address_m.push_back(l_base_address_m | l_alias_mask());
        // second half
        // MAX_INTERLEAVE_GROUP_SIZE = 0x40000000000
        o_base_address_m.push_back((l_base_address_m | l_alias_mask()) + MAX_INTERLEAVE_GROUP_SIZE / 2);

        // second non-mirrored msel region unusable with HW423589_OPTION2
        // (no MCD resources available to map)
        o_base_address_nm1.push_back(l_base_address_nm1 | l_alias_mask());
    }
}
```
