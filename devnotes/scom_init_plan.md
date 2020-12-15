# About this document
This document describes plans of implementing SCOM init steps 8.9 and 8.10

# soc
Here files that are directly responsible for soc
- Add ibm directory
  - Add power9 directory
    - Add Kconfig specific to POWER9 family
    - Add bootblock, and romstage that are responsible for cpu configuration
    - Memlayout is already defined in Motherboard section,\
      so it is probably not needed here
    - Add chip.c for cpu structures
  - Add Kconfig for all POWER processors

# Access to scom
According to Christian Geddes, access to SCOM is possible through XSCOM
using offset 0x000603FC00000000\
[source](https://lists.ozlabs.org/pipermail/openpower-firmware/2020-December/000602.html)

# Mainboard
- Memory layout
- Bootblock

# Summary
