# Dasharo Trustworthy Computing

[https://dasharo.com/](https://dasharo.com/)

Talos II support in coreboot

## v0.1.0

### Changelog

Added:
* SCOM registers support
* RAM initialization
* support for reading from VPD partition

### Statistics

Since bootblock release: \
<span style="color:yellow">61</span> files were changed including \
<span style="color:lightgreen">12408</span> lines of code were added \
<span style="color:orangered">86</span> lines of code were removed

Check the statistics with:
```
git diff --stat 692bd9facd 34f2678e08
```

### Hardware configuration

Configuration with a single IBM POWER9 64bit CPU is supported. \
Dual CPU setup not supported currently.

Following RAM configurations were tested and are proved to be properly initialized.
<pre>
MCS0, MCA0
   DIMM0: <a href=https://www.samsung.com/semiconductor/dram/module/M393A2K40CB2-CTD>1Rx4 16GB PC4-2666V-RC2-12-PA0</a>
   DIMM1: not installed
MCS0, MCA1
   DIMM0: <a href=https://www.crucial.com/memory/server-ddr4/mta9asf1g72pz-2g6j1>1Rx8 8GB PC4-2666V-RD1-12</a>
   DIMM1: not installed
MCS1, MCA0
   DIMM0: <a href=https://www.samsung.com/semiconductor/dram/module/M393A4K40CB2-CTD/>2Rx4 32GB PC4-2666V-RB2-12-MA0</a>
   DIMM1: not installed
MCS1, MCA1
   DIMM0: <a href=https://mis-prod-koce-homepage-cdn-01-blob-ep.azureedge.net/web/static_file/12701730956286135.pdf>2Rx8 16GB PC4-2666V-RE2-12</a>
   DIMM1: not installed
</pre>

All 3 major DRAM vendors are supported, namely Samsung, Micron and Hynix.

### Release binaries

* [dasharo-trustworthy-computing-v0.1.0.rom.signed.ecc.SHA256.sig](https://cloud.3mdeb.com/index.php/s/QX5CcteHppoNynT)
* [dasharo-trustworthy-computing-v0.1.0.rom.SHA256.sig](https://cloud.3mdeb.com/index.php/s/Kq9GbWwZegWQdpb)
* [dasharo-trustworthy-computing-v0.1.0.rom.signed.ecc](https://cloud.3mdeb.com/index.php/s/7F9zxPcRnaBkRiD)
* [dasharo-trustworthy-computing-v0.1.0.rom.signed.ecc.SHA256](https://cloud.3mdeb.com/index.php/s/4arNninMLdYZwxt)
* [dasharo-trustworthy-computing-v0.1.0.rom](https://cloud.3mdeb.com/index.php/s/4Aa9Et3eL44yzsn)
* [dasharo-trustworthy-computing-v0.1.0.rom.SHA256](https://cloud.3mdeb.com/index.php/s/xBrXpbqPWpJXydw)

See how to verify signatures on
[![asciinema](https://asciinema.org/a/4HLDDfLNBshqEVi9wEBdMuFXf.svg)](https://asciinema.org/a/4HLDDfLNBshqEVi9wEBdMuFXf?t=7)

### How to build and use it

#### Environment preparation

In order to build coreboot, we use docker container. So in order to setup
environment, ensure that:

1. You have docker installed as described on [docker site](https://docs.docker.com/engine/install/)
   for your Linux distro.
2. When you have the docker installed pull the container:

   ```
   docker pull coreboot/coreboot-sdk:65718760fa
   ```

In order to start from a common point, flash the original OpenPOWER firmware
for Talos II.

1. Log into the BMC via SSH:

   ```
   ssh root@<BMC_IP>
   ```

2. Download the stock firmware image:

   ```
   wget https://cloud.3mdeb.com/index.php/s/canxPx5d4X8c2wk/download -O /tmp/flash.pnor
   ```

3. Flash the firmware:

   ```
   pflash -E -p /tmp/flash.pnor
   ```

   > You will see warning like `About to erase chip !` and
   > `WARNING ! This will modify your HOST flash chip content !`. When the
   > `Enter "yes" to confirm:` prompt appears, type `yes` and press enter.

   At the end of the process (it may take several minutes) you should have
   something like this:

   ```
   About to program "/tmp/flash.pnor" at 0x00000000..0x04000000 !
   Programming & Verifying...
   [==================================================] 100% ETA:0s
   ```

4. * Log into the BMC GUI at https://<BMC_IP>/. \
     Make sure to use `https`.
   * Enter the Server power operations
     `https://<BMC_IP>/#/server-control/power-operations` and invoke
     warm reboot.
   * Then move to Serial over LAN remote console
     `https://<BMC_IP>/#/server-control/remote-console` to observe
     whether the platform is booting.

#### Buidling coreboot image

In order to build coreboot image, follow the steps below:

1. Clone the coreboot repository:

   ```
   git clone git@github.com:3mdeb/coreboot.git -b dasharo-trustworthy-computing-v0.1.0
   # or HTTPS alternatively
   git clone https://github.com/3mdeb/coreboot.git -b dasharo-trustworthy-computing-v0.1.0
   ```
   `talos_2_support` is the main development branch for Talos II support.

2. Get the submodules:

   ```
   cd coreboot
   git submodule update --init --checkout
   ```

3. Start docker container (assuming you are already in coreboot root
   directory):

   ```
   docker run --rm -it -v $PWD:/home/coreboot/coreboot -w /home/coreboot/coreboot coreboot/coreboot-sdk:65718760fa /bin/bash
   ```

4. When inside of the container, configure the build for Talos II:

   ```
   make menuconfig
   ```

   * Navigate to the **Mainboard** submenu.
   * As a **Mainboard vendor** select `Raptor Computing Systems`
   * If it wasn't selected autmatically, as **Mainboard model** select `Talos II`
   * In the **ROM chip size** option select `512 KB`
   * Save the configuration and exit.

   ![make menuconfig](images/cb_menuconfig_romstage.png)

5. Start the build process of coreboot inside the container:

   ```
   make
   ```

#### Running the coreboot on Talos II

1. At the end of build process you should see `Built raptor-cs/talos-2 (Talos II)`.
   Copy the result binary from `<coreboot_dir>/build/coreboot.rom.signed.ecc` to the BMC
   (assuming in the coreboot root directory):

   ```
   scp build/coreboot.rom.signed.ecc root@<BMC_IP>:/tmp
   ```

2. Backup the HBB partition (for faster later recovery) by invoking this
   command on BMC:

   ```
   pflash -P HBB -r /tmp/hbb.bin
   ```

3. Flash the binary by replacing HBB partition (execute from BMC):

   ```
   pflash -e -P HBB -p /tmp/coreboot.signed.ecc
   ```

   Answer yes to the prompt and wait for the process to finish.

4. Log into the BMC GUI again at https://<BMC_IP>/. Enter the Server power
   operations (https://<BMC_IP>/#/server-control/power-operations) and invoke
   warm reboot. Then move to Serial over LAN remote console
   (https://<BMC_IP>/#/server-control/remote-console)

   Wait for a while until coreboot shows up:

   [![asciicast](https://asciinema.org/a/hbeSMdHqHxJiYKxZCdRq3AIGa.svg)](https://asciinema.org/a/hbeSMdHqHxJiYKxZCdRq3AIGa)

5. Enjoy the coreboot running on Talos II.

> **Optional:** In order to recovery the platform quickly to healthy state, flash
> the HBB partition back with: \
> `pflash -e -P HBB -p /tmp/hbb.bin`

### Coming soon
1. Building a HDAT structure
2. Booting skiboot as a payload
