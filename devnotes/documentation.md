class: center, middle, intro

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

1. Log into the BMC via SSH:

   ```
   ssh root@23.155.224.90
   ```

   > The password is `wfv978h4JSG`

2. Download the stock firmware image:

   ```
   wget https://cloud.3mdeb.com/index.php/s/canxPx5d4X8c2wk/download \
         -O /tmp/flash.pnor
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

4. Log into the BMC GUI at https://23.155.224.90/. Enter the
   [Server power operations](https://23.155.224.90/#/server-control/power-operations)
   and invoke warm reboot. Then move to [Serial over LAN remote console](https://23.155.224.90/#/server-control/remote-console)
   to observe whether the platform is booting. It should boot up to Debian.

---

## Buidling coreboot image

In order to build coreboot image, follow the steps below:

1. Clone the coreboot repository:

   ```
   git clone git@github.com:InsurgoTech/coreboot.git -b power_bootblock
   # or HTTPS alternatively
   git clone https://github.com/InsurgoTech/coreboot.git -b power_bootblock
   ```

2. Get the submodules:

   ```
   cd coreboot
   git submodule update --init --checkout
   ```

3. Start docker container (assuming you are already in coreboot root
   directory):

   ```
   docker run --rm -it -v $PWD:/home/coreboot/coreboot \
      -w /home/coreboot/coreboot coreboot/coreboot-sdk:65718760fa /bin/bash
   ```

4. When inside of the container, configure the build for Talos II:

   ```
   make menuconfig
   ```

   Navigate to the Mainboard submenu and select the:
   `Raptor Computign Systems -> Talos II`. Then save the configuration and
   exit.

   .center[.image-90[![](images/cb_menuconfig.png)]]

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
   scp build/coreboot.rom root@23.155.224.90:/tmp
   ```

2. Backup the HBB partition (for faster later recovery) by invoking this
   command on BMC:

   ```
   pflash -P HBB -r /tmp/hbb.bin
   ```

3. Flash the binary by replacing HBB partition (execute from BMC):

   ```
   pflash -e -P HBB -p /tmp/coreboot.rom
   ```

   Answer yes to the prompt and wait for the process to finish.

4. Log into the BMC GUI again at https://23.155.224.90/. Enter the
   [Server power operations](https://23.155.224.90/#/server-control/power-operations)
   and invoke warm reboot. Then move to [Serial over LAN remote console](https://23.155.224.90/#/server-control/remote-console)

   Wait for a while until coreboot shows up:

   .center[.image-90[![](images/cb_bootblock.png)]]

5. Enjoy coreboot bootblock running on Talos II.

> OPTIONAL: in order to recovery the platform quickly to healthy state, flash
> the HBB partition back with:
> `pflash -e -P HBB -p /tmp/hbb.bin`

---
class: center, middle, outro
