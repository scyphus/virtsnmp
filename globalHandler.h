/*_
 * Copyright 2012 Scyphus Solutions Co. Ltd.  All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@scyphus.co.jp>
 */

/* $Id$ */

#ifndef _GLOBAL_HANDLER_H
#define _GLOBAL_HANDLER_H

#include <stdint.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

/* Default DB file path */
#define UUID_DB_FILE "/var/run/uuid.db"

/* vm-mib OID */
#define VM_MIB_OID 23456

typedef struct _hv_cpu_entry {
    long clockrate;
} hv_cpu_entry_t;

typedef struct _hv_cpu_table {
    unsigned long ncpu;
    hv_cpu_entry_t *entries;
} hv_cpu_table_t;

typedef struct _vm_cpu_table {
    /* vcpuIndex */
    long index;
    /* Affinity list: Size is determined by hv_cpu_table_t -> ncpu */
    char *affinity;
} vm_cpu_table_t;

typedef struct _vm_storage {
    /* vmStorageIndex */
    long index;
    /* vmStorageParent */
    long parent;
    /* vmStorageSourceType */
    long type;
    /* vmStorageSourceTypeString */
    char typestr[256];
    /* vmStorageAccess */
    long access;
    /* vmStorageResourceID */
    char resourceid[256];
    /* vmStorageMediaType */
    long mtype;
    /* vmStorageMediaTypeString */
    char mtypestr[256];
    /* vmStorageSizeUnit */
    long sizeunit;
    /* vmStorageDefinedSize */
    long defsize;
    /* vmStorageAllocatedSize */
    long allocsize;
    /* vmReadIOs */
    uint64_t readios;
    /* vmWriteIOs */
    uint64_t writeios;
    /* vmReadOctets */
    uint64_t readoctets;
    /* vmWriteOctets */
    uint64_t writeoctets;
} vm_storage_t;

typedef struct _vm_interface {
    /* vmNetworkIndex */
    long index;
    /* vmNetworkIfIndex */
    long ifindex;
    /* vmNetowkrParent */
    long parent;
    /* vmNetworkName */
    char name[256];
    /* vmNetworkModel */
    char model[256];
    /* vmNetworkPhysAddress */
    char physaddress[256];
} vm_interface_t;


typedef struct _vm_entry {
    long index;
    /* RFC4122 UUID */
    unsigned char uuid[VIR_UUID_BUFLEN];
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    virDomainPtr dom;
    /* Uptime */
    unsigned long uptime;
    signed long time_last;
    /* Virtual CPU information */
    size_t nvcpu;
    vm_cpu_table_t *vcpus;
    /* Virtual storage */
    size_t nvstorage;
    vm_storage_t *vstorages;
    /* Virtual interfaces */
    size_t nvif;
    vm_interface_t *vifs;
} vm_entry_t;

typedef struct _vm_list_item {
    vm_entry_t *entry;
    struct _vm_list_item *prev;
    struct _vm_list_item *next;
} vm_list_item_t;

typedef struct _vm_list {
    long num;
    vm_list_item_t *head;
    vm_list_item_t *tail;
} vm_list_t;

enum {
    GH_VM_EVENT_INITIALIZED,
    GH_VM_EVENT_FOUND,
    GH_VM_EVENT_REMOVED,
};
typedef struct _vm_event {
    int event;
    long index;
} vm_event_t;
typedef struct _vm_event_queue_item {
    vm_event_t *event;
    struct _vm_event_queue_item *next;
} vm_event_queue_item_t;
typedef struct _vm_event_queue {
    vm_event_queue_item_t *head;
} vm_event_queue_t;

typedef struct _global_handler {
    virConnectPtr conn;
    char *uuid_db_file;
    struct timeval start;
    /* CPU table */
    hv_cpu_table_t cpu_table;
    /* Virtual machine list */
    vm_list_t *vms;
    unsigned long vms_last_change;
    /* FIXME: events */
    long vms_added[256];
    long vms_removed[256];
} global_handler_t;

#ifdef __cplusplus
extern "C" {
#endif

    void init_globalHandler(const char *);
    void shutdown_globalHandler(void);

    int gh_virt_connect(void);

    char * gh_getHvSoftware(void);
    char * gh_getHvVersion(void);
    unsigned long gh_getHvUpTime(void);
    unsigned long gh_getHvCpuNumber(void);
    long gh_getHvCpuClockRate(long);

    long gh_uuid_to_index(const char *, const char *);

    long gh_getVmNumber(void);
    unsigned long gh_getVmTableLastChange(void);

    /* vmTable */
    size_t gh_getVmTable_vmName(long, char *, size_t);
    size_t gh_getVmTable_vmUUID(long, unsigned char *);
    size_t gh_getVmTable_vmUUIDString(long, char *, size_t);
    size_t gh_getVmTable_vmOSType(long, char *, size_t);
    long gh_getVmTable_vmAdminState(long);
    long gh_getVmTable_vmOperState(long);
    long gh_getVmTable_vmCurCpuNumber(long);
    long gh_getVmTable_vmMinCpuNumber(long);
    long gh_getVmTable_vmMaxCpuNumber(long);
    long gh_getVmTable_vmMemUnit(long);
    long gh_getVmTable_vmMaxMem(long);
    long gh_getVmTable_vmMinMem(long);
    long gh_getVmTable_vmCurMem(long);
    unsigned long gh_getVmTable_vmUpTime(long);
    uint64_t gh_getVmTable_vmCpuTime(long);
    long gh_getVmTable_vmStorageNumber(long);
    long gh_getVmTable_vmIfNumber(long);
    long gh_getVmTable_vmAutoStart(long);
    long gh_getVmTable_vmPersistent(long);

    uint64_t gh_getVcpuTable_vcpuCpuTime(long, long);

    /* vmCpuAffinityTable */
    long gh_getVcpuAffinityTable_vcpuAffinity(long, long, long);

    /* vmStorageTable */
    long gh_getVstorageTable_vmStorageSourceType(long, long);
    int
    gh_getVstorageTable_vmStorageSourceTypeString(long, long, char *, size_t);
    long gh_getVstorageTable_vmStorageAccess(long, long);
    int gh_getVstorageTable_vmStorageResourceID(long, long, char *, size_t);
    long gh_getVstorageTable_vmStorageMediaType(long, long);
    int
    gh_getVstorageTable_vmStorageMediaTypeString(long, long, char *, size_t);
    long gh_getVstorageTable_vmStorageSizeUnit(long, long);
    long gh_getVstorageTable_vmStorageDefinedSize(long, long);
    long gh_getVstorageTable_vmStorageAllocatedSize(long, long);
    uint64_t gh_getVstorageTable_vmStorageReadIOs(long, long);
    uint64_t gh_getVstorageTable_vmStorageWriteIOs(long, long);
    uint64_t gh_getVstorageTable_vmStorageReadOctets(long, long);
    uint64_t gh_getVstorageTable_vmStorageWriteOctets(long, long);

    /* vmNetworkTable */
    long gh_getVifTable_vmNetworkIfIndex(long, long);
    long gh_getVifTable_vmNetworkParent(long, long);
    int gh_getVifTable_vmNetworkModel(long, long, char *, size_t);
    int gh_getVifTable_vmNetworkPhysAddress(long, long, char *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* _GLOBAL_HANDLER_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
