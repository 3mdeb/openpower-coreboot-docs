   putScom(target, address, data)
-> platPutScom(target, address, data)
-> deviceWrite(target, data, size, DEVICE_SCOM_ADDRESS(address, opMode))
-> Singleton<Associator>::instance().performOp(WRITE, target, buffer, buflen, accessType, args)
-> findDeviceRoute(opType = WRITE, devType, accessType)(opType = WRITE, target, buffer, buflen, accessType, addr)

before findDeviceRoute(), procedure has to be registerd using Associator.registerRoute()

/src/usr/fsiscom/fsiscom.C:178 fsiScomPerformOp() is probably a function responsible for reading/writing

