# This file describes MVPD partition internal structure

## TOC
At the begining of `MVPD` partition `Table Of Content` is placed.
It includes names and addresses of `Records` counting from the begining
of the partition, but not including checksums.
`TOC` can have up to 32 entries, but only 20 are filled by `Hostboot`.

```cpp
struct toc_def {
  char recordName[4],
  record* recordAddress,  // Little Endian
                          // Offset from the beginning of partition.
                          // Doesn't include checksums.
                          // Remember to account for that if present
                          // or remove checksums using ecc tool.
  uint16_t unused,
};

toc_def TOC[32];
```

## Record
Each `Record` includes some kind of address which purpouse is yet unknown,
`RT Keyword` holding 1 byte of data, string name of the record
and an array of `Keywords` which can be as long as
the space before the next `Keyword` beginning.

Following records are present in `MVPD` after `Hostboot` has filled the partition:\
`CP00`, `CRP0`, `LRP0`, `LRP1`,\
`LRP2`, `LRP3`, `LRP4`, `LRP5`,\
`LWP0`, `LWP1`, `LWP2`, `LWP3`,\
`LWP4`, `LWP5`, `MER0`, `VER0`,\
`VINI`, `VMSC`, `VRML`, `VWML`\
It is unknown if other `Keyword` names could also be present in `MVPD`.

```cpp
struct record {
  uint16_t address,
  keyword RTKeyword,    // Total size of this keyword is always 3
                        // containing only 1 byte of data.
  char recordName[4],
  keyword keywords[]    // Can hold any amount of keywords.
}

```

## Keyword

There are two possible types of `Keywords`. `Pound` and `Non-Pound` ones.
In each case the structure is a bit different and `Pound Keywords` can be larger.

Only some defined `Keywords` names are valid.

Possible `Keyword` names:\
`20`, `21`, `30`, `31`, `AW`, `CC`,\
`CE`, `CH`, `CT`, `DD`, `DN`, `DR`,\
`ED`, `FN`, `HE`, `HW`, `IN`, `IQ`,\
`L1`, `L2`, `L3`, `L4`, `L5`, `L6`,\
`L7`, `L8`, `PB`, `PG`, `PK`, `PM`,\
`PN`, `PR`, `PZ`, `RT`, `SB`, `SN`,\
`TE`, `VD`, `VZ`

Possible `Pound Keyword` names:\
`#G`, `#H`, `#I`, `#M`, `#R`, `#V`, `#W`

```cpp
struct keyword {
  char keywordName[2],
  uint8_t keywordSize, // Little Endian
                       // Doesn't include checksums.
  char data[keywordSize],
};

struct pound_keyword {
  char keywordName[2],  // NOTE must include leading '#'
  uint16_t keywordSize, // Little Endian
                        // Doesn't include checksums.
  char data[keywordSize],
} ;
```
