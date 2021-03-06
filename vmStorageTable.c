/*
 * Note: this file originally auto-generated by mib2c using
 *  : mib2c.iterate.conf 19302 2010-08-13 12:19:42Z dts12 $
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include "vmStorageTable.h"
#include "globalHandler.h"

/** Initializes the vmStorageTable module */
void
init_vmStorageTable(void)
{
  /* here we initialize all the tables we're planning on supporting */
    initialize_table_vmStorageTable();
}

/* # Determine the first/last column names */

/** Initialize the vmStorageTable table by defining its contents and how it's structured */
void
initialize_table_vmStorageTable(void)
{
    const oid vmStorageTable_oid[] = {1,3,6,1,2,1,VM_MIB_OID,1,7};
    const size_t vmStorageTable_oid_len   = OID_LENGTH(vmStorageTable_oid);
    netsnmp_handler_registration    *reg;
    netsnmp_iterator_info           *iinfo;
    netsnmp_table_registration_info *table_info;

    DEBUGMSGTL(("vmStorageTable:init", "initializing table vmStorageTable\n"));

    reg = netsnmp_create_handler_registration(
              "vmStorageTable",     vmStorageTable_handler,
              vmStorageTable_oid, vmStorageTable_oid_len,
              HANDLER_CAN_RONLY
              );

    table_info = SNMP_MALLOC_TYPEDEF( netsnmp_table_registration_info );
    netsnmp_table_helper_add_indexes(table_info,
                           ASN_INTEGER,  /* index: vmStorageVmIndex */
                           ASN_INTEGER,  /* index: vmStorageIndex */
                           0);
    table_info->min_column = COLUMN_VMSTORAGEVMINDEX;
    table_info->max_column = COLUMN_VMSTORAGEWRITEOCTETS;

    iinfo = SNMP_MALLOC_TYPEDEF( netsnmp_iterator_info );
    iinfo->get_first_data_point = vmStorageTable_get_first_data_point;
    iinfo->get_next_data_point  = vmStorageTable_get_next_data_point;
    iinfo->table_reginfo        = table_info;

    netsnmp_register_table_iterator( reg, iinfo );

    /* Initialise the contents of the table here */
}

    /* Typical data structure for a row entry */
struct vmStorageTable_entry {
    /* Index values */
    long vmStorageVmIndex;
    long vmStorageIndex;

    /* Column values */

    /* Illustrate using a simple linked list */
    int   valid;
    struct vmStorageTable_entry *next;
};

struct vmStorageTable_entry  *vmStorageTable_head;

/* create a new row in the (unsorted) table */
struct vmStorageTable_entry *
vmStorageTable_createEntry(long vmStorageVmIndex, long vmStorageIndex) {
    struct vmStorageTable_entry *entry;

    entry = SNMP_MALLOC_TYPEDEF(struct vmStorageTable_entry);
    if (!entry)
        return NULL;

    entry->vmStorageVmIndex = vmStorageVmIndex;
    entry->vmStorageIndex = vmStorageIndex;
    entry->next = vmStorageTable_head;
    vmStorageTable_head = entry;
    return entry;
}
void
vmStorageTable_createEntryByIndex(long vmStorageVmIndex, long vmStorageIndex)
{
    (void)vmStorageTable_createEntry(vmStorageVmIndex, vmStorageIndex);
}

/* remove a row from the table */
void
vmStorageTable_removeEntry( struct vmStorageTable_entry *entry ) {
    struct vmStorageTable_entry *ptr, *prev;

    if (!entry)
        return;    /* Nothing to remove */

    for ( ptr  = vmStorageTable_head, prev = NULL;
          ptr != NULL;
          prev = ptr, ptr = ptr->next ) {
        if ( ptr == entry )
            break;
    }
    if ( !ptr )
        return;    /* Can't find it */

    if ( prev == NULL )
        vmStorageTable_head = ptr->next;
    else
        prev->next = ptr->next;

    SNMP_FREE( entry );   /* XXX - release any other internal resources */
}
void
vmStorageTable_removeEntryByIndex(long vmStorageVmIndex, long vmStorageIndex)
{
    struct vmStorageTable_entry *ptr, *prev;

    for ( ptr  = vmStorageTable_head, prev = NULL;
          ptr != NULL;
          prev = ptr, ptr = ptr->next ) {
        if ( ptr->vmStorageVmIndex == vmStorageVmIndex
             && ptr->vmStorageIndex == vmStorageIndex ) {
            break;
        }
    }

    vmStorageTable_removeEntry(ptr);
}


/* Example iterator hook routines - using 'get_next' to do most of the work */
netsnmp_variable_list *
vmStorageTable_get_first_data_point(void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata)
{
    *my_loop_context = vmStorageTable_head;
    return vmStorageTable_get_next_data_point(my_loop_context, my_data_context,
                                    put_index_data,  mydata );
}

netsnmp_variable_list *
vmStorageTable_get_next_data_point(void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata)
{
    struct vmStorageTable_entry *entry = (struct vmStorageTable_entry *)*my_loop_context;
    netsnmp_variable_list *idx = put_index_data;

    if ( entry ) {
        snmp_set_var_typed_integer( idx, ASN_INTEGER, entry->vmStorageVmIndex );
        idx = idx->next_variable;
        snmp_set_var_typed_integer( idx, ASN_INTEGER, entry->vmStorageIndex );
        idx = idx->next_variable;
        *my_data_context = (void *)entry;
        *my_loop_context = (void *)entry->next;
        return put_index_data;
    } else {
        return NULL;
    }
}


/** handles requests for the vmStorageTable table */
int
vmStorageTable_handler(
    netsnmp_mib_handler               *handler,
    netsnmp_handler_registration      *reginfo,
    netsnmp_agent_request_info        *reqinfo,
    netsnmp_request_info              *requests) {

    netsnmp_request_info       *request;
    netsnmp_table_request_info *table_info;
    struct vmStorageTable_entry          *table_entry;

    long vmStorageSourceType;
    char vmStorageSourceTypeString[256];
    char vmStorageResourceID[256];
    long vmStorageAccess;
    long vmStorageMediaType;
    char vmStorageMediaTypeString[256];
    long vmStorageSizeUnit;
    long vmStorageDefinedSize;
    long vmStorageAllocatedSize;
    U64 vmStorageReadIOs;
    U64 vmStorageWriteIOs;
    U64 vmStorageReadOctets;
    U64 vmStorageWriteOctets;
    uint64_t val64;
    int ret;

    DEBUGMSGTL(("vmStorageTable:handler", "Processing request (%d)\n", reqinfo->mode));

    switch (reqinfo->mode) {
        /*
         * Read-support (also covers GetNext requests)
         */
    case MODE_GET:
        for (request=requests; request; request=request->next) {
            table_entry = (struct vmStorageTable_entry *)
                              netsnmp_extract_iterator_context(request);
            table_info  =     netsnmp_extract_table_info(      request);

            switch ( table_info->colnum ) {
            case COLUMN_VMSTORAGEVMINDEX:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            table_entry->vmStorageVmIndex);
                break;
            case COLUMN_VMSTORAGEINDEX:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            table_entry->vmStorageIndex);
                break;
            case COLUMN_VMSTORAGESOURCETYPE:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageSourceType
                    = gh_getVstorageTable_vmStorageSourceType(
                        table_entry->vmStorageVmIndex,
                        table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageSourceType);
                break;
            case COLUMN_VMSTORAGESOURCETYPESTRING:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                ret = gh_getVstorageTable_vmStorageSourceTypeString(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex,
                    vmStorageSourceTypeString,
                    sizeof(vmStorageSourceTypeString));
                if ( 0 != ret ) {
                    (void)strcpy(vmStorageSourceTypeString, "");
                }
                snmp_set_var_typed_value( request->requestvb, ASN_OCTET_STR,
                                          vmStorageSourceTypeString,
                                          strlen(vmStorageSourceTypeString));
                break;
            case COLUMN_VMSTORAGERESOURCEID:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                ret = gh_getVstorageTable_vmStorageResourceID(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex,
                    vmStorageResourceID, sizeof(vmStorageResourceID));
                if ( 0 != ret ) {
                    (void)strcpy(vmStorageResourceID, "");
                }
                snmp_set_var_typed_value( request->requestvb, ASN_OCTET_STR,
                                          vmStorageResourceID,
                                          strlen(vmStorageResourceID));
                break;
            case COLUMN_VMSTORAGEACCESS:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageAccess = gh_getVstorageTable_vmStorageAccess(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageAccess);
                break;
            case COLUMN_VMSTORAGEMEDIATYPE:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageMediaType
                    = gh_getVstorageTable_vmStorageMediaType(
                        table_entry->vmStorageVmIndex,
                        table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageMediaType);
                break;
            case COLUMN_VMSTORAGEMEDIATYPESTRING:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                ret = gh_getVstorageTable_vmStorageMediaTypeString(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex,
                    vmStorageMediaTypeString,
                    sizeof(vmStorageMediaTypeString));
                if ( 0 != ret ) {
                    (void)strcpy(vmStorageMediaTypeString, "");
                }
                snmp_set_var_typed_value( request->requestvb, ASN_OCTET_STR,
                                          vmStorageMediaTypeString,
                                          strlen(vmStorageMediaTypeString));
                break;
            case COLUMN_VMSTORAGESIZEUNIT:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageSizeUnit = gh_getVstorageTable_vmStorageSizeUnit(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageSizeUnit);
                break;
            case COLUMN_VMSTORAGEDEFINEDSIZE:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageDefinedSize = gh_getVstorageTable_vmStorageDefinedSize(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageDefinedSize);
                break;
            case COLUMN_VMSTORAGEALLOCATEDSIZE:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                vmStorageAllocatedSize
                    = gh_getVstorageTable_vmStorageAllocatedSize(
                        table_entry->vmStorageVmIndex,
                        table_entry->vmStorageIndex);
                snmp_set_var_typed_integer( request->requestvb, ASN_INTEGER,
                                            vmStorageAllocatedSize);
                break;
            case COLUMN_VMSTORAGEREADIOS:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                val64 = gh_getVstorageTable_vmStorageReadIOs(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                vmStorageReadIOs.high = (val64 >> 32) & 0xffffffff;
                vmStorageReadIOs.low = val64 & 0xffffffff;

                snmp_set_var_typed_value( request->requestvb, ASN_COUNTER64,
                                          &vmStorageReadIOs,
                                          sizeof(vmStorageReadIOs));
                break;
            case COLUMN_VMSTORAGEWRITEIOS:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                val64 = gh_getVstorageTable_vmStorageWriteIOs(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                vmStorageWriteIOs.high = (val64 >> 32) & 0xffffffff;
                vmStorageWriteIOs.low = val64 & 0xffffffff;

                snmp_set_var_typed_value( request->requestvb, ASN_COUNTER64,
                                          &vmStorageWriteIOs,
                                          sizeof(vmStorageWriteIOs));
                break;
            case COLUMN_VMSTORAGEREADOCTETS:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                val64 = gh_getVstorageTable_vmStorageReadOctets(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                vmStorageReadOctets.high = (val64 >> 32) & 0xffffffff;
                vmStorageReadOctets.low = val64 & 0xffffffff;

                snmp_set_var_typed_value( request->requestvb, ASN_COUNTER64,
                                          &vmStorageReadOctets,
                                          sizeof(vmStorageReadOctets));
                break;
            case COLUMN_VMSTORAGEWRITEOCTETS:
                if ( !table_entry ) {
                    netsnmp_set_request_error(reqinfo, request,
                                              SNMP_NOSUCHINSTANCE);
                    continue;
                }
                val64 = gh_getVstorageTable_vmStorageWriteOctets(
                    table_entry->vmStorageVmIndex, table_entry->vmStorageIndex);
                vmStorageWriteOctets.high = (val64 >> 32) & 0xffffffff;
                vmStorageWriteOctets.low = val64 & 0xffffffff;

                snmp_set_var_typed_value( request->requestvb, ASN_COUNTER64,
                                          &vmStorageWriteOctets,
                                          sizeof(vmStorageWriteOctets));
                break;
            default:
                netsnmp_set_request_error(reqinfo, request,
                                          SNMP_NOSUCHOBJECT);
                break;
            }
        }
        break;

    }
    return SNMP_ERR_NOERROR;
}
