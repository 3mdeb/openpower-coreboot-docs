# openpower-coreboot-docs

Documentation related to POWER9 coreboot porting effort.

## Introduction

3mdeb Embedded Systems Consulting is porting POWER9 architecture with Raptor
Comupting Systems' Talos II and Talos II lite as reference platforms. The
project has been initiated and is sponsored by Insurgo Technologies Libres/Open
Technologies. The development process is open and anyoen can join. See
[How to help and contribute](#how-to-help-and-contribute) section.

## Repository overview

* [devnotes](devnotes/) - various developer notes created during the porting
* [logs](logs/) - a place to put important dumps and logs, which can be linked
  in the documents
* [images](images/) - directory containins images linked in the documents

## Public documentation

Various related documentation of OpenPOWER architecture, registers and
programming guides:

- [OpenPOWER 64bit ELF ABI](http://cdn.openpowerfoundation.org/wp-content/uploads/resources/leabi/leabi-20170510.pdf)
- [OpenPOWER 64bit ELF ABI errata](http://cdn.openpowerfoundation.org/wp-content/uploads/resources/elfv2-1_4-errata-9/elfv2-1_4-errata-20180313.pdf)
- [POWER9 IPL flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)
- [OpenFSI specification](https://wiki.raptorcs.com/w/images/9/97/OpenFSI-spec-20161212.pdf)
- [POWER9 processor programming model](https://ibm.ent.box.com/s/8qsbki409iq704wx5gvikz8h6fj8ixre)
- [POWER9 Registers vol1](https://ibm.ent.box.com/s/ddcdl3g0otdzyiajhkfe3jjh2oy5p3mt)
- [POWER9 Registers vol2](https://ibm.ent.box.com/s/gcg7o0sgke0cdqqw2z9pc9xc7zgjj1wu)
- [POWER9 Registers vol3](https://ibm.ent.box.com/s/flt3hs6eiwd9glq3yzzff0flnup2j7p0)
- [POWER ISA v3.0B](https://ibm.ent.box.com/s/1hzcwkwf8rbju5h9iyf44wm94amnlcrv)
- [POWER9 processor errata](https://ibm.ent.box.com/s/0ixfserqjzjmt3q6vabotz9arxzs59md)

Other useful information extracted form documents aboive may be found in
[ppc.md](devnotes/ppc.md).

Also information about the porting process are included in
[porting.md](devnotes/porting.md).

## How to help and contribute

If you have Talos II or Talos II Lite simply joins forces with us. Everything
is developed in open on 3mdeb's GitHub:

- [coreboot](https://github.com/3mdeb/coreboot/tree/talos_2_support), use
  `talos_2_support` branch as base, create own branch and set up a PR
- [pnor](https://github.com/3mdeb/pnor/tree/coreboot_support), use `coreboot`
  branch as a base, create own branch and set up a PR
- [talos-op-build](https://github.com/3mdeb/talos-op-build/tree/coreboot_support),
  use `coreboot` branch as a base, create own branch and set up a PR
- [op-docker](https://github.com/3mdeb/op-docker), dockerized environment to
  build full PNOR images for Talos II op-build, refer to its
  [README](https://github.com/3mdeb/op-docker/blob/main/README.md) to get
  details how to use it. Use own branches for `talos-op-build` to test your
  changes. Refer to [Implementing op-build support for coreboot](devnotes/porting.md#implementing-op-build-support-for-coreboot)
  to get details how to modify op-build to your needs.

If you need another repo, let us know.

**Do not have hardware?** No problem, we can arrange remote access to our
development machine. Just [contact us](mailto:contact@3mdeb.com) to get
details.

**Do not have time to develop?** No problem. If you are knowledgeable about
OpenPOWER and POWER9 architecture, feel free to use your knowledge to support
us (e.g. through PR reviews).
