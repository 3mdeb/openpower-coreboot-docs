# Talos II Secure Boto control

The `FSI_CP0_CMD` and `FSI_CP1_CMD`  are responsible for controllign the secure
boot state. The pin state is probed by SBE at the launch time. High state
(1.1V) disables the Secure Boot while leaving it floating enables it. A simple
electric circuit has been created to remotely control the Secure Boot state.
When Secure Boot is disabled, one may read SCOM reigsters from BMC level.

The schematics of the circuit:

![](../images/powert_sb_jmp.png)

> Do not use resistors with high resistance. 10k Ohm resistor were not able to
> pull the voltage up to 1.1V on the FSI_CPX_SMD node. One must use a lwo
> resistance components to drive the pins with enough current.

One may simply control the state by driving RTE GPIO411 low or high:

* `echo 0 > /sys/class/gpio/gpio411/value` - to disable the Secure Boot
* `echo 1 > /sys/class/gpio/gpio411/value` - to enable the Secure Boot
