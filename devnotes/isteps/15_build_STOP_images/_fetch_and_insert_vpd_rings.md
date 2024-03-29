```cpp
static void _fetch_and_insert_vpd_rings(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    TorHeader_t*    i_ringSection,
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

    // #G keyword contains Time rings
    // #R keyword contains Repair rings
    mvpdRingFunc(
        procChip,
        "CP00"
        (i_ring.vpdKeyword == VPD_KEYWORD_PDG) ? "#G" : "#R",
        i_chipletId,
        i_ring.ringId,          // looking for a ring with this id (beware magic in mvpdRingFuncFindHdr)
        i_vpdRing,              // ring is retured in this parameter (in a compressed form)
        i_vpdRingSize,
        (uint8_t*)i_ringBuf2);  // just a temporary buffer to use for this function

    if(i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_NEST
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EQ
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EX
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EC)
    {
        memset(i_ringBuf2, 0, i_ringBufSize2);
        memset(i_ringBuf3, 0, i_ringBufSize3);

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
    i_bImgOutOfSpace = i_maxRingSectionSize < (io_ringSectionSize + i_vpdRingSize);
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

// this function puts extracted, compressed ring into a free slot in ringSection
static void tor_append_ring(
    TorHeader_t*    i_ringSection,      // Ring section ptr
    uint32_t&       io_ringSectionSize, // In: Exact size of ring section.
                                        // Out: Updated size of ring section.
    RingId_t        i_ringId,           // Ring ID
    PpeType_t       i_ppeType,          // CME, SGPE
    uint8_t         i_instanceId,       // Instance ID
    CompressedScanData* i_rs4Container) // RS4 ring container
{
    uint32_t l_buf;
    uint32_t l_torOffsetSlot;

    tor_access_ring(
        i_ringSection,
        i_ringId,
        i_ppeType, // CME / SGPE
        i_instanceId,
        &l_buf,
        l_torOffsetSlot,
        PUT_SINGLE_RING);

    if(io_ringSectionSize - l_buf <= MAX_TOR_RING_OFFSET)
    {
        uint16_t l_ringOffset16 = htobe16(io_ringSectionSize - l_buf);
        memcpy(i_ringSection + l_torOffsetSlot, &l_ringOffset16, l_ringOffset16);
        uint32_t l_ringBlockSize = be16toh(i_rs4Container->iv_size);
        memcpy(i_ringSection + io_ringSectionSize, i_rs4Container, l_ringBlockSize);
        io_ringSectionSize += l_ringBlockSize;
        i_ringSection->size = htobe32(be32toh(i_ringSection->size) + l_ringBlockSize);
    }
}

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
    uint32_t&       io_ringBlockSize,  // Size of ring data
    RingBlockType_t i_ringBlockType)   // PUT or GET
{
    uint8_t* postHeaderStart = (uint8_t*)i_ringSection + sizeof(TorHeader_t);

    if (be32toh(i_ringSection->magic) >> 8 == TOR_MAGIC
    && i_ringSection->version <= TOR_VERSION
    && i_ringSection->version != 0
    && i_ringSection->chipType < NUM_CHIP_TYPES)
    {
        get_ring_from_ring_section(
            (be32toh(i_ringSection->magic) == TOR_MAGIC_HW) ?
                postHeaderStart + be32toh((TorPpeBlock_t*)(postHeaderStart + i_ppeType * sizeof(TorPpeBlock_t))->offset)
              : i_ringSection,
            i_ringId,
            io_instanceId,
            &io_ringBlockPtr,
            io_ringBlockSize,
            i_ringBlockType);
    }
}

typedef struct
{
    const char*  ringName;
    RingId_t     ringId;
    uint8_t      instanceIdMin;
    uint8_t      instanceIdMax;
    RingClass_t  ringClass;
    uint32_t     scanScomAddress; // probably used unly with Centaur
} GenRingIdList;

typedef struct
{
    // Chiplet ID of the first instance of the Chiplet
    uint8_t iv_base_chiplet_number;
    // Number of common rings for the Chiplet
    uint8_t iv_num_common_rings;
    // Number of instance rings for the Chiplet (w/different ringId values)
    uint8_t iv_num_instance_rings;
    // Number of instance rings for the Chiplet (w/different ringId values
    // AND different scanAddress values)
    uint8_t iv_num_instance_rings_scan_addrs;
    // Number of variants for common rings (instance rings only have BASE variant)
    uint8_t iv_num_common_ring_variants;
} ChipletData_t;

static void get_ring_from_ring_section(
    TorHeader_t*    i_ringSection,    // Ring section ptr
    RingId_t        i_ringId,         // Ring ID
    uint8_t&        io_instanceId,    // Instance ID
    void**          o_ringBlockPtr,   // Input/Output ring buffer
    uint32_t&       io_ringBlockSize, // if GET_SINGLE_RING:
                                      // input: maximum space available for a ring
                                      // output: size of extracted ring
                                      // if PUT_SINGLE_RING:
                                      // input: nothing
                                      // output: offset to the ring in ringSection
    RingBlockType_t i_ringBlockType)  // PUT_SINGLE_RING or GET_SINGLE_RING
{
    ChipletData_t* cpltData;
    GenRingIdList* ringIdListCommon;
    GenRingIdList* ringIdListInstance;
    RingProperties_t* ringProperties;

    // get some predefined properties based on ringSection type (magic)
    ringid_get_properties(
        be32toh(i_ringSection->magic),
        &cpltData,           // number of common and instance rings are used
        &ringIdListCommon,
        &ringIdListInstance,
        &ringProperties);    // only ringProperties->name is used

    // process ringIdListCommon and ringIdListInstance
    for(uint8_t bInstCase = 0; bInstCase <= 1; bInstCase++)
    {
        // ringIdListCommon in the first iteration
        // ringIdListInstance in the second iteration
        GenRingIdList* ringIdList = !bInstCase ?
            ringIdListCommon
            : ringIdListInstance;

        if(ringIdList)
        {
            TorCpltOffset_t cpltOffset =
                sizeof(TorHeader_t)
                + be32toh(*(uint32_t*)(
                    (void*)i_ringSection
                + sizeof(TorHeader_t)
                + bInstCase * sizeof(TorCpltOffset_t)));

            uint32_t torSlotNum = 0;
            // for each instance of each ring
            // if ring name is as predefined and instanceId is correct or we are itherating over ringIdInstance list
            for(ringInstance = ringIdList->instanceIdMin; ringInstance <= ringIdList->instanceIdMax; ringInstance++)
            for(ringIndex = 0; ringIndex < bInstCase ? cpltData->iv_num_instance_rings : cpltData->iv_num_common_rings; ringIndex++)
            if(strcmp(ringIdList[ringIndex].ringName, ringProperties[i_ringId].iv_name) == 0
            && (ringInstance == io_instanceId || bInstCase == 0))
            {
                TorRingOffset_t ringOffset = be16toh(*(TorRingOffset_t*)(
                    i_ringSection
                    + torSlotNum
                    * sizeof(TorRingOffset_t)));
                uint32_t ringSize = be16toh(((CompressedScanData*)((uint8_t*)i_ringSection + ringOffset + cpltOffset))->iv_size);
                if (i_ringBlockType == GET_SINGLE_RING && ringOffset)
                {
                    if(io_ringBlockSize && io_ringBlockSize >= ringSize)
                    {
                        memcpy(*o_ringBlockPtr, (uint8_t*)i_ringSection + ringOffset + cpltOffset, ringSize);
                        io_instanceId = bInstCase ?
                            io_instanceId
                            : ringIdList[ringIndex].instanceIdMin;
                    }
                    io_ringBlockSize = ringSize;
                }
                else if (i_ringBlockType == PUT_SINGLE_RING && !ringOffset)
                {
                    *o_ringBlockPtr = cpltOffset;
                    io_ringBlockSize = cpltOffset + torSlotNum * sizeof(TorRingOffset_t);
                }
                return;
            }
            torSlotNum++;
        }
    }
}

typedef struct
{
    uint32_t   magic;       // =TOR_MAGIC_xyz
    uint8_t    version;     // =TOR version
    ChipType_t chipType;    // Value from ChipType enum
    uint8_t    ddLevel;     // Actual DD level of ringSection
    uint8_t    undefined;
    uint32_t   size;        // Size of ringSection.
} TorHeader_t;

enum TorMagicNum
{
    TOR_MAGIC      = 0x544F52,   // "TOR"
    TOR_MAGIC_HW   = 0x544F5248, // "TORH"
    TOR_MAGIC_SBE  = 0x544F5242, // "TORB"
    TOR_MAGIC_SGPE = 0x544F5247, // "TORG"
    TOR_MAGIC_CME  = 0x544F524D, // "TORM"
    TOR_MAGIC_OVRD = 0x544F5252, // "TORR"
    TOR_MAGIC_OVLY = 0x544F524C, // "TORL"
};

static void ringid_get_properties(
    uint32_t           i_torMagic,
    ChipletData_t**    o_chipletData,
    GenRingIdList**    o_ringIdListCommon,
    GenRingIdList**    o_ringIdListInstance,
    RingProperties_t** o_ringProps)
{
    if(i_torMagic == TOR_MAGIC_CME)
    {
        *o_chipletData        = (ChipletData_t*)&P9_RID::EC::g_chipletData;
        *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_COMMON;
        *o_ringIdListInstance = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_INSTANCE;
    }
    else if(i_torMagic == TOR_MAGIC_SGPE)
    {
        *o_chipletData        = (ChipletData_t*)&P9_RID::EQ::g_chipletData;
        *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_COMMON;
        *o_ringIdListInstance = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_INSTANCE;
    }
    *o_ringProps = (RingProperties_t*)P9_RID::RING_PROPERTIES;
}

void P9_RID::ringid_get_chiplet_properties(
    ChipletType_t      i_chipletType,
    ChipletData_t**    o_cpltData,
    GenRingIdList**    o_ringComm,
    GenRingIdList**    o_ringInst)
{
    switch (i_chipletType)
    {
        case PERV_TYPE :
            *o_cpltData = (ChipletData_t*)&PERV::g_chipletData;
            *o_ringComm = (GenRingIdList*)PERV::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)PERV::RING_ID_LIST_INSTANCE;
            break;

        case N0_TYPE :
            *o_cpltData = (ChipletData_t*)&N0::g_chipletData;
            *o_ringComm = (GenRingIdList*)N0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)N0::RING_ID_LIST_INSTANCE;
            break;

        case N1_TYPE :
            *o_cpltData = (ChipletData_t*)&N1::g_chipletData;
            *o_ringComm = (GenRingIdList*)N1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)N1::RING_ID_LIST_INSTANCE;
            break;

        case N2_TYPE :
            *o_cpltData = (ChipletData_t*)&N2::g_chipletData;
            *o_ringComm = (GenRingIdList*)N2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)N2::RING_ID_LIST_INSTANCE;
            break;

        case N3_TYPE :
            *o_cpltData = (ChipletData_t*)&N3::g_chipletData;
            *o_ringComm = (GenRingIdList*)N3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)N3::RING_ID_LIST_INSTANCE;
            break;

        case XB_TYPE :
            *o_cpltData = (ChipletData_t*)&XB::g_chipletData;
            *o_ringComm = (GenRingIdList*)XB::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)XB::RING_ID_LIST_INSTANCE;
            break;

        case MC_TYPE :
            *o_cpltData = (ChipletData_t*)&MC::g_chipletData;
            *o_ringComm = (GenRingIdList*)MC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)MC::RING_ID_LIST_INSTANCE;
            break;

        case OB0_TYPE :
            *o_cpltData = (ChipletData_t*)&OB0::g_chipletData;
            *o_ringComm = (GenRingIdList*)OB0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)OB0::RING_ID_LIST_INSTANCE;
            break;

        case OB1_TYPE :
            *o_cpltData = (ChipletData_t*)&OB1::g_chipletData;
            *o_ringComm = (GenRingIdList*)OB1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)OB1::RING_ID_LIST_INSTANCE;
            break;

        case OB2_TYPE :
            *o_cpltData = (ChipletData_t*)&OB2::g_chipletData;
            *o_ringComm = (GenRingIdList*)OB2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)OB2::RING_ID_LIST_INSTANCE;
            break;

        case OB3_TYPE :
            *o_cpltData = (ChipletData_t*)&OB3::g_chipletData;
            *o_ringComm = (GenRingIdList*)OB3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)OB3::RING_ID_LIST_INSTANCE;
            break;

        case PCI0_TYPE :
            *o_cpltData = (ChipletData_t*)&PCI0::g_chipletData;
            *o_ringComm = (GenRingIdList*)PCI0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)PCI0::RING_ID_LIST_INSTANCE;
            break;

        case PCI1_TYPE :
            *o_cpltData = (ChipletData_t*)&PCI1::g_chipletData;
            *o_ringComm = (GenRingIdList*)PCI1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)PCI1::RING_ID_LIST_INSTANCE;
            break;

        case PCI2_TYPE :
            *o_cpltData = (ChipletData_t*)&PCI2::g_chipletData;
            *o_ringComm = (GenRingIdList*)PCI2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)PCI2::RING_ID_LIST_INSTANCE;
            break;

        case EQ_TYPE :
            *o_cpltData = (ChipletData_t*)&EQ::g_chipletData;
            *o_ringComm = (GenRingIdList*)EQ::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)EQ::RING_ID_LIST_INSTANCE;
            break;

        case EC_TYPE :
            *o_cpltData = (ChipletData_t*)&EC::g_chipletData;
            *o_ringComm = (GenRingIdList*)EC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)EC::RING_ID_LIST_INSTANCE;
            break;

        default :
            *o_cpltData = NULL;
            *o_ringComm = NULL;
            *o_ringInst = NULL;
            break;
    }
}

static void ringid_get_noof_chiplets(
    uint32_t    i_torMagic,
    uint8_t*    o_numChiplets)
{
    if (i_torMagic == TOR_MAGIC_CME)
    {
        *o_numChiplets = P9_RID::CME_NOOF_CHIPLETS;
    }
    else if (i_torMagic == TOR_MAGIC_SGPE)
    {
        *o_numChiplets = P9_RID::SGPE_NOOF_CHIPLETS;
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
    void*               i_overlaysSection,
    uint8_t             i_ddLevel,
    CompressedScanData* io_vpdRing,
    void*               l_overlayRs4Ring, // zeroed
    void*               l_overlayRawRing) // zeroed
{
    get_overlays_ring(
        i_overlaysSection,
        i_ddLevel,
        (RingId_t)be16toh(io_vpdRing->iv_ringId),
        &l_overlayRs4Ring,
        &l_overlayRawRing);

    apply_overlays_ring(
        io_vpdRing,
        l_overlayRs4Ring,
        l_overlayRawRing);
}

void get_overlays_ring(
    void*     i_overlaysSection,
    uint8_t   i_ddLevel,
    RingId_t  i_ringId,
    void**    io_compressedRingData,
    void**    io_decompressedRingData)
{
    uint8_t  l_instanceId = 0;
    uint32_t l_ringBlockSize = 0xFFFFFFFF;
    tor_access_ring(
        i_overlaysSection,
        i_ringId,
        UNDEFINED_PPE_TYPE,
        l_instanceId,
        io_compressedRingData,
        l_ringBlockSize,
        GET_SINGLE_RING);

    uint32_t l_ovlyUncmpSize;
    _rs4_decompress(
        *io_decompressedRingData,
        *io_decompressedRingData + MAX_RING_BUF_SIZE / 2,
        MAX_RING_BUF_SIZE / 2,
        &l_ovlyUncmpSize,
        (CompressedScanData*)(*io_compressedRingData));
}

// This function decompreses data, applies a mask and compresses it back
void apply_overlays_ring(
    CompressedScanData* io_vpdRing,
    void*               io_overlayRs4Ring,
    uint8_t*            i_overlayRawRing)
{
    uint8_t* dataVpd = (uint8_t*)io_overlayRs4Ring;
    uint8_t* careVpd = (uint8_t*)io_overlayRs4Ring + MAX_RING_BUF_SIZE / 2;
    uint8_t* careOvly = i_overlayRawRing + MAX_RING_BUF_SIZE / 2;
    uint32_t vpdUncmpSize;

    _rs4_decompress(
        dataVpd,
        careVpd,
        MAX_RING_BUF_SIZE / 2,
        &vpdUncmpSize, // returns length of decompressed data in bits
        io_vpdRing);

    // Algorithm bellow copies bits from i_overlayRawRing
    // into both dataVpd and careVpd but
    // only if bit at the same index in careOvly is set
    for(uint32_t bitIndex = 0; bitIndex < vpdUncmpSize; ++bitIndex)
    {
        int byteIndex = bitIndex / 8;
        int bitInByte = bitIndex % 8;
        if (careOvly[byteIndex] & (0x80 >> bitInByte))
        {
            if (i_overlayRawRing[byteIndex] & (0x80 >> bitInByte))
            {
                dataVpd[byteIndex] |= (0x80 >> bitInByte);
                careVpd[byteIndex] |= (0x80 >> bitInByte);
            }
            else
            {
                dataVpd[byteIndex] &= ~(0x80 >> bitInByte);
                careVpd[byteIndex] &= ~(0x80 >> bitInByte);
            }
        }
    }

    _rs4_compress(
        io_vpdRing,
        MAX_RING_BUF_SIZE,
        dataVpd,
        careVpd,
        vpdUncmpSize,
        be32toh(io_vpdRing->iv_scanAddr),
        be16toh(io_vpdRing->iv_ringId));
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

// This function extract Ring data from particular keyword in a given Record
static void mvpdRingFunc(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    fapi2::MvpdRecord   i_record,
    fapi2::MvpdKeyword  i_keyword,
    const uint8_t       i_chipletId,
    const RingId_t      i_ringId,
    CompressedScanData* o_pRingBuf,
    uint32_t&           o_rRingBufsize,
    uint8_t*            i_recordBuf)
{
    uint32_t l_recordLen  = 0;

    // Will call IpVpdFacade::read
    Singleton<MvpdFacade>::instance().read(
        i_record,
        i_keyword,
        procChip.get(),
        i_recordBuf,
        l_recordLen);

    uint8_t* l_pRing = NULL;
    uint32_t l_ringLen = 0;

    mvpdRingFuncFind(
        i_chipletId,
        i_ringId,
        i_recordBuf,
        l_recordLen,
        l_pRing,
        l_ringLen);

    if(l_ringLen && o_pRingBuf)
    {
        o_rRingBufsize = l_ringLen;
        memcpy(o_pRingBuf, l_pRing, l_ringLen);
    }
}

// this function tries to find correct mvpd ring in given keyword buffer
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

typedef uint16_t  RingId_t;

typedef struct
{
    uint16_t iv_magic;
    uint8_t  iv_version;
    uint8_t  iv_type;
    uint16_t iv_size;
    RingId_t iv_ringId;
    uint32_t iv_scanAddr;
} CompressedScanData;

// This function seeks for a correct Ring by id (doing some magic with id)
// and if found, copies it to the output buffer
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

// Check if rings end here. End is marked by an "END" string
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

static void IpVpdFacade::read(
    fapi2::MvpdRecord   i_record,
    fapi2::MvpdKeyword  i_keyword,
    TARGETING::Target * i_target,
    void* o_buffer,
    size_t & io_buflen)
{
    uint16_t recordOffset;
    findRecordOffsetPnor(i_record, recordOffset, i_target, VPD::AUTOSELECT);
    // vpd data can also originate from VPD::SEEPROM
    // findRecordOffsetSeeprom(i_record, o_offset, o_length, i_target, i_args);
    retrieveKeyword(i_keyword, i_record, recordOffset, 0, i_target, o_buffer, io_buflen);
}


static void IpVpdFacade::retrieveKeyword(
    const char * i_keywordName,
    const char * i_recordName,
    uint16_t i_offset,
    uint16_t i_index,
    TARGETING::Target * i_target,
    void * o_buffer,
    size_t & io_buflen)
{
    uint64_t byteAddr;
    findKeywordAddr(i_keywordName, i_recordName, i_offset, i_index, i_target, io_buflen, byteAddr);
    if(NULL == o_buffer)
    {
        return;
    }
    fetchData(i_offset + byteAddr, io_buflen, o_buffer, i_target, i_recordName );
}

// RECORD_BYTE_SIZE        = 4,
// RECORD_ADDR_BYTE_SIZE   = 2,
// KEYWORD_BYTE_SIZE       = 2,
// KEYWORD_SIZE_BYTE_SIZE  = 1,
// RECORD_TOC_UNUSED       = 2,
// RT_SKIP_BYTES           = 3,

// This function looks for an address of a particular keyword
static void IpVpdFacade::findKeywordAddr(
    const char * i_keywordName,
    const char * i_recordName,
    uint16_t i_offset,
    uint16_t i_index,
    TARGETING::Target * i_target,
    size_t& o_keywordSize,
    uint64_t& o_byteAddr)
{
    uint16_t offset = i_offset;
    uint16_t recordSize = 0;

    fetchData(offset, RECORD_ADDR_BYTE_SIZE, &recordSize, i_target, VPD::AUTOSELECT, i_recordName);
    offset += RECORD_ADDR_BYTE_SIZE + RT_SKIP_BYTES; // 5 bytes in total - skip over to the keyword name

    // RT keyword is always first and holds record name
    if(memcmp(i_keywordName, "RT", KEYWORD_BYTE_SIZE) == 0 && i_index == 0)
    {
        o_keywordSize = RECORD_BYTE_SIZE;
        o_byteAddr = offset - i_offset;
        return;
    }
    offset += RECORD_BYTE_SIZE;
    int matchesFound = 0;
    while(offset < le16toh(recordSize) + i_offset + RECORD_ADDR_BYTE_SIZE)
    {
        char keyword[KEYWORD_BYTE_SIZE] = { '\0' };
        fetchData(offset, KEYWORD_BYTE_SIZE, keyword, i_target, VPD::AUTOSELECT, i_recordName );
        offset += KEYWORD_BYTE_SIZE;

        uint32_t keywordLength = KEYWORD_SIZE_BYTE_SIZE;
        bool isPoundKwd = false;
        if(keyword[0] == '#')
        {
            isPoundKwd = true;
            keywordLength++;
        }

        uint16_t keywordSize = 0;
        fetchData(offset, keywordLength, &keywordSize, i_target, VPD::AUTOSELECT, i_recordName);
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
```
