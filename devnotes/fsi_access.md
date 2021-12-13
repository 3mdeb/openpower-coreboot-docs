# Some definitions

* FSI - see below
* MFSI - Master FSI
* hMFSI - Hub MFSI
* cMFSI - Cascaded MFSI
* OPB - On-chip Peripheral Bus
* PIB - "Pervasive Interconnect Bus. A bus that provides access from masters
        through external interfaces and internal masters to common PIB attached
        slaves. The PIB is used for read and full write access."
* PCB - Pervasive Control Bus

# Introduction to FSI

Official documentation defines FSI in at least three different ways:

1. FRU service interface
2. FRU support interface
3. Flexible Service Interface

Where FRU is Field Replaceable Unit:
> A module or component which will typically be replaced in its entirety as part
> of a field service repair operation.

Closely related to FSI is CFAM (Common Field replaceable unit Access Macro):
> A CFAM is an ASIC residing in any device requiring FSI communications. CFAMs
> consist of an array of hardware 'engines' used for various purposes. I2C
> masters, UARTs, General Purpose IO hardware are common types of these engines.

In other words, CFAM seems to be a physical embodiment of FSI within a device.
POWER9 chip itself has one FSI slave and FSI master, which are used to implement
SMP in two-chip configuration (second chip is connected as an FSI slave to the
first one) or for hardware debugging.

The number of FSI buses is not clear:
[POWER9 Sforza Single-Chip Module Datasheet](https://wiki.raptorcs.com/w/images/f/ff/POWER9_Sforza_DS_v18_13JUN2019_pub.pdf)
says there is one slave and one master bus,
[POWER9 Processor User’s Manual](https://wiki.raptorcs.com/w/images/c/ce/POWER9_um_OpenPOWER_v21_10OCT2019_pub.pdf)
lists 2 slaves and 2x8 masters, and
[talos.xml](https://git.raptorcs.com/git/talos-xml/plain/talos.xml)
defines 3 masters of type FSIM, 4 masters of type FSICM and 2 slaves. That last
source also defines connection between `fsim-1` on socket 0 and `fsi-slave-0` on
socket 1, which probably is the only connection we care about.

# Addressing

From register specification documents:

> `FSI`, `FSI_BYTE`, and `SCOM` addresses are defined as follows:
> * `*_FSI` is a word-based address that is used by external tooling like
>    pdbg/ecmd and Cronus. Note: EEXX is EE== Engine ID (08, 09,10, and so on) and
>    XX is a word offset into said engine.
> * `*_FSI_BYTE` is the raw FSI byte address.
> * `*_SCOM` is the address via the internal PIB bus.

SCOM is what's being accessed through FSI most of the time (see below). FSI
addresses don't match SCOM addresses. Word-based addresses are also referred to
as "CFAM addresses" and CFAM-related functions in Hostboot accept `*_FSI`
constants, which are then translated to `*FSI_BYTE` the following way before
being handled by FSI read/write functions:

```c
const uint32_t CFAM_ADDRESS_MASK = 0x1FF;
const uint32_t CFAM_ENGINE_OFFSET_MASK = 0xFE00;

fsi = ((cfam & CFAM_ADDRESS_MASK) * 4) | (cfam & CFAM_ENGINE_OFFSET_MASK);
```

Both of these addresses include register and engine parts and only register part
is word-based and needs modification.

There are at least "TP Chiplet CFAM" registers which are exposed only through
FSI, so FSI addresses don't always correspond to some SCOM addresses.  See page
42 in
[POWER9 Registers Specification vol.1](https://wiki.raptorcs.com/w/images/f/f5/POWER9_Registers_vol1_version1.2_pub.pdf).

# Accessing

Looks like FSI is accessed indirectly through XSCOM. Specifically, there is
PIB2OPB which exposes FSI via its registers. Prior to that one has to translate
FSI address (relative to FSI target) into absolute FSI address (relative to OPB
target).

# Relevant sources in Hostboot

* `src/usr/fsi/fsidd.H`
* `src/usr/fsi/fsidd.C`
* `src/include/usr/fsi/fsiif.H`
* `src/usr/fsiscom/fsiscom.H`
* `src/usr/fsiscom/fsiscom.C`

# Analysis assumptions

* Simics is not running.
* Not MPIPL.
* Not Murano chip type.

# General notes

`TARGETING::Target` is PROC and it's checked to not be master CPU.

# Declarations in Hostboot

```cpp
/**
 * Master processor target
 */
TARGETING::Target* iv_master;

/**
 * Non-zero if a Task is currently collecting FFDC
 */
tid_t iv_ffdcTask;

/**
 * PIB2OPB Registers
 */
enum Pib2OpbRegisters {
    OPB_REG_CMD   = 0x0000, /**< Command Register */
    OPB_REG_STAT  = 0x0001, /**< Status Register */
    OPB_REG_LSTAT = 0x0002, /**< Locked Status */
    // no reg for 0x0003
    OPB_REG_RES   = 0x0004, /**< Reset */
    OPB_REG_CRSIC = 0x0005, /**< cMFSI Remote Slave Interrupt Condition */
    OPB_REG_CRSIM = 0x0006, /**< cMFSI Remote Slave Interrupt Mask */
    OPB_REG_CRSIS = 0x0007, /**< cMFSI Remote Slave Interrupt Status */
    OPB_REG_RSIC  = 0x0008, /**< MFSI Remote Slave Interrupt Condition */
    OPB_REG_RSIM  = 0x0009, /**< MFSI Remote Slave Interrupt Mask */
    OPB_REG_RSIS  = 0x000A, /**< MFSI Remote Slave Interrupt Status */

    // Offsets for cMFSI
    FSI2OPB_OFFSET_0 = 0x00020000, /**< cMFSI 0 and MFSI */
    FSI2OPB_OFFSET_1 = 0x00030000, /**< cMFSI 1 */

    // Bit masks
    OPB_STAT_BUSY       = 0x00010000, /**< 15 is the Busy bit */
    OPB_STAT_READ_VALID = 0x00020000, /**< 14 is the Valid Read bit */
    OPB_STAT_ERRACK     = 0x00100000, /**< 11 is OPB errAck */
    OPB_STAT_ANYERR     = 0x80000000, /**< 0 is Any error */
    OPB_STAT_ERR_OPB    = 0x7FEC0000, /**< 1:10,12:13 are OPB errors */
    OPB_STAT_ERR_CMFSI  = 0x0000FC00, /**< 16:21 are cMFSI errors */
    OPB_STAT_ERR_MFSI   = 0x000000FC, /**< 24:29 are MFSI errors */
    OPB_STAT_ERR_ANY    = (OPB_STAT_ERR_OPB |
                           OPB_STAT_ERR_CMFSI |
                           OPB_STAT_ERR_MFSI |
                           OPB_STAT_ERRACK |
                           OPB_STAT_ANYERR ),
};

/**
 * FSI Address Space
 */
enum FsiAddressSpace {
    // Master control registers
    CMFSI_CONTROL_REG = 0x003000, /**< cMFSI Control Register */
    MFSI_CONTROL_REG  = 0x003400, /**< MFSI Control Register */

    // cMFSI Ports  (32KB each)
    CMFSI_PORT_0      = 0x040000, /**< cMFSI port 0 */
    CMFSI_PORT_1      = 0x048000, /**< cMFSI port 1 */
    CMFSI_PORT_2      = 0x050000, /**< cMFSI port 2 */
    CMFSI_PORT_3      = 0x058000, /**< cMFSI port 3 */
    CMFSI_PORT_4      = 0x060000, /**< cMFSI port 4 */
    CMFSI_PORT_5      = 0x068000, /**< cMFSI port 5 */
    CMFSI_PORT_6      = 0x070000, /**< cMFSI port 6 */
    CMFSI_PORT_7      = 0x078000, /**< cMFSI port 7 */
    CMFSI_PORT_MASK   = 0x078000, /**< Mask to look for a valid cMFSI port */

    // Offsets to cascaded slaves within a cMFSI port
    CMFSI_SLAVE_0     = 0x000000, /**< cMFSI - Slave 0 */
    CMFSI_SLAVE_1     = 0x002000, /**< cMFSI - Slave 1 */
    CMFSI_SLAVE_2     = 0x004000, /**< cMFSI - Slave 2 */
    CMFSI_SLAVE_3     = 0x006000, /**< cMFSI - Slave 3 */

    // MFSI Ports  (512KB each)
    MFSI_PORT_LOCAL   = 0x000000, /**< Local master (for local cMFSI) */
    MFSI_PORT_0       = 0x080000, /**< MFSI port 0 */
    MFSI_PORT_1       = 0x100000, /**< MFSI port 1 */
    MFSI_PORT_2       = 0x180000, /**< MFSI port 2 */
    MFSI_PORT_3       = 0x200000, /**< MFSI port 3 */
    MFSI_PORT_4       = 0x280000, /**< MFSI port 4 */
    MFSI_PORT_5       = 0x300000, /**< MFSI port 5 */
    MFSI_PORT_6       = 0x380000, /**< MFSI port 6 */
    MFSI_PORT_7       = 0x400000, /**< MFSI port 7 */
    MFSI_PORT_MASK    = 0x780000, /**< Mask to look for a valid MFSI port */

    // Offsets to cascaded slaves within a MFSI port
    MFSI_SLAVE_0      = 0x000000, /**< MFSI - Slave 0 */
    MFSI_SLAVE_1      = 0x020000, /**< MFSI - Slave 1 */
    MFSI_SLAVE_2      = 0x040000, /**< MFSI - Slave 2 */
    MFSI_SLAVE_3      = 0x060000, /**< MFSI - Slave 3 */
};

/**
 * FSI Control Registers
 */
enum FsiControlRegisters {
    FSI_MMODE_000   = 0x000,
    FSI_MDLYR_004   = 0x004,
    FSI_MCRSP0_008  = 0x008,
    FSI_MENP0_010   = 0x010,
    FSI_MLEVP0_018  = 0x018,
    FSI_MSENP0_018  = 0x018,
    FSI_MCENP0_020  = 0x020,
    FSI_MSIEP0_030  = 0x030,
    FSI_MAESP0_050  = 0x050,
    FSI_MAEB_070    = 0x070, //MREFP0
    FSI_MBSYP0_078  = 0x078,
    FSI_MRESP0_0D0  = 0x0D0,
    FSI_MSTAP0_0D0  = 0x0D0,
    FSI_MRESP0_0D1  = 0x0D1,
    FSI_MSTAP0_0D1  = 0x0D1,
    FSI_MRESP0_0D2  = 0x0D2,
    FSI_MSTAP0_0D2  = 0x0D2,
    FSI_MRESP0_0D3  = 0x0D3,
    FSI_MSTAP0_0D3  = 0x0D3,
    FSI_MRESP0_0D4  = 0x0D4,
    FSI_MSTAP0_0D4  = 0x0D4,
    FSI_MRESP0_0D5  = 0x0D5,
    FSI_MSTAP0_0D5  = 0x0D5,
    FSI_MRESP0_0D6  = 0x0D6,
    FSI_MSTAP0_0D6  = 0x0D6,
    FSI_MRESP0_0D7  = 0x0D7,
    FSI_MSTAP0_0D7  = 0x0D7,
    FSI_MESRB0_1D0  = 0x1D0,
    FSI_MSCSB0_1D4  = 0x1D4,
    FSI_MATRB0_1D8  = 0x1D8,
    FSI_MDTRB0_1DC  = 0x1DC,
    FSI_MECTRL_2E0  = 0x2E0,
    FSI_CTLREG_MASK = 0x2FF
};

/**
 * FSI Slave Registers for P8
 *   These registers are repeated for every master+port+cascade combo
 */
enum SlaveRegistersP8
{
    // Local FSI Space
    SLAVE_CFG_TABLE  = 0x000000, /**< Configuration Table of CFAM */
    SLAVE_PEEK_TABLE = 0x000400, /**< Peek Table */

    SLAVE_REGS       = 0x000800, /**< FSI Slave Register */
    SMODE_00         = SLAVE_REGS|0x00,
    SLBUS_30         = SLAVE_REGS|0x30,
    SLRES_34         = SLAVE_REGS|0x34,

    FSI_SHIFT_ENGINE = 0x000C00, /**< FSI Shift Engine (SCAN) */

    FSI2PIB_ENGINE   = 0x001000, /**< FSI2PIB Engine (SCOM) */
    FSI2PIB_RESET    = FSI2PIB_ENGINE|0x18, /**< see 1006 */
    FSI2PIB_STATUS   = FSI2PIB_ENGINE|0x1C, /**< see 1007 */
    FSI2PIB_COMPMASK = FSI2PIB_ENGINE|0x30, /**< see 100C */
    FSI2PIB_TRUEMASK = FSI2PIB_ENGINE|0x34, /**< see 100D */

    FSI_SCRATCHPAD   = 0x001400, /**< FSI Scratchpad */

    FSI_I2C_MASTER   = 0x001800, /**< FSI I2C-Master */

    FSI_GEMINI_MBOX  = 0x002800, /**< FSI Gemini Mailbox with FSI GPx Registers */
};

/**
 * Common id to identify a FSI position to use in error logs and traces
 */
union FsiLinkId_t
{
    uint32_t id;
    struct
    {
        uint8_t node; ///< Physical Node of FSI Master processor
        uint8_t proc; ///< Physical Position of FSI Master processor
        uint8_t type; ///< FSI Master type (FSI_MASTER_TYPE)
        uint8_t port; ///< Slave link/port number
    };
};

/**
 * @brief Structure which defines info necessary to access a chip via FSI
 */
struct FsiChipInfo_t
{
    TARGETING::Target* slave; //< FSI Slave chip
    TARGETING::Target* master; ///< FSI Master
    TARGETING::FSI_MASTER_TYPE type; ///< Master or Cascaded Master
    uint8_t port; ///< Which port is this chip hanging off of
    uint8_t cascade; ///< Slave cascade position
    union {
        TARGETING::FsiOptionFlags flagbits; ///< Special flags
        uint16_t flags; ///< Special flags
    };
    FsiLinkId_t linkid; ///< Id for traces and error logs

    FsiChipInfo_t() : slave(NULL), master(NULL), type(TARGETING::FSI_MASTER_TYPE_NO_MASTER),
                      port(UINT8_MAX), cascade(0), flags(0)
    { linkid.id = 0; };
};

/**
 * @brief Holds a set of addressing information to describe the
 *   current FSI operation
 */
struct FsiAddrInfo_t {
    TARGETING::Target* fsiTarg; ///< Target of FSI operation
    TARGETING::Target* opbTarg; ///< OPB control reg target
    uint32_t relAddr; ///< Input FSI address (relative to fsiTarg)
    uint32_t absAddr; ///< Absolute FSI address (relative to opbTarg)
    FsiChipInfo_t accessInfo; ///< FSI Access Info

    /** Input Arg Constructor */
    FsiAddrInfo_t( TARGETING::Target* i_target, uint64_t i_address ) :
      fsiTarg(i_target), opbTarg(NULL), relAddr(i_address), absAddr(UINT32_MAX)
    {}

  private:
    /** Default Constructor is not allowed */
    FsiAddrInfo_t() : fsiTarg(NULL), opbTarg(NULL), relAddr(UINT32_MAX),absAddr(UINT32_MAX)
    {}
};
```

# FSI read/write operations in Hostboot

```cpp
/**
 * @brief Performs an FSI Read Operation to a relative address
 *
 * @param[in] i_target  Chip target of FSI operation
 * @param[in] i_address  Address to read (relative to target)
 * @param[out] o_buffer  Destination buffer for data
 * @parm[in]  i_buflen   Length of data to read in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t read(TARGETING::Target* i_target,
                uint64_t i_address,
                uint32_t* o_buffer,
                size_t  i_buflen = 4)
{
    errlHndl_t l_err = NULL;

    do {
        // verify slave is present before doing anything
        l_err = verifyPresent( i_target );
        if(l_err)
        {
            break;
        }

        // prefix the appropriate MFSI/cMFSI slave port offset
        FSI::FsiAddrInfo_t addr_info( i_target, i_address );
        l_err = genFullFsiAddr( addr_info );
        if(l_err)
        {
            break;
        }

        // perform the read operation
        l_err = read( addr_info, o_buffer, i_buflen );

        if(l_err)
        {
            break;
        }
    } while(0);

    return l_err;
}

/**
 * @brief Performs an FSI Write Operation
 *
 * @param[in] i_target  Chip target of FSI operation
 * @param[in] i_address  Address to write (relative to target)
 * @param[out] i_buffer  Source buffer for data
 * @parm[in]  i_buflen   Length of data to write in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t write(TARGETING::Target* i_target,
                 uint64_t i_address,
                 uint32_t* i_buffer,
                 size_t  i_buflen = 4)
{
    errlHndl_t l_err = NULL;

    do {
        // verify slave is present before doing anything
        l_err = verifyPresent( i_target );
        if(l_err)
        {
            break;
        }

        // prefix the appropriate MFSI/cMFSI slave port offset
        FSI::FsiAddrInfo_t addr_info( i_target, i_address );
        l_err = genFullFsiAddr( addr_info );
        if(l_err)
        {
            break;
        }

        // perform the write operation
        l_err = write( addr_info, o_buffer, i_buflen );
        if(l_err)
        {
            break;
        }
    } while(0);

    return l_err;
}

/**
 * @brief Verify that the slave target was detected
 *
 * @param[in] i_target  FSI Slave target
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t FsiDD::verifyPresent( TARGETING::Target* i_target )
{
    errlHndl_t l_err = NULL;

    uint8_t slaves = 0;
    if (!isSlavePresent(i_target, slaves) && i_target != iv_master)
    {
        // Requested target was never detected during FSI Init
        l_err = new ERRORLOG::ErrlEntry();
    }

    return l_err;
}

/**
 * @brief Retrieves the FSI status of a given chip
 *
 * @param[in] i_target
 * @param[out] o_detected  Bitstring of detected slaves
 *
 * @return bool  true if port sensed as active during FSI initialization
 */
bool FsiDD::isSlavePresent( TARGETING::Target* i_target, uint8_t& o_detected )
{
    // look up the FSI information for this target
    FSI::FsiChipInfo_t info = getFsiInfo(i_target);
    return isSlavePresent( info.master, info.type, info.port, o_detected );
}

/**
 * @brief Retrieves the status of a given port
 *
 * @param[in] i_fsiMaster  FSI Master chip
 * @param[in] i_type  FSI Master Type (MFSI or cMFSI)
 * @param[in] i_port  Slave port number
 * @param[out] o_detected  Bitstring of detected slaves
 *
 * @return bool  true if port sensed as active during FSI initialization
 */
bool FsiDD::isSlavePresent( TARGETING::Target* i_fsiMaster,
                            TARGETING::FSI_MASTER_TYPE i_type,
                            uint8_t i_port,
                            uint8_t& o_detected )
{
    o_detected = 0;
    if( i_port < MAX_SLAVE_PORTS
        && TARGETING::FSI_MASTER_TYPE_NO_MASTER != i_type
        && NULL != i_fsiMaster )
    {
        uint64_t slave_index = getSlaveEnableIndex(i_fsiMaster,i_type);
        if( INVALID_SLAVE_INDEX == slave_index )
        {
            return false;
        }
        else
        {
            o_detected = iv_slaves[slave_index];
            return ( o_detected & (0x80 >> i_port) );
        }
    }
    else
    {
        return false;
    }
}

/**
 * @brief Retrieve the slave enable index
 * @param[in] i_master  Target of FSI Master
 * @param[in] i_type  Type of FSI interface
 * @return uint64_t  Index into iv_slaves array
 */
uint64_t FsiDD::getSlaveEnableIndex( TARGETING::Target* i_master,
                                     TARGETING::FSI_MASTER_TYPE i_type )
{
    if( i_master == NULL )
    {
        return INVALID_SLAVE_INDEX;
    }

    //default to local slave ports
    uint64_t slave_index = MAX_SLAVE_PORTS+i_type;
    if( i_master != iv_master )
    {
        FSI::FsiChipInfo_t m_info = getFsiInfo(i_master);
        if( m_info.type == TARGETING::FSI_MASTER_TYPE_NO_MASTER )
        {
            slave_index = INVALID_SLAVE_INDEX;
        }
        else
        {
            slave_index = m_info.port;
        }
    }
    return slave_index;
}

/**
 * @brief Performs an FSI Read Operation to an absolute address
 *   using the master processor chip to drive it
 *
 * @param[in] i_address  Absolute FSI address to read relative to
 *     the OPB master processor chip
 * @param[out] o_buffer  Destination buffer for data
 * @parm[in]  i_buflen   Length of data to read in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t read(uint64_t i_address, uint32_t* o_buffer, size_t i_buflen = 4)
{
    // generate a set of address info for this manual operation
    //  note that relAddr==absAddr in this case
    FSI::FsiAddrInfo_t addr_info( iv_master, i_address );
    addr_info.opbTarg = iv_master;
    addr_info.absAddr = i_address;

    // call to low-level read function
    errlHndl_t l_err = read( addr_info, o_buffer, i_buflen );

    return l_err;
}

/**
 * @brief Performs an FSI Write Operation to an absolute address
 *   using the master processor chip to drive it
 *
 * @param[in] i_address  Absolute FSI address to write relative to
 *     the OPB master processor chip
 * @param[out] i_buffer  Source buffer for data
 * @parm[in]  i_buflen   Length of data to write in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t write(uint64_t i_address, uint32_t* i_buffer, size_t i_buflen = 4)
{
    // generate a set of address info for this manual operation
    //  note that relAddr==absAddr in this case
    FSI::FsiAddrInfo_t addr_info( iv_master, i_address );
    addr_info.opbTarg = iv_master;
    addr_info.absAddr = i_address;

    // call to low-level write function
    errlHndl_t l_err = write( addr_info, i_buffer, i_buflen );

    return l_err;
}

/**
 * @brief Performs an FSI Read Operation
 *
 * @param[in] i_addrInfo  Addressing information
 * @param[out] o_buffer  Destination buffer for data
 * @parm[in]  i_buflen   Length of data to read in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t read(FSI::FsiAddrInfo_t& i_addrInfo, uint32_t* o_buffer, size_t i_buflen = 4)
{
    errlHndl_t l_err = NULL;
    bool need_unlock = false;
    mutex_t* l_mutex = NULL;
    *o_buffer = 0xDEADBEEF;

    do {
        // setup the OPB command register

        uint64_t fsicmd = 0;

        if ( i_buflen == 4 )
            fsicmd = i_addrInfo.absAddr | 0x60000000; // 011=Read Full Word
        else if ( i_buflen == 1 )
            fsicmd = i_addrInfo.absAddr | 0x00000000; // 000=Read 1 Byte
        else
            fsicmd = i_addrInfo.absAddr | 0x20000000; // 001=Read 2 Bytes

        fsicmd <<= 32; // Command is in the upper word

        // generate the proper OPB SCOM address
        uint64_t opbaddr = genOpbScomAddr(i_addrInfo,OPB_REG_CMD);

        // atomic section >>

        if( (iv_ffdcTask == 0)  // performance hack for typical case
            || (iv_ffdcTask != task_gettid()) )
        {
            l_mutex = i_addrInfo.opbTarg->getHbMutexAttr<TARGETING::ATTR_FSI_MASTER_MUTEX>();
            mutex_lock(l_mutex);
            need_unlock = true;
        }

        // make sure there are no other ops running before we start
        l_err = pollForComplete( i_addrInfo, NULL );
        if( l_err )
        {
            // FSI Errors before doing read operation
            break;
        }

        // always read/write 64 bits to SCOM
        size_t scom_size = sizeof(uint64_t);

        // write the OPB command register to trigger the read
        iv_lastOpbCmd = fsicmd;
        l_err = deviceOp( DeviceFW::WRITE,
                          i_addrInfo.opbTarg,
                          &fsicmd,
                          scom_size,
                          DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( l_err )
        {
            break;
        }

        // poll for complete and get the data back
        l_err = pollForComplete( i_addrInfo, o_buffer );
        if( l_err )
        {
            // FSI Errors after doing read operation
            break;
        }

        //check for general errors
        l_err = checkForErrors( i_addrInfo );
        if( l_err )
        {
            // FSI Errors after doing read operation
            break;
        }

        // atomic section <<
    } while(0);

    if(need_unlock)
    {
        mutex_unlock(l_mutex);
    }

    return l_err;
}

/**
 * @brief Performs an FSI Write Operation to an absolute address
 *
 * @param[in] i_addrInfo  Addressing information
 * @param[out] i_buffer  Source buffer for data
 * @parm[in]  i_buflen   Length of data to write in bytes
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t write(FSI::FsiAddrInfo_t& i_addrInfo, uint32_t* i_buffer, size_t i_buflen = 4)
{
    errlHndl_t l_err = NULL;
    bool need_unlock = false;
    mutex_t* l_mutex = NULL;

    do {
        // pull out the data to write (length has been verified)
        uint32_t fsidata = *i_buffer;

        // setup the OPB command register

        uint64_t fsicmd = 0;

        if ( i_buflen == 4 )
            fsicmd = i_addrInfo.absAddr | 0xE0000000; // 111=Write Full Word
        else if ( i_buflen == 1 )
            fsicmd = i_addrInfo.absAddr | 0x80000000; // 100=Write 1 Byte
        else
            fsicmd = i_addrInfo.absAddr | 0xA0000000; // 101=Write 2 Bytes

        fsicmd <<= 32; // Command is in the upper 32-bits
        fsicmd |= fsidata; // Data is in the bottom 32-bits
        size_t scom_size = sizeof(uint64_t);

        // generate the proper OPB SCOM address
        uint64_t opbaddr = genOpbScomAddr(i_addrInfo,OPB_REG_CMD);

        // atomic section >>

        if( (iv_ffdcTask == 0)  // performance hack for typical case
            || (iv_ffdcTask != task_gettid()) )
        {
            l_mutex = (i_addrInfo.opbTarg)
                ->getHbMutexAttr<TARGETING::ATTR_FSI_MASTER_MUTEX>();
            mutex_lock(l_mutex);
            need_unlock = true;
        }

        // make sure there are no other ops running before we start
        l_err = pollForComplete( i_addrInfo, NULL );
        if( l_err )
        {
            // FSI Errors before doing write operation
            break;
        }

        // write the OPB command register
        iv_lastOpbCmd = fsicmd;
        l_err = deviceOp( DeviceFW::WRITE,
                          i_addrInfo.opbTarg,
                          &fsicmd,
                          scom_size,
                          DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( l_err )
        {
            // Error from device
            break;
        }

        // poll for complete (no return data)
        l_err = pollForComplete( i_addrInfo, NULL );
        if( l_err )
        {
            // FSI Errors after doing write operation
            break;
        }

        //check for general errors
        l_err = checkForErrors( i_addrInfo );
        if( l_err )
        {
            // FSI Errors after doing write operation
            break;
        }

        // atomic section <<
    } while(0);

    if(need_unlock)
    {
        mutex_unlock(l_mutex);
    }

    return l_err; 
}

/**
 * @brief  Poll for completion of a FSI operation, return data on read
 *
 * @param[in] i_addrInfo  FSI addressing information
 * @param[out] o_readData  buffer to copy read data into, set to NULL
 *       for write operations
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t FsiDD::pollForComplete(FSI::FsiAddrInfo_t& i_addrInfo,
                                  uint32_t* o_readData)
{
    errlHndl_t l_err = NULL;
    enum { MAX_OPB_TIMEOUT_NS = 10*NS_PER_MSEC }; //=10ms

    do {
        // poll for complete
        uint32_t read_data[2];
        size_t scom_size = sizeof(uint64_t);
        uint64_t opbaddr = genOpbScomAddr(i_addrInfo,OPB_REG_STAT);

        // Do not look at error bits for the Master we're not using
        uint32_t l_opbErrorMask = iv_opbErrorMask;
        if( i_addrInfo.accessInfo.type == TARGETING::FSI_MASTER_TYPE_CMFSI )
        {
            l_opbErrorMask &= ~OPB_STAT_ERR_MFSI;
        }
        else if( i_addrInfo.accessInfo.type == TARGETING::FSI_MASTER_TYPE_MFSI )
        {
            l_opbErrorMask &= ~OPB_STAT_ERR_CMFSI;
        }
        else //NO_MASTER, meaning that we only care about OPB stuff
        {
            l_opbErrorMask &= ~OPB_STAT_ERR_MFSI;
            l_opbErrorMask &= ~OPB_STAT_ERR_CMFSI;
        }

        uint64_t elapsed_time_ns = 0;
        do
        {
            l_err = deviceOp( DeviceFW::READ,
                              i_addrInfo.opbTarg,
                              read_data,
                              scom_size,
                              DEVICE_XSCOM_ADDRESS(opbaddr) );
            if( l_err )
            {
               break;
            }

            // check for completion or error
            if( ((read_data[0] & OPB_STAT_BUSY) == 0)  //not busy
                || (read_data[0] & l_opbErrorMask) ) //error bits
            {
                break;
            }

            nanosleep( 0, 10000 ); //sleep for 10,000 ns
            elapsed_time_ns += 10000;
        } while( elapsed_time_ns <= MAX_OPB_TIMEOUT_NS ); // hardware has 1ms limit
        if( l_err ) { break; }

        // check if we got an error from the OPB
        //   (will also check for busy/timeout)
        l_err = handleOpbErrors( i_addrInfo, opbaddr, read_data[0] );
        if( l_err )
        {
            break;
        }

        // we should never timeout because the hardware should set an error
        if( elapsed_time_ns > MAX_OPB_TIMEOUT_NS )
        {
            // Never got complete or error on OPB operation
            l_err = new ERRORLOG::ErrlEntry();
            break;
        }

        // read valid isn't on
        if( o_readData )  // only check if we're doing a read
        {
            if( !(read_data[0] & OPB_STAT_READ_VALID) )
            {
                // Read valid never came on
                l_err = new ERRORLOG::ErrlEntry();
                break;
            }

            *o_readData = read_data[1];
        }

    } while(0);

    return l_err;
}

/**
 * @brief Analyze error bits and recover hardware as needed
 *
 * @param[in] i_addrInfo  FSI addressing information
 * @param[in] i_opbStatAddr  OPB Status Register Address
 * @param[in] i_opbStatData  OPB Status bits (OPB_REG_STAT[0:31])
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t handleOpbErrors(FSI::FsiAddrInfo_t& i_addrInfo,
                           uint32_t i_opbStatAddr,
                           uint32_t i_opbStatData)
{
    errlHndl_t l_err = NULL;

    // Do not look at error bits for the Master we're not using
    uint32_t l_opbErrorMask = iv_opbErrorMask;
    if( i_addrInfo.accessInfo.type == TARGETING::FSI_MASTER_TYPE_CMFSI )
    {
        l_opbErrorMask &= ~OPB_STAT_ERR_MFSI;
    }
    else if( i_addrInfo.accessInfo.type == TARGETING::FSI_MASTER_TYPE_MFSI )
    {
        l_opbErrorMask &= ~OPB_STAT_ERR_CMFSI;
    }
    else //NO_MASTER, meaning that we only care about OPB stuff
    {
        l_opbErrorMask &= ~OPB_STAT_ERR_MFSI;
        l_opbErrorMask &= ~OPB_STAT_ERR_CMFSI;
    }

    // Fail if there is a relevant error bit or the op never finished
    if( (i_opbStatData & l_opbErrorMask) || (i_opbStatData & OPB_STAT_BUSY) )
    {
        // If we're already in the middle of handling an error and we failed
        //  again it isn't worth going to all of the effort to isolate the
        //  error and collect more FFDC that is just going to be deleted.
        if( iv_ffdcTask != 0 )
        {
            //Clear out the error indication so that we can
            // do subsequent FSI operations
            errlHndl_t tmp_err = errorCleanup(i_addrInfo,FSI::RC_OPB_ERROR);
            if(tmp_err) { delete tmp_err; }
            return l_err; // just leave
        }

        // Error during FSI access
        l_err = new ERRORLOG::ErrlEntry();

        // << Lots of debug and error recovery code was here >>
    }

    return l_err;
}

/**
 * @brief Clear out the error indication so that we can do more FSI ops
 */
errlHndl_t FsiDD::errorCleanup( FSI::FsiAddrInfo_t& i_addrInfo, FSI::FSIReasonCode i_errType )
{
    errlHndl_t l_err = NULL;

    do {
        if( FSI::RC_OPB_ERROR == i_errType )
        {
            //Clear out the pib2opb logic for the master
            // that failed
            l_err = resetPib2Opb( i_addrInfo.opbTarg );
            if(l_err) break;
        }
        else if( FSI::RC_ERROR_IN_MAEB == i_errType )
        {
            uint32_t data = 0;

            //Reset the bridge to clear up the residual errors
            // 0=Bridge: General reset
            data = 0x80000000;
            uint64_t mesrb0_reg = getControlReg(i_addrInfo.accessInfo.type)
              | FSI_MESRB0_1D0;
            l_err = write( i_addrInfo.accessInfo.master, mesrb0_reg, &data );
            if(l_err) break;

            //perform error reset on Centaur fsi slave:
            //  write 0x4000000 to addr=834.
            data = 0x4000000;
            l_err = write( i_addrInfo.fsiTarg, FSI::SLRES_34, &data );
            if(l_err) break;

            //Need to save/restore the true/comp masks or the FSP will
            // get annoyed
            uint32_t compmask = 0;
            l_err = read( i_addrInfo.fsiTarg,
                          FSI::FSI2PIB_COMPMASK,
                          &compmask );
            if(l_err) break;
            uint32_t truemask = 0;
            l_err = read( i_addrInfo.fsiTarg,
                          FSI::FSI2PIB_TRUEMASK,
                          &truemask );
            if(l_err) break;

            //then, write arbitrary data to 1018  (putcfam 1006) to
            //reset any pending FSI2PIB errors.
            data = 0xFFFFFFFF;
            l_err = write( i_addrInfo.fsiTarg, FSI::FSI2PIB_RESET, &data );
            if(l_err) break;

            //Reset the master's bridge to clear up the residual errors
            // unless the FSI master has no master above it
            if( i_addrInfo.accessInfo.master != iv_master )
            {
                // 0=Bridge: General reset
                data = 0x80000000;
                mesrb0_reg = FSI::MFSI_CONTROL_REG | FSI_MESRB0_1D0;
                l_err = write( iv_master, mesrb0_reg, &data );
                if(l_err) break;
            }

            //Restore the true/comp masks
            l_err = write( i_addrInfo.fsiTarg,
                           FSI::FSI2PIB_COMPMASK,
                           &compmask );
            if(l_err) break;
            l_err = write( i_addrInfo.fsiTarg,
                           FSI::FSI2PIB_TRUEMASK,
                           &truemask );
            if(l_err) break;

            if( iv_ffdcTask == 0 )
            {
                //skip the extra FFDC if we aren't in the middle of
                // handling an error
                break;
            }

            //Trace some values for FFDC in case this cleanup
            // didn't really work
            uint32_t grabregs[] = {
                FSI::MFSI_CONTROL_REG|FSI_MSIEP0_030,
                FSI::CMFSI_CONTROL_REG|FSI_MSIEP0_030,
                FSI::MFSI_CONTROL_REG|FSI_MAEB_070,
                FSI::CMFSI_CONTROL_REG|FSI_MAEB_070
            };
            for( size_t r = 0;
                 r < (sizeof(grabregs)/sizeof(grabregs[0]));
                 r++ )
            {
                l_err = read( i_addrInfo.accessInfo.master,
                              FSI::MFSI_CONTROL_REG|FSI_MSIEP0_030, &data );
                if(l_err) break;
            }
            if(l_err) break;
        }

    } while(0);

    return l_err;
}

/**
 * @brief Check for FSI errors anywhere in the system
 *
 * @param[in] i_addrInfo  FSI Operation in error
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t checkForErrors( FSI::FsiAddrInfo_t& i_addrInfo )
{
    errlHndl_t l_err = NULL;

    if( i_addrInfo.fsiTarg == i_addrInfo.opbTarg )
    {
        //nothing to check here in operations directed at FSI Master
        return NULL;
    }

    uint32_t maeb_reg = getControlReg(i_addrInfo.accessInfo.type)|FSI_MAEB_070;

    //check for general errors
    if( (maeb_reg != i_addrInfo.absAddr) //avoid recursive fails
        && (iv_ffdcTask == 0) )
    {
        iv_ffdcTask = task_gettid();

        uint32_t maeb_data = 0;
        l_err = read( i_addrInfo.accessInfo.master, maeb_reg, &maeb_data );
        if( !l_err && maeb_data != 0 )
        {
            // Error discovered in MAEB

            // We can't really isolate the fail so callout a procedure, but
            //  deconfigure the slave chip so that we have a chance of moving
            //  forward
        }

        iv_ffdcTask = 0;
    }

    return l_err;
}

/**
 * @brief Retrieve the control register address based on type
 * @param[in] i_type  Type of FSI interface
 * @return uint64_t  FSI address offset 
 */
uint64_t getControlReg(TARGETING::FSI_MASTER_TYPE i_type)
{
    uint64_t ctl_reg = FSI::MFSI_CONTROL_REG;
    if(i_type == TARGETING::FSI_MASTER_TYPE_CMFSI)
    {
        ctl_reg = FSI::CMFSI_CONTROL_REG;
    }
    return ctl_reg;
}

/**
 * @brief Generate a valid SCOM address to access the OPB, this will
 *    choose the correct PIB2OPB port.
 *
 * @param[in] i_addrInfo  FSI connection data for the target of the FSI operation
 * @param[in] i_address  Address of OPB register relative to OPB space, e.g. OPB_REG_CMD
 *
 * @return uint64_t  Fully qualified OPB SCOM address
 */
uint64_t FsiDD::genOpbScomAddr(FSI::FsiAddrInfo_t& i_addrInfo,
                               uint64_t i_opbOffset)
{
    uint64_t opbaddr = FSI2OPB_OFFSET_0;

    // use the other port if told to
    if( i_addrInfo.opbTarg != iv_master )
    {
        if( iv_useAlt )
        {
            opbaddr = FSI2OPB_OFFSET_1;
        }
        else if(TARGETING::FSI_MASTER_TYPE_CMFSI == i_addrInfo.accessInfo.type)
        {
            FSI::FsiChipInfo_t chipinfo = getFsiInfo(i_addrInfo.opbTarg);
            if( chipinfo.flagbits.flipPort )
            {
                opbaddr = FSI2OPB_OFFSET_1;
            }
        }
    }

    opbaddr |= i_opbOffset;
    return opbaddr;
}

/**
 * @brief Retrieve the connection information needed to access FSI
 *        registers within the given chip target
 *
 * @param[in] i_target  Target of FSI Slave to access
 *
 * @return FsiChipInfo_t  FSI Chip Information
 */
FSI::FsiChipInfo_t getFsiInfo( TARGETING::Target* i_target )
{
    FSI::FsiChipInfo_t info;

    mutex_lock(&iv_dataMutex);

    // Check if we have a cached version first
    std::map<TARGETING::Target*,FSI::FsiChipInfo_t>::iterator itr = iv_fsiInfoMap.find(i_target);
    if( itr != iv_fsiInfoMap.end() )
    {
        info = itr->second;
    }
    else
    {
        // fetch the data from the attributes
        info = getFsiInfoFromAttr( i_target );
        // then cache it for next time
        iv_fsiInfoMap[i_target] = info;
    }

    mutex_unlock(&iv_dataMutex);

    return info;
}

/**
 * @brief Retrieve the connection information needed to access FSI
 *        registers within the given chip target
 */
FSI::FsiChipInfo_t FsiDD::getFsiInfoFromAttr( TARGETING::Target* i_target )
{
    FSI::FsiChipInfo_t info;
    info.slave = i_target;

    using namespace TARGETING;

    EntityPath epath;

    /* ATTR_FSI_MASTER_TYPE is NO_MASTER for master CPU and MFSI for slave one */

    /* iv_useAlt should be 0 because first CPU is the master */

    if( i_target != NULL && i_target->tryGetAttr<ATTR_FSI_MASTER_TYPE>(info.type) )
    {
        if( info.type != FSI_MASTER_TYPE_NO_MASTER )
        {
            if( !iv_useAlt && i_target->tryGetAttr<ATTR_FSI_MASTER_CHIP>(epath) )
            {
                info.master = targetService().toTarget(epath);

                if( i_target->tryGetAttr<ATTR_FSI_MASTER_PORT>(info.port) )
                {
                    if( i_target->tryGetAttr<ATTR_FSI_SLAVE_CASCADE>(info.cascade) )
                    {
                        if( !i_target->tryGetAttr<ATTR_FSI_OPTION_FLAGS>(info.flagbits) )
                        {
                            info.master = NULL;
                        }
                    }
                    else
                    {
                        info.master = NULL;
                    }
                }
                else
                {
                    info.master = NULL;
                }
            }
            else if( iv_useAlt && i_target->tryGetAttr<ATTR_ALTFSI_MASTER_CHIP>(epath) )
            {
                info.master = targetService().toTarget(epath);

                if( i_target->tryGetAttr<ATTR_ALTFSI_MASTER_PORT>(info.port) )
                {
                    if( i_target->tryGetAttr<ATTR_FSI_SLAVE_CASCADE>
                        (info.cascade) )
                    {
                        if( !i_target->tryGetAttr<ATTR_FSI_OPTION_FLAGS>
                            (info.flagbits) )
                        {
                            info.master = NULL;
                        }
                    }
                    else
                    {
                        info.master = NULL;
                    }
                }
                else
                {
                    info.master = NULL;
                }
            }
            else
            {
                info.master = NULL;
            }
        }
    }

    if(info.master == NULL || info.type == FSI_MASTER_TYPE_NO_MASTER || info.port == UINT8_MAX)
    {
        info.master = NULL;
        info.type = FSI_MASTER_TYPE_NO_MASTER;
        info.port = UINT8_MAX;
        info.cascade = 0;
        info.flags = 0;
        info.linkid.id = 0;
    }
    else
    {
        TARGETING::EntityPath master_phys;
        if( info.master->tryGetAttr<TARGETING::ATTR_PHYS_PATH>(master_phys) )
        {
            info.linkid.node = master_phys.pathElementOfType(TARGETING::TYPE_NODE).instance;
            info.linkid.proc = master_phys.pathElementOfType(TARGETING::TYPE_PROC).instance;
            info.linkid.type = static_cast<uint8_t>(info.type);
            // << Port is 1 for the second CPU and 0xff for the first one >>
            info.linkid.port = info.port;
        }
    }
    return info;
}

/**
 * @brief Figure out the optimal OPB Master to use and generate a
 *    complete FSI address relative to that master based on the target
 *    and the FSI offset within that target
 *
 * @param[inout] io_addrInfo  FSI addressing information,
 *     expects fsiTarg and relAddr to be populated as input
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t FsiDD::genFullFsiAddr(FSI::FsiAddrInfo_t& io_addrInfo)
{
    errlHndl_t l_err = NULL;

    //default to using the master chip for OPB XSCOM ops
    io_addrInfo.opbTarg = iv_master;

    //start off with the addresses being the same
    io_addrInfo.absAddr = io_addrInfo.relAddr;

    //pull the FSI info out for this target
    io_addrInfo.accessInfo = getFsiInfo( io_addrInfo.fsiTarg );

    //target matches master so the address is correct as-is
    if( io_addrInfo.fsiTarg == iv_master )
    {
        return NULL;
    }

    //FSI master is the master proc, find the port
    if( io_addrInfo.accessInfo.master == iv_master )
    {
        //append the appropriate offset
        io_addrInfo.absAddr += FSI::getPortOffset(io_addrInfo.accessInfo.type,
                                                  io_addrInfo.accessInfo.port);
    }
    //verify this target has a valid FSI master
    else if( TARGETING::FSI_MASTER_TYPE_CMFSI != io_addrInfo.accessInfo.type )
    {
        // Master Type is not supported
        return new ERRORLOG::ErrlEntry();
    }
    //target is behind another proc
    else
    {
        //append the CMFSI portion first
        io_addrInfo.absAddr += FSI::getPortOffset(io_addrInfo.accessInfo.type,
                                                  io_addrInfo.accessInfo.port);

        //find this port's master and then get its port information
        FSI::FsiChipInfo_t mfsi_info = getFsiInfo(io_addrInfo.accessInfo.master);

        //check for invalid topology
        if( mfsi_info.master != iv_master )
        {
            // Cannot chain 2 masters
            return new ERRORLOG::ErrlEntry();
        }
        else if(mfsi_info.type != TARGETING::FSI_MASTER_TYPE_MFSI)
        {
            // Invalid master type for the target's master
            return new ERRORLOG::ErrlEntry();
        }

        // If powerbus is alive, we can use the local master
        //
        if( io_addrInfo.accessInfo.master->getAttr<TARGETING::ATTR_SCOM_SWITCHES>().useXscom)
        {
            //use the local proc to drive the operation instead of
            // going through the master proc indirectly
            io_addrInfo.opbTarg = io_addrInfo.accessInfo.master;
            // Note: no need to append the MFSI port since it is now local

            // set a flag to flip the OPB port if this slave's master
            //  is reversed and it isn't the acting master
            if( io_addrInfo.accessInfo.master != iv_master )
            {
                io_addrInfo.accessInfo.flagbits.flipPort = mfsi_info.flagbits.flipPort;
            }
        }
        else
        {
            //using the master chip so we need to append the MFSI port
            io_addrInfo.absAddr += FSI::getPortOffset(mfsi_info.type,
                                                      mfsi_info.port);
        }
    }

    return NULL;
}

/**
 * @brief Convert a type/port pair into a FSI address offset
 *
 * @param[in] i_type  Type of FSI interface
 * @param[in] i_port  FSI link number
 * @return uint64_t  FSI address offset
 */
uint64_t FSI::getPortOffset(TARGETING::FSI_MASTER_TYPE i_type, uint8_t i_port)
{
    uint64_t offset = 0;
    if(i_type == TARGETING::FSI_MASTER_TYPE_MFSI)
    {
        switch(i_port)
        {
            case(0): offset = FSI::MFSI_PORT_0; break;
            case(1): offset = FSI::MFSI_PORT_1; break;
            case(2): offset = FSI::MFSI_PORT_2; break;
            case(3): offset = FSI::MFSI_PORT_3; break;
            case(4): offset = FSI::MFSI_PORT_4; break;
            case(5): offset = FSI::MFSI_PORT_5; break;
            case(6): offset = FSI::MFSI_PORT_6; break;
            case(7): offset = FSI::MFSI_PORT_7; break;
        }
    }
    else if(i_type == TARGETING::FSI_MASTER_TYPE_CMFSI)
    {
        switch(i_port)
        {
            case(0): offset = FSI::CMFSI_PORT_0; break;
            case(1): offset = FSI::CMFSI_PORT_1; break;
            case(2): offset = FSI::CMFSI_PORT_2; break;
            case(3): offset = FSI::CMFSI_PORT_3; break;
            case(4): offset = FSI::CMFSI_PORT_4; break;
            case(5): offset = FSI::CMFSI_PORT_5; break;
            case(6): offset = FSI::CMFSI_PORT_6; break;
            case(7): offset = FSI::CMFSI_PORT_7; break;
        }
    }

    return offset;
}
```

# FSI read/write operations for SCOM in Hostboot

```cpp
enum {
    //FSI addresses are byte offsets, so need to multiply by 4
    // since each register is 4 bytes long.
    // prefix with 0x10xx for FSI2PIB engine offset
    DATA0_REG         = 0x1000,  /* SCOM Data Register 0 (0x00) */
    DATA1_REG         = 0x1004,  /* SCOM Data Register 1 (0x01) */
    COMMAND_REG       = 0x1008,  /* SCOM Command Register (0x02) */
    ENGINE_RESET_REG  = 0x1018,  /* Engine Reset Register (0x06) */
    STATUS_REG        = 0x101C,  /* STATUS Register (0x07) */
    PIB_RESET_REG     = 0x101C,  /* PIB Reset Register (0x07) */

    PARITY_CHECK      = 0x04000000, /* 5= Parity check error */
    PROTECTION_CHECK  = 0x01000000, /* 7= Blocked due to secure mode */
    PIB_ABORT_BIT     = 0x00100000, /* 12= PIB Abort */
    PIB_ERROR_BITS    = 0x00007000, /* 17:19= PCB/PIB Errors */
    ANY_ERROR_BIT     = PARITY_CHECK |
                        PROTECTION_CHECK |
                        PIB_ABORT_BIT |
                        PIB_ERROR_BITS
};

errlHndl_t fsiScomPerformOp(DeviceFW::OperationType i_opType,
                         TARGETING::Target* i_target,
                         void* io_buffer,
                         size_t& io_buflen,
                         int64_t i_accessType,
                         va_list i_args)
{
    errlHndl_t l_err = NULL;

    uint64_t l_scomAddr = va_arg(i_args,uint64_t);
    ioData6432 scratchData;
    uint32_t l_command = 0;
    uint32_t l_status = 0;
    size_t op_size = sizeof(uint32_t);
    uint32_t l_any_error_bits = ANY_ERROR_BIT;

    do{

        if( io_buflen != sizeof(uint64_t) )
            die("Invalid data length");

        if( (l_scomAddr & 0xFFFFFFFF80000000) != 0)
            die("Address contains more than 31 bits");

        l_command = static_cast<uint32_t>(l_scomAddr & 0x000000007FFFFFFF);

        if(i_opType == DeviceFW::WRITE)
        {
            memcpy(&(scratchData.data64), io_buffer, 8);

            //write bits 0-31 to data0
            l_err = DeviceFW::deviceOp( DeviceFW::WRITE,
                                        i_target,
                                        &scratchData.data32_0,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(DATA0_REG));
            if(l_err)
            {
                break;
            }

            //write bits 32-63 to data1
            l_err = DeviceFW::deviceOp( DeviceFW::WRITE,
                                        i_target,
                                        &scratchData.data32_1,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(DATA1_REG));
            if(l_err)
            {
                break;
            }

            //write to FSI2PIB command reg starts write operation
             //bit 0 high => write command
            l_command = 0x80000000 | l_command;
            l_err = DeviceFW::deviceOp( DeviceFW::WRITE,
                                        i_target,
                                        &l_command,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(COMMAND_REG));
            if(l_err)
            {
                break;
            }

            //check status reg to see result
            l_err = DeviceFW::deviceOp( DeviceFW::READ,
                                        i_target,
                                        &l_status,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(STATUS_REG));
            if(l_err)
            {
                break;
            }

            // Check the status reg for errors
            if( l_status & l_any_error_bits )
                die("PCB/PIB error received");
        }
        else if(i_opType == DeviceFW::READ)
        {
            //write to FSI2PIB command reg starts read operation
            // bit 0 low -> read command
            l_err = DeviceFW::deviceOp( DeviceFW::WRITE,
                                        i_target,
                                        &l_command,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(COMMAND_REG));
            if(l_err)
            {
                break;
            }

            //check ststus reg to see result
            l_err = DeviceFW::deviceOp( DeviceFW::READ,
                                        i_target,
                                        &l_status,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(STATUS_REG));
            if(l_err)
            {
                break;
            }

            // Check the status reg for errors
            if( l_status & l_any_error_bits )
                die("PCB/PIB error received");

            //read bits 0-31 to data0
            l_err = DeviceFW::deviceOp( DeviceFW::READ,
                                        i_target,
                                        &scratchData.data32_0,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(DATA0_REG));
            if(l_err)
            {
                break;
            }

            //read bits 32-63 to data1
            l_err = DeviceFW::deviceOp( DeviceFW::READ,
                                        i_target,
                                        &scratchData.data32_1,
                                        op_size,
                                        DEVICE_FSI_ADDRESS(DATA1_REG));
            if(l_err)
            {
                break;
            }

            memcpy(io_buffer, &(scratchData.data64), 8);
        }
        else
        {
            die("Unsupported Operation Type");
        }

    } while(0);

    return l_err;
}
```

# FSI initialization in Hostboot

This is basically istep 6.5. IPL flow document says SP has done it already, but
looks like it doesn't apply to OpenPower (wording isn't very clear).

```cpp
/**
 * @brief Initialize the FSI hardware (called through FSI::initializeHardware())
 */
errlHndl_t FsiDD::initializeHardware()
{
    errlHndl_t l_err = NULL;

    do{
        // Determine if we are running on the primary or alternate
        //  master, per the SBE architecture the primary is chip0
        //  and the alternate is chip1
        iv_useAlt = iv_master->getAttr<TARGETING::ATTR_FABRIC_CHIP_ID>();

        typedef struct {
            TARGETING::Target* targ;
            FSI::FsiChipInfo_t info;
        } target_chipInfo_t ;

        // list of ports off of local MFSI
        target_chipInfo_t local_mfsi[MAX_SLAVE_PORTS] = {};

        // list of possible ports off of local cMFSI
        target_chipInfo_t local_cmfsi[MAX_SLAVE_PORTS] = {};

        // array of possible ports to initialize : [mfsi port][cmfsi port]
        target_chipInfo_t remote_cmfsi[MAX_SLAVE_PORTS][MAX_SLAVE_PORTS] = {};

        FSI::FsiChipInfo_t info;

        // loop through every CHIP target
        TARGETING::PredicateCTM l_chipClass(TARGETING::CLASS_CHIP,
                                            TARGETING::TYPE_NA,
                                            TARGETING::MODEL_NA);
        TARGETING::TargetService& targetService = TARGETING::targetService();
        TARGETING::TargetIterator t_itr = targetService.begin();
        while( t_itr != targetService.end() )
        {
            // Sorting into buckets because we must maintain the init order
            //  the MFSI port that goes out to a remote cMFSI driver
            //  must be initialized before we can deal with the cMFSI port
            if( l_chipClass(*t_itr) )
            {
                info = getFsiInfo(*t_itr);

                if( info.type == TARGETING::FSI_MASTER_TYPE_MFSI )
                {
                    local_mfsi[info.port].targ = *t_itr;
                    local_mfsi[info.port].info = info;
                }
                else if( info.type == TARGETING::FSI_MASTER_TYPE_CMFSI )
                {
                    if( info.master == iv_master )
                    {
                        local_cmfsi[info.port].targ = *t_itr;
                        local_cmfsi[info.port].info = info;
                    }
                    else
                    {
                        FSI::FsiChipInfo_t info2 = getFsiInfo(info.master);
                        if( info2.master == NULL )
                        {
                            // Problem with attribute data.
                            // Unexpected attribute data for remote FSI link.
                            l_err = new ERRORLOG::ErrlEntry();
                            break;
                        }

                        remote_cmfsi[info2.port][info.port].info = info;
                        remote_cmfsi[info2.port][info.port].targ = *t_itr;
                    }
                }
            }

            ++t_itr;
        }
        if( l_err ) { break; }

        // Cleanup any initial error states
        l_err = resetPib2Opb( iv_master );
        if( l_err ) { break; }

        // setup the local master control regs for the MFSI
        l_err = initMasterControl(iv_master,TARGETING::FSI_MASTER_TYPE_MFSI);
        if( l_err )
        {
            errlCommit(l_err,FSI_COMP_ID);
        }
        else
        {
            // initialize all of the local MFSI ports
            for( uint8_t mfsi=0; mfsi<MAX_SLAVE_PORTS; mfsi++ )
            {
                bool slave_present = false;
                l_err = initPort(local_mfsi[mfsi].info, slave_present);
                if(l_err)
                {
                    //if this fails then some of the slaves below won't init,
                    //  but that is okay because the detected ports will be
                    //  zero which will cause the initPort call to be a NOOP
                    continue;
                }

                // the slave wasn't present so we can't do anything with the
                //   downstream ports

                if(!slave_present)
                {
                    continue;
                }

                // initialize all of the remote cMFSI ports off the master
                //   we just initialized
                bool master_init_done = false;
                for( uint8_t cmfsi=0; cmfsi<MAX_SLAVE_PORTS; cmfsi++ )
                {
                    // skip ports that have no possible slaves
                    if( remote_cmfsi[mfsi][cmfsi].targ == NULL )
                    {
                        continue;
                    }

                    if( !master_init_done )
                    {
                        // init the remote cMFSI master on this MFSI slave
                        l_err = initMasterControl( local_mfsi[mfsi].targ,
                                                   TARGETING::FSI_MASTER_TYPE_CMFSI );
                        if( l_err )
                        {
                            // commit the log here so that we can move on
                            //  to the next port
                            errlCommit(l_err,FSI_COMP_ID);
                            break;
                        }

                        // only init the master once
                        master_init_done = true;
                    }

                    // initialize the port/slave
                    l_err = initPort( remote_cmfsi[mfsi][cmfsi].info,
                                      slave_present );
                }
            }
        }

        // setup the local master control regs for the cMFSI
        l_err = initMasterControl(iv_master,TARGETING::FSI_MASTER_TYPE_CMFSI);
        if( l_err )
        {
            errlCommit(l_err,FSI_COMP_ID);
        }
        else
        {
            // initialize all of the local cMFSI ports
            for (uint8_t cmfsi=0; cmfsi<MAX_SLAVE_PORTS; cmfsi++)
            {
                // skip ports that have no possible slaves
                if( local_cmfsi[cmfsi].targ == NULL )
                {
                    continue;
                }

                bool slave_present = false;
                l_err = initPort(local_cmfsi[cmfsi].info, slave_present);
            }
        }

    } while(0);

    return l_err;
}

/**
 * @brief Cleanup the FSI PIB2OPB logic on the procs
 *
 * @param[in] i_target  Proc Chip Target to reset
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t FsiDD::resetPib2Opb( TARGETING::Target* i_target )
{
    errlHndl_t errhdl = NULL;

    do {
        uint64_t opb_offset = FSI2OPB_OFFSET_0;
        if(i_target != iv_master
           && i_target->getAttr<TARGETING::ATTR_FSI_OPTION_FLAGS>().flipPort
           && !iv_useAlt)
        {
            // Flipping
            opb_offset = FSI2OPB_OFFSET_1;
        }
        else if(i_target != iv_master && iv_useAlt)
        {
            // Using alt path
            opb_offset = FSI2OPB_OFFSET_1;
        }

        // Clear out OPB error
        uint64_t scom_data = 0;
        size_t scom_size = sizeof(scom_data);

        uint64_t opbaddr = opb_offset | OPB_REG_RES;
        scom_data = 0x8000000000000000; // 0 = Unit Reset
        errhdl = deviceOp( DeviceFW::WRITE,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) { break; }

        opbaddr = opb_offset | OPB_REG_STAT;
        errhdl = deviceOp( DeviceFW::WRITE,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) { break; }

        // Check if we have any errors left
        opbaddr = opb_offset | OPB_REG_STAT;
        scom_data = 0;
        errhdl = deviceOp( DeviceFW::READ,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) { break; }
    } while(0);

    return errhdl;
}

/**
 * @brief Initializes the FSI master control registers
 */
errlHndl_t FsiDD::initMasterControl(TARGETING::Target* i_master,
                                    TARGETING::FSI_MASTER_TYPE i_type)
{
    errlHndl_t l_err = NULL;

    do {
        // Do not initialize the masters because they are already
        //  working before we run
        TARGETING::Target * sys = NULL;
        TARGETING::targetService().getTopLevelTarget( sys );
        TARGETING::SpFunctions spfuncs = sys->getAttr<TARGETING::ATTR_SP_FUNCTIONS>();
        if( spfuncs.fsiMasterInit )
        {
            // Debug print: Skipping Master Init because SP did it
        }

        bool hb_doing_init = !spfuncs.fsiMasterInit;

        uint32_t databuf = 0;

        //find the full offset to the master control reg
        //  first get the address of the control reg to use
        uint64_t ctl_reg = getControlReg(i_type);
        //  append the master port offset to get to the remote master
        if( i_master != iv_master )
        {
            FSI::FsiChipInfo_t m_info = getFsiInfo(i_master);
            ctl_reg += FSI::getPortOffset(TARGETING::FSI_MASTER_TYPE_MFSI,
                                          m_info.port);
        }

        //Always clear out any pending errors before we start anything
        FSI::FsiAddrInfo_t addr_info( i_master, 0 );
        l_err = genFullFsiAddr(addr_info);
        if( l_err ) { break; }

        // Ensure we don't have any errors before we even start
        uint32_t scom_data[2] = {};
        size_t scom_size = sizeof(scom_data);
        uint64_t opbaddr = genOpbScomAddr(addr_info,OPB_REG_STAT);
        l_err = deviceOp( DeviceFW::READ,
                          iv_master,
                          scom_data,
                          scom_size,
                          DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( l_err ) { break; }

        // Temporarily ignore the master-specific errors
        uint32_t old_mask = iv_opbErrorMask;
        iv_opbErrorMask &= ~OPB_STAT_ERR_MFSI;
        iv_opbErrorMask &= ~OPB_STAT_ERR_CMFSI;
        l_err = handleOpbErrors( addr_info, opbaddr, scom_data[0] );
        if( l_err )
        {
            // Unclearable FSI Errors present at the beginning, no choice but to fail
            iv_opbErrorMask = old_mask;
            break;
        }
        iv_opbErrorMask = old_mask;

        // Initialize the FSI Master regs if they aren't already setup
        if( hb_doing_init )
        {
            //Setup clock ratios and some error checking
            // 1= Enable hardware error recovery
            // 3= Enable parity checking
            // 4:13= FSI clock ratio 0 is 1:1
            // 14:23= FSI clock ratio 1 is 4:1
            databuf = 0x50040400;
            l_err = write( ctl_reg|FSI_MMODE_000, &databuf );
            if( l_err ) { break; }

            //Setup error control reg to do nothing
            // 16= Enable OPB_errAck [=1]
            // 18= Freeze FSI port on FSI/OPB bridge error [=0]
            databuf = 0x00008000;
            l_err = write( ctl_reg|FSI_MECTRL_2E0, &databuf );
            if( l_err ) { break; }

            //Clear fsi port errors and general reset on all ports
            for( uint32_t port = 0; port < MAX_SLAVE_PORTS; ++port )
            {
                // 0= Port: General reset
                // 1= Port: Error reset
                // 2= General reset to all bridges
                // 3= General reset to all port controllers
                // 4= Reset all FSI Master control registers
                // 5= Reset parity error source latch
                databuf = 0xFC000000;
                l_err = write( ctl_reg|FSI_MRESP0_0D0|(port*4), &databuf );
                if( l_err ) { break; }
            }
            if( l_err ) { break; }

            //Wait a little bit to be sure the reset is done
            nanosleep( 0, 1000000 ); //sleep for 1ms

            //Setup error control reg for regular use
            // 16= Enable OPB_errAck [=1]
            // 18= Freeze FSI port on FSI/OPB bridge error [=0]
            databuf = 0x00008000;
            l_err = write( ctl_reg|FSI_MECTRL_2E0, &databuf );
            if( l_err ) { break; }

            //Set MMODE reg to enable HW recovery, parity checking,
            //  setup clock ratio
            // 1= Enable hardware error recovery
            // 3= Enable parity checking
            // 4:13= FSI clock ratio 0 is 1:1
            // 14:23= FSI clock ratio 1 is 4:1
            databuf = 0x50040400;

            //Setup timeout so that:
            //   code(10ms) > masterproc (0.9ms) > remote fsi master (0.8ms)
            if( i_master == iv_master )
            {
                // 26:27= Timeout (b01) = 0.9ms
                databuf |= 0x00000010;
            }
            else
            {
                // 26:27= Timeout (b10) = 0.8ms
                databuf |= 0x00000020;
            }

            l_err = write( ctl_reg|FSI_MMODE_000, &databuf );
            if( l_err ) { break; }
        }

        //NOTE: Need to do slave detection even in non-init cases
        //  because we cache this data up to use later

        //Determine which links are present
        l_err = read( ctl_reg|FSI_MLEVP0_018, &databuf );
        if( l_err ) { break; }

        //When FSP has init'ed the bus, MLEVP is toggling,
        //rely only on MENP
        if( spfuncs.fsiSlaveInit )
        {
            l_err = read( ctl_reg|FSI_MENP0_010, &databuf );
            if( l_err ) { break; }
        }

        // Only looking at the top bits
        uint64_t slave_index = getSlaveEnableIndex(i_master,i_type);
        iv_slaves[slave_index] = (uint8_t)(databuf >> (32-MAX_SLAVE_PORTS));

        //If we aren't doing the initialization then we're all done
        if( !hb_doing_init )
        {
            break; //all done
        }


        //Clear FSI Slave Interrupt on ports 0-7
        databuf = 0x00000000;
        l_err = write( ctl_reg|FSI_MSIEP0_030, &databuf );
        if( l_err ) { break; }

        //Set the delay rates
        // 0:3,8:11= Echo delay cycles is 15
        // 4:7,12:15= Send delay cycles is 15
        databuf = 0xFFFF0000;
        l_err = write( ctl_reg|FSI_MDLYR_004, &databuf );
        if( l_err ) { break; }

        //Enable the Ports
        databuf = 0xFF000000;
        l_err = write( ctl_reg|FSI_MSENP0_018, &databuf );
        if( l_err ) { break; }

        //Wait 1ms
        nanosleep( 0, 1000000 );

        //Clear the port enable
        databuf = 0xFF000000;
        l_err = write( ctl_reg|FSI_MCENP0_020, &databuf );
        if( l_err ) { break; }

        //Reset all bridges and ports (again?)
        databuf = 0xF0000000;
        l_err = write( ctl_reg|FSI_MRESP0_0D0, &databuf );
        if( l_err ) { break; }

        //Wait a little bit to be sure reset is done
        nanosleep( 0, 1000000 ); //sleep for 1ms

        //Note: Not enabling IPOLL because we don't support hotplug

        //Turn off Legacy mode
        l_err = read( ctl_reg|FSI_MMODE_000, &databuf );
        if( l_err ) { break; }
        databuf &= ~0x00000040; //25: clock/4 mode
        l_err = write( ctl_reg|FSI_MMODE_000, &databuf );
        if( l_err ) { break; }

    } while(0);

    if( l_err )
    {
        uint64_t slave_index = getSlaveEnableIndex(i_master,i_type);
        iv_slaves[slave_index] = 0;
    }

    return l_err;
}

/**
 * @brief Initializes the FSI link to allow slave access
 */
errlHndl_t FsiDD::initPort(FSI::FsiChipInfo_t i_fsiInfo,
                           bool& o_enabled)
{
    errlHndl_t l_err = NULL;
    o_enabled = false;

    do {
        uint32_t databuf = 0;

        uint8_t portbit = 0x80 >> i_fsiInfo.port;

        // need to add the extra MFSI port offset for a remote cMFSI
        uint64_t master_offset = 0;
        if( (TARGETING::FSI_MASTER_TYPE_CMFSI == i_fsiInfo.type)
            && (i_fsiInfo.master != iv_master) )
        {
            // look up the FSI information for this target's master
            FSI::FsiChipInfo_t mfsi_info = getFsiInfo(i_fsiInfo.master);

            // append the master's port offset to the slave's
            master_offset = FSI::getPortOffset(TARGETING::FSI_MASTER_TYPE_MFSI,
                                 mfsi_info.port );
        }

        // control register is determined by the type of port
        uint64_t master_ctl_reg = getControlReg(i_fsiInfo.type);
        master_ctl_reg += master_offset;

        // slave offset is determined by which port it is on
        uint64_t slave_offset = FSI::getPortOffset( i_fsiInfo.type,
                                                    i_fsiInfo.port );
        slave_offset += master_offset;

        // nothing was detected on this port so this is just a NOOP
        uint8_t slave_list = 0;
        if( !isSlavePresent(i_fsiInfo.master,i_fsiInfo.type,
                            i_fsiInfo.port,slave_list) )
        {
            break;
        }

        // Do not initialize slaves because they are already done
        //  before we run
        TARGETING::Target * sys = NULL;
        TARGETING::targetService().getTopLevelTarget( sys );
        TARGETING::SpFunctions spfuncs =
          sys->getAttr<TARGETING::ATTR_SP_FUNCTIONS>();
        if(spfuncs.fsiSlaveInit)
        {
            o_enabled = true;

            //Reset the port to clear up any previous error state
            //  (using idec reg as arbitrary address for lookups)
            FSI::FsiAddrInfo_t addr_info( i_fsiInfo.slave, 0x1028 );
            l_err = genFullFsiAddr( addr_info );
            if( l_err ) { break; }
            l_err = errorCleanup( addr_info, FSI::RC_ERROR_IN_MAEB );
            if(l_err) { delete l_err; l_err = NULL; }

            break;
        }

        //Write the port enable (enables clocks for FSI link)
        databuf = static_cast<uint32_t>(portbit) << 24;
        l_err = write( master_ctl_reg|FSI_MSENP0_018, &databuf );
        if( l_err ) { break; }

        //Any pending errors before we do anything else?
        l_err = read( master_ctl_reg|FSI_MESRB0_1D0, &databuf );
        if( l_err ) { break; }
        uint32_t orig_mesrb0 = databuf; //save for later

        //Send the BREAK command to all slaves on this port (target slave0)
        //  part of FSI definition, write magic string into address zero
        databuf = 0xC0DE0000;
        l_err = write( slave_offset|0x00, &databuf );
        if( l_err ) { break; }

        //check for errors
        l_err = checkForErrors( i_fsiInfo );
        if( l_err ) { break; }

        // Note: need to write to 1st slave spot because the BREAK
        //   resets everything to that window
        uint32_t tmp_slave_offset = slave_offset;
        if( TARGETING::FSI_MASTER_TYPE_CMFSI == i_fsiInfo.type )
        {
            slave_offset |= FSI::CMFSI_SLAVE_0;
        }
        else if( TARGETING::FSI_MASTER_TYPE_MFSI == i_fsiInfo.type )
        {
            slave_offset |= FSI::MFSI_SLAVE_0;
        }

        //Setup the FSI slave to enable HW recovery, lbus ratio
        // 2= Enable HW error recovery (bit 2)
        // 6:7=	Slave ID: 3 (default)
        // 8:11= Echo delay: 0xF (default)
        // 12:15= Send delay cycles: 0xF
        // 20:23= Local bus ratio: 0x1
        databuf = 0x23FF0100;
        l_err = write( tmp_slave_offset|FSI::SMODE_00, &databuf );
        if( l_err ) { break; }

        //Note - We are not changing the slave ID due to bug HW239758
#if 0 // Leaving code in place in case P9 fixes the bug
        //Note - this is a separate write because we want to have HW recovery
        //  enabled when we switch the window
        //Set FSI slave ID to 0 (move slave to 1st 2MB address window)
        // 6:7=	Slave ID: 0
        databuf = 0x20FF0100;
        l_err = write( tmp_slave_offset|FSI::SMODE_00, &databuf );
        if( l_err ) { break; }
#endif

        //Note : from here on use the real cascade offset

        //Force the local bus to my side
        //databuf = 0x80000000;
        //l_err = write( slave_offset|FSI::SLBUS_30, &databuf );
        //if( l_err ) { break; }
        //Uncomment above if they ever wire it to not default to us

        // wait for a little bit to be sure everything is done
        nanosleep( 0, 1000000 ); //sleep for 1ms

        // No support for slave cascades so we're done
        o_enabled = true;

        //Reset the port to clear up any previous error state
        //  (using idec reg as arbitrary address for lookups)
        //Note, initial cfam reset should have cleaned up everything
        // but this makes sure we're in a consistent state
        FSI::FsiAddrInfo_t addr_info( i_fsiInfo.slave, 0x1028 );
        l_err = genFullFsiAddr( addr_info );
        if( l_err ) { break; }
        l_err = errorCleanup( addr_info, FSI::RC_ERROR_IN_MAEB );
        if(l_err) { delete l_err; l_err = NULL; }

    } while(0);

    return l_err;
}
```

# read, write operations in short

These two functions are similar enough to describe them once. Note that
`OPB_REG_CMD` isn't the same as command/response from
[OpenFSI spec](https://wiki.raptorcs.com/w/images/9/97/OpenFSI-spec-20161212.pdf).

Assumptions:
- FSIM only
- always read/write 4B
- no mutexes
- info.port = 1

```cpp
uint32_t read(uint32_t addr)
void write(uint32_t addr, uint32_t data)
{
	int port = 1;
	uint64_t opbaddr = FSI2OPB_OFFSET_0 + port * 0x10;   // 0x20010

	// make sure there are no other ops running before we start
	// timeout after 10ms, check every 10us, supposedly there is hardware timeout after 1ms
	tmp = read_scom(opbaddr + OPB_REG_STAT);
	while (tmp & OPB_STAT_BUSY && (tmp & 0xFFFC00FC) == 0) {
		delay(10us);
		tmp = read_scom(opbaddr + OPB_REG_STAT);
	}

	if (tmp & 0xFFFC00FC)     // or timeout hit
		die();

	/*
	 * Register is mentioned in docs, but mostly reserved fields. This is what
	 * can be decoded from code:
	 * [0]     WRITE_NOT_READ = 1 for write, 0 for read
	 * [1-2]   size           = 3      // 0b00 - 1B; 0b01 - 2B; 0b11 - 4B
	 * [3-31]  FSI address    = addr   // FSI spec says address is 23 bits
	 * [32-63] data to write  = data   // don't care for read
	 */
	scom_write(opbaddr + OPB_REG_CMD, xxx /* see above */);

	// poll for complete and get the data back
	// timeout after 10ms, check every 10us, supposedly there is hardware timeout after 1ms
	tmp = read_scom(opbaddr + OPB_REG_STAT);
	while ((tmp & OPB_STAT_BUSY) && (tmp & 0xFFFC00FC) == 0) {
		delay(10us);
		tmp = read_scom(opbaddr + OPB_REG_STAT);
	}

	if (tmp & 0xFFFC00FC)     // or timeout hit
		die();

	if (read) {
		if ((tmp & OPB_STAT_READ_VALID) == 0)
			die();
		return tmp & 0xFFFFFFFF;
	}
}
```
