## Topology of POWER9 memory system

Different names are used in the documentation than in the code. Sometimes the
scope of a component is slightly different between code and docs. The list below
may not provide exact matching, but it should be enough for counting elements on
a given level in topology. Name used in the code is listed first, documentation
names are in brackets.

* MCBIST (MC, memory controller in documentation) - one per CPU
* MCS (EMC, extended memory controller) - two per MCBIST
* MCA (MCU, port, sometimes DDR PHY, although PHY is just a part of MCU) - two
  per MCS, four per MCBIST
* DIMM (physical memory stick) - two per MCA, four per MCS, eight per MCBIST

Additionally, DIMM consists of one or two master ranks. Higher numbers are not
supported by this PHY, but JEDEC defines up to 8 master ranks (package ranks per
DIMM). There are also slave (logical) ranks in 3DS stacks; a maximum of four
logical ranks is supported by RDIMM. See DDR4 SPD specification for details
(byte 12), as well as RDIMM specification for ranks limitations caused by the
use of additional register.

## VPD

VPD is saved in MEMD partition in PNOR. That partition is protected with ECC.
MEMD has a header (in addition to obligatory partition and SB headers), its
format is described in `src/build/buildpnor/memd_creation.pl`:

```
# Header format:
#   uint32_t eyecatch        /* Eyecatch to determine validity "OKOK" */
#   uint32_t header_version  /* What version of the header this is in */
#   uint32_t memd_version    /* What version of the MEMD this includes */
#   uint32_t expected_size   /* Size in megabytes of the biggest MEMD section */
#   uint16_t expected_num    /* Number of MEMD instances in this section */
```

Even though `*_version` fields are described as `uint32_t` they are written as
an ASCII, it is "01.0" for both fields. `expected_size` is 21 (0x15), the
description in a comment listed above is obviously wrong. According to the code,
it is the size of the biggest section (there is only one section in this case)
divided (as an integer) by 1000 (not 1024), increased by one. This means that a
blob that is exactly 7000 bytes long will have 8 in this field, so this is not a
proper rounding. `expected_num` is 1. After these fields there is a 8B padding
(fields reserved for the future) and an alignment to 16 bytes (6B), not sure why
these two parts are separated. Both of these fields are filled with ASCII '0'
characters.

After that comes the VPD itself. It is almost an exact copy of
`sforza-MEMD.rvpd` from [talos-xml](https://git.raptorcs.com/git/talos-xml/tree/memd_binaries)
repository, except the first byte of the file (always 0x84) is skipped. This
makes the rounding algorithm used in the header even less accurate. The actual
size of data is always smaller than the one written in the header so if code
allocates buffer based on the size reported there it should never overflow, but
it may unnecessarily waste some memory.

Binary VPD file is created along with [blackbird-MEMD.tvpd](https://git.raptorcs.com/git/blackbird-vpd/tree/mainboard/blackbird-MEMD.tvpd)
(yes, Talos uses VPD from Blackbird, see README in `talos-xml/memd_binaries`).
This is an XML file with most of the fields written in hex. It is much bigger
than the binary version and still doesn't give any useful pieces of information
about the meaning of its bytes. Its format is described [here](https://git.raptorcs.com/git/vpdtools/tree/docs/xmlformat.md).

The most useful and readable documentation can be found in the [genMemVpd.pl script](https://git.raptorcs.com/git/talos-hostboot/tree/src/import/tools/genMemVpd.pl).
It describes the roles of individual fields, as well as number of possible and
implemented configurations. There are also examples of `.vpd` files which are
used to create both `*.rvpd` (raw) and `*.tvpd` (text) versions of VPD. These
source files are nowhere to be found. For details please refer to `genMemVpd.pl`
but this is a quick introduction to VPD fields:

* RT, VD, VM - metadata; in order: type, version and timestamp of this record
* MR - Memory Rotator mapper; maps MCS, rank configuration and frequency to one
  of the J# configurations, for MR actually only the number of populated DIMM
  slots (aka drops) is taken into account
* J0-J9, JA-JZ - Memory Rotator data
* MT - Memory Terminator mapper; maps MCS, rank configuration and frequency to
  one of the X# configurations, for MT only rank configuration changes between
  X# configurations, MCS and frequency masks specify all possible positions (at
  least in VPD for Talos)
* X0-X9, XA-XZ - Memory Terminator data
* CK - Memory Clock Enable
* Q0 - DQ mapper
* Q1-Q9 - DQ data

For Talos, there are 32 individual entries in MR (4 MCS positions \* 2 drop
configurations \* 4 frequencies) and 4 in MT (only possible rank configurations:
1R in DIMM0 and no DIMM1, 1R for both DIMMs, 2R in DIMM0 and no DIMM1, 2R in
both DIMMs). J# configurations for all possible MCS positions are separate even
though they are the same, which gives just 8 unique combinations of settings for
MR.

There are also 2 distinctive DQ configurations mapped by 8 entries in Q0, based
on the MCS position. Nevertheless, both configurations are identical.

No separate configurations exist for nonfunctional (unpopulated) MCAs/MCSs,
those are simply not configured at all.

#### VPD decoding

VPD is decoded as a part of istep 7.4, on a per-MCS basis, by calling a function
located in `src/import/chips/p9/procedures/hwp/memory/lib/dimm/eff_dimm.C`:

```
fapi2::ReturnCode eff_dimm::decode_vpd(const fapi2::Target<TARGET_TYPE_MCS>& i_target)
```

This function retrieves data for a given set of MCS, rank and frequency masks
for all ports (MCAs) where appropriate. It then passes the information read from
VPD to another function, located in `src/import/chips/p9/procedures/hwp/memory/lib/mss_vpd_decoder.H`:

```
fapi2::ReturnCode eff_decode(
        const fapi2::Target<fapi2::TARGET_TYPE_MCS>& i_target,
        const std::vector<uint8_t*>& i_mt_blob,
        const uint8_t* i_mr_blob,
        const uint8_t* i_cke_blob,
        const uint8_t* i_dq_blob)
```

> As you can see, there is a vector for MT blobs (one element per port), but
> only one MR blob. As the MR configuration index depends on number of drops,
> it may be different for different ports. Hostboot uses MR based on the last
> functional MCA parsed. This is either an oversight, or it may as well be
> accounted for by a proper order of DIMM population.

`eff_decode()` is just a long list of about 80 function calls to the actual
parsers. Each of them takes MCS target and one of the blobs as its parameters.
An example of a function called by `eff_decode()` (error checking was removed to
improve readability):

```
fapi2::ReturnCode vpd_mt_dram_rtt_wr(
        const fapi2::Target<fapi2::TARGET_TYPE_MCS>& i_target,
        const std::vector<uint8_t*>& i_blobs)
{
    uint8_t l_value[2][2][4] = {};
    constexpr uint64_t l_start = 70;
    constexpr uint64_t l_length = 16;

    constexpr uint64_t l_num_bytes_to_copy = l_length / mss::PORTS_PER_MCS;

    for (const auto& p : mss::find_targets<fapi2::TARGET_TYPE_MCA>(i_target))
    {
        const auto l_index = mss::index(p);
        const uint8_t* l_blob = i_blobs[l_index];
        const uint64_t l_offset = l_start + (l_index * l_num_bytes_to_copy);

        memcpy(&(l_value[l_index][0][0]), l_blob + l_offset, l_num_bytes_to_copy);
    }

    FAPI_ATTR_SET(fapi2::ATTR_MSS_VPD_MT_DRAM_RTT_WR, i_target, l_value);
}
```

Functions for decoding MR usually are even simpler, as they don't have a loop
for MCAs inside them. Multibyte values have their endianness established by a
call to one of the `beXXtoh()` functions, even though those are no-ops for big
endian Hostboot. The layout of VPD is sometimes changed. The code has to deal
with such changes, e.g. in `vpd_mt_mc_dq_acboost_rd_up()`:

```
    uint8_t l_layer_version = 0;
    FAPI_TRY( mss::vpd_mt_0_version_layout(i_target, l_layer_version) );

    switch(l_layer_version)
    {
        case 0:
            l_offset = 86;
            l_num_bytes_to_copy = 8;
            break;

        case 1:
            l_offset = 88;
            l_num_bytes_to_copy = 8;
            break;

        default:
            FAPI_ERR("Invalid layer version received: %d for %s", l_layer_version, mss::c_str(i_target));
            fapi2::Assert(false);
            break;

    };
```
