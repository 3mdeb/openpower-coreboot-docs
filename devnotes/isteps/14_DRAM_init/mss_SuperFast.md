```cpp
const fapi2::Target<fapi2::TARGET_TYPE_MBA> iv_target;          // MBA
fapi2::Target<fapi2::TARGET_TYPE_MEMBUF_CHIP> iv_targetCentaur; // Centaur associated with this MBA
fapi2::buffer<uint64_t> iv_start_addr;                          // Start address
fapi2::buffer<uint64_t> iv_end_addr;                            // End address
uint32_t iv_stop_condition;                                     // Mask of stop contitions
bool iv_poll;                                                   // Set true to wait for cmd complete
const CmdType iv_cmd_type;                                      // Command type
uint8_t iv_mbaPosition;                                         // 0 = mba01, 1 = mba23

fapi2::ReturnCode mss_MaintCmd::loadPattern(PatternIndex i_initPattern)
{
    static constexpr uint32_t maintBufferDataRegs[MAX_MBA_PER_CEN][NUM_BEATS][NUM_WORDS] =
    {
            // port0
        {   {CEN_MAINT0_MAINT_BUFF0_DATA0_WO, CEN_MAINT0_MAINT_BUFF0_DATA_ECC0_WO},// DW0
            {CEN_MAINT0_MAINT_BUFF2_DATA0_WO, CEN_MAINT0_MAINT_BUFF2_DATA_ECC0_WO}, // DW2
            {CEN_MAINT0_MAINT_BUFF0_DATA1_WO, CEN_MAINT0_MAINT_BUFF0_DATA_ECC1_WO}, // DW4
            {CEN_MAINT0_MAINT_BUFF2_DATA1_WO, CEN_MAINT0_MAINT_BUFF2_DATA_ECC1_WO}, // DW6
            {CEN_MAINT0_MAINT_BUFF0_DATA2_WO, CEN_MAINT0_MAINT_BUFF0_DATA_ECC2_WO}, // DW8
            {CEN_MAINT0_MAINT_BUFF2_DATA2_WO, CEN_MAINT0_MAINT_BUFF2_DATA_ECC2_WO}, // DW10
            {CEN_MAINT0_MAINT_BUFF0_DATA3_WO, CEN_MAINT0_MAINT_BUFF0_DATA_ECC3_WO}, // DW12
            {CEN_MAINT0_MAINT_BUFF2_DATA3_WO, CEN_MAINT0_MAINT_BUFF2_DATA_ECC3_WO}, // DW14

            // port1
            {CEN_MAINT0_MAINT_BUFF1_DATA0_WO, CEN_MAINT0_MAINT_BUFF1_DATA_ECC0_WO}, // DW1
            {CEN_MAINT0_MAINT_BUFF3_DATA0_WO, CEN_MAINT0_MAINT_BUFF3_DATA_ECC0_WO}, // DW3
            {CEN_MAINT0_MAINT_BUFF1_DATA1_WO, CEN_MAINT0_MAINT_BUFF1_DATA_ECC1_WO}, // DW5
            {CEN_MAINT0_MAINT_BUFF3_DATA1_WO, CEN_MAINT0_MAINT_BUFF3_DATA_ECC1_WO}, // DW7
            {CEN_MAINT0_MAINT_BUFF1_DATA2_WO, CEN_MAINT0_MAINT_BUFF1_DATA_ECC2_WO}, // DW9
            {CEN_MAINT0_MAINT_BUFF3_DATA2_WO, CEN_MAINT0_MAINT_BUFF3_DATA_ECC2_WO}, // DW11
            {CEN_MAINT0_MAINT_BUFF1_DATA3_WO, CEN_MAINT0_MAINT_BUFF1_DATA_ECC3_WO}, // DW13
            {CEN_MAINT0_MAINT_BUFF3_DATA3_WO, CEN_MAINT0_MAINT_BUFF3_DATA_ECC3_WO}  // DW15
        },

            // port2
        {   {CEN_MAINT1_MAINT_BUFF0_DATA0_WO, CEN_MAINT1_MAINT_BUFF0_DATA_ECC0_WO},// DW0
            {CEN_MAINT1_MAINT_BUFF2_DATA0_WO, CEN_MAINT1_MAINT_BUFF2_DATA_ECC0_WO}, // DW2
            {CEN_MAINT1_MAINT_BUFF0_DATA1_WO, CEN_MAINT1_MAINT_BUFF0_DATA_ECC1_WO}, // DW4
            {CEN_MAINT1_MAINT_BUFF2_DATA1_WO, CEN_MAINT1_MAINT_BUFF2_DATA_ECC1_WO}, // DW6
            {CEN_MAINT1_MAINT_BUFF0_DATA2_WO, CEN_MAINT1_MAINT_BUFF0_DATA_ECC2_WO}, // DW8
            {CEN_MAINT1_MAINT_BUFF2_DATA2_WO, CEN_MAINT1_MAINT_BUFF2_DATA_ECC2_WO}, // DW10
            {CEN_MAINT1_MAINT_BUFF0_DATA3_WO, CEN_MAINT1_MAINT_BUFF0_DATA_ECC3_WO}, // DW12
            {CEN_MAINT1_MAINT_BUFF2_DATA3_WO, CEN_MAINT1_MAINT_BUFF2_DATA_ECC3_WO}, // DW14

            // port3
            {CEN_MAINT1_MAINT_BUFF1_DATA0_WO, CEN_MAINT1_MAINT_BUFF1_DATA_ECC0_WO}, // DW1
            {CEN_MAINT1_MAINT_BUFF3_DATA0_WO, CEN_MAINT1_MAINT_BUFF3_DATA_ECC0_WO}, // DW3
            {CEN_MAINT1_MAINT_BUFF1_DATA1_WO, CEN_MAINT1_MAINT_BUFF1_DATA_ECC1_WO}, // DW5
            {CEN_MAINT1_MAINT_BUFF3_DATA1_WO, CEN_MAINT1_MAINT_BUFF3_DATA_ECC1_WO}, // DW6
            {CEN_MAINT1_MAINT_BUFF1_DATA2_WO, CEN_MAINT1_MAINT_BUFF1_DATA_ECC2_WO}, // DW9
            {CEN_MAINT1_MAINT_BUFF3_DATA2_WO, CEN_MAINT1_MAINT_BUFF3_DATA_ECC2_WO}, // DW11
            {CEN_MAINT1_MAINT_BUFF1_DATA3_WO, CEN_MAINT1_MAINT_BUFF1_DATA_ECC3_WO}, // DW13
            {CEN_MAINT1_MAINT_BUFF3_DATA3_WO, CEN_MAINT1_MAINT_BUFF3_DATA_ECC3_WO}  // DW15
        }
    };


    static constexpr uint32_t maintBuffer65thRegs[4][2] =
    {
        // MBA01                                         MBA23
        {CEN_MAINT0_MAINT_BUFF_65TH_BYTE_64B_ECC0_WO,    CEN_MAINT1_MAINT_BUFF_65TH_BYTE_64B_ECC0_WO}, // 1st 64B of cacheline
        {CEN_MAINT0_MAINT_BUFF_65TH_BYTE_64B_ECC1_WO,    CEN_MAINT1_MAINT_BUFF_65TH_BYTE_64B_ECC1_WO}, // 1st 64B of cacheline
        {CEN_MAINT0_MAINT_BUFF_65TH_BYTE_64B_ECC2_WO,    CEN_MAINT1_MAINT_BUFF_65TH_BYTE_64B_ECC2_WO}, // 2nd 64B of cacheline
        {CEN_MAINT0_MAINT_BUFF_65TH_BYTE_64B_ECC3_WO,    CEN_MAINT1_MAINT_BUFF_65TH_BYTE_64B_ECC3_WO}
    };// 2nd 64B of cacheline

    fapi2::buffer<uint64_t> l_data;
    fapi2::buffer<uint64_t> l_ecc;
    fapi2::buffer<uint64_t> l_65th;
    fapi2::buffer<uint64_t> l_mbmmr;
    fapi2::buffer<uint64_t> l_mbsecc;
    uint32_t loop = 0;
    uint8_t l_dramWidth = 0;
    uint8_t l_attr_centaur_ec_enable_rce_with_other_errors_hw246685 = 0;
    //----------------------------------------------------
    // Get l_dramWidth
    //----------------------------------------------------
    FAPI_ATTR_GET(fapi2::ATTR_CEN_EFF_DRAM_WIDTH, iv_target,  l_dramWidth);
    // Convert from attribute enum values: 8,4 to index values: 0,1
    if(l_dramWidth == mss_memconfig::X8)
    {
        l_dramWidth = 0;
    }
    else
    {
        l_dramWidth = 1;
    }
    //----------------------------------------------------
    // Load the data: 16 loops x 64bits = 128B cacheline
    //----------------------------------------------------
    // Set bit 9 so that hw will generate the fabric ECC.
    // This is an 8B ECC protecting the data moving on internal buses in
    // the Centaur.
    l_ecc.flush<0>();
    l_ecc.setBit<9>();
    for(loop = 0; loop < NUM_BEATS; ++loop)
    {
        // A write to MAINT_BUFFx_DATAy will not update until the corresponding
        // MAINT_BUFFx_DATA_ECCy is written to.
        l_data.insert<0, 32, 0>(mss_maintBufferData[l_dramWidth][i_initPattern][loop][0]);
        l_data.insert<32, 32, 0>(mss_maintBufferData[l_dramWidth][i_initPattern][loop][1]);
        fapi2::putScom(iv_targetCentaur, maintBufferDataRegs[iv_mbaPosition][loop][0], l_data);
        fapi2::putScom(iv_targetCentaur, maintBufferDataRegs[iv_mbaPosition][loop][1], l_ecc);
    }
    //----------------------------------------------------
    // Load the 65th byte: 4 loops to fill in the two 65th bytes in cacheline
    //----------------------------------------------------
    l_65th.flush<0>();
    // Set bit 56 so that hw will generate the fabric ECC.
    // This is an 8B ECC protecting the data moving on internal buses in Centaur.
    l_65th.setBit<56>();
    for(loop = 0; loop < NUM_LOOPS_FOR_65TH_BYTE; ++loop )
    {
        l_65th.insert<1, 3, 1>(mss_65thByte[l_dramWidth][i_initPattern][loop]);
        fapi2::putScom(iv_targetCentaur, maintBuffer65thRegs[loop][iv_mbaPosition], l_65th);
    }
    //----------------------------------------------------
    // Save i_initPattern in unused maint mark reg
    // so we know what pattern was used when we do
    // UE isolation
    //----------------------------------------------------
    // No plans to use maint mark, but make sure it's disabled to be safe
    fapi2::getScom(iv_targetCentaur, mss_mbsecc[iv_mbaPosition], l_mbsecc);
    l_mbsecc.clearBit<4>();
    FAPI_ATTR_GET(fapi2::ATTR_CEN_CENTAUR_EC_FEATURE_ENABLE_RCE_WITH_OTHER_ERRORS_HW246685, iv_targetCentaur,
                           l_attr_centaur_ec_enable_rce_with_other_errors_hw246685);
    if(l_attr_centaur_ec_enable_rce_with_other_errors_hw246685)
    {
        l_mbsecc.setBit<16>();
    }
    fapi2::putScom(iv_targetCentaur, mss_mbsecc[iv_mbaPosition], l_mbsecc);
    l_mbmmr.flush<0>();
    // Store i_initPattern, with range 0-8, in MBMMR bits 4-7
    l_mbmmr.insert <4, 4, 8-4> (static_cast<uint8_t>(i_initPattern));
    fapi2::putScom(iv_targetCentaur, mss_mbmmr[iv_mbaPosition] , l_mbmmr);
}

fapi2::ReturnCode mss_MaintCmd::loadCmdType()
{
    fapi2::buffer<uint64_t> l_data;
    uint8_t l_dram_gen = 0;
    // Get DDR3/DDR4: ATTR_EFF_DRAM_GEN
    // 0x01 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3
    // 0x02 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4
    FAPI_ATTR_GET(fapi2::ATTR_CEN_EFF_DRAM_GEN, iv_target,  l_dram_gen);
    fapi2::getScom(iv_target, CEN_MBA_MBMCTQ, l_data);
    l_data.insert <0, 5, 32-5>(static_cast<uint32_t>(iv_cmd_type));
    // Setting super fast address increment mode for DDR3, where COL bits are LSB. Valid for all cmds.
    // NOTE: Super fast address increment mode is broken for DDR4 due to DD1 bug
    if (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3)
    {
        l_data.setBit<5>();
    }
    fapi2::putScom(iv_target, CEN_MBA_MBMCTQ, l_data);
}

fapi2::ReturnCode mss_MaintCmd::loadCmdType()
{
    fapi2::buffer<uint64_t> l_data;
    uint8_t l_dram_gen = 0;
    // Get DDR3/DDR4: ATTR_EFF_DRAM_GEN
    // 0x01 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3
    // 0x02 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4
    FAPI_ATTR_GET(fapi2::ATTR_CEN_EFF_DRAM_GEN, iv_target,  l_dram_gen);
    fapi2::getScom(iv_target, CEN_MBA_MBMCTQ, l_data);
    l_data.insert<0, 5, 32-5>(static_cast<uint32_t>(iv_cmd_type));

    // Setting super fast address increment mode for DDR3, where COL bits are LSB. Valid for all cmds.
    // NOTE: Super fast address increment mode is broken for DDR4 due to DD1 bug
    if(l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3)
    {
        l_data.setBit<5>();
    }

    fapi2::putScom(iv_target, CEN_MBA_MBMCTQ, l_data);
}

fapi2::ReturnCode mss_MaintCmd::loadStartAddress()
{
    fapi2::buffer<uint64_t> l_data;
    fapi2::getScom(iv_target, CEN_MBA_MBMACAQ, l_data);
    // Load address bits 0:39
    l_data.insert<0, 40, 0>(iv_start_addr);
    l_data.writeBit<CEN_MBA_MBMACAQ_CMD_ROW17>(iv_start_addr.getBit<CEN_MBA_MBMACAQ_CMD_ROW17>());
    // Clear error status bits 40:46
    l_data.clearBit<40, 7>();
    fapi2::putScom(iv_target, CEN_MBA_MBMACAQ, l_data);
}

fapi2::ReturnCode mss_MaintCmd::loadEndAddress()
{
    fapi2::buffer<uint64_t> l_data;
    fapi2::getScom(iv_target, CEN_MBA_MBMEAQ, l_data);
    l_data.insert<0, 41, 0>(iv_end_addr);
    fapi2::putScom(iv_target, CEN_MBA_MBMEAQ, l_data);
}

fapi2::ReturnCode mss_get_address_range( const fapi2::Target<fapi2::TARGET_TYPE_MBA>& i_target,
        uint8_t i_rank,
        fapi2::buffer<uint64_t>& o_start_addr,
        fapi2::buffer<uint64_t>& o_end_addr )
{
    constexpr uint8_t NUM_CONFIG_TYPES = 9;
    constexpr uint8_t NUM_CONFIG_SUBTYPES = 4;
    constexpr uint8_t NUM_SLOT_CONFIGS = 2;
    static const uint8_t memConfigType[NUM_CONFIG_TYPES][NUM_CONFIG_SUBTYPES][NUM_SLOT_CONFIGS] =
    {

        // Refer to Centaur Workbook: 5.2 Master and Slave Rank Usage
        //
        //       SUBTYPE_A                    SUBTYPE_B                    SUBTYPE_C                    SUBTYPE_D
        //
        //SLOT_0_ONLY   SLOT_0_AND_1   SLOT_0_ONLY   SLOT_0_AND_1   SLOT_0_ONLY   SLOT_0_AND_1   SLOT_0_ONLY   SLOT_0_AND_1
        //
        //master slave  master slave   master slave  master slave   master slave  master slave   master slave  master slave
        //
        {{0xff,         0xff},         {0xff,        0xff},         {0xff,         0xff},       {0xff,         0xff}},  // TYPE_0
        {{0x00,         0x40},         {0x10,        0x50},         {0x30,         0x70},       {0xff,         0xff}},  // TYPE_1
        {{0x01,         0x41},         {0x03,        0x43},         {0x07,         0x47},       {0xff,         0xff}},  // TYPE_2
        {{0x11,         0x51},         {0x13,        0x53},         {0x17,         0x57},       {0xff,         0xff}},  // TYPE_3
        {{0x31,         0x71},         {0x33,        0x73},         {0x37,         0x77},       {0xff,         0xff}},  // TYPE_4
        {{0x00,         0x40},         {0x10,        0x50},         {0x30,         0x70},       {0xff,         0xff}},  // TYPE_5
        {{0x01,         0x41},         {0x03,        0x43},         {0x07,         0x47},       {0xff,         0xff}},  // TYPE_6
        {{0x11,         0x51},         {0x13,        0x53},         {0x17,         0x57},       {0xff,         0xff}},  // TYPE_7
        {{0x31,         0x71},         {0x33,        0x73},         {0x37,         0x77},       {0xff,         0xff}}   // TYPE_8
    };

    fapi2::buffer<uint64_t> l_data;
    mss_memconfig::MemOrg l_row;
    mss_memconfig::MemOrg l_col;
    mss_memconfig::MemOrg l_bank;
    uint32_t l_dramSize = 0;
    uint8_t l_dramWidth = 0;
    uint8_t l_mbaPosition = 0;
    uint8_t l_slotConfig = 0;
    uint8_t l_configType = 0;
    uint8_t l_configSubType = 0;
    uint8_t l_end_master_rank = 0;
    uint8_t l_end_slave_rank = 0;
    uint8_t l_dram_gen = 0;
    bool l_row18 = false;

    // Get Centaur target for the given MBA
    const auto l_targetCentaur = i_target.getParent<fapi2::TARGET_TYPE_MEMBUF_CHIP>();
    // Get MBA position: 0 = mba01, 1 = mba23
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, i_target,  l_mbaPosition);
    // Get l_dramWidth
    FAPI_ATTR_GET(fapi2::ATTR_CEN_EFF_DRAM_WIDTH, i_target,  l_dramWidth);
    // Get DDR3/DDR4: ATTR_EFF_DRAM_GEN
    // 0x01 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3
    // 0x02 = fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4
    FAPI_ATTR_GET(fapi2::ATTR_CEN_EFF_DRAM_GEN, i_target,  l_dram_gen);
    // Check MBAXCRn, to show memory configured behind this MBA
    fapi2::getScom(l_targetCentaur, mss_mbaxcr[l_mbaPosition], l_data);

    //********************************************************************
    // Find max row/col/bank, based on l_dramSize and l_dramWidth
    //********************************************************************

    // Get l_dramSize
    l_data.extract<6, 2, 32-2>(l_dramSize);

    if((l_dramWidth == mss_memconfig::X8) &&
       (l_dramSize == mss_memconfig::GBIT_2) &&
       (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 256Mbx8 (2Gb), row/col/bank = 15/10/3
        l_row =   mss_memconfig::ROW_15;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_2) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 256Mbx8 (2Gb), row/col/bank = 14/10/4
        l_row =   mss_memconfig::ROW_14;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_2) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 512Mbx4 (2Gb), row/col/bank = 15/11/3
        l_row =   mss_memconfig::ROW_15;
        l_col =   mss_memconfig::COL_11;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_2) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 512Mbx4 (2Gb), row/col/bank = 15/10/4
        l_row =   mss_memconfig::ROW_15;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_4) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 512Mbx8 (4Gb), row/col/bank = 16/10/3
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_4) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 512Mbx8 (4Gb), row/col/bank = 14/10/4
        l_row =   mss_memconfig::ROW_15;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_4) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 1Gbx4 (4Gb), row/col/bank = 16/11/3
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_11;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_4) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 1Gbx4 (4Gb), row/col/bank = 16/10/4
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_8) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 1Gbx8 (8Gb), row/col/bank = 16/11/3
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_11;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_8) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 1Gbx8 (8Gb), row/col/bank = 16/10/4
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_8) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR3))
    {
        // For memory part Size = 2Gbx4 (8Gb), row/col/bank = 16/12/3
        l_row =   mss_memconfig::ROW_16;
        l_col =   mss_memconfig::COL_12;
        l_bank =  mss_memconfig::BANK_3;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_8) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 2Gbx4 (8Gb), row/col/bank = 17/10/4
        l_row =   mss_memconfig::ROW_17;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    } else if((l_dramWidth == mss_memconfig::X4) &&
              (l_dramSize == mss_memconfig::GBIT_16) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 4Gbx4 (16Gb), row/col/bank = 18/10/4
        // Only up to row 17 is contiguous, we set row 18 below
        l_row =   ss_memconfig::ROW_17;
        l_col =   ss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = true;
    } else if((l_dramWidth == mss_memconfig::X8) &&
              (l_dramSize == mss_memconfig::GBIT_16) &&
              (l_dram_gen == fapi2::ENUM_ATTR_CEN_EFF_DRAM_GEN_DDR4))
    {
        // For memory part Size = 2Gbx8 (16Gb), row/col/bank = 17/10/4
        l_row =   mss_memconfig::ROW_17;
        l_col =   mss_memconfig::COL_10;
        l_bank =  mss_memconfig::BANK_4;
        l_row18 = false;
    }

    //********************************************************************
    // Find l_end_master_rank and l_end_slave_rank based on DIMM configuration
    //********************************************************************

    // (0:3) Configuration type (1-8)
    l_data.extract<0, 4, 8-4>(l_configType);
    // (4:5) Configuration subtype (A, B, C, D)
    l_data.extract<4, 2, 8-2>(l_configSubType);
    // (8)   Slot Configuration
    // 0 = Centaur DIMM or IS DIMM, slot0 only, 1 = IS DIMM slots 0 and 1
    l_data.extract<8, 1, 8-1>(l_slotConfig);
    l_end_master_rank = (memConfigType[l_configType][l_configSubType][l_slotConfig] & 0xf0) >> 4;
    l_end_slave_rank = memConfigType[l_configType][l_configSubType][l_slotConfig] & 0x0f;

    //********************************************************************
    // Get address range for all ranks configured behind this MBA
    //********************************************************************
    if (i_rank == MSS_ALL_RANKS)
    {
        o_start_addr.flush<0>();
        o_end_addr.flush<0>();
        o_end_addr.insert <0, 4, 8-4>(l_end_master_rank);
        o_end_addr.insert <4, 3, 8-3>(l_end_slave_rank);
        o_end_addr.insert <7, 4, 32-4>((uint32_t)l_bank);
        o_end_addr.insert <11, 17, 32-17>((uint32_t)l_row);
        o_end_addr.insert <28, 12, 32-12>((uint32_t)l_col);
        o_end_addr.writeBit<CEN_MBA_MBMEAQ_CMD_ROW17>(l_row18);
    }
    //********************************************************************
    // Get address range for single rank configured behind this MBA
    //********************************************************************
    else
    {
        o_start_addr.flush<0>();
        o_start_addr.insert<0, 4, 8-4>(i_rank);
        o_end_addr.flush<0>();
        o_end_addr.insert<0, 4, 8-4>(i_rank);
        o_end_addr.insert<4, 3, 8-3>(l_end_slave_rank);
        o_end_addr.insert<7, 4, 32-4>((uint32_t)l_bank);
        o_end_addr.insert<11, 17, 32-17>((uint32_t)l_row);
        o_end_addr.insert<28, 12, 32-12>((uint32_t)l_col);
        o_end_addr.writeBit<CEN_MBA_MBMEAQ_CMD_ROW17>(l_row18);
    }
}

fapi2::ReturnCode mss_MaintCmd::loadSpeed(const TimeBaseSpeed i_speed)
{
    fapi2::buffer<uint64_t> l_data;
    uint32_t l_ddr_freq = 0;
    uint64_t l_step_size = 0;
    uint64_t l_num_address_bits = 0;
    uint64_t l_num_addresses = 0;
    uint64_t l_address_bit = 0;
    fapi2::buffer<uint64_t> l_start_address;
    fapi2::buffer<uint64_t> l_end_address;
    uint64_t l_cmd_interval = 0;
    uint8_t l_burst_window_sel = 0;
    uint8_t l_timebase_sel = 0;
    uint8_t l_timebase_burst_sel = 0;
    uint32_t l_timebase_interval = 1;
    uint8_t l_burst_window = 0;
    uint8_t l_burst_interval = 0;
    constexpr uint64_t TIMEBASE_SEL01 = 8192;
    constexpr uint64_t PICO_TO_NANOS = 1000;

    fapi2::getScom(iv_target, CEN_MBA_MBMCTQ, l_data);
    if (FAST_MAX_BW_IMPACT == i_speed)
    {
        l_timebase_sel = 0;
        l_timebase_interval = 1;
    }
    else if (FAST_MED_BW_IMPACT == i_speed)
    {
        l_timebase_sel = 0;
        l_timebase_interval = 512;
    }
    else if (FAST_MIN_BW_IMPACT == i_speed)
    {
        l_timebase_sel = 1;
        l_timebase_interval = 12;
    } else { // BG_SCRUB
        // Get l_ddr_freq from ATTR_MSS_FREQ
        // Possible frequencies are 800, 1066, 1333, 1600, 1866, and 2133 MHz
        // NOTE: Max 32 address bits using 800 and 1066 result in scrub
        // taking longer than 12h, but these is no plan to actually use
        // those frequencies.
        FAPI_ATTR_GET(fapi2::ATTR_CEN_MSS_FREQ, iv_targetCentaur,  l_ddr_freq);
        // l_timebase_sel
        // MBMCTQ[9:10]: 00 = 1 * Maint Clk
        //               01 = 8192 * Maint Clk
        // Where Maint Clk = 2/1_ddr_freq
        l_timebase_sel = 1;
        // Get l_step_size in nSec
        l_step_size = TIMEBASE_SEL01 * 2 * PICO_TO_NANOS / l_ddr_freq;
        // Get l_end_address
        mss_get_address_range(
          iv_target,
          MSS_ALL_RANKS,
          l_start_address,
          l_end_address );

        // Get l_num_address_bits by counting bits set to 1 in l_end_address.
        for(l_address_bit = 0; l_address_bit < VALID_BITS_IN_ADDR_STRING; l_address_bit++ )
        {
            if(l_end_address.getBit(l_address_bit))
            {
                l_num_address_bits++;
            }
        }
        // Adds in the row for 16Gb. Yes, it's located in a different location
        if(l_end_address.getBit<CEN_MBA_MBMEAQ_CMD_ROW17>())
        {
            l_num_address_bits++;
        }
        // NOTE: Smallest number of address bits is supposed to be 25.
        // So if for some reason it's less (like in VBU),
        // use 25 anyway so the scrub rate calculation still works.
        l_num_address_bits = min(25, l_num_address_bits);
        // Get l_num_addresses
        l_num_addresses = 1;
        for(uint32_t i = 0; i < l_num_address_bits; i++ )
        {
            l_num_addresses *= 2;
        }
        // Convert to M addresses
        l_num_addresses /= 1000000;
        // Get interval between cmds in order to through l_num_addresses in 12h
        l_cmd_interval = (12 * 60 * 60 * 1000) / l_num_addresses;
        // How many times to multiply l_step_size to get l_cmd_interval?
        l_timebase_interval = l_cmd_interval / l_step_size;
        // Round up to nearest integer for more accurate number
        l_timebase_interval += (l_cmd_interval % l_step_size >= l_step_size / 2) ? 1 : 0;
        // Make sure smallest is 1
        l_timebase_interval = min(1, l_timebase_interval);
    }
    // burst_window_sel
    // MBMCTQ[6]
    l_data.insert<6, 1, 8-1>(l_burst_window_sel);
    // timebase_sel
    // MBMCTQ[9:10]
    l_data.insert<9, 2, 8-2>(l_timebase_sel);
    // timebase_burst_sel
    // MBMCTQ[11]
    l_data.insert<11, 1, 8-1>(l_timebase_burst_sel);
    // timebase_interval
    // MBMCTQ[12:23]
    l_data.insert<12, 12, 32-12>(l_timebase_interval);
    // burst_window
    // MBMCTQ[24:31]
    l_data.insert<24, 8, 8-8>(l_burst_window);
    // burst_interval
    // MBMCTQ[32:39]
    l_data.insert<32, 8, 8-8>(l_burst_interval);
    fapi2::putScom(iv_target, CEN_MBA_MBMCTQ, l_data);
}

fapi2::ReturnCode mss_MaintCmd::loadStopCondMask()
{
    fapi2::buffer<uint64_t> l_mbasctlq;
    uint8_t l_mbspa_0_fixed_for_dd2 = 0;
    // Get attribute that tells us if mbspa 0 cmd complete attention is fixed for dd2
    FAPI_ATTR_GET(fapi2::ATTR_CEN_CENTAUR_EC_FEATURE_HW217608_MBSPA_0_CMD_COMPLETE_ATTN_FIXED, iv_targetCentaur,
                           l_mbspa_0_fixed_for_dd2);
    // Get stop conditions from MBASCTLQ
    fapi2::getScom(iv_target, CEN_MBA_MBASCTLQ, l_mbasctlq);
    // Start by clearing all bits 0:12 and bit 16
    l_mbasctlq.clearBit<0, 13>();
    l_mbasctlq.clearBit<16>();
    if(0 != (iv_stop_condition & STOP_IMMEDIATE))
    {
        l_mbasctlq.setBit<0>();
    }
    if(0 != (iv_stop_condition & STOP_END_OF_RANK))
    {
        l_mbasctlq.setBit<1>();
    }
    if(0 != (iv_stop_condition & STOP_ON_HARD_NCE_ETE))
    {
        l_mbasctlq.setBit<2>();
    }
    if(0 != (iv_stop_condition & STOP_ON_INT_NCE_ETE))
    {
        l_mbasctlq.setBit<3>();
    }
    if(0 != (iv_stop_condition & STOP_ON_SOFT_NCE_ETE))
    {
        l_mbasctlq.setBit<4>();
    }
    if(0 != (iv_stop_condition & STOP_ON_SCE))
    {
        l_mbasctlq.setBit<5>();
    }
    if(0 != (iv_stop_condition & STOP_ON_MCE))
    {
        l_mbasctlq.setBit<6>();
    }
    if(0 != (iv_stop_condition & STOP_ON_RETRY_CE_ETE))
    {
        l_mbasctlq.setBit<7>();
    }
    if(0 != (iv_stop_condition & STOP_ON_MPE))
    {
        l_mbasctlq.setBit<8>();
    }
    if(0 != (iv_stop_condition & STOP_ON_UE))
    {
        l_mbasctlq.setBit<9>();
    }
    if(0 != (iv_stop_condition & STOP_ON_END_ADDRESS))
    {
        l_mbasctlq.setBit<10>();
    }
    if(0 != (iv_stop_condition & ENABLE_CMD_COMPLETE_ATTENTION))
    {
        l_mbasctlq.setBit<11>();
    }
    if(0 != (iv_stop_condition & STOP_ON_SUE))
    {
        l_mbasctlq.setBit<12>();
    }
    // Command complete attention on clean and error
    // DD2: enable (fixed)
    // DD1: disable (broken)
    if (l_mbspa_0_fixed_for_dd2)
    {
        l_mbasctlq.setBit<16>();
    }
    // Write stop conditions to MBASCTLQ
    fapi2::putScom(iv_target, CEN_MBA_MBASCTLQ, l_mbasctlq);
}

fapi2::ReturnCode mss_MaintCmd::startMaintCmd()
{
    fapi2::buffer<uint64_t> l_data;
    fapi2::getScom(iv_target, CEN_MBA_MBMCCQ, l_data);
    l_data.setBit<0>();
    fapi2::putScom(iv_target, CEN_MBA_MBMCCQ, l_data);
}

fapi2::ReturnCode mss_MaintCmd::pollForMaintCmdComplete()
{
    fapi2::buffer<uint64_t> l_data;
    do {
        sleep(1ms)
        // Want to see cmd complete attention
        fapi2::getScom(iv_target, CEN_MBA_MBSPAQ, l_data);
        // Read MBMACAQ just to see if it's incrementing
        fapi2::getScom(iv_target, CEN_MBA_MBMACAQ, l_data);
        // Waiting for MBMSRQ[0] maint cmd in progress bit to turn off
        fapi2::getScom(iv_target, CEN_MBA_MBMSRQ, l_data);
    } while(l_data.getBit<0>());
}

fapi2::ReturnCode mss_MaintCmd::collectFFDC()
{
    fapi2::buffer<uint64_t> l_data;
    uint8_t l_dramSparePort0Symbol = MSS_INVALID_SYMBOL;
    uint8_t l_dramSparePort1Symbol = MSS_INVALID_SYMBOL;
    uint8_t l_eccSpareSymbol = MSS_INVALID_SYMBOL;
    uint8_t l_symbol_mark = MSS_INVALID_SYMBOL;
    uint8_t l_chip_mark = MSS_INVALID_SYMBOL;

    fapi2::getScom(iv_target, CEN_MBA_MBMCTQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBMACAQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBMEAQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBASCTLQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBMCCQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBMSRQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBAFIRQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBSPAQ, l_data);
    fapi2::getScom(iv_target, CEN_MBA_MBACALFIRQ, l_data);
    fapi2::getScom(iv_targetCentaur, mss_mbeccfir[iv_mbaPosition], l_data);

    for ( uint8_t l_rank = 0; l_rank < MAX_RANKS_PER_PORT; ++l_rank )
    {
        fapi2::getScom(iv_targetCentaur, mss_markstore_regs[l_rank][iv_mbaPosition], l_data);
        mss_get_mark_store(iv_target, l_rank, l_symbol_mark, l_chip_mark);
        mss_check_steering(
          iv_target, l_rank, l_dramSparePort0Symbol,
          l_dramSparePort1Symbol, l_eccSpareSymbol);
    }
}

fapi2::ReturnCode mss_SuperFastRandomInit::setupAndExecuteCmd()
{
    fapi2::buffer<uint64_t> l_data;
    // preConditionCheck(); error checking here
    loadPattern(iv_initPattern);
    loadCmdType();
    loadStartAddress();
    loadEndAddress();
    loadStopCondMask();
    // Disable 8B ECC check/correct on WRD data bus: MBA_WRD_MODE(0:1) = 11
    // before a SuperFastRandomInit command is issued
    fapi2::getScom(iv_target, CEN_MBA_MBA_WRD_MODE, iv_saved_MBA_WRD_MODE);
    l_data.insert<0, 64, 0>(iv_saved_MBA_WRD_MODE);
    l_data.setBit<0>();
    l_data.setBit<1>();
    fapi2::putScom(iv_target, CEN_MBA_MBA_WRD_MODE, l_data);
    startMaintCmd();
    // postConditionCheck(); error checking here
    pollForMaintCmdComplete();
    collectFFDC();
}

fapi2::ReturnCode mss_SuperFastRead::setupAndExecuteCmd()
{
    fapi2::buffer<uint64_t> l_data;

    // preConditionCheck(); error checking here
    ueTrappingSetup();
    loadCmdType();
    loadStartAddress();
    loadEndAddress();
    loadStopCondMask();
    // Need to set RRQ to fifo mode to ensure super fast read commands
    // are done on order. Otherwise, if cmds get out of order we can't be sure
    // the trapped address in MBMACA will be correct when we stop
    // on error. That means we could unintentionally skip addresses if we just
    // try to increment MBMACA and continue.
    // NOTE: Cleanup needs to be done to restore settings done.
    fapi2::getScom(iv_target, CEN_MBA_MBA_RRQ0Q, iv_saved_MBA_RRQ0);
    l_data.insert<0, 64, 0>(iv_saved_MBA_RRQ0);
    l_data.clearBit<6, 5>(); // Set 6:10 = 00000 (fifo mode)
    l_data.setBit<12>();    // Disable MBA RRQ fastpath
    fapi2::putScom(iv_target, CEN_MBA_MBA_RRQ0Q, l_data);
    startMaintCmd();
    // postConditionCheck();  error checking here
    pollForMaintCmdComplete();
    collectFFDC();
}

///
/// @brief  Saves any settings that need to be restored when command is done.
///         Loads the setup parameters into the hardware. Starts the command,
///         then either polls for complete or exits with command running.
/// @return Non-SUCCESS if an internal function fails, SUCCESS otherwise.
///
fapi2::ReturnCode mss_SuperFastInit::setupAndExecuteCmd()
{
    fapi2::buffer<uint64_t> l_data;
    // preConditionCheck(); error checking here
    loadPattern(iv_initPattern);
    loadCmdType();
    loadStartAddress();
    loadEndAddress();
    loadSpeed(iv_speed));
    loadStopCondMask();
    startMaintCmd();
    // postConditionCheck(); error checking here
    pollForMaintCmdComplete();
    collectFFDC();
}

const mss_MaintCmd::CmdType mss_TimeBaseScrub::cv_cmd_type = TIMEBASE_SCRUB;

///
/// @brief TimeBaseScrub Constructor
/// @param[in] i_target          MBA target
/// @param[in] i_start_addr       Address cmd will start at
/// @param[in] i_end_addr,        Address cmd will stop at
/// @param[in] i_speed           TimeBase Speed
/// @param[in] i_stop_condition   Mask of error conditions cmd should stop on
/// @param[in] i_poll            Set to true if you wait for command to complete
///
mss_TimeBaseScrub::mss_TimeBaseScrub( const fapi2::Target<fapi2::TARGET_TYPE_MBA>& i_target,
                                      const fapi2::buffer<uint64_t>& i_start_addr,
                                      const fapi2::buffer<uint64_t>& i_end_addr,
                                      const TimeBaseSpeed i_speed,
                                      const uint32_t i_stop_condition,
                                      const bool i_poll) :
    mss_MaintCmd(
      i_target,
      i_start_addr,
      i_end_addr,
      i_stop_condition,
      i_poll,
      cv_cmd_type),

// NOTE: iv_speed is instance variable of TimeBaseScrub, since not
// needed in parent class
    iv_speed(i_speed)
{}

class mss_TimeBaseScrub : public mss_MaintCmd
{
    public:
        mss_TimeBaseScrub(const fapi2::Target<fapi2::TARGET_TYPE_MBA>& i_target,
                          const fapi2::buffer<uint64_t>& i_start_addr,
                          const fapi2::buffer<uint64_t>& i_end_addr,
                          TimeBaseSpeed i_speed,
                          uint32_t i_stop_condition,
                          bool i_poll );
        fapi2::ReturnCode setupAndExecuteCmd();
        CmdType getCmdType() const
        {
            return cv_cmd_type;
        }
        void setStartAddr(fapi2::buffer<uint64_t> i_start_addr)
        {
            iv_start_addr = i_start_addr;
        }
        void setEndAddr(  fapi2::buffer<uint64_t> i_end_addr  )
        {
            iv_end_addr   = i_end_addr;
        }
        fapi2::buffer<uint64_t> getStartAddr() const
        {
            return iv_start_addr;
        }
        fapi2::buffer<uint64_t> getEndAddr()   const
        {
            return iv_end_addr;
        }
    private:
        fapi2::ReturnCode setSavedData( uint32_t i_savedData )
        {
            fapi2::ReturnCode l_rc;
            iv_savedData = i_savedData;
            return l_rc;
        }
        uint32_t getSavedData()
        {
            return iv_savedData;
        }
        static const CmdType cv_cmd_type;
        uint32_t iv_savedData;
        TimeBaseSpeed iv_speed;
};
```
