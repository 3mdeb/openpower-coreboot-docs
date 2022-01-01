# SBE IO

SBE can be interacted with using SBE IO/FIFO. Among other operations it
provides GETSCOM, PUTSCOM, MODIFY and PUT_UNDER_MASK, which are referred to as
SBE SCOM.

SCOM addresses of a slave CPU with less than 32 bits can also be accessed via
FSI SCOM, but full range (64-bits) is needed during initialization and SBE SCOM
is the only way of getting to them until XSCOM is up (i.e., before SMP is on).

SBE interactions are carried out over FSI.

# Analysis assumptions

* Interested only in GETSCOM and PUTSCOM.
* `#undef TOLERATE_BLACKLIST_ERRS`

# Relevant sources in Hostboot

* `src/usr/sbeio/` directory

# Analysis

## `src/include/usr/sbeio/sbe_sp_intf.H`

```cpp
/**
 * @brief enums for primary SBE response
 */
enum sbePrimResponse
{
    SBE_PRI_OPERATION_SUCCESSFUL        = 0x00,
    SBE_PRI_INVALID_COMMAND             = 0x01,
    SBE_PRI_INVALID_DATA                = 0x02,
    SBE_PRI_USER_ERROR                  = 0x03,
    SBE_PRI_INTERNAL_ERROR              = 0x04,
    SBE_PRI_UNSECURE_ACCESS_DENIED      = 0x05,
    SBE_PRI_FFDC_ERROR                  = 0x40, /* FFDC Package Present */
    SBE_PRI_GENERIC_EXECUTION_FAILURE   = 0xFE,
};

/**
 * @brief enums for secondary SBE response
 *   @TODO via RTC: 129763
 *         Discuss on SBE_SEC_INVALID_TARGET_ID_PASSED
 *
*/
enum sbeSecondaryResponse
{
    SBE_SEC_OPERATION_SUCCESSFUL              = 0x00,
    SBE_SEC_COMMAND_CLASS_NOT_SUPPORTED       = 0x01,
    SBE_SEC_COMMAND_NOT_SUPPORTED             = 0x02,
    SBE_SEC_INVALID_ADDRESS_PASSED            = 0x03,
    SBE_SEC_INVALID_TARGET_TYPE_PASSED        = 0x04,
    SBE_SEC_INVALID_CHIPLET_ID_PASSED         = 0x05,
    SBE_SEC_SPECIFIED_TARGET_NOT_PRESENT      = 0x06,
    SBE_SEC_SPECIFIED_TARGET_NOT_FUNCTIONAL   = 0x07,
    SBE_SEC_COMMAND_NOT_ALLOWED_IN_THIS_STATE = 0x08,
    SBE_SEC_FUNCTIONALITY_NOT_SUPPORTED       = 0x09,
    SBE_SEC_GENERIC_FAILURE_IN_EXECUTION      = 0x0A,
    SBE_SEC_BLACKLISTED_REG_ACCESS            = 0x0B,
    SBE_SEC_OS_FAILURE                        = 0x0C,
    SBE_SEC_FIFO_ACCESS_FAILURE               = 0x0D,
    SBE_SEC_UNEXPECTED_EOT_INSUFFICIENT_DATA  = 0x0E,
    SBE_SEC_UNEXPECTED_EOT_EXCESS_DATA        = 0x0F,
    SBE_SEC_HW_OP_TIMEOUT                     = 0x10,
    SBE_SEC_PCB_PIB_ERR                       = 0x11,
    SBE_SEC_FIFO_PARITY_ERROR                 = 0x12,
    SBE_SEC_TIMER_ALREADY_STARTED             = 0x13,
    SBE_SEC_BLACKLISTED_MEM_ACCESS            = 0x14,
    SBE_SEC_MEM_REGION_NOT_FOUND              = 0x15,
    SBE_SEC_MAXIMUM_MEM_REGION_EXCEEDED       = 0x16,
    SBE_SEC_MEM_REGION_AMEND_ATTEMPTED        = 0x17,
    SBE_SEC_INPUT_BUFFER_OVERFLOW             = 0x18,
};
```

## `src/usr/sbeio/sbe_fifodd.H`

```cpp
/**
 * @brief enums for SBE command class
 */
enum fifoCommandClass
{
    // ...
    SBE_FIFO_CLASS_SCOM_ACCESS         = 0xA2,
    // ...
};

/**
 * @brief SBE FIFO FSI register addresses
 */
enum fifoRegisterAddress
{
    SBE_FIFO_UPFIFO_DATA_IN    = 0x00002400,
    SBE_FIFO_UPFIFO_STATUS     = 0x00002404,
    SBE_FIFO_UPFIFO_SIG_EOT    = 0x00002408,
    SBE_FIFO_UPFIFO_REQ_RESET  = 0x0000240C,
    SBE_FIFO_DNFIFO_DATA_OUT   = 0x00002440,
    SBE_FIFO_DNFIFO_STATUS     = 0x00002444,
    SBE_FIFO_DNFIFO_RESET      = 0x00002450,
    SBE_FIFO_DNFIFO_ACK_EOT    = 0x00002454,
    SBE_FIFO_DNFIFO_MAX_TSFR   = 0x00002458,
};

enum sbeFifoUpstreamStatus
{
    // ...
    // Upstream FIFO is full
    UPFIFO_STATUS_FIFO_FULL =0x00200000,
    // ...
};

enum sbeUpstreamEot
{
    FSB_UPFIFO_SIG_EOT =0x80000000,
};

enum sbeFifoDownstreamStatus
{
    // Reserved
    DNFIFO_STATUS_RESERVED0 =0xC0000000,
    // Parity Error: dequeuing operation has detected a data parity error
    DNFIFO_STATUS_PARITY_ERROR =0x20000000,
    // Reserved
    DNFIFO_STATUS_RESERVED3 =0x1C000000,
    // SBE is requesting a FIFO reset
    DNFIFO_STATUS_REQ_RESET_FR_SP =0x02000000,
    // Service Processor (SP) is requesting a FIFO reset
    // through downstream path
    DNFIFO_STATUS_REQ_RESET_FR_SBE =0x01000000,
    // A fifo entry has been dequeued with set EOT flag
    DNFIFO_STATUS_DEQUEUED_EOT_FLAG =0x00800000,
    // Reserved
    DNFIFO_STATUS_RESERVED9 =0x00400000,
    // Downstream FIFO is full
    DNFIFO_STATUS_FIFO_FULL =0x00200000,
    // Downstream FIFO is empty
    DNFIFO_STATUS_FIFO_EMPTY =0x00100000,
    // Number of currently hold entries
    DNFIFO_STATUS_FIFO_ENTRY_COUNT =0x000F0000,
    // Valid flags of ALL currently hold entries
    DNFIFO_STATUS_FIFO_VALID_FLAGS =0x0000FF00,
    // EOT flags of ALL currently hold entries
    DNFIFO_STATUS_FIFO_EOT_FLAGS =0x000000FF,
};

/**
 * @brief enums for FIFO SCOM Access Messages
 */
enum fifoScomAccessMessage
{
    SBE_FIFO_CMD_GET_SCOM            = 0x01,
    SBE_FIFO_CMD_PUT_SCOM            = 0x02,
    SBE_FIFO_CMD_MODIFY_SCOM         = 0x03,
    SBE_FIFO_CMD_PUT_SCOM_UNDER_MASK = 0x04,
};

enum { MAX_UP_FIFO_TIMEOUT_NS = 90000*NS_PER_MSEC }; //=90s

/**
 * @brief Struct for FIFO Get SCOM request
 *
 */
struct fifoGetScomRequest
{
    uint32_t wordCnt;
    uint16_t reserved;
    uint8_t  commandClass;
    uint8_t  command;
    uint64_t address;  // Register Address (0..31) + (32..63)
    fifoGetScomRequest() : wordCnt(4), reserved(0)
    { }
} PACKED;

/**
 * @brief Struct for FIFO Put SCOM request
 *
 */
struct fifoPutScomRequest
{
    uint32_t wordCnt;
    uint16_t reserved;
    uint8_t  commandClass;
    uint8_t  command;
    uint64_t address;     // Register Address (0..31) + (32..63)
    uint64_t data;        // Data (0..31) + (32..63)
    fifoPutScomRequest() : wordCnt(6), reserved(0), data(0)
    { }
} PACKED;

/**
 * @brief Struct for FIFO Get SCOM response
 *
 * The actual number of returned words varies based on whether there was
 * an error.
 */
struct fifoGetScomResponse
{
    uint64_t     data;          // Data (0..31) + (32..63)
    statusHeader status;
    struct       fapi2::ffdc_struct ffdc;  // ffdc data
    uint32_t     status_distance; // distance to status
    fifoGetScomResponse() {}
} PACKED;

/**
 * @brief Struct for FIFO Put SCOM and Put SCOM under mask response
 *
 * The actual number of returned words varies based on whether there was
 * an error.
 */
struct fifoPutScomResponse
{
    statusHeader status;
    struct       fapi2::ffdc_struct ffdc;  // ffdc data
    uint32_t     status_distance; // distance to status
    fifoPutScomResponse() {}
} PACKED;

/**
 * @brief Struct for FIFO status
 *
 */
struct statusHeader
{
    uint16_t  magic;     // set to 0xC0DE
    uint8_t   commandClass;
    uint8_t   command;
    uint16_t  primaryStatus;
    uint16_t  secondaryStatus;
} PACKED;

```

## `src/usr/sbeio/sbe_scomAccess.C`

```cpp
// Get SCOM via SBE FIFO
errlHndl_t getFifoScom(TARGETING::Target * i_target, uint64_t i_addr, uint64_t & o_data)
{
    errlHndl_t errl = NULL;

    do
    {
        // set up FIFO request message
        SbeFifo::fifoGetScomRequest  l_fifoRequest;
        SbeFifo::fifoGetScomResponse l_fifoResponse;
        l_fifoRequest.commandClass = SbeFifo::SBE_FIFO_CLASS_SCOM_ACCESS;
        l_fifoRequest.command = SbeFifo::SBE_FIFO_CMD_GET_SCOM;
        l_fifoRequest.address = i_addr;

        errl = SbeFifo::getTheInstance().performFifoChipOp(i_target,
                                                           (uint32_t *)&l_fifoRequest,
                                                           (uint32_t *)&l_fifoResponse,
                                                           sizeof(SbeFifo::fifoGetScomResponse));
        //always return data even if there is an error
        o_data = l_fifoResponse.data;
    }
    while (0);

    return errl;
};

// Put SCOM via SBE FIFO
errlHndl_t putFifoScom(TARGETING::Target * i_target, uint64_t i_addr, uint64_t i_data)
{
    errlHndl_t errl = NULL;

    // set up FIFO request message
    SbeFifo::fifoPutScomRequest  l_fifoRequest;
    SbeFifo::fifoPutScomResponse l_fifoResponse;
    l_fifoRequest.commandClass = SbeFifo::SBE_FIFO_CLASS_SCOM_ACCESS;
    l_fifoRequest.command = SbeFifo::SBE_FIFO_CMD_PUT_SCOM;
    l_fifoRequest.address = i_addr;
    l_fifoRequest.data    = i_data;

    errl = SbeFifo::getTheInstance().performFifoChipOp(i_target,
                                                       (uint32_t *)&l_fifoRequest,
                                                       (uint32_t *)&l_fifoResponse,
                                                       sizeof(SbeFifo::fifoPutScomResponse));

    return errl;
};
```

## `src/usr/sbeio/sbe_fifodd.C`

```cpp
// These functions are used by the code below, but aren't reproduced here. //

/**
 * @brief write FSI
 */
errlHndl_t SbeFifo::writeFsi(TARGETING::Target * i_target,
                     uint64_t   i_addr,
                     uint32_t * i_pData);

/**
 * @Brief perform SBE FIFO chip-op
 *
 * @param[in]  i_target        Target to access
 * @param[in]  i_pFifoRequest  Pointer to request
 * @param[in]  i_pFifoResponse Pointer to response
 * @param[in]  i_responseSize  Size of response in bytes
 *
 * @return errlHndl_t Error log handle on failure.
 */
errlHndl_t SbeFifo::performFifoChipOp(TARGETING::Target * i_target,
                                      uint32_t * i_pFifoRequest,
                                      uint32_t * i_pFifoResponse,
                                      uint32_t i_responseSize)
{
    errl = writeRequest(i_target, i_pFifoRequest);
    if (errl) return errl;

    errl = readResponse(i_target, i_pFifoRequest, i_pFifoResponse, i_responseSize);
    if (errl) return errl;
}

/**
 * @brief send a request via SBE FIFO
 *
 * @param[in]  i_target         Target to access
 * @param[in]  i_pFifoRequest   Pointer to FIFO request.
 *                              First word has count of unit32_t words.
 * @return errlHndl_t Error log handle on failure.
 */
errlHndl_t writeRequest(TARGETING::Target * i_target, uint32_t * i_pFifoRequest)
{
    errlHndl_t errl = NULL;

    // Ensure Downstream Max Transfer Counter is 0 since
    // hostboot has no need for it (non-0 can cause
    // protocol issues)
    uint32_t l_data       = 0;
    errl = writeFsi(i_target,SBE_FIFO_DNFIFO_MAX_TSFR,&l_data);
    if (errl) return errl;

    //The first uint32_t has the number of uint32_t words in the request
    uint32_t * l_pSent    = i_pFifoRequest; //advance as words sent
    uint32_t   l_cnt      = *l_pSent;
    for (uint32_t i=0;i<l_cnt;i++)
    {
        // Wait for room to write into fifo
        errl = waitUpFifoReady(i_target);
        if (errl) return errl;

        // Send data into fifo
        errl = writeFsi(i_target,SBE_FIFO_UPFIFO_DATA_IN,l_pSent);
        if (errl) return errl;

        l_pSent++;
    }

    //notify SBE that last word has been sent
    errl = waitUpFifoReady(i_target);
    if (errl) return errl;

    l_data = FSB_UPFIFO_SIG_EOT;
    errl = writeFsi(i_target,SBE_FIFO_UPFIFO_SIG_EOT,&l_data);
    if (errl) return errl;
}

/**
 * @brief poll until upstream Fifo has room to write into
 *
 * @param[in]  i_target         Target to access
 * @return errlHndl_t Error log handle on failure.
 */
errlHndl_t waitUpFifoReady(TARGETING::Target * i_target)
{
    uint64_t l_elapsed_time_ns = 0;

    while (true)
    {
        // read upstream status to see if room for more data
        uint32_t l_data = 0;
        errlHndl_t errl = readFsi(i_target,SBE_FIFO_UPFIFO_STATUS,&l_data);
        if (errl) return errl;

        if (!(l_data & UPFIFO_STATUS_FIFO_FULL))
            break;

        // time out if wait too long
        if (l_elapsed_time_ns >= MAX_UP_FIFO_TIMEOUT_NS)
            die("waitUpFifoReady: timeout waiting for upstream FIFO to be not full");

        // try later
        nanosleep( 0, 10000 ); //sleep for 10,000 ns
        l_elapsed_time_ns += 10000;
    }

    return NULL;
}

/**
 * @brief read the response via SBE FIFO
 *
 * @param[in]  i_target         Target to access
 * @param[in]  i_pFifoRequest   Pointer to FIFO request.
 * @param[out] o_pFifoResponse  Pointer to FIFO response.
 * @param[in]  i_responseSize   Size of response buffer in bytes.
 * @return errlHndl_t Error log handle on failure.
 */
errlHndl_t readResponse(TARGETING::Target * i_target,
                        uint32_t * i_pFifoRequest,
                        uint32_t * o_pFifoResponse,
                        uint32_t   i_responseSize)
{
    auto l_pFifoRequest = reinterpret_cast<SbeFifo::fifoGetSbeFfdcRequest *>(i_pFifoRequest);
    SbeFifoRespBuffer l_fifoBuffer(o_pFifoResponse, i_responseSize/sizeof(uint32_t));

    errlHndl_t errl = NULL;

    // EOT is expected before the response buffer is full. Room for
    // the PCBPIB status or FFDC is included, but is only returned
    // if there is an error. The last received word has the distance
    // to the status, which is placed at the end of the returned data
    // in order to reflect errors during transfer.

    bool l_EOT = false;

    while (l_fifoBuffer) // keep reading data until an error or until the
                         // message is completely read.
    {
        // Wait for data to be ready to receive (download) or if the EOT
        // has been sent. If not EOT, then data ready to receive.
        uint32_t l_status = 0;
        errl = waitDnFifoReady(i_target, l_status);
        if (errl) return errl;

        if (l_status & DNFIFO_STATUS_DEQUEUED_EOT_FLAG)
        {
            l_EOT = true;
            l_fifoBuffer.completeMessage();
        }
        else
        {
            uint32_t l_data = 0;
            // read next word
            errl = readFsi(i_target,SBE_FIFO_DNFIFO_DATA_OUT,&l_data);
            if (errl) return errl;

            l_fifoBuffer.append(l_data);
        }
    }

    // EOT is expected before running out of response buffer
    if (!l_EOT)
        die("EOT not received before downstream buffer full.");

    //notify that EOT has been received
    uint32_t l_eotSig = FSB_UPFIFO_SIG_EOT;
    errl = writeFsi(i_target,SBE_FIFO_DNFIFO_ACK_EOT,&l_eotSig);
    if (errl) return errl;

    // Determine if successful.
    if (!l_fifoBuffer.getStatus())
        die("The distance to the status header is not within the response buffer.");

    // Check status for success.
    const statusHeader * l_pStatusHeader = l_fifoBuffer.getStatusHeader();
    if (l_pStatusHeader->magic != FIFO_STATUS_MAGIC ||
        l_pStatusHeader->primaryStatus != SBE_PRI_OPERATION_SUCCESSFUL ||
        l_pStatusHeader->secondaryStatus != SBE_SEC_OPERATION_SUCCESSFUL)
    {
        // Status header does not start with magic number or
        // non-zero primary or secondary status
        die("here");
    }

    return NULL;
}

/**
 * @brief poll until downstream Fifo has a value to read or has hit EOT.
 *
 * @param[in]  i_target         Target to access
 * @param[out] o_status         Down load door bell status
 * @return errlHndl_t Error log handle on failure.
 */
errlHndl_t waitDnFifoReady(TARGETING::Target * i_target, uint32_t & o_status)
{
    uint64_t l_elapsed_time_ns = 0;

    while (true)
    {
        // read downstream status to see if data ready to be read
        // or if has hit the EOT
        errlHndl_t errl = readFsi(i_target, SBE_FIFO_DNFIFO_STATUS,&o_status);
        if (errl) return errl;

        if (!(o_status & DNFIFO_STATUS_FIFO_EMPTY) ||
            (o_status & DNFIFO_STATUS_DEQUEUED_EOT_FLAG) )
        {
            break;
        }

        // time out if wait too long
        if (l_elapsed_time_ns >= MAX_UP_FIFO_TIMEOUT_NS)
            die("timeout waiting for downstream FIFO to be not empty");

        // try later
        nanosleep( 0, 10000 ); //sleep for 10,000 ns
        l_elapsed_time_ns += 10000;
    }

    return NULL;
}
```

## `src/usr/sbeio/sbe_fifo_buffer.C`

Includes declarations from `src/usr/sbeio/sbe_fifo_buffer.H`.

```cpp
/**
 * @brief A class for managing duel input buffers for sbeio fifo response
 *        messaging.
 *
 * Sbeio messaging uses a pair of buffers for parsing fifo response
 * messages. The first buffer is a caller supplied buffer that must be large
 * enough to hold required data for a given message response. For example,
 * a read command will have a response that will contain the data read and a
 * status header. A write command will have a response that will contain
 * only a status header detailing the result of the command. In addition,
 * response data can include FFDC data upon an error. The user supplied
 * buffer does not need to be big enough to hold the extra data since this
 * information is processed by the messaging code before returning to the
 * caller. As a result, a second buffer which should be large enough to
 * to contain any FFDC information is processed in parallel with the caller
 * supplied buffer in order to capture a full response.
 */
class SbeFifoRespBuffer
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] i_fifoBuffer -   The caller supplied buffer.
     * @param[in] bufferWordSize - The size of the caller supplied buffer in
     *                             uint32_t units.
     * @param[in] i_getSbeFfdcFmt - A bool indicating if this is for a get SBE
     *                              FFDC request with a special buffer format.
     */
    explicit SbeFifoRespBuffer(uint32_t * i_fifoBuffer, size_t bufferWordSize,
                               bool i_getSbeFfdcFmt = false);

    /**
     * @brief append a uint32 to the next buffer insert position.
     *
     * @param[in] i_value - The value to add to the buffers.
     *
     * @return True if the value was able to be stored in at least the local
     *         buffer, false otherwise.
     */
    bool append(uint32_t i_value);

    /**
     * @brief When the DN FIFO Dequeued EOT flag is detected
     *        externally, this method is called to validate
     *        the buffer data and set indexes to the status
     *        and ffdc areas.
     */
    void completeMessage();

    /**
     * @brief A simplified state accessor.
     *
     * @return True if the state is MSG_COMPLETE or MSG_INCOMPLETE
     *         False otherwise.
     */
    bool getStatus() const
    { return (MSG_COMPLETE == iv_state || MSG_INCOMPLETE == iv_state); }

    /**
     * @brief operator that returns true if the messaging
     *        state is MSG_INCOMPLETE. This indicates that
     *        data is able to be appended to the buffer(s)
     */
    operator bool() const
    { return MSG_INCOMPLETE == iv_state; }

    /**
     * @brief Current state of the messaging buffer
     */
    enum State {
        INVALID_CALLER_BUFFER, // Caller buffer too short
        OVERRUN,               // Message larger than local buffer
        MSG_SHORT_READ,        // Message is shorter that header
        MSG_INVALID_OFFSET,    // The message contains an invalid status header offset
        MSG_COMPLETE,          // The message was read in successfully
        MSG_INCOMPLETE         // The message is being constructed
    };

    static const size_t MSG_BUFFER_SIZE = 2048;
    uint32_t iv_localMsgBuffer[MSG_BUFFER_SIZE]; /**< local buffer large enough
                                                   to hold FFDC data */

    // ...
};

constexpr size_t STATUS_WORD_SIZE =
                         sizeof(SBEIO::SbeFifo::statusHeader)/sizeof(uint32_t);

bool SbeFifoRespBuffer::append(uint32_t i_value)
{
    bool retval = false;

    if(iv_state == MSG_INCOMPLETE)
    {
        if(iv_index < iv_callerWordSize)
        {
            iv_callerBufferPtr[iv_index] = i_value;
        }

        if(iv_index < MSG_BUFFER_SIZE)
        {
            iv_localMsgBuffer[iv_index] = i_value;
            ++iv_index;
            retval = true;
        }
        else
        {
            die("Overran buffer.");
            iv_state = OVERRUN;
        }
    }
    else
    {
         die("Invalid state for append, current state");
    }

    return retval;
}

void SbeFifoRespBuffer::completeMessage()
{
    if (MSG_INCOMPLETE != iv_state)
        return;

    // Message Schema:
    //  |Return Data (optional)| Status Header | FFDC (optional)
    //  |Offset to Status Header (starting from EOT) | EOT |

    // Final index for a minimum complete message (No return data and no FFDC):
    //  Word Length of status header + Length of Offset (1) + Length of EOT (1)
    if (iv_index < STATUS_WORD_SIZE + 2)
    {
        // Complete call caused short read.
        iv_state = MSG_SHORT_READ;
        return;
    }

    // |offset to header| EOT marker | current insert pos | <- iv_index
    // The offset is how far to move back from from the EOT position to
    // to get the index of the Status Header.
    iv_offsetIndex = iv_index - 2;

    // Validate that the offset to the status header is in range
    if (iv_localMsgBuffer[iv_offsetIndex] - 1 > iv_offsetIndex)
    {
        // offset is to large - would go before the buffer.
        iv_state = MSG_INVALID_OFFSET;
        return;
    }
    else if (iv_localMsgBuffer[iv_offsetIndex] < STATUS_WORD_SIZE + 1)
    {
        // Minimum offset (no ffdc) is StatusHeader size + 1
        iv_state = MSG_INVALID_OFFSET;
        return;
    }

    // Set The StatusHeader index
    iv_statusIndex = iv_offsetIndex - (iv_localMsgBuffer[iv_offsetIndex] - 1);

    // FFDC handling was removed for simplicity //

    iv_state = MSG_COMPLETE;
}
```
