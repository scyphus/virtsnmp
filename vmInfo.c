/*_
 * Copyright 2013 Scyphus Solutions Co. Ltd.  All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@scyphus.co.jp>
 */

/* $Id$ */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include "globalHandler.h"
#include "vmInfo.h"

/** Initializes the hypervisor module */
void
init_vmInfo(void)
{
    const oid vmNumber_oid[] = { 1,3,6,1,2,1,VM_MIB_OID,1,2 };
    const oid vmTableLastChange_oid[] = { 1,3,6,1,2,1,VM_MIB_OID,1,3 };

    DEBUGMSGTL(("vmInfo", "Initializing\n"));

    netsnmp_register_instance(
        netsnmp_create_handler_registration("vmNumber",
                                            handle_vmNumber,
                                            vmNumber_oid,
                                            OID_LENGTH(vmNumber_oid),
                                            HANDLER_CAN_RONLY
            ));
    netsnmp_register_instance(
        netsnmp_create_handler_registration("vmTableLastChange",
                                            handle_vmTableLastChange,
                                            vmTableLastChange_oid,
                                            OID_LENGTH(vmTableLastChange_oid),
                                            HANDLER_CAN_RONLY
            ));
}

int
handle_vmNumber(netsnmp_mib_handler *handler,
                netsnmp_handler_registration *reginfo,
                netsnmp_agent_request_info *reqinfo,
                netsnmp_request_info *requests)
{
    /* We are never called for a GETNEXT if it's registered as a
       "instance", as it's "magically" handled for us.  */

    /* a instance handler also only hands us one request at a time, so
       we don't need to loop over a list of requests; we'll only get one. */

    u_long val;
    switch ( reqinfo->mode ) {

        case MODE_GET:
            val = gh_getVmNumber();
            snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                                     (u_char *)&val, sizeof(u_long));
            break;

        default:
            /* we should never get here, so this is a really bad error */
            snmp_log(LOG_ERR, "unknown mode (%d) in handle_vmNumber\n",
                     reqinfo->mode);
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


int
handle_vmTableLastChange(netsnmp_mib_handler *handler,
                         netsnmp_handler_registration *reginfo,
                         netsnmp_agent_request_info *reqinfo,
                         netsnmp_request_info *requests)
{
    /* We are never called for a GETNEXT if it's registered as a
       "instance", as it's "magically" handled for us.  */

    /* a instance handler also only hands us one request at a time, so
       we don't need to loop over a list of requests; we'll only get one. */

    u_long timeticks;

    /* netsnmp_get_agent_uptime?? */

    switch ( reqinfo->mode ) {

        case MODE_GET:
            timeticks = gh_getVmTableLastChange();
            snmp_set_var_typed_value(requests->requestvb, ASN_TIMETICKS,
                                     (u_char *)&timeticks,
                                     sizeof(u_long));
            break;


        default:
            /* we should never get here, so this is a really bad error */
            snmp_log(LOG_ERR, "unknown mode (%d) in handle_vmTableLastChange\n",
                     reqinfo->mode);
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}



/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
