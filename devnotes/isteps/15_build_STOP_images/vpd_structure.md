```cpp

// TOC at the begining of the MVPD partition has up to 32 entries
toc_entry TOC[32];

struct toc_entry {
  char* name[4],
  uint16_t offset, // if looking at the raw, undecoded data,
                   // remember to multiply this address by 1.125
                   // to account for checksums every 9th byte
  uint16_t unused,
};

// possible keyword names:
// "20","21","30",
// "31","AW""CC","CE","CH","CT",
// "DD","DN","DR","ED","FN","HE",
// "HW","IN","IQ","L1","L2","L3",
// "L4","L5","L6","L7","L8","PB",
// "PG","PK","PM","PN","PR","PZ",
// "RT","SB","SN","TE","VD","VZ",

// possible pound keyword names:
// "#G","#H","#I","#M","#R","#V","#W"


struct keyword {
  uint16_t record_addr,
  uint8_t skip[3],
  char* keywordName[2],
  uint8_t keywordSize,
};

struct pound_keyword {
  uint16_t record_addr,
  uint8_t skip[3],
  char* keywordName[2], // NOTE must include leading '#'
  uint16_t keywordSize,
} ;
```
