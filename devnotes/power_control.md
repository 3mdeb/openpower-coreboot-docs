# Talos II power management from BMC console

## Power operations

* To power on the machine use:
```
obmcutil -w poweron
```

* To power off the machine use:
```
obmcutil -w poweroff
```

If you don't need to wait for the end of power operation omit the
`-w` parameter for non-blocking operation.

> Note: It is also possible to use chassisoff version of this command.

## Check power and booting status
To check current power status and booting progress use:
```
obmcutil status
```

