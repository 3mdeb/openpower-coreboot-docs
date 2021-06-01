## OpenPOWER coreboot

## Documentation

### v1.0.0

---

## Environment preparation

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

1. Checkout Talos II in [snipeit](http://snipeit) to avoid conflicts when
   someone else is also working with the device.\
   **Note:** `snipeit` is an internal tool avilable only from 3mdeb's LAN
   network or via VPN connection.

2. Log into the BMC via SSH:

   ```
   ssh root@<BMC_IP>
   ```
   Ask the administrator for IP address and password to the Talos II BMC.

3. Download the stock firmware image:

   ```
   wget https://cloud.3mdeb.com/index.php/s/canxPx5d4X8c2wk/download -O /tmp/flash.pnor
   ```

4. Flash the firmware:

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

5. * Log into the BMC GUI at https://\<BMC_IP\>/.\
     Make sure to use `https`.
   * Enter the Server power operations
     `https://\<BMC_IP\>/#/server-control/power-operations` and invoke
     warm reboot.
   * Then move to Serial over LAN remote console
     `https://\<BMC_IP\>/#/server-control/remote-console` to observe
     whether the platform is booting.

---

## Buidling coreboot image

In order to build coreboot image, follow the steps below:

1. Clone the coreboot repository:

   ```
   git clone git@github.com:3mdeb/coreboot.git -b talos_2_support_ramstage
   # or HTTPS alternatively
   git clone https://github.com/3mdeb/coreboot.git -b talos_2_support_ramstage
   ```
   `talos_2_support_ramstage` - ramstage devlopment branch - merge requests should go here
   `squashed_talos_2_support` - upstream branch, can be regularly pushed with force
   `talos_2_support` - legacy branch for bootblock and romstage release - as of today nothing should be pushed here

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

   ![](../images/cb_menuconfig.png)

5. Start the build process of coreboot inside the container:

   ```
   make
   ```

---

## Running the coreboot on Talos II

1. At the end of build process you should see `Built raptor-cs/talos-2 (Talos II)`.
   Copy the result binary from `<coreboot_dir>/build/coreboot.rom` to the BMC
   (assuming in the coreboot root directory):

   ```
   scp build/coreboot.rom.signed.ecc root@<BMC_IP>:/tmp
   ```
   > If that file is not present, use `coreboot.rom` instead

2. Backup the HBB partition (for faster later recovery) by invoking this
   command on BMC:

   ```
   pflash -P HBB -r /tmp/hbb.bin
   ```

3. Flash the binary by replacing HBB partition (execute from BMC):

   ```
   pflash -e -P HBB -p /tmp/coreboot.signed.ecc
   ```
   > Again, if that file is not present, use `coreboot.rom` instead

   Answer yes to the prompt and wait for the process to finish.

4. Log into the BMC GUI again at https://\<BMC_IP\>/. Enter the Server power
   operations (https://\<BMC_IP\>/#/server-control/power-operations) and invoke
   warm reboot. Then move to Serial over LAN remote console
   (https://\<BMC_IP\>/#/server-control/remote-console)

   Wait for a while until coreboot shows up:

   [![asciicast](https://asciinema.org/a/OTEPFRHlasyXQI2eRBLso0AB0.svg)](https://asciinema.org/a/OTEPFRHlasyXQI2eRBLso0AB0)

5. Enjoy the coreboot running on Talos II.

> **Optional:** In order to recovery the platform quickly to healthy state, flash
> the HBB partition back with: \
> `pflash -e -P HBB -p /tmp/hbb.bin`

## Hardware configuration

Configuration with a single IBM POWER9 64bit CPU is supported. \
Dual CPU setup not supported currently.

Following RAM configurations were tested and are proved to be properly initialized.
   ```
   MCS0, MCA0
      DIMM0: 1Rx4 16GB PC4-2666V-RC2-12-PA0
      DIMM1: not installed
   MCS0, MCA1
      DIMM0: 1Rx8 8GB
      DIMM1: not installed
   MCS1, MCA0
      DIMM0: 2Rx4 32GB PC4-2666V-RB2-12-MA0
      DIMM1: not installed
   MCS1, MCA1
      DIMM0: 2Rx8 16GB PC4-2666V-RE2-12
      DIMM1: not installed
   ```
