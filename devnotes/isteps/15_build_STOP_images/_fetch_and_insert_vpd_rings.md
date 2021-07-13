```cpp
static void _fetch_and_insert_vpd_rings(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    void*           i_ringSection,
    uint32_t&       io_ringSectionSize,
    uint32_t        i_maxRingSectionSize,
    void*           i_overlaysSection,
    uint8_t         i_ddLevel,
    uint8_t         i_sysPhase,             // CME/SGPE
    CompressedScanData* i_vpdRing,
    uint32_t        i_vpdRingSize,
    void*           i_ringBuf2,
    uint32_t        i_ringBufSize2,
    void*           i_ringBuf3,
    uint32_t        i_ringBufSize3,
    uint8_t         i_chipletId,
    uint8_t         i_evenOdd,              // 0, optimized out in analysis
    const RingIdList i_ring,
    uint8_t&        io_ringStatusInMvpd,    // RING_SCAN - unused here, but changed to different status as an output
    bool&           i_bImgOutOfSpace,       // false
    uint32_t&       io_bootCoreMask)        // unused
{
    memset(i_vpdRing,  0, i_vpdRingSize);
    memset(i_ringBuf2, 0, i_ringBufSize2);
    memset(i_ringBuf3, 0, i_ringBufSize3);

    mvpdRingFunc(
        procChip,
        i_chipletId,
        i_ring.ringId,
        (uint8_t*)i_vpdRing,
        i_vpdRingSize,
        (uint8_t*)i_ringBuf2,
        i_ringBufSize2);

    memset(i_ringBuf2, 0, i_ringBufSize2);

    if(i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_NEST
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EQ
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EX
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EC)
    {
        process_gptr_rings(
            i_overlaysSection,
            i_ddLevel,
            i_vpdRing,
            i_ringBuf2,
            i_ringBuf3);

        i_vpdRingSize = be16toh(i_vpdRing->iv_size);
    }

    if (i_vpdRing->iv_magic == htobe16(RS4_MAGIC))
    {
        int redundant = 0;
        rs4_redundant(i_vpdRing, &redundant);
        if(redundant)
        {
            io_ringStatusInMvpd = RING_REDUNDANT;
            return;
        }
    }
    i_bImgOutOfSpace = i_maxRingSectionSize < (io_ringSectionSize + i_vpdRingSize);

    uint8_t l_chipletTorId =
        (i_chipletId - i_ring.instanceIdMin)
      * (i_ring.vpdRingClass == VPD_RING_CLASS_EX_INS ? 1 : 0)
      + i_chipletId;

    tor_append_ring(
        i_ringSection,
        io_ringSectionSize, // In: Exact size. Out: Updated size.
        i_ring.ringId,
        (i_sysPhase == SYSPHASE_RT_CME) ? PT_CME : PT_SGPE,
        l_chipletTorId,  // Chiplet instance TOR Index
        i_vpdRing);      // The VPD RS4 ring container
    io_ringStatusInMvpd = RING_FOUND;
}

/// Traverse on TOR structure and copies absolute memory address of Ringtype
///  offset addres and TOR offset slot address
///
/// \param[in]  i_ringSection A pointer to a Ring section binary image.
/// It contains details of p9 Ring which is used for scanning operation.
///  TOR API supports SEEPROM image format.
///
/// \param[in/out]  io_ringSectionSize   In: Exact size of i_ringSection.
/// Out: Updated size of i_ringSection.
/// Note: Caller manages this buffer and must make sure the RS4 ring fits
/// before making this call
///
/// \param[in]  i_ringId A enum to indicate unique ID for the ring
///
/// \param[in]  i_ppeType A enum to indicate ppe type. They are SBE,
/// CME and SGPE. It is used to decode TOR structure
///
/// \param[in] i_instanceId A variable to indicate chiplet instance ID
///
/// \param[in] i_rs4Container A void pointer. Contains RS4 compressed ring
/// data which eventually attached into void image pointer i_ringSection
///
/// This API contains wrapper on tor_access_ring to get \a io_ringBlockPtr
/// contains absolute memory address of ring type start address of the ring
/// \a io_ringBlockSize contains absolute memory address of ringTorslot. Then
/// appends new rs4 ring container at the end of ring section and updates new
/// ring offset address on ring offset location. the slot must be empty. If there
/// is non-zero content in the slot, the API will fail catastrophically. Do not
/// "insert" or "replace" rings at ring section.

static void tor_append_ring(
    void*           i_ringSection,      // Ring section ptr
    uint32_t&       io_ringSectionSize, // In: Exact size of ring section.
                                        // Out: Updated size of ring section.
    RingId_t        i_ringId,           // Ring ID
    PpeType_t       i_ppeType,          // CME, SGPE
    uint8_t         i_instanceId,       // Instance ID
    void*           i_rs4Container)     // RS4 ring container
{
    uint32_t   l_buf;
    uint32_t   l_torOffsetSlot;

    tor_access_ring(
        i_ringSection,
        i_ringId,
        i_ppeType,
        i_instanceId,
        &l_buf,
        l_torOffsetSlot);

    if(io_ringSectionSize - l_buf <= MAX_TOR_RING_OFFSET)
    {
        uint16_t l_ringOffset16 = htobe16(io_ringSectionSize - l_buf);
        memcpy(i_ringSection + l_torOffsetSlot, &l_ringOffset16, l_ringOffset16);
        uint32_t l_ringBlockSize = be16toh(((CompressedScanData*)i_rs4Container)->iv_size);
        memcpy(i_ringSection + io_ringSectionSize, i_rs4Container, l_ringBlockSize);
        io_ringSectionSize += l_ringBlockSize;
        TorHeader_t* torHeader = (TorHeader_t*)i_ringSection;
        torHeader->size = htobe32(be32toh(torHeader->size) + l_ringBlockSize);
    }
}

///
/// ****************************************************************************
/// Function declares.
/// ****************************************************************************
///
/// Traverse on TOR structure and copies data in granular up to DD type,
/// ppe type, ring type, RS4 ring container and ring address
///
/// \param[in]  i_ringSection A pointer to a Ring section binary image.
/// It contains details of p9 Ring, which is used for scanning operation.
/// TOR API supports two type of binary image. 1) HW image format and 2)
/// SEEPROM image format binary
///
/// \param[in]  i_ringId A enum to indicate unique ID for the ring
///
/// \param[in]  i_ppeType A enum to indicate ppe type. They are SBE, CME
/// and SGPE. It is used to decode TOR structure
///
/// \param[in/out] io_instanceId A variable to indicate chiplet instance ID.
/// It returns Chiplet instance ID while doing get single ring operation
///
/// \param[in/out] io_ringBlockPtr A void pointer to pointer. Returns data
/// which copied during extract ring operation and returns tor absolute address
/// where offset slot is located while PUT_SINGLE_RING call.
/// Note:- Caller's responsibility for free() to avoid memory leak
///
/// \param[in/out] io_ringBlockSize A variable. Returns size of data copied
/// into io_ringBlockPtr and returns absolute offset where ring RS4 starts in
/// TOR during PUT_SINGLE_RING call

static void tor_access_ring(
    TorHeader_t*    i_ringSection,     // Ring section ptr
    RingId_t        i_ringId,          // Ring ID
    PpeType_t       i_ppeType,         // CME, SGPE
    uint8_t&        io_instanceId,     // Instance ID
    void*           io_ringBlockPtr,   // Ring data buffer
    uint32_t&       io_ringBlockSize)  // Size of ring data
{
    uint8_t* postHeaderStart = (uint8_t*)i_ringSection + sizeof(TorHeader_t);

    if (be32toh(i_ringSection->magic) >> 8 != TOR_MAGIC
    || i_ringSection->version > TOR_VERSION
    || i_ringSection->version == 0
    || i_ringSection->chipType >= NUM_CHIP_TYPES)
    {
        return;
    }

    if(be32toh(i_ringSection->magic) == TOR_MAGIC_SBE
    || be32toh(i_ringSection->magic) == TOR_MAGIC_CME
    || be32toh(i_ringSection->magic) == TOR_MAGIC_SGPE)
    {
        get_ring_from_ring_section(
            (be32toh(i_ringSection->magic) != TOR_MAGIC_HW) ?
                i_ringSection
              : postHeaderStart + be32toh((TorPpeBlock_t*)(postHeaderStart + i_ppeType * sizeof(TorPpeBlock_t))->offset);
            i_ringId,
            io_instanceId,
            io_ringBlockPtr,
            io_ringBlockSize);
    }
}

static void get_ring_from_ring_section(
    TorHeader_t*    i_ringSection,     // Ring section ptr
    RingId_t        i_ringId,          // Ring ID
    uint8_t&        io_instanceId,     // Instance ID
    void*           io_ringBlockPtr,   // Output ring buffer
    uint32_t&       io_ringBlockSize)  // Size of ring data
{
    ChipletType_t numChiplets;

    ringid_get_noof_chiplets(
        i_ringSection->chipType,
        be32toh(i_ringSection->magic),
        &numChiplets);

    for (ChipletType_t iCplt = 0; iCplt < numChiplets; iCplt++)
    {
        ChipletData_t* cpltData;
        GenRingIdList* ringIdListCommon;
        GenRingIdList* ringIdListInstance;
        RingProperties_t* ringProps;
        RingVariantOrder* ringVariantOrder;
        uint8_t numVariants;

        ringid_get_properties(
            i_ringSection->chipType,
            be32toh(i_ringSection->magic),
            i_ringSection->version,
            iCplt,
            &cpltData,
            &ringIdListCommon,
            &ringIdListInstance,
            &ringVariantOrder,
            &ringProps,
            &numVariants);
        for(uint8_t bInstCase = 0; bInstCase <= 1; bInstCase++)
        {
            GenRingIdList* ringIdList = bInstCase ? ringIdListInstance : ringIdListCommon;
            if(i_ringSection->version >= 7)
            {
                numVariants = bInstCase ? 1 : numVariants;
            }

            if(ringIdList)
            {
                TorCpltOffset_t cpltOffset =
                    sizeof(TorHeader_t)
                  + be32toh(*(uint32_t*)(
                      i_ringSection
                    + sizeof(TorHeader_t)
                    + iCplt * sizeof(TorCpltBlock_t)
                    + bInstCase * sizeof(TorCpltOffset_t)));

                uint32_t torSlotNum = 0;

                for(ringInstance = ringIdList->instanceIdMin;
                    ringInstance <= ringIdList->instanceIdMax;
                    ringInstance++)
                {
                    for(ringIndex = 0;
                        ringIndex < bInstCase ? cpltData->iv_num_instance_rings : cpltData->iv_num_common_rings;
                        ringIndex++)
                    {
                        for (varianIndex = 0; varianIndex < numVariants; varianIndex++)
                        {
                            if(strcmp((ringIdList + ringIndex)->ringName, ringProps[i_ringId].iv_name) == 0
                            && (RV_BASE == ringVariantOrder->variant[varianIndex] || numVariants == 1)
                            && ((bInstCase && ringInstance == io_instanceId) || bInstCase == 0))
                            {
                                TorRingOffset_t ringOffset = be16toh(*(TorRingOffset_t*)(
                                    i_ringSection
                                  + cpltOffset
                                  + torSlotNum * sizeof(ringOffset)));
                                if(ringOffset)
                                {
                                    return;
                                }
                                memcpy(io_ringBlockPtr, &cpltOffset, sizeof(cpltOffset));
                                io_ringBlockSize = cpltOffset + torSlotNum * sizeof(ringOffset);
                                return;
                            }
                            torSlotNum++;
                        }
                    }
                }
            }
        }
    }
}

static void ringid_get_properties(
    ChipType_t         i_chipType
    uint32_t           i_torMagic
    uint8_t            i_torVersion
    ChipletType_t      i_chipletType
    ChipletData_t**    o_chipletData
    GenRingIdList**    o_ringIdListCommon
    GenRingIdList**    o_ringIdListInstance
    RingVariantOrder** o_ringVariantOrder
    RingProperties_t** o_ringProps
    uint8_t*           o_numVariants)
{
    switch (i_chipType)
    {
        case CT_P9N:
        case CT_P9C:
        case CT_P9A:
            if(i_torMagic == TOR_MAGIC_SBE
            || i_torMagic == TOR_MAGIC_OVRD
            || i_torMagic == TOR_MAGIC_OVLY)
            {
                P9_RID::ringid_get_chiplet_properties(
                    i_chipletType,
                    o_chipletData,
                    o_ringIdListCommon,
                    o_ringIdListInstance,
                    o_ringVariantOrder,
                    o_numVariants);

                if(i_torVersion < 7
                && (i_chipletType == P9_RID::EQ_TYPE
                 || i_chipletType == P9_RID::EC_TYPE))
                {
                    *o_numVariants = *o_numVariants - 3;
                }

                if(i_torMagic == TOR_MAGIC_OVRD
                || i_torMagic == TOR_MAGIC_OVLY)
                {
                    *o_numVariants = 1;
                }
            }
            else if(i_torMagic == TOR_MAGIC_CME)
            {
                *o_chipletData        = (ChipletData_t*)&P9_RID::EC::g_chipletData;
                *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_COMMON;
                *o_ringIdListInstance = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_INSTANCE;
                *o_ringVariantOrder   = (RingVariantOrder*)P9_RID::EC::RING_VARIANT_ORDER;
                *o_numVariants        = P9_RID::EC::g_chipletData.iv_num_common_ring_variants;

                if (i_torVersion < 7)
                {
                    *o_numVariants = *o_numVariants - 3;
                }
            }
            else if(i_torMagic == TOR_MAGIC_SGPE)
            {
                *o_chipletData        = (ChipletData_t*)&P9_RID::EQ::g_chipletData;
                *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_COMMON;
                *o_ringIdListInstance = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_INSTANCE;
                *o_ringVariantOrder   = (RingVariantOrder*)P9_RID::EQ::RING_VARIANT_ORDER;
                *o_numVariants        = P9_RID::EQ::g_chipletData.iv_num_common_ring_variants;

                if (i_torVersion < 7)
                {
                    *o_numVariants = *o_numVariants - 3;
                }
            }
            else
            {
                return;
            }
            *o_ringProps = (RingProperties_t*)P9_RID::RING_PROPERTIES;
            break;
        case CT_CEN:
            if(i_torMagic == TOR_MAGIC_CEN
            || i_torMagic == TOR_MAGIC_OVRD)
            {
                CEN_RID::ringid_get_chiplet_properties(
                    i_chipletType,
                    o_chipletData,
                    o_ringIdListCommon,
                    o_ringIdListInstance,
                    o_ringVariantOrder,
                    o_numVariants);

                if(i_torMagic == TOR_MAGIC_OVRD)
                {
                    *o_numVariants = 1;
                }
            }
            else
            {
                return;
            }
            *o_ringProps = (RingProperties_t*)CEN_RID::RING_PROPERTIES;
            return;
    }
}

void CEN_RID::ringid_get_chiplet_properties(
    ChipletType_t      i_chipletType,
    ChipletData_t**    o_cpltData,
    GenRingIdList**    o_ringComm,
    GenRingIdList**    o_ringInst,
    RingVariantOrder** o_varOrder,
    uint8_t*           o_numVariants)
{
    switch (i_chipletType)
    {
        case CEN_TYPE :
            *o_cpltData = (ChipletData_t*)   &CEN::g_chipletData;
            *o_ringComm = (GenRingIdList*)    CEN::RING_ID_LIST_COMMON;
            *o_ringInst = NULL;
            *o_varOrder = (RingVariantOrder*) CEN::RING_VARIANT_ORDER;
            *o_numVariants = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        default :
            *o_cpltData = NULL;
            *o_ringComm = NULL;
            *o_ringInst = NULL;
            *o_varOrder = NULL;
            *o_numVariants = 0;
            break;
    }
}

void P9_RID::ringid_get_chiplet_properties(
    ChipletType_t      i_chipletType,
    ChipletData_t**    o_cpltData,
    GenRingIdList**    o_ringComm,
    GenRingIdList**    o_ringInst,
    RingVariantOrder** o_varOrder,
    uint8_t*           o_numVariants)
{
    switch (i_chipletType)
    {
        case PERV_TYPE :
            *o_cpltData = (ChipletData_t*)   &PERV::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PERV::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PERV::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PERV::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N0_TYPE :
            *o_cpltData = (ChipletData_t*)   &N0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N1_TYPE :
            *o_cpltData = (ChipletData_t*)   &N1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N2_TYPE :
            *o_cpltData = (ChipletData_t*)   &N2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N3_TYPE :
            *o_cpltData = (ChipletData_t*)   &N3::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N3::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N3::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case XB_TYPE :
            *o_cpltData = (ChipletData_t*)   &XB::g_chipletData;
            *o_ringComm = (GenRingIdList*)    XB::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    XB::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) XB::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case MC_TYPE :
            *o_cpltData = (ChipletData_t*)   &MC::g_chipletData;
            *o_ringComm = (GenRingIdList*)    MC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    MC::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) MC::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB0_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB1_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB2_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB3_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB3::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB3::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB3::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI0_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI1_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI2_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case EQ_TYPE :
            *o_cpltData = (ChipletData_t*)   &EQ::g_chipletData;
            *o_ringComm = (GenRingIdList*)    EQ::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    EQ::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) EQ::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case EC_TYPE :
            *o_cpltData = (ChipletData_t*)   &EC::g_chipletData;
            *o_ringComm = (GenRingIdList*)    EC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    EC::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) EC::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        default :
            *o_cpltData = NULL;
            *o_ringComm = NULL;
            *o_ringInst = NULL;
            *o_varOrder = NULL;
            *o_numVariants = 0;
            break;
    }
}

void ringid_get_noof_chiplets(
    ChipType_t  i_chipType,
    uint32_t    i_torMagic,
    uint8_t*    o_numChiplets )
{
    switch (i_chipType)
    {
        case CT_P9N:
        case CT_P9C:
        case CT_P9A:
            if(i_torMagic == TOR_MAGIC_SBE
            || i_torMagic == TOR_MAGIC_OVRD
            || i_torMagic == TOR_MAGIC_OVLY)
            {
                *o_numChiplets = P9_RID::SBE_NOOF_CHIPLETS;
            }
            else if ( i_torMagic == TOR_MAGIC_CME)
            {
                *o_numChiplets = P9_RID::CME_NOOF_CHIPLETS;
            }
            else if ( i_torMagic == TOR_MAGIC_SGPE)
            {
                *o_numChiplets = P9_RID::SGPE_NOOF_CHIPLETS;
            }
            else
            {
                return TOR_INVALID_MAGIC_NUMBER;
            }
            break;

        case CT_CEN:
            if(i_torMagic == TOR_MAGIC_CEN
            || i_torMagic == TOR_MAGIC_OVRD )
            {
                *o_numChiplets = CEN_RID::CEN_NOOF_CHIPLETS;
            }
            else
            {
                return TOR_INVALID_MAGIC_NUMBER;
            }
            break;
    }
}

/// Determine if an RS4 compressed scan string is all 0
///
/// \param[in] i_data A pointer to the CompressedScanData header + data to be
///
/// \param[out] o_redundant Set to 1 if the RS4 string is the compressed form
/// of a scan string that is all 0; Otherwise set to 0.
///
/// \returns See \ref scan _compression_code

int rs4_redundant(const CompressedScanData* i_data, int* o_redundant)
{
    uint8_t* data = (uint8_t*)i_data + sizeof(CompressedScanData);
    uint32_t length;
    uint32_t pos = stop_decode(&length, data, 0);

    *o_redundant = 0;
    if (rs4_get_nibble(data, pos) == 0)
    {
        if (rs4_get_nibble(data, pos + 1) == 0)
        {
            *o_redundant = 1;
        }
        else if (rs4_get_nibble(data, pos + 2) == 0)
        {
            *o_redundant = 1;
        }
    }
}

static int rs4_get_nibble(const uint8_t* i_string, const uint32_t index)
{
    uint8_t byte = i_string[index / 2];
    if (index % 2)
    {
        return byte & 0xf;
    }
    else
    {
        return byte >> 4;
    }
    return nibble;
}

static int stop_decode(uint32_t* o_count, const uint8_t* i_string, const uint32_t index)
{
    int digits = 0, nibble;
    uint32_t count = 0;

    for(int nibble; nibble & 0x8 == 0;)
    {
        nibble = rs4_get_nibble(i_string, index);
        count = (count * 8) + (nibble & 0x7);
        index++;
        digits++;
    }
    *o_count = count;
    return digits;
}

void process_gptr_rings(
    void*   i_overlaysSection,
    uint8_t i_ddLevel,
    void*   io_vpdRing,
    void*   io_ringBuf2,
    void*   io_ringBuf3)
{
    uint32_t l_ovlyUncmpSize = 0;
    void* l_ovlyRs4Ring = io_ringBuf2;
    void* l_ovlyRawRing = io_ringBuf3;

    get_overlays_ring(
        i_overlaysSection,
        i_ddLevel,
        (RingId_t)be16toh(((CompressedScanData*)io_vpdRing)->iv_ringId),
        &l_ovlyRs4Ring,
        &l_ovlyRawRing,
        &l_ovlyUncmpSize);

    if (l_ovlyRs4Ring == io_ringBuf2 && l_ovlyRawRing == io_ringBuf3)
    {
        apply_overlays_ring(
            io_vpdRing,
            io_ringBuf2,
            l_ovlyRawRing,
            l_ovlyUncmpSize);
    }
}

void apply_overlays_ring(
    void*    io_vpdRing,
    void*    io_ringBuf2,
    void*    i_ovlyRawRing,
    uint32_t i_ovlyUncmpSize)
{
    uint8_t* dataVpd = (uint8_t*)io_ringBuf2;
    uint8_t* careVpd = (uint8_t*)io_ringBuf2 + MAX_RING_BUF_SIZE / 2;
    uint8_t* dataOvly = NULL;
    uint8_t* careOvly = NULL;
    uint32_t vpdUncmpSize = 0;

    dataOvly = (uint8_t*)i_ovlyRawRing;
    careOvly = (uint8_t*)i_ovlyRawRing + MAX_RING_BUF_SIZE / 2;

    _rs4_decompress(
        dataVpd,
        careVpd,
        MAX_RING_BUF_SIZE / 2,
        &vpdUncmpSize,
        (CompressedScanData*)io_vpdRing);

    for (int index = 0; index < vpdUncmpSize / 8; index++)
    {
        if (careOvly[index] > 0)
        {
            for (int j = 0; j < 8; j++)
            {
                if (careOvly[index] & (0x80 >> j))
                {
                    if (dataOvly[index] & (0x80 >> j))
                    {
                        dataVpd[index] |= (0x80 >> j);
                        careVpd[index] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[index] &= ~(0x80 >> j);
                        careVpd[index] &= ~(0x80 >> j);
                    }
                }
            }
        }
    }

    if (vpdUncmpSize % 8)
    {
        int index = (int)vpdUncmpSize / 8;
        careOvly[index] &= ~(0xFF << (8 - (vpdUncmpSize % 8)));

        if (careOvly[index] > 0)
        {
            for (int j = 0; j < (int)vpdUncmpSize % 8; j++)
            {
                if (careOvly[index] & (0x80 >> j))
                {
                    if(dataOvly[index] & (0x80 >> j))
                    {
                        dataVpd[index] |= (0x80 >> j);
                        careVpd[index] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[index] &= ~(0x80 >> j);
                        careVpd[index] &= ~(0x80 >> j);
                    }
                }
            }
        }
    }
    _rs4_compress(
        (CompressedScanData*)io_vpdRing,
        MAX_RING_BUF_SIZE,
        dataVpd,
        careVpd,
        vpdUncmpSize,
        be32toh(((CompressedScanData*)io_vpdRing)->iv_scanAddr),
        be16toh(((CompressedScanData*)io_vpdRing)->iv_ringId));
}

/// Compress a scan string using the RS4 compression algorithm
///
/// \param[in,out] io_rs4 This is a pointer to a memory area which must be
/// large enough to hold the worst-case result of compressing \a i_data_str
/// and \a i_care_str (see below). Note that the CompressedScanData is
/// always created in big-endian format, however the caller can use
/// compresed_scan_data_translate() to create a copy of the header in
/// host format.
///
/// \param[in] i_size The size of the buffer pointed to by \a io_rs4.
///
/// \param[in] i_data_str The string to compress.  Scan data to compress is
/// left-justified in this input string.
///
/// \param[in] i_care_str The care mask that identifies which bits in the
/// i_data_str that need to be scanned (written). String is left-justified.
///
/// \param[in] i_length The length of the input string in \e bits.  It is
/// assumed the \a i_string contains at least (\a i_length + 7) / 8 bytes.
///
/// \param[in] i_scanAddr The 32-bit scan address.
///
/// \param[in] i_ringId The ring ID that uniquely identifies the ring. (See
/// <ChipType> ring ID header files for more info.)
///
/// This API is required for integration with PHYP which does not support
/// local memory allocation, like malloc() and new().  Applications in
/// environments supporting local memory allocation can use rs4_compress()
/// instead.
///
/// We always require the worst-case amount of memory including the header and
/// any rounding required to guarantee that the data size is a multiple of 8
/// bytes.  The final image size is also rounded up to a multiple of 8 bytes.
/// If the \a io_size is less than this amount (based on \a i_length) the
/// call will fail.
///
/// \returns See \ref scan_compression_codes

static void _rs4_compress(
    CompressedScanData* io_rs4,
    const uint32_t i_size,
    const uint8_t* i_data_str,
    const uint8_t* i_care_str,
    const uint32_t i_length,
    const uint32_t i_scanAddr,
    const RingId_t i_ringId);

static int rs4_set_nibble(uint8_t* io_string, const uint32_t index, const int i_nibble)
{
    if (index % 2)
    {
        io_string[index / 2] = (io_string[index / 2] & 0xf0) | i_nibble;
    }
    else
    {
        io_string[index / 2] = (io_string[index / 2] & 0x0f) | (i_nibble << 4);
    }
    return i_nibble;
}

/// Decompress a scan string compressed using the RS4 compression algorithm
///
/// \param[in,out] io_data_str A caller-supplied data area to contain the
/// decompressed string. The \a i_stringSize must be large enough to contain
/// the decompressed string, which is the size of the original string in bits
/// rounded up to the nearest byte.
///
/// \param[in,out] io_care_str A caller-supplied data area to contain the
/// decompressed care mask. The \a i_stringSize must be large enough to contain
/// the decompressed care mask, which is the size of the original string in
/// bits rounded up to the nearest byte.
///
/// \param[in] i_size The size in \e bytes of \a o_data_str and \a o_care_str
/// buffers and which represents the max number of raw ring bits x 8 that may
/// fit into the two raw ring buffers.
///
/// \param[out] o_length The length of the decompressed string in \e bits.
///
/// \param[in] i_rs4 A pointer to the CompressedScanData header + data to be
/// decompressed.
///
/// This API is required for integration with PHYP which does not support
/// local memory allocation, such as malloc() and new().  Applications in
/// environments supporting local memory allocation can use rs4_decompress()
/// instead.
///
/// \returns See \ref scan_compression_codes

int _rs4_decompress(
    uint8_t* o_data_str,
    uint8_t* o_care_str,
    uint32_t i_size,
    uint32_t* o_length,
    const CompressedScanData* i_rs4);

void get_overlays_ring(
    void*     i_overlaysSection,
    uint8_t   i_ddLevel,
    RingId_t  i_ringId,
    void**    io_ringBuf2,
    void**    io_ringBuf3,
    uint32_t* o_ovlyUncmpSize)
{
    ReturnCode l_fapiRc = fapi2::FAPI2_RC_SUCCESS;
    uint32_t l_maxRingByteSize = MAX_RING_BUF_SIZE;
    uint32_t l_ovlyUncmpSize = 0;
    uint8_t  l_instanceId = 0;
    uint32_t l_ringBlockSize = 0xFFFFFFFF;

    tor_access_ring(
        i_overlaysSection,
        i_ringId,
        UNDEFINED_PPE_TYPE,
        UNDEFINED_RING_VARIANT,
        l_instanceId,
        io_ringBuf2,
        l_ringBlockSize);

    _rs4_decompress(
        (uint8_t*)(*io_ringBuf3),
        (uint8_t*)(*io_ringBuf3) + l_maxRingByteSize / 2,
        l_maxRingByteSize / 2,
        &l_ovlyUncmpSize,
        (CompressedScanData*)(*io_ringBuf2));
    *o_ovlyUncmpSize = l_ovlyUncmpSize;
}

static void mvpdRingFunc(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    const mvpdRingFuncOp i_mvpdRingFuncOp,
    const uint8_t        i_chipletId,
    const RingId_t       i_ringId,
    uint8_t*             o_pRingBuf,
    uint32_t&            io_rRingBufsize,
    uint8_t*             i_pTempBuf,
    uint32_t             i_tempBufsize)
{
    uint32_t                l_recordLen  = 0;
    uint8_t*                l_recordBuf  = i_pTempBuf;
    uint8_t*                l_pRing      = NULL;
    uint32_t                l_ringLen    = 0;

    // mvpd.C
    // DEVICE_REGISTER_ROUTE( DeviceFW::READ,
    //                        DeviceFW::MVPD,
    //                        TARGETING::TYPE_PROC,
    //                        mvpdRead );

    // read "ED" keyword from "CRP0" record
    mvpdRead(
        procChip.get(),
        l_recordBuf,
        l_recordLen);

    mvpdRingFuncFind(
        i_chipletId,
        i_ringId,
        l_recordBuf,
        l_recordLen,
        l_pRing,
        l_ringLen);

    if(l_ringLen && o_pRingBuf)
    {
        io_rRingBufsize = l_ringLen;
        memcpy(o_pRingBuf, l_pRing, l_ringLen);
    }
}

// this function tries to find next mvpd ring
/**
*  @brief MVPD Ring Function Find
*
*  @par Detailed Description:
*           Step through the record looking at rings for a match.
*
*  @param[in]  i_chipletId
*                   Chiplet ID for the op
*
*  @param[in]  i_ringId
*                   Ring ID for the op
*
*  @param[in]  i_pRecordBuf
*                   Pointer to record buffer
*
*  @param[in]  i_recordBufLen
*                   Length of record buffer
*
*  @param[out] o_rpRing
*                   Pointer to the ring in the record, if it is there
*                   Pointer to the start of the padding after the last
*                   ring, if it is not there
*
*  @param[out] o_rRingLen
*                   Number of bytes in the ring (header and data)
*                   Will be 0 if ring not found
**/
static void mvpdRingFuncFind(
    const uint8_t       i_chipletId,
    const RingId_t      i_ringId,
    uint8_t*            i_pRecordBuf,
    uint32_t            i_recordBufLen,
    uint8_t*&           o_rpRing,
    uint32_t&           o_rRingLen)
{
    bool l_mvpdEnd;
    CompressedScanData* o_rpRing = NULL;
    i_recordBufLen--;
    i_pRecordBuf++;

    do
    {
        // Check if mvpd ends here
        mvpdRingFuncFindEnd(
            &i_pRecordBuf,
            &i_recordBufLen,
            &l_mvpdEnd);
        if (!l_mvpdEnd && !o_rpRing)
        {
            // Searches for next mvpdRing header
            mvpdRingFuncFindHdr(
                i_chipletId,
                i_ringId,
                &i_pRecordBuf,
                &i_recordBufLen,
                &o_rpRing);
        }
    }
    // while mvpd has not ended,
    // next ring hasn't been found and
    // space in i_pRecordBuf has not ended
    while (!l_mvpdEnd && !o_rpRing && i_recordBufLen);

    o_rRingLen = be16toh(o_rpRing->iv_size);
}

 static void mvpdRead(
    TARGETING::Target * i_target,
    void * io_buffer,
    size_t & io_buflen)
{
    // Will call IpVpdFacade::read
    Singleton<MvpdFacade>::instance().read(
        i_target,
        io_buffer,
        io_buflen);
}

static void IpVpdFacade::read(
    TARGETING::Target * i_target,
    void* o_buffer,
    size_t & io_buflen)
{
    uint16_t recordOffset = 0x0;
    findRecordOffsetPnor("CRP0", recordOffset, i_target, VPD::AUTOSELECT);
    // vpd data can also originate from VPD::SEEPROM
    // findRecordOffsetSeeprom(i_record, o_offset, o_length, i_target, i_args);

    IpVpdFacade::input_args_t args;
    args.record = MVPD::CRP0;
    args.keyword = MVPD::ED;
    args.location = VPD::AUTOSELECT;
    retrieveKeyword("ED", "CRP0", recordOffset, 0, i_target, o_buffer, io_buflen, args);
}


static void IpVpdFacade::retrieveKeyword(
    const char * i_keywordName,
    const char * i_recordName,
    uint16_t i_offset,
    uint16_t i_index,
    TARGETING::Target * i_target,
    void * o_buffer,
    size_t & io_buflen,
    input_args_t i_args)
{
    uint64_t byteAddr = 0x0;
    findKeywordAddr(i_keywordName, i_recordName, i_offset, i_index, i_target, io_buflen, byteAddr, i_args);
    if(NULL == o_buffer)
    {
        return;
    }
    fetchData(i_offset + byteAddr, io_buflen, o_buffer, i_target, i_recordName );
}

// This function looks for an address of a particular keyword
static void IpVpdFacade::findKeywordAddr(
    const char * i_keywordName,
    const char * i_recordName,
    uint16_t i_offset,
    uint16_t i_index,
    TARGETING::Target * i_target,
    size_t& o_keywordSize,
    uint64_t& o_byteAddr,
    input_args_t i_args)
{
    uint16_t offset = i_offset;
    uint16_t recordSize = 0;

    fetchData(offset, RECORD_ADDR_BYTE_SIZE, &recordSize, i_target, i_args.location, i_recordName);
    offset += RECORD_ADDR_BYTE_SIZE + RT_SKIP_BYTES; // 5 bytes in total

    // RT keyword is always first, but probably doesn't exist in pound records
    if(memcmp(i_keywordName, "RT", KEYWORD_BYTE_SIZE) == 0 && i_index == 0)
    {
        o_keywordSize = RECORD_BYTE_SIZE;
        o_byteAddr = offset - i_offset;
        return;
    }
// RECORD_BYTE_SIZE        = 4,
// RECORD_ADDR_BYTE_SIZE   = 2,
// KEYWORD_BYTE_SIZE       = 2,
// KEYWORD_SIZE_BYTE_SIZE  = 1,
// RECORD_TOC_UNUSED       = 2,
// RT_SKIP_BYTES           = 3,
// VHDR_ECC_DATA_SIZE      = 11,
// VHDR_RESOURCE_ID_SIZE   = 1,
    offset += RECORD_BYTE_SIZE;
    int matchesFound = 0;
    while(offset < le16toh(recordSize) + i_offset + RECORD_ADDR_BYTE_SIZE)
    {
        char keyword[KEYWORD_BYTE_SIZE] = { '\0' };
        fetchData(offset, KEYWORD_BYTE_SIZE, keyword, i_target, i_args.location, i_recordName );
        offset += KEYWORD_BYTE_SIZE;

        uint32_t keywordLength = KEYWORD_SIZE_BYTE_SIZE;
        bool isPoundKwd = false;
        if(keyword[0] == '#')
        {
            isPoundKwd = true;
            keywordLength++;
        }

        uint16_t keywordSize = 0;
        fetchData(offset, keywordLength, &keywordSize, i_target, i_args.location, i_recordName);
        offset += keywordLength;

        if(isPoundKwd)
        {
            keywordSize = le16toh(keywordSize);
        }
        else
        {
            keywordSize = keywordSize >> 8;
        }

        if(!(memcmp(keyword, i_keywordName, KEYWORD_BYTE_SIZE)))
        {
            matchesFound++;
            if (matchesFound == i_index + 1)
            {
                o_keywordSize = keywordSize;
                o_byteAddr = offset - i_offset;
                break;
            }
        }
        offset += keywordSize;
    }
}

static void getMvpdField(
    const fapi2::MvpdRecord i_record,
    const fapi2::MvpdKeyword i_keyword,
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_procTarget,
    uint8_t * const i_pBuffer,
    uint32_t &io_fieldSize)
{
    uint8_t l_recIndex = MVPD_INVALID_CHIP_UNIT;
    uint8_t l_keyIndex = MVPD_INVALID_CHIP_UNIT;

    MVPD::mvpdRecord l_hbRecord = MVPD::MVPD_INVALID_RECORD;
    fapi2::MvpdRecordXlate(i_record, l_hbRecord, l_recIndex);
    MVPD::mvpdKeyword l_hbKeyword = MVPD::INVALID_MVPD_KEYWORD;
    fapi2::MvpdKeywordXlate(i_keyword, l_hbKeyword, l_keyIndex);
    size_t l_fieldLen = io_fieldSize;
    deviceRead(
            i_procTarget.get(),
            i_pBuffer,
            l_fieldLen,
            DEVICE_MVPD_ADDRESS(l_hbRecord, l_hbKeyword));
    io_fieldSize = l_fieldLen;
}

static void IpVpdFacade::fetchData(
    uint64_t i_byteAddr,
    size_t i_numBytes,
    void * o_data,
    TARGETING::Target * i_target,
    const char* i_record )
{
    VPD::RecordTargetPair_t l_recTarg = VPD::makeRecordTargetPair(i_record,i_target);
    // only record SPDX supports overriding
    VPD::OverrideMap_t::iterator l_overrideItr = iv_overridePtr.find(l_recTarg);
    if(l_overrideItr != iv_overridePtr.end())
    {
        if(l_overrideItr->second)
        {
            memcpy(o_data, l_overrideItr->second+i_byteAddr, i_numBytes);
            return;
        }
    }
    else if(0 == memcmp(i_record, "VHDR", 4)
         || 0 == memcmp(i_record, "VTOC", 4))
    {
        iv_overridePtr[l_recTarg] = nullptr;
    }

    fetchDataFromPnor(i_byteAddr, i_numBytes, o_data, i_target);
    // vpd data can also originate from VPD::SEEPROM
    // fetchDataFromEeprom(i_byteAddr, i_numBytes, o_data, i_target, i_args.eepromSource);
}

// return offset where the record is located in o_offset argument
static void IpVpdFacade::findRecordOffsetPnor(
    const char * i_record,
    uint16_t & o_offset,
    TARGETING::Target * i_target,
    VPD::vpdCmdTarget i_location)
{
    uint16_t offset = 0;
    char l_record[RECORD_BYTE_SIZE] = { '\0' };
    // --------------------------------------
    // Start reading at beginning of file
    // First 256 bytes are the TOC
    // --------------------------------------
    // TOC Format is as follows:
    //      8 bytes per entry - 32 entries possible
    //   Entry:
    //      byte 0 - 3: ASCII Record Name
    //      byte 4 - 5: OFFSET (byte swapped)
    //      byte 6 - 7: UNUSED
    // --------------------------------------
    for(uint64_t tmpOffset = 0;
        tmpOffset < IPVPD_TOC_SIZE;
        tmpOffset += RECORD_BYTE_SIZE + RECORD_ADDR_BYTE_SIZE + RECORD_TOC_UNUSED)
    {
        //Read Record Name
        fetchData(tmpOffset, RECORD_BYTE_SIZE, l_record, i_target, i_location, i_record);
        if(!(memcmp(l_record, i_record, RECORD_BYTE_SIZE)))
        {
            fetchData(tmpOffset + RECORD_BYTE_SIZE, RECORD_ADDR_BYTE_SIZE, &offset, i_target, i_location, i_record);
            o_offset = le16toh(offset);
            return;
        }
    }
}

static void CvpdFacade::checkForRecordOverride(
    const char* i_record,
    TARGETING::Target* i_target,
    uint8_t*& o_ptr)
{
    o_ptr = nullptr;
    VPD::RecordTargetPair_t l_recTarg = VPD::makeRecordTargetPair(i_record,i_target);

    if(strcmp(i_record, "SPDX") != 0)
    {
        iv_overridePtr[l_recTarg] = nullptr;
        return;
    }
    getMEMDFromPNOR(i_target);
    o_ptr = iv_overridePtr[l_recTarg];
}

// This function sets up override pointer based on MEMD section.
// Override is supported only for SPDX record
static void IpVpdFacade::getMEMDFromPNOR(TARGETING::Target* i_target)
{
    struct MemdHeader_t
    {
        uint32_t eyecatch;         /* Eyecatch to determine validity. "OKOK" */
        uint32_t header_version;   /* What version of the header this is in */
        uint32_t memd_version;     /* What version of the MEMD this includes */
        uint32_t expected_size_k;  /* Size in thousands (not KB) of each MEMD instance */
        uint16_t expected_num;     /* Number of MEMD instances in this section */
        uint8_t  padding[14];      /* Padding for future changes */
    }__attribute__((packed));

    enum MEMD_valid_constants
    {
        MEMD_VALID_HEADER = 0x4f4b4f4b, // "OKOK"
        MEMD_VALID_HEADER_VERSION = 0x30312e30, // "01.0";
    };
    // Get the Record/keyword names
    const char* l_record = nullptr;
    translateRecord(CVPD::SPDX, l_record);

    const char* l_keyword = nullptr;
    translateKeyword(CVPD::VM, l_keyword);

    VPD::RecordTargetPair_t l_recTarg = VPD::makeRecordTargetPair(l_record, i_target);

#ifdef __HOSTBOOT_RUNTIME
    uint64_t l_memdSize = 0;
    uint64_t l_memd_addr = hb_get_rt_rsvd_mem(Util::HBRT_MEM_LABEL_VPD_MEMD, 0, l_memdSize );
    if(l_memd_addr == 0 || l_memdSize == 0)
    {
        break;
    }
    uint8_t* l_memd_vaddr = l_memd_addr;
#else
    PNOR::SectionInfo_t l_memd_info;
    PNOR::getSectionInfo(PNOR::MEMD,l_memd_info);
    uint8_t* l_memd_vaddr = l_memd_info.vaddr;
#endif

    MemdHeader_t l_header;
    memcpy(&l_header, l_memd_vaddr, sizeof(l_header));
    if(!(l_header.eyecatch == MEMD_VALID_HEADER
      && l_header.header_version == MEMD_VALID_HEADER_VERSION
      && (l_header.memd_version == l_recTarg.first
          || l_header.memd_version == MEMD_VALID_HEADER_VERSION)))
    {
        iv_overridePtr[l_recTarg] = nullptr;
        return;
    }

    size_t l_vm_size = 0;
    input_args_t l_vm_args = { CVPD::SPDX, CVPD::VM, VPD::USEVPD };
    read(i_target, nullptr, l_vm_size, l_vm_args );
    uint32_t l_vm_kw = 0;
    read(i_target, &l_vm_kw, l_vm_size, l_vm_args );
    uint16_t l_memd_offset = sizeof(MemdHeader_t);
    bool l_found_match = false;

    for(auto l_inst = 0; l_inst < l_header.expected_num; ++l_inst)
    {
        iv_overridePtr[l_recTarg] = l_memd_vaddr + l_memd_offset;
        uint32_t l_memd_vm = 0;
        retrieveKeyword(l_keyword, l_record, 0, 0, i_target, &l_memd_vm, l_vm_size, { CVPD::SPDX, CVPD::VM, VPD::USEOVERRIDE });

        if(l_memd_vm != 0
        && l_vm_kw & 0xF00 == l_memd_vm & 0xF00 )
        {
            return;
        }
        l_memd_offset += (l_header.expected_size_k * 1000);
    }

    if(!l_found_match)
    {
        // Is it ok? It sets only last entry
        iv_overridePtr[l_recTarg] = nullptr;
    }
}

uint64_t hb_get_rt_rsvd_mem(
    Util::hbrt_mem_label_t i_label,
    uint32_t i_instance,
    uint64_t & o_size)
{
    uint64_t l_label_data_addr = 0;
    o_size = 0;
    if(g_hostInterfaces != NULL
    && g_hostInterfaces->get_reserved_mem)
    {
        uint64_t hb_data_addr = g_hostInterfaces->get_reserved_mem(HBRT_RSVD_MEM__DATA, i_instance);
        if (0 != hb_data_addr)
        {
            Util::hbrtTableOfContents_t * toc_ptr = hb_data_addr;
            l_label_data_addr = Util::hb_find_rsvd_mem_label(i_label, toc_ptr, o_size);
        }
    }
    return l_label_data_addr;
}

uint64_t hb_find_rsvd_mem_label(
    hbrt_mem_label_t i_label,
    hbrtTableOfContents_t * i_hb_data_toc_addr,
    uint64_t & o_size)
{
    uint64_t l_label_data_addr = 0;
    o_size = 0;
    hbrtTableOfContents_t * toc_ptr = i_hb_data_toc_addr;
    for (uint16_t i = 0; i < toc_ptr->total_entries; i++)
    {
        if (toc_ptr->entry[i].label == i_label)
        {
            l_label_data_addr = i_hb_data_toc_addr + toc_ptr->entry[i].offset;
            o_size = toc_ptr->entry[i].size;
            break;
        }
    }
    return l_label_data_addr;
}

static void IpVpdFacade::translateKeyword(
    VPD::vpdKeyword i_keyword,
    const char *& o_keyword )
{
    keywordInfo tmpKeyword;
    tmpKeyword.keyword = i_keyword;
    o_keyword = std::lower_bound(iv_vpdKeywords, &iv_vpdKeywords[iv_keySize], tmpKeyword, compareKeywords)->keywordName;
}

static void IpVpdFacade::translateRecord(VPD::vpdRecord i_record, const char *& o_record)
{
    recordInfo tmpRecord;
    tmpRecord.record = i_record;
    o_record = std::lower_bound(iv_vpdRecords, &iv_vpdRecords[iv_recSize], tmpRecord, compareRecords)->recordName;
}

static void mvpdRingFuncFindEnd(
    uint8_t** io_pBufLeft,
    uint32_t* io_pBufLenLeft,
    bool*      o_mvpdEnd)
{
    *o_mvpdEnd = false;
    if(*io_pBufLenLeft >= 3
    && be32toh(**io_pBufLeft) & 0xffffff00 == MVPD_END_OF_DATA_MAGIC & 0xffffff00)
    {
        *o_mvpdEnd = true;
        *io_pBufLeft    += 3;
        *io_pBufLenLeft -= 3;
    }
}

static void mvpdRingFuncFindHdr(
    const uint8_t        i_chipletId,
    const RingId_t       i_ringId,
    CompressedScanData** io_pBufLeft,
    uint32_t*            io_pBufLenLeft,
    CompressedScanData** o_pScanData)
{
    CompressedScanData* l_pScanData = *io_pBufLeft;
    *o_pScanData = NULL;

    if (*io_pBufLenLeft < sizeof(CompressedScanData)
    || be16toh(*io_pBufLeft->iv_magic) != RS4_MAGIC)
    {
        return;
    }

    *io_pBufLeft    += be16toh(*io_pBufLeft->iv_size);
    *io_pBufLenLeft -= be16toh(*io_pBufLeft->iv_size);

    uint32_t l_evenOddMask =
        (i_ringId == 0xDD) ? 0x00001000
      : (i_ringId == 0xDE) ? 0x00000400
      : (i_ringId == 0xB9 || i_ringId == 0xDF) ? 0x00000040
      : 0;

    if (be16toh(*io_pBufLeft->iv_ringId) == i_ringId
    && (be32toh(*io_pBufLeft->iv_scanAddr) >> 24) & 0xFF == i_chipletId
    && (l_evenOddMask == 0 || be32toh(*io_pBufLeft->iv_scanAddr) & l_evenOddMask))
    {
        *o_pScanData = *io_pBufLeft;
    }
}
```
