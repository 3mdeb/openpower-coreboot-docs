```cpp
void* call_host_mpipl_service (void *io_pArgs)
{
#ifdef CONFIG_DRTM

        if(!l_err)
        {
            do {

                bool drtmMpipl = false;
                SECUREBOOT::DRTM::isDrtmMpipl(drtmMpipl);
                if(drtmMpipl)
                {
                    l_err = SECUREBOOT::DRTM::validateDrtmPayload();
                    l_err = SECUREBOOT::DRTM::completeDrtm();
                }
            } while(0);
        }
#endif
        // No error on the procedure.. proceed to collect the dump.
        if (!l_err)
        {
            // currently according to Adriana, the dump calls should only cause
            // an istep failure when the dump collect portion of this step
            // fails..  We will not fail the istep on any mbox message failures.
            // instead we will simply commit the errorlog and continue.
            errlHndl_t l_errMsg = NULL;
            // Use relocated payload base to get MDST, MDDT, MDRT details
            RUNTIME::useRelocatedPayloadAddr(true);
            // send the start message
            l_errMsg = DUMP::sendMboxMsg(DUMP::DUMP_MSG_START_MSG_TYPE);

            //Fips950 firmware release should not be supporting MPIPL data
            //collection for OPAL based systems.Hence the below code is
            //disabled for OPAL
            if(is_phyp_load())
            {
                // Call the dump collect
                l_err = DUMP::doDumpCollect();
            }
            DUMP::DUMP_MSG_TYPE msgType = DUMP::DUMP_MSG_END_MSG_TYPE;
            // Send an Error mbox msg to FSP (either end or error)
            l_errMsg = DUMP::sendMboxMsg(msgType);
            RUNTIME::useRelocatedPayloadAddr(false);
            // Wipe out our cache of the NACA/SPIRA pointers
            RUNTIME::rediscover_hdat();
        }

    }
}
```
