# Grepping through registers PDFs

## pdfgrep

* [homepage](https://pdfgrep.org/)

* installation

```
sudo apt install pdfgrep
```

## Basic grep

```
pdfgrep --cache --page-number 800AE8000601143F \
  POWER9_Registers_vol1_version1.1_pub.pdf \
  POWER9_Registers_vol2_version1.2_pub.pdf \
  POWER9_Registers_vol3_version1.2_pub.pdf
```

The result is something like:

```
POWER9_Registers_vol2_version1.2_pub.pdf:191:IOF2.RX.RX0.RXCTL.GLBSM.REGS.RX_GLBSM_CNTL3_EO_PG                        0x800AE8000601143F      1128
POWER9_Registers_vol2_version1.2_pub.pdf:1128: Address          800AE8000601143F (SCOM)
```

It provides useful info such as the volmue and page number.

## More advanced tries

We could try to parse required information from the PDF files and create some
kind of database based on that. But there are a few problems

Some basic tries to parse out all addresses:

```
pdfgrep --cache --no-filename  "Address\s+[[:alnum:]]+\s+\(SCOM\)" \
  POWER9_Registers_vol1_version1.1_pub.pdf \
  POWER9_Registers_vol2_version1.2_pub.pdf \
  POWER9_Registers_vol3_version1.2_pub.pdf \
  | tr -d ' ' | sed -e "s/Address//"
```

11061 were found

Some basic tries to parse out all mnemonics:

```
pdfgrep --cache --no-filename  " Mnemonic\s+[[:alnum:]]+\.[[:alnum:]]" \
  POWER9_Registers_vol1_version1.1_pub.pdf \
  POWER9_Registers_vol2_version1.2_pub.pdf \
  POWER9_Registers_vol3_version1.2_pub.pdf \
  | tr -d ' ' | sed -e "s/Mnemonic//"
```

11253 were found

Quick look indicates that it matches 1:1 (N entry in `address` list matches
with N entry in the `mnemonics` list) until around 7-8k entry.

There are a few exceptions in the PDFs tables which would make the parsing more
complicated (but it still should be feasible).

For example, some Address fields contains two or more entries like here:

```
000000000006C42E (SCOM)
000000000006C52E (SCOM1)
```

Only the first one one would be parsed out this way, as only the first entry in
the table is in the same line with the `Address` string.

```
pdfgrep --cache --page-number 000000000006C42E  \
  POWER9_Registers_vol1_version1.1_pub.pdf \
  POWER9_Registers_vol2_version1.2_pub.pdf \
  POWER9_Registers_vol3_version1.2_pub.pdf

POWER9_Registers_vol1_version1.1_pub.pdf:47:TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OPIT1C14    0x000000000006C42E      293
POWER9_Registers_vol1_version1.1_pub.pdf:293: Address               000000000006C42E (SCOM)


pdfgrep --cache --page-number 000000000006C52E  \
  POWER9_Registers_vol1_version1.1_pub.pdf \
  POWER9_Registers_vol2_version1.2_pub.pdf \
  POWER9_Registers_vol3_version1.2_pub.pdf

POWER9_Registers_vol1_version1.1_pub.pdf:293:                       000000000006C52E (SCOM1)
```
