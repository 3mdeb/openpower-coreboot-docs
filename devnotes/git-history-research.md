# hostboot git history lookup

## init file compiler

* source code from 2011:

https://github.com/open-power/hostboot/blob/0f6e334655d2eb7026e9f992a1b32b6d37915563/src/build/ifcompiler/makefile#L6

* `ifcompiler/initCompiler.C` history:

```
git lola -- src/build/ifcompiler/initCompiler.C
* 11c80c5abcf2 HWPF: Only support initfile attributes in fapiGetInitFileAttr()
* 0f6e334655d2 SCOM Initfile - Updates based on design doc review with hardware team.
* d034089348a1 Initial SCOM initfile compiler support.
```

* it was later moved to `src/usr/hwpf/ifcompiler/initCompiler.C`

* the history of `src/usr/hwpf/ifcompiler/initCompiler.C`

```
git lola -- src/usr/hwpf/ifcompiler/initCompiler.C
* 76f1c48130a0 Removing some more old fapi1 and hwp code
* 1ae25b625d12 SW265478: INITPROC: FSP - FAPI updates needed so file versions will appear in th
* ce5d000adc77 Fix uninitialized value in initfile compiler
* aa0446e9d1c2 Change copyright prolog for all files to Apache.
* 5151c0b22f5d SCOM Initfile:  Improve error and debug tracing in the initfile compiler error/debug paths.
* 4a47221cbf1a Right justify SCOM data
* 11c80c5abcf2 HWPF: Only support initfile attributes in fapiGetInitFileAttr()
```

* the `initCompiler.C` was completey removed in the
  `76f1c48130a060fbe83c851fce2474c17b2df9b2` commit

* it means that the most recent version is available in the
  `3967f43b9478d7e6b58180dd0b331e61412997cd` commit

* it cannot be compiled because of the missing header

```
    CXX        initCompiler.C
In file included from ifcompiler/initCompiler.H:47,
                 from ifcompiler/initCompiler.C:52:
ifcompiler/initRpn.H:55:10: fatal error: fapiHwpInitFileInclude.H: No such file or directory
   55 | #include <fapiHwpInitFileInclude.H>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~
compilation terminated.
make[1]: *** [makefile:351: ../../../obj/genfiles/hwp_ifcompiler/initCompiler.host.o] Error 1
make: *** [../../../src/build/mkrules/passes.rules.mk:105: _BUILD/PASSES/GEN/BODY] Error 2
```

* The mentioned header was removed in `4b4772ef8b18f2e9c80795c47b3a5f81b3521c1f`
