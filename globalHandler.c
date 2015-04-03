/*_
 * Copyright 2012 Scyphus Solutions Co. Ltd.  All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@scyphus.co.jp>
 */

/* $Id$ */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sysexits.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <assert.h>
#include "globalHandler.h"
#include "vmTable.h"
#include "vmCpuTable.h"
#include "vmCpuAffinityTable.h"
#include "vmNetworkTable.h"
#include "vmStorageTable.h"

global_handler_t gh;

#ifndef virConnectIsAlive
#define virConnectIsAlive(conn) ((conn) != NULL)
#endif

/*
 * Static functions
 */
static void _initialize_hv_cpu_table(void);
static void _update_hv_cpu_table(void);
static void _initialize_hv_vms(void);
static void _update_hv_vms(void);
int _add_vm_entry(virDomainPtr, vm_list_t *);
int _remove_vm_entry(long, vm_list_t *);
static vm_entry_t * _search_vm_entry(long, vm_list_t *);
static int _update_vm_entry(vm_entry_t *);



#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>          /* For BLKGETSIZE */
#endif
#if TARGET_DARWIN
#include <sys/disk.h>
#endif

/*
 * Detect the size to be exported
 */
static off_t
_detectsize(const char *fname)
{
    int fd;
    off_t off;
    struct stat stat;
    int err;

    /* Open the target file */
    fd = open(fname, O_RDONLY);
    if ( fd < 0 ) {
        return 0;
    }

#ifdef HAVE_SYS_MOUNT_H
#ifdef HAVE_SYS_IOCTL_H
#ifdef BLKGETSIZE64
    uint64_t blks;

    /* Looking for export size with ioctl BLKGETSIZE64 */
    if ( 0 == ioctl(fd, BLKGETSIZE64, &blks) && blks ) {
        (void)close(fd);
        return (off_t)blks;
    }
#endif /* BLKGETSIZE64 */
#endif /* HAVE_SYS_IOCTL_H */
#endif /* HAVE_SYS_MOUNT_H */

#if TARGET_DARWIN
#ifdef DKIOCGETBLOCKSIZE
#ifdef DKIOCGETBLOCKCOUNT
    uint32_t d_blks;
    uint64_t d_blkc;

    /* Get block size and block count */
    if ( 0 == ioctl(fd, DKIOCGETBLOCKSIZE, &d_blks) && d_blks ) {
        if ( 0 == ioctl(fd, DKIOCGETBLOCKCOUNT, &d_blkc) && d_blkc ) {
            (void)close(fd);
            return (off_t)d_blks * d_blkc;
        }
    }
#endif /* DKIOCGETBLOCKCOUNT */
#endif /* DKIOCGETBLOCKSIZE */
#endif /* TARGET_DARWIN */

    /* Looking for the size with fstat */
    stat.st_size = 0;
    err = fstat(fd, &stat);
    if ( !err ) {
        if ( stat.st_size > 0 ) {
            /* Could get the size */
            (void)close(fd);
            return (off_t)stat.st_size;
        }
    } else {
        /* Fstat failed */
    }

    /* Looking for the size with lseek SEEK_END */
    off = lseek(fd, (off_t)0, SEEK_END);
    if ( off >= ((off_t)0) ) {
        /* Could get the size */
        (void)close(fd);
        return off;
    } else {
        /* Lseek failed */
    }

    (void)close(fd);

    /* Could not detect the size */
    return -1;
}


/*
 * Initialize hypervisor CPU table
 */
static void
_initialize_hv_cpu_table(void)
{
    /* Discussion: Should this OS-sopecific code rewrite with macro and
       autoconf? */
    char buf[1024];
    const char *criteria = "processor\x09:";
    unsigned long ncpus;
    hv_cpu_entry_t *entries;
    unsigned long i;

    ncpus = 0;

    /* Try Linux-specific code first */
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if ( NULL != fp ) {
        /* Read the first column */
        while ( NULL != fgets(buf, sizeof(buf), fp) ) {
            if ( 0 == strncmp(buf, criteria, strlen(criteria)) ) {
                ncpus++;
            }
        }
        fclose(fp);
    } else {
        /* For BSD */
#if defined(CTL_HW) && defined(HW_NCPU)
        int mib[2] = { CTL_HW, HW_NCPU };
        int value;
        size_t size;
        /* Retrieve the required size first */
        if ( -1 == sysctl(mib, 2, &value, &size, NULL, 0) ) {
            ncpus = 0;
        } else {
            ncpus = value;
        }
#endif
    }

    entries = NULL;
    if ( ncpus > 0 ) {
        entries = malloc(sizeof(hv_cpu_entry_t) * ncpus);
        if ( NULL == entries ) {
            /* TODO: Error output */
            ncpus = 0;
        } else {
            /* Initialize entries */
            for ( i = 0; i < ncpus; i++ ) {
                entries[i].clockrate = 0;
            }
        }
    }
    gh.cpu_table.ncpu = ncpus;
    gh.cpu_table.entries = entries;

    /* Execute update */
    _update_hv_cpu_table();
}

/*
 * Update hypervisor CPU table
 */
static void
_update_hv_cpu_table(void)
{
    /* Discussion: Should this OS-sopecific code rewrite with macro and
       autoconf? */
    char buf[1024];
    const char *crit_proc = "processor\x09: ";
    const char *crit_freq = "cpu MHz\x09\x09: ";
    long long i;
    float freq;

    /* Reset */
    for ( i = 0; i < gh.cpu_table.ncpu; i++ ) {
        gh.cpu_table.entries[i].clockrate = 0;
    }

    i = -1;
    /* FIXME: Parse by block */
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if ( NULL != fp ) {
        /* Read the first column */
        while ( NULL != fgets(buf, sizeof(buf), fp) ) {
            if ( 0 == strncmp(buf, crit_proc, strlen(crit_proc)) ) {
                i++;
            }
            if ( i >=0 && i < gh.cpu_table.ncpu ) {
                if ( 0 == strncmp(buf, crit_freq, strlen(crit_freq)) ) {
                    /* Frequency */
                    freq = strtof(buf + strlen(crit_freq), NULL);
                    gh.cpu_table.entries[i].clockrate
                        = (long)(freq * 1000);
                }
            }
        }
        fclose(fp);
    } else {
        /* For other OSes: Not supported */
    }
}


/*
 * Allocaate new vm_list_t
 */
static vm_list_t *
_new_vm_list(void)
{
    vm_list_t *vms;

    vms = malloc(sizeof(vm_list_t));
    if ( NULL == vms ) {
        return NULL;
    }
    vms->num = 0;
    vms->head = NULL;
    vms->tail = NULL;

    return vms;
}

/*
 * Update VM entry
 */
static int
_update_vm_entry_for_vcpu(vm_entry_t *entry)
{
    virDomainInfo info;
    vm_cpu_table_t *vcpus;
    unsigned char *cpumaps;
    virVcpuInfoPtr vcpuinfo;
    int i;
    int j;
    int ptr;
    int saved;
    int ret;
    int avaff;

    if ( NULL == entry ) {
        return -1;
    }

    /* Delete old information */
    if ( NULL != entry->vcpus ) {
        for ( i = 0; i < entry->nvcpu; i++ ) {
            /* For vmCpuTable */
            vmCpuTable_removeEntryByIndex(entry->index, entry->vcpus[i].index);
            for ( j = 0; j < gh.cpu_table.ncpu; j++ ) {
                vmCpuAffinityTable_removeEntryByIndex(entry->index,
                                                      entry->vcpus[i].index,
                                                      /*j + 1*/ j + 768);
            }
            free(entry->vcpus[i].affinity);
        }
        /* Free VCPUs */
        free(entry->vcpus);
        entry->vcpus = NULL;
    }

    /* Get the number of virtual CPUs */
    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        entry->nvcpu = 0;
        return 0;
    }
    entry->nvcpu = info.nrVirtCpu;

    /* Allocate for vcpu table */
    vcpus = malloc(sizeof(vm_cpu_table_t) * entry->nvcpu);
    if ( NULL == vcpus ) {
        entry->nvcpu = 0;
        return -1;
    }

    /* Get affinity information */
    cpumaps = calloc(entry->nvcpu, (gh.cpu_table.ncpu + 7)/8);
    if ( NULL == cpumaps ) {
        entry->nvcpu = 0;
        free(vcpus);
        return -1;
    }
    vcpuinfo = malloc(sizeof(virVcpuInfo) * entry->nvcpu);
    if ( NULL == vcpuinfo ) {
        free(cpumaps);
        entry->nvcpu = 0;
        free(vcpus);
        return -1;
    }
    ret = virDomainGetVcpus(entry->dom, vcpuinfo, entry->nvcpu,
                            cpumaps, (gh.cpu_table.ncpu + 7)/8);
    if ( entry->nvcpu != ret ) {
        /* Just in case for not implemented */
        avaff = 0;
    } else {
        avaff = 1;
    }
    /* Not needed */
    free(vcpuinfo);

    for ( i = 0; i < entry->nvcpu; i++ ) {
        vcpus[i].index = i + 1;
        vcpus[i].affinity = malloc(sizeof(char) * gh.cpu_table.ncpu);

        if ( NULL == vcpus[i].affinity ) {
            saved = i;
            goto error_vcpu_affinity;
        }
        if ( avaff ) {
            for ( j = 0; j < gh.cpu_table.ncpu; j++ ) {
                ptr = i * ((gh.cpu_table.ncpu + 7)/8);
                /* Little endian for cpumap */
                switch ( (cpumaps[ptr + j/8] >> (j % 8)) & 0x1 ) {
                case 0:
                    /* Disabled */
                    vcpus[i].affinity[j] = 2;
                    break;
                case 1:
                    /* Enabled */
                    vcpus[i].affinity[j] = 1;
                    break;
                default:
                    /* Other: couldn't be reached here */
                    vcpus[i].affinity[j] = 0;
                }
            }
        } else {
            for ( j = 0; j < gh.cpu_table.ncpu; j++ ) {
                vcpus[i].affinity[j] = 0;
            }
        }
    }
    free(cpumaps);

    /* Set to the entry */
    entry->vcpus = vcpus;

    /* For vmCpuTable */
    for ( i = 0; i < entry->nvcpu; i++ ) {
        vmCpuTable_createEntryByIndex(entry->index, entry->vcpus[i].index);
        for ( j = 0; j < gh.cpu_table.ncpu; j++ ) {
            vmCpuAffinityTable_createEntryByIndex(entry->index,
                                                  entry->vcpus[i].index,
                                                  /*j + 1*/ j + 768);
        }
    }

    return 0;

error_vcpu_affinity:
    free(cpumaps);
    for ( i = 0; i < saved; i++ ) {
        free(vcpus[i].affinity);
    }
    free(vcpus);
    entry->nvcpu = 0;

    return -1;
}
static int
_update_vm_entry_for_vstorage(vm_entry_t *entry)
{
    int i;
    char *xmldesc;
    int nvstorage;
    vm_storage_t *vstorages;
    xmlDocPtr doc;
    xmlXPathContextPtr xpathctx;
    xmlXPathObjectPtr xpathobj;
    xmlNodeSetPtr nodeset;
    xmlNodePtr node;
    xmlNodePtr child;
    xmlChar *prop;
    off_t dsize;
    virDomainBlockStatsStruct bstats;

    if ( NULL == entry ) {
        return -1;
    }

    /* Delete old information */
    if ( NULL != entry->vstorages ) {
        for ( i = 0; i < entry->nvstorage; i++ ) {
            /* For vmStorageTable */
            vmStorageTable_removeEntryByIndex(entry->index,
                                              entry->vstorages[i].index);
        }
        /* Free virtual interfaces */
        free(entry->vstorages);
        entry->vstorages = NULL;
    }
    entry->nvstorage = 0;

    /* Discussion: to specifiy VIR_DOMAIN_XML_INACTIVE?? */
    xmldesc = virDomainGetXMLDesc(entry->dom, 0);
    if ( NULL == xmldesc ) {
        return -1;
    }
    /* Parse XML */
    doc = xmlParseDoc((const xmlChar *)xmldesc);
    if ( NULL == doc ) {
        free(xmldesc);
        return -1;
    }
    /* Create xpath evaluation context */
    xpathctx = xmlXPathNewContext(doc);
    if ( NULL == xpathctx ) {
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }
    /* Evaluate xpath expression */
    xpathobj = xmlXPathEvalExpression((const xmlChar *)"/domain/devices/disk",
                                      xpathctx);
    if ( NULL == xpathobj ) {
        xmlXPathFreeContext(xpathctx);
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }

    nodeset = xpathobj->nodesetval;
    nvstorage = nodeset ? nodeset->nodeNr : 0;

    /* Allocate for vifs */
    vstorages = malloc(sizeof(vm_storage_t) * nvstorage);
    if  ( NULL == vstorages ) {
        /* Memory error */
        xmlXPathFreeObject(xpathobj);
        xmlXPathFreeContext(xpathctx);
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }

    for ( i = 0; i < nvstorage; i++ ) {
        /* Initialize the virtual interface structure */
        vstorages[i].index = i + 1;
        vstorages[i].type = 0;
        vstorages[i].parent = 0;
        (void)strcpy(vstorages[i].typestr, "");
        (void)strcpy(vstorages[i].resourceid, "");
        vstorages[i].access = 0;
        vstorages[i].mtype = 1;
        (void)strcpy(vstorages[i].mtypestr, "");
        vstorages[i].sizeunit = 1024 * 1024; /* MiB */
        vstorages[i].defsize = 0;
        vstorages[i].allocsize = 0;
        vstorages[i].readios = 0;
        vstorages[i].writeios = 0;

        node = nodeset->nodeTab[i];
        assert ( node->type == XML_ELEMENT_NODE );

        /* Support bridge only */
        child = node->children;
        for ( ; NULL != child; child = child->next ) {
            if ( XML_ELEMENT_NODE != child->type ) {
                continue;
            }
            if ( 0 == strcmp("driver", (char *)child->name) ) {
                /* For type and typestr */
                prop = xmlGetProp(child, (const xmlChar *)"name");
                if ( NULL != prop ) {
                    if ( 0 == strcmp("phy", (char *)prop) ) {
                        /* Block device */
                        vstorages[i].type = 1;
                    } else if ( 0 == strcmp("file", (char *)prop)
                                || 0 == strcmp("raw", (char *)prop)) {
                        /* RAW */
                        vstorages[i].type = 2;
                    } else if ( 0 == strcmp("qemu", (char *)prop)
                                || 0 == strcmp("qcow2", (char *)prop)) {
                        /* QCOW2 */
                        vstorages[i].type = 3;
                    }
                    (void)strncpy(vstorages[i].typestr, (char *)prop,
                                  sizeof(vstorages[i].typestr));
                    xmlFree(prop);
                }
#if 0
            } else if ( 0 == strcmp("target", (char *)child->name) ) {
                /* for name */
                prop = xmlGetProp(child, (const xmlChar *)"dev");
                if ( NULL != prop ) {
                    (void)strncpy(vstorages[i].name, (char *)prop,
                                  sizeof(vstorages[i].name));
                    xmlFree(prop);
                }
#endif
            } else if ( 0 == strcmp("source", (char *)child->name) ) {
                /* for resourceid, defsize, and allocsize */
                prop = xmlGetProp(child, (const xmlChar *)"dev");
                if ( NULL != prop ) {
                    (void)strncpy(vstorages[i].resourceid, (char *)prop,
                                  sizeof(vstorages[i].resourceid));
                    dsize = _detectsize((char *)prop);
                    if ( vstorages[i].allocsize >= 0 ) {
                        vstorages[i].allocsize = dsize / vstorages[i].sizeunit;
                    }
                    xmlFree(prop);
                } else {
                    prop = xmlGetProp(child, (const xmlChar *)"file");
                    if ( NULL != prop ) {
                        (void)strncpy(vstorages[i].resourceid, (char *)prop,
                                      sizeof(vstorages[i].resourceid));
                        dsize = _detectsize((char *)prop);
                        if ( vstorages[i].allocsize >= 0 ) {
                            vstorages[i].allocsize
                                = dsize / vstorages[i].sizeunit;
                        }

                        xmlFree(prop);
                    }
                }
                /* update read/write ios/octets */
                if (0 == virDomainBlockStats(entry->dom,
                                             vstorages[i].resourceid,
                                             &bstats, sizeof(bstats))) {
                    vstorages[i].readios = bstats.rd_req;
                    vstorages[i].readoctets = bstats.rd_bytes;
                    vstorages[i].writeios = bstats.wr_req;
                    vstorages[i].writeoctets = bstats.wr_bytes;
                }
            }
        }
    }

    xmlXPathFreeObject(xpathobj);
    xmlXPathFreeContext(xpathctx);
    xmlFreeDoc(doc);
    free(xmldesc);

    /* Set to the entry */
    entry->vstorages = vstorages;
    entry->nvstorage = nvstorage;

    /* For vmStorageTable */
    for ( i = 0; i < entry->nvstorage; i++ ) {
        vmStorageTable_createEntryByIndex(entry->index,
                                          entry->vstorages[i].index);
    }

    return 0;
}
static long
_target_dev_to_index(const char *dev)
{
    unsigned int ifidx;
    char buf[64];
    unsigned int idx1;
    unsigned int idx2;
    int ret;

    /* Rough check */
    ret = sscanf(dev, "vif%u.%u", &idx1, &idx2);
    if ( 2 == ret ) {
        /* Check tap first */
        (void)snprintf(buf, sizeof(buf), "tap%u.%u", idx1, idx2);
        ifidx = if_nametoindex(buf);
        if ( ifidx > 0 ) {
            return ifidx;
        }
    }

    ifidx = if_nametoindex(dev);

    return ifidx;
}
static long
_source_dev_to_index(const char *dev)
{
    /* Note: Return 0 for error */
    return if_nametoindex(dev);
}
static int
_update_vm_entry_for_vif(vm_entry_t *entry)
{
    int i;
    char *xmldesc;
    int nvif;
    vm_interface_t *vifs;
    xmlDocPtr doc;
    xmlXPathContextPtr xpathctx;
    xmlXPathObjectPtr xpathobj;
    xmlNodeSetPtr nodeset;
    xmlNodePtr node;
    xmlNodePtr child;
    xmlChar *prop;
    xmlChar *prop2;

    if ( NULL == entry ) {
        return -1;
    }

    /* Delete old information */
    if ( NULL != entry->vifs ) {
        for ( i = 0; i < entry->nvif; i++ ) {
            /* For vmNetworkTable */
            vmNetworkTable_removeEntryByIndex(entry->index,
                                              entry->vifs[i].index);
        }
        /* Free virtual interfaces */
        free(entry->vifs);
        entry->vifs = NULL;
    }
    entry->nvif = 0;

    /* Discussion: to specifiy VIR_DOMAIN_XML_INACTIVE?? */
    xmldesc = virDomainGetXMLDesc(entry->dom, 0);
    if ( NULL == xmldesc ) {
        return -1;
    }
    /* Parse XML */
    doc = xmlParseDoc((const xmlChar *)xmldesc);
    if ( NULL == doc ) {
        free(xmldesc);
        return -1;
    }
    /* Create xpath evaluation context */
    xpathctx = xmlXPathNewContext(doc);
    if ( NULL == xpathctx ) {
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }
    /* Evaluate xpath expression */
    xpathobj = xmlXPathEvalExpression(
        (const xmlChar *)"/domain/devices/interface", xpathctx);
    /* "/domain/devices/interface[@type='bridge']/target/@dev" ? */
    if ( NULL == xpathobj ) {
        xmlXPathFreeContext(xpathctx);
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }

    nodeset = xpathobj->nodesetval;
    nvif = nodeset ? nodeset->nodeNr : 0;

    /* Allocate for vifs */
    vifs = malloc(sizeof(vm_interface_t) * nvif);
    if  ( NULL == vifs ) {
        /* Memory error */
        xmlXPathFreeObject(xpathobj);
        xmlXPathFreeContext(xpathctx);
        xmlFreeDoc(doc);
        free(xmldesc);
        return -1;
    }

    for ( i = 0; i < nvif; i++ ) {
        /* Initialize the virtual interface structure */
        vifs[i].index = i + 1;
        vifs[i].ifindex = 0;
        vifs[i].parent = 0;
        snprintf(vifs[i].name, sizeof(vifs[i].name), "ve%d", i + 1);
        strcpy(vifs[i].model, "");
        strcpy(vifs[i].physaddress, "");

        node = nodeset->nodeTab[i];
        assert ( node->type == XML_ELEMENT_NODE );

        prop = xmlGetProp(node, (const xmlChar *)"type");
        if ( NULL != prop ) {
            if ( 0 == strcmp((char *)prop, "bridge") ) {
                /* Support bridge only */
                child = node->children;
                for ( ; NULL != child; child = child->next ) {
                    if ( XML_ELEMENT_NODE != child->type ) {
                        continue;
                    }
                    if ( 0 == strcmp("target", (char *)child->name) ) {
                        /* for ifindex */
                        prop2 = xmlGetProp(child, (const xmlChar *)"dev");
                        if ( NULL != prop2 ) {
                            vifs[i].ifindex
                                = _target_dev_to_index((char *)prop2);
                            xmlFree(prop2);
                        }
                    } else if ( 0 == strcmp("model", (char *)child->name) ) {
                        /* for model */
                        prop2 = xmlGetProp(child, (const xmlChar *)"type");
                        if ( NULL != prop2 ) {
                            (void)strncpy(vifs[i].model, (char *)prop2,
                                          sizeof(vifs[i].model));
                            xmlFree(prop2);
                        }
                    } else if ( 0 == strcmp("mac", (char *)child->name) ) {
                        /* for physaddress */
                        prop2 = xmlGetProp(child, (const xmlChar *)"address");
                        if ( NULL != prop2 ) {
                            (void)strncpy(vifs[i].physaddress, (char *)prop2,
                                          sizeof(vifs[i].physaddress));
                            xmlFree(prop2);
                        }
                    } else if ( 0 == strcmp("source", (char *)child->name) ) {
                        /* for physaddress */
                        prop2 = xmlGetProp(child, (const xmlChar *)"bridge");
                        if ( NULL != prop2 ) {
                            vifs[i].parent
                                = _source_dev_to_index((char *)prop2);
                            xmlFree(prop2);
                        }
                    }
                }
            }
            xmlFree(prop);
        }
    }

    xmlXPathFreeObject(xpathobj);
    xmlXPathFreeContext(xpathctx);
    xmlFreeDoc(doc);
    free(xmldesc);

    /* Set to the entry */
    entry->vifs = vifs;
    entry->nvif = nvif;

    /* For vmNetworkTable */
    for ( i = 0; i < entry->nvif; i++ ) {
        vmNetworkTable_createEntryByIndex(entry->index, entry->vifs[i].index);
    }

    return 0;
}
static int
_update_vm_uptime(vm_entry_t *entry)
{
    long now;
    long state;

    /* Uptime */
#ifdef virDomainGetState
    if ( 0 != virDomainGetState(entry->dom, &state, NULL, 0) ) {
        state = -1;
    }
#else
    virDomainInfo info;
    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        state = -1;
    }
    state = info.state;
#endif

    switch ( state ) {
    case VIR_DOMAIN_NOSTATE:
    case VIR_DOMAIN_RUNNING:
    case VIR_DOMAIN_BLOCKED:
    case VIR_DOMAIN_SHUTDOWN:
        now = netsnmp_get_agent_uptime();
        if ( entry->time_last >= 0 ) {
            /* Don't care overflow */
            entry->uptime += (now - entry->time_last);
        } else {
            entry->uptime = 0;
        }
        entry->time_last = now;
        break;
    default:
        entry->uptime = 0;
        entry->time_last = -1;
    }

    return 0;
}

static int
_update_vm_entry(vm_entry_t *entry)
{
    /* Uptime */
    _update_vm_uptime(entry);

    /* Other tables */
    _update_vm_entry_for_vcpu(entry);
    _update_vm_entry_for_vstorage(entry);
    _update_vm_entry_for_vif(entry);

    return 0;
}

/*
 * Add VM entry to the list
 */
int
_add_vm_entry(virDomainPtr dom, vm_list_t *vms)
{
    unsigned char uuid[VIR_UUID_BUFLEN];
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    long index;
    vm_entry_t *entry;
    vm_list_item_t *item;
    int ret;

    /* Get UUID string */
    ret = virDomainGetUUIDString(dom, uuidstr);
    if ( 0 != ret ) {
        /* Cannot get UUID */
        return -1;
    }

    /* Get UUID */
    ret = virDomainGetUUID(dom, uuid);
    if ( 0 != ret ) {
        /* Cannot get UUID */
        return -1;
    }

    /* Get index */
    index = gh_uuid_to_index(uuidstr, gh.uuid_db_file);
    if ( index <= 0 ) {
        /* DB error */
        return -1;
    }

    /* Allocate an entry */
    entry = malloc(sizeof(vm_entry_t));
    if ( NULL == entry ) {
        /* Memory error */
        return -1;
    }
    entry->index = index;
    (void)strcpy(entry->uuidstr, uuidstr);
    (void)memcpy(entry->uuid, uuid, VIR_UUID_BUFLEN);
    entry->dom = dom;
    entry->uptime = 0;
    entry->time_last = -1;
    entry->nvcpu = 0;
    entry->vcpus = NULL;
    entry->nvstorage = 0;
    entry->vstorages = NULL;
    entry->nvif = 0;
    entry->vifs = NULL;

    /* Allocate an entry item */
    item = malloc(sizeof(vm_list_item_t));
    if ( NULL == item ) {
        /* Memory error */
        free(entry);
        return -1;
    }
    item->entry = entry;
    item->prev = NULL;
    item->next = NULL;

    /* Append to the list */
    if ( NULL == vms->head ) {
        /* If no entry exists */
        vms->head = item;
        vms->tail = item;
    } else {
        /* Append to the tail */
        vms->tail->next = item;
        item->prev = vms->tail;
        vms->tail = item;
    }
    vms->num++;

    vmTable_createEntryByIndex(index);

    _update_vm_entry(entry);

    return 0;
}

int
_remove_vm_entry(long idx, vm_list_t *vms)
{
    vm_list_item_t *item;

    item = vms->head;
    while ( NULL != item ) {
        if ( item->entry->index == idx ) {
            if ( item == vms->head && item == vms->tail ) {
                /* Head and tail, then free and set NULL */
                virDomainFree(item->entry->dom);
                free(item->entry);
                vms->head = NULL;
                vms->tail = NULL;
            } else if ( item == vms->head ) {
                /* Head */
                item->next->prev = NULL;
                vms->head = item->next;
                virDomainFree(item->entry->dom);
                free(item->entry);
            } else if ( item == vms->tail ) {
                /* Tail */
                item->prev->next = NULL;
                vms->tail = item->prev;
                virDomainFree(item->entry->dom);
                free(item->entry);
            } else {
                /* Others */
                item->prev->next = item->next;
                item->next->prev = item->prev;
                virDomainFree(item->entry->dom);
                free(item->entry);
            }
            vms->num--;

            vmTable_removeEntryByIndex(idx);
            return 1;
        }
    }

    return 0;
}

static vm_entry_t *
_search_vm_entry(long idx, vm_list_t *vms)
{
    vm_list_item_t *item;

    item = vms->head;
    while ( NULL != item ) {
        if ( item->entry->index == idx ) {
            return item->entry;
        }
        item = item->next;
    }

    return NULL;
}

/*
 * Virtual machines
 */
static void
_initialize_hv_vms(void)
{
    int ndoms;
    virDomainPtr dom;
    int ret;
    char *names[256];
    int ids[256];
    int max = 256;
    int i;
    vm_list_t *vms;

    /* Check the connection first */
    if ( NULL == gh.conn || 1 != virConnectIsAlive(gh.conn) ) {
        if ( gh_virt_connect() < 0 ) {
            /* Error */
            return;
        }
    }

    /* Allocate a new vm list */
    vms = _new_vm_list();
    if ( NULL == vms ) {
        /* TODO: print out an error message */
        exit(EX_OSERR);
    }

    /* Get running domains */
    ndoms = virConnectListDomains(gh.conn, ids, max);
    if ( -1 == ndoms ) {
        /* Error */
        return;
    }
    for ( i = 0; i < ndoms; i++ ) {
        /* Lookup domain */
        dom = virDomainLookupByID(gh.conn, ids[i]);
        if ( (virDomainPtr)VIR_ERR_NO_DOMAIN == dom ) {
            /* No domain */
            continue;
        }
        ret = _add_vm_entry(dom, vms);
        if ( 0 != ret ) {
            /* Error.  TODO: print out an error message */
            (void)virDomainFree(dom);
            continue;
        }
    }

    /* Get defined domains */
    ndoms = virConnectListDefinedDomains(gh.conn, names, max);
    if ( -1 == ndoms ) {
        /* Error */
        return;
    }
    for ( i = 0; i < ndoms; i++ ) {
        dom = virDomainLookupByName(gh.conn, names[i]);
        free(names[i]);
        if ( (virDomainPtr)VIR_ERR_NO_DOMAIN == dom ) {
            /* No domain */
            continue;
        }

        ret = _add_vm_entry(dom, vms);
        if ( 0 != ret ) {
            /* Error.  TODO: print out an error message */
            (void)virDomainFree(dom);
            continue;
        }
    }

    gh.vms = vms;
    gh.vms_last_change = gh_getHvUpTime();
}

/*
 * Update virtual machines
 */
static void
_update_hv_vms(void)
{
    static long cache_last_checked;
    long cache_now;

    cache_now = netsnmp_get_agent_uptime();
    if ( cache_last_checked > 0 && (cache_now - cache_last_checked) >= 0
         && (cache_now - cache_last_checked) <= 10 * 100 ) {
        /* Cache 10 secs (and not overflown) */
        return;
    }
    cache_last_checked = cache_now;

    int ndoms;
    virDomainPtr dom;
    int ret;
    char *names[256];
    int ids[256];
    long removes[256];
    int rmcnt;
    int max = 1024;
    int i;
    long index;
    char uuidstr[VIR_UUID_STRING_BUFLEN];
    int change;
    vm_list_item_t *item;

    /* Check the connection first */
    if ( NULL == gh.conn || 1 != virConnectIsAlive(gh.conn) ) {
        if ( gh_virt_connect() < 0 ) {
            /* Error */
            return;
        }
    }

    change = 0;

    /* Get running domains */
    ndoms = virConnectListDomains(gh.conn, ids, max);
    if ( -1 == ndoms ) {
        /* Error */
        return;
    }
    for ( i = 0; i < ndoms; i++ ) {
        /* Lookup domain */
        dom = virDomainLookupByID(gh.conn, ids[i]);
        if ( (virDomainPtr)VIR_ERR_NO_DOMAIN == dom ) {
            /* No domain */
            continue;
        }
        /* Get UUID string */
        ret = virDomainGetUUIDString(dom, uuidstr);
        if ( 0 != ret ) {
            /* Cannot get UUID */
            (void)virDomainFree(dom);
            continue;
        }
        index = gh_uuid_to_index(uuidstr, gh.uuid_db_file);

        if ( NULL == _search_vm_entry(index, gh.vms) ) {
            ret = _add_vm_entry(dom, gh.vms);
            if ( 0 != ret ) {
                /* Error.  TODO: print out an error message */
                (void)virDomainFree(dom);
                continue;
            }
            change++;
        } else {
            (void)virDomainFree(dom);
        }
    }

    /* Get defined domains */
    ndoms = virConnectListDefinedDomains(gh.conn, names, max);
    if ( -1 == ndoms ) {
        /* Error */
        return;
    }
    for ( i = 0; i < ndoms; i++ ) {
        dom = virDomainLookupByName(gh.conn, names[i]);
        free(names[i]);
        if ( (virDomainPtr)VIR_ERR_NO_DOMAIN == dom ) {
            /* No domain */
            continue;
        }

        /* Get UUID */
        ret = virDomainGetUUIDString(dom, uuidstr);
        if ( 0 != ret ) {
            /* Cannot get UUID */
            (void)virDomainFree(dom);
            continue;
        }
        index = gh_uuid_to_index(uuidstr, gh.uuid_db_file);

        if ( NULL == _search_vm_entry(index, gh.vms) ) {
            ret = _add_vm_entry(dom, gh.vms);
            if ( 0 != ret ) {
                /* Error.  TODO: print out an error message */
                (void)virDomainFree(dom);
                continue;
            }
            change++;
        } else {
            (void)virDomainFree(dom);
        }
    }

    /* For removal */
    item = gh.vms->head;
    rmcnt = 0;
    while ( NULL != item ) {
        dom = virDomainLookupByUUID(gh.conn, item->entry->uuid);
        if ( (virDomainPtr)VIR_ERR_NO_DOMAIN == dom ) {
            /* No domain */
            if ( rmcnt < max ) {
                removes[rmcnt++] = item->entry->index;
            }
            continue;
        }
        item = item->next;
    }

    for ( i = 0; i < rmcnt; i++ ) {
        _remove_vm_entry(removes[i], gh.vms);
        change++;
    }

    if ( change > 0 || gh.vms_last_change == 0 ) {
        /* Update */
        gh.vms_last_change = gh_getHvUpTime();
    }

    /* For update */
    item = gh.vms->head;
    while ( NULL != item ) {
        _update_vm_entry(item->entry);
        item = item->next;
    }

    return;
}


/*
 * UUID to index
 */
long
gh_uuid_to_index(const char *uuidstr, const char *uuiddb)
{
    FILE *fp;
    char buf[1024];
    char uuid[256];
    long found;
    long idx;
    long maxidx;

    maxidx = 0;
    found = -1;

    /* Open the database file to search the specified UUID */
    fp = fopen(uuiddb, "r");
    if ( NULL != fp ) {
        (void)flock(fileno(fp), LOCK_SH);
        while ( NULL != fgets(buf, sizeof(buf), fp) ) {
            if ( 2 == sscanf(buf, "%255s %ld", uuid, &idx) ) {
                if ( 0 == strcmp(uuidstr, uuid) ) {
                    found = idx;
                    break;
                } else if ( idx > maxidx ) {
                    maxidx = idx;
                }
            }
        }
        (void)flock(fileno(fp), LOCK_UN);
        (void)fclose(fp);
    }

    if ( found > 0 ) {
        return found;
    }

    /* Open the database file to add the specified UUID */
    found = maxidx + 1;
    fp = fopen(uuiddb, "a");
    if ( NULL == fp ) {
        return -1;
    }
    (void)flock(fileno(fp), LOCK_EX);
    (void)snprintf(buf, sizeof(buf), "%s %ld\n", uuidstr, found);
    (void)fputs(buf, fp);
    (void)flock(fileno(fp), LOCK_UN);
    (void)fclose(fp);

    return found;
}


static void
_customErrorFunc(void *userdata, virErrorPtr err)
{
    return;
}

/*
 * Initialize the global handler
 */
void
init_globalHandler(const char *uuidfile)
{
    FILE *fp;

    /* Initialize libxml */
    xmlInitParser();

    bzero(&gh, sizeof(global_handler_t));

#if !DEBUG
    virSetErrorFunc(NULL, _customErrorFunc);
#endif

    /* Set startup time */
    gettimeofday(&(gh.start), NULL);

    /* Try to open libvirt API (FIXME: to add error handling) */
    (void)gh_virt_connect();

    gh.uuid_db_file = strdup(uuidfile);
    if ( NULL == gh.uuid_db_file ) {
        /* FIXME: Check the error */
        exit(EX_OSERR);
    }

    /* Check first w/ trying to create if not exists */
    fp = fopen(gh.uuid_db_file, "a+");
    if ( NULL == fp ) {
        /* Cannot open the DB file */
        fprintf(stderr, "DB file permission error: %s\n", gh.uuid_db_file);
        exit(EX_OSERR);
    }
    fclose(fp);

    /* Initialize hypervisor's CPU table */
    _initialize_hv_cpu_table();

    /* Initialize virtual machine table */
    _initialize_hv_vms();
}

/*
 * Get a connection to libvirt
 */
int
gh_virt_connect(void)
{
    virConnectPtr conn;

    if ( NULL != gh.conn ) {
        if ( 1 == virConnectIsAlive(gh.conn) ) {
            /* 1: alive, 0: dead, -1: error */
            return 1;
        }
        /* Close if it's already open */
        virConnectClose(gh.conn);
    }

    /* Open */
    conn = virConnectOpen(NULL);
    if ( NULL == conn ) {
        return -1;
    }

    /* Set */
    gh.conn = conn;

    return 0;
}

/*
 * Get the name of hypervisor software
 */
char *
gh_getHvSoftware(void)
{
    const char *soft;
    if ( NULL != gh.conn && 1 == virConnectIsAlive(gh.conn) ) {
         soft = virConnectGetType(gh.conn);
         if ( NULL != soft ) {
             return strdup(soft);
         } else {
             return NULL;
         }
    } else {
        return NULL;
    }
}

/*
 * Get the version of hypervisor software
 */
char *
gh_getHvVersion(void)
{
    unsigned long ver;
    char buf[256];

    if ( NULL != gh.conn && 1 == virConnectIsAlive(gh.conn) ) {
        if ( 0 != virConnectGetVersion(gh.conn, &ver) ) {
            return NULL;
        }

        (void)snprintf(buf, sizeof(buf), "%lu.%lu.%lu", ver / 1000000,
                       (ver % 1000000) / 1000, ver % 1000);
        return strdup(buf);
    } else {
        return NULL;
    }
}

/*
 * Get the uptime of the hypervisor
 */
unsigned long
gh_getHvUpTime(void)
{
    /* Discussion: same as sysUpTime? */
    return netsnmp_get_agent_uptime();
}

/*
 * Get the number of CPUs on the hypervisor
 */
unsigned long
gh_getHvCpuNumber(void)
{
    return gh.cpu_table.ncpu;
}

/*
 * Get the clock rate of a hypervisor's CPU
 */
long
gh_getHvCpuClockRate(long idx)
{
    /* Update first, ToDo: Implement cache-like mechanism */
    _update_hv_cpu_table();

    idx--;
    if ( idx >= 0 && idx < gh.cpu_table.ncpu && gh.cpu_table.entries ) {
        return gh.cpu_table.entries[idx].clockrate;
    } else {
        return 0;
    }
}

/*
 * Get the number of VMs
 */
long
gh_getVmNumber(void)
{
    _update_hv_vms();
    return gh.vms->num;
}

/*
 * Get the vmTableLastChange
 */
unsigned long
gh_getVmTableLastChange(void)
{
    _update_hv_vms();
    return gh.vms_last_change;
}

/*
 * vMTable
 */
size_t
gh_getVmTable_vmName(long idx, char *name, size_t max)
{
    const char *retname;
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        name[0] = '\0';
        return 0;
    }
    retname = virDomainGetName(entry->dom);
    if ( NULL == retname ) {
        name[0] = '\0';
        return 0;
    }
    (void)strncpy(name, retname, max);

    return strlen(name);
}

size_t
gh_getVmTable_vmUUID(long idx, unsigned char *uuid)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    if ( VIR_UUID_BUFLEN < 16 ) {
        (void)memset(uuid, 0, 16);
        (void)memcpy(uuid, entry->uuid, VIR_UUID_BUFLEN);
    } else {
        (void)memcpy(uuid, entry->uuid, 16);
    }

    return 16;
}

size_t
gh_getVmTable_vmUUIDString(long idx, char *uuidstr, size_t max)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        uuidstr[0] = '\0';
        return 0;
    }
    (void)strncpy(uuidstr, entry->uuidstr, max);

    return strlen(uuidstr);
}

size_t
gh_getVmTable_vmOSType(long idx, char *ostype, size_t max)
{
    vm_entry_t *entry;
    char *val;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        ostype[0] = '\0';
        return 0;
    }

    val = virDomainGetOSType(entry->dom);
    if ( NULL == val ) {
        ostype[0] = '\0';
        return 0;
    }

    (void)strncpy(ostype, val, max);
    free(val);

    return strlen(ostype);
}

long
gh_getVmTable_vmAdminState(long idx)
{
    /* FIXME: maintain an "internal" admin state */
    vm_entry_t *entry;
    int state;
    int admin_state;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

#ifdef virDomainGetState
    if ( 0 != virDomainGetState(entry->dom, &state, NULL, 0) ) {
        return 0;
    }
#else
    virDomainInfo info;
    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        return 0;
    }
    state = info.state;
#endif

    switch ( state ) {
    case VIR_DOMAIN_NOSTATE:
    case VIR_DOMAIN_RUNNING:
    case VIR_DOMAIN_BLOCKED:
    case VIR_DOMAIN_SHUTDOWN:
        admin_state = 1;
        break;
    case VIR_DOMAIN_SHUTOFF:
        admin_state = 4;
        break;
    case VIR_DOMAIN_PAUSED:
        admin_state = 3;
        break;
    default:
        admin_state = 0;
    }

    return admin_state;
}
long
gh_getVmTable_vmOperState(long idx)
{
    vm_entry_t *entry;
    int state;
    int ret;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 1;
    }

#ifdef virDomainGetState
    if ( 0 != virDomainGetState(entry->dom, &state, NULL, 0) ) {
        return 1;
    }
#else
    virDomainInfo info;
    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        return 1;
    }
    state = info.state;
#endif

    switch ( state ) {
    case VIR_DOMAIN_NOSTATE:
    case VIR_DOMAIN_RUNNING:
        ret = 4;
        break;
    case VIR_DOMAIN_BLOCKED:
        ret = 5;
        break;
    case VIR_DOMAIN_PAUSED:
        ret = 9;
        break;
    case VIR_DOMAIN_SHUTDOWN:
        ret = 11;
        break;
    case VIR_DOMAIN_SHUTOFF:
        ret = 12;
        break;
    case VIR_DOMAIN_CRASHED:
        ret = 13;
        break;
    default:
        ret = 2;
    }

    return ret;
}
long
gh_getVmTable_vmCurCpuNumber(long idx)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    return entry->nvcpu;
}
long
gh_getVmTable_vmMinCpuNumber(long idx)
{
    return -1;
}
long
gh_getVmTable_vmMaxCpuNumber(long idx)
{
    return -1;
}

uint64_t
gh_getVmTable_vmCpuTime(long idx)
{
    vm_entry_t *entry;
    virDomainInfo info;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        return 0;
    }

    /* in microsecond */
    return info.cpuTime / 1000;
}
unsigned long
gh_getVmTable_vmUpTime(long idx)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    _update_vm_uptime(entry);

    return entry->uptime;
}
long
gh_getVmTable_vmMemUnit(long idx)
{
    /* in KiB */
    return 1024;
}
long
gh_getVmTable_vmMaxMem(long idx)
{
    vm_entry_t *entry;
    long maxmem;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    maxmem = virDomainGetMaxMemory(entry->dom);
    if ( maxmem < 0 || maxmem >= (1<<30) ) {
        /* Out of range (0..2147483647) */
        return -1;
    } else {
        return maxmem;
    }
}
long
gh_getVmTable_vmMinMem(long idx)
{
    return -1;
}
long
gh_getVmTable_vmCurMem(long idx)
{
    vm_entry_t *entry;
    virDomainInfo info;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    if ( 0 != virDomainGetInfo(entry->dom, &info) ) {
        return 0;
    }

    return info.memory;
}
long
gh_getVmTable_vmStorageNumber(long idx)
{
    return 0;
}
long
gh_getVmTable_vmIfNumber(long idx)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    return entry->nvif;
}
long
gh_getVmTable_vmAutoStart(long idx)
{
    vm_entry_t *entry;
    int flag;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    if ( 0 != virDomainGetAutostart(entry->dom, &flag) ) {
        return 0;
    }

    if ( flag ) {
        return 1;
    } else {
        return 2;
    }
}
long
gh_getVmTable_vmPersistent(long idx)
{
    vm_entry_t *entry;
    int flag;

    entry = _search_vm_entry(idx, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    flag = virDomainIsPersistent(entry->dom);
    switch ( flag ) {
    case 1:
        /* Persistent */
        return 1;
    case 0:
        /* Transient */
        return 2;
    default:
        /* Error (-1) */
        return 0;
    }
}


uint64_t
gh_getVcpuTable_vcpuCpuTime(long vmIndex, long vmCpuIndex)
{
    vm_entry_t *entry;
    virVcpuInfoPtr info;
    int ret;
    uint64_t counter;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }

    if ( vmCpuIndex - 1 < 0 || vmCpuIndex - 1 >= entry->nvcpu ) {
        /* Out of range */
        return 0;
    }

    /* Allocate memory for the array of virVcpuInfo */
    info = malloc(sizeof(virVcpuInfo) * entry->nvcpu);
    if ( NULL == info ) {
        /* Memory error */
        return 0;
    }

    ret = virDomainGetVcpus(entry->dom, info, entry->nvcpu, NULL, 0);
    counter = 0;
    if ( ret == entry->nvcpu ) {
        /* OK */
        counter = info[vmCpuIndex - 1].cpuTime;

        assert ( info[vmCpuIndex - 1].number == vmCpuIndex - 1 );
    }

    free(info);

    /* in microsec */
    return counter / 1000;
}

long
gh_getVcpuAffinityTable_vcpuAffinity(long vmIndex, long vmCpuIndex,
                                     long vmCpuPhysIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vcpu(entry);

    if ( vmCpuIndex - 1 < 0 || vmCpuIndex - 1 >= entry->nvcpu ) {
        /* Out of range */
        return 0;
    }

    if ( vmCpuPhysIndex - 768 < 0 || vmCpuPhysIndex - 768 >= gh.cpu_table.ncpu ) {
        /* Out of range */
        return 0;
    }
    assert ( entry->vcpus[vmCpuIndex - 1].index == vmCpuIndex );

    return (long)entry->vcpus[vmCpuIndex - 1].affinity[vmCpuPhysIndex - 768];
}

long
gh_getVifTable_vmNetworkIfIndex(long vmIndex, long vmNetworkIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vif(entry);

    if ( vmNetworkIndex - 1 < 0 || vmNetworkIndex - 1 >= entry->nvif ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vifs[vmNetworkIndex - 1].index == vmNetworkIndex );

    return entry->vifs[vmNetworkIndex - 1].ifindex;
}
long
gh_getVifTable_vmNetworkPaerent(long vmIndex, long vmNetworkIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vif(entry);

    if ( vmNetworkIndex - 1 < 0 || vmNetworkIndex - 1 >= entry->nvif ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vifs[vmNetworkIndex - 1].index == vmNetworkIndex );

    return entry->vifs[vmNetworkIndex - 1].parent;
}
int
gh_getVifTable_vmNetworkModel(long vmIndex, long vifIndex, char *model,
                              size_t len)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return -1;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vif(entry);

    if ( vifIndex - 1 < 0 || vifIndex - 1 >= entry->nvif ) {
        /* Out of range */
        return -1;
    }

    assert ( entry->vifs[vifIndex - 1].index == vifIndex );

    (void)strncpy(model, entry->vifs[vifIndex - 1].model, len);

    return 0;
}
int
gh_getVifTable_vmNetworkPhysAddress(long vmIndex, long vifIndex,
                                    char *physaddress, size_t len)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return -1;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vif(entry);

    if ( vifIndex - 1 < 0 || vifIndex - 1 >= entry->nvif ) {
        /* Out of range */
        return -1;
    }

    assert ( entry->vifs[vifIndex - 1].index == vifIndex );

    (void)strncpy(physaddress, entry->vifs[vifIndex - 1].physaddress, len);

    return 0;
}
long
gh_getVstorageTable_vmStorageSourceType(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].type;
}
int
gh_getVstorageTable_vmStorageSourceTypeString(long vmIndex, long vmStorageIndex,
                                              char *typestr, size_t len)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return -1;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return -1;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    (void)strncpy(typestr, entry->vstorages[vmStorageIndex - 1].typestr, len);

    return 0;
}
long
gh_getVstorageTable_vmStorageAccess(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].access;
}
long
gh_getVstorageTable_vmStorageMediaType(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].mtype;
}
int
gh_getVstorageTable_vmStorageMediaTypeString(long vmIndex, long vmStorageIndex,
                                             char *typestr, size_t len)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return -1;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return -1;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    (void)strncpy(typestr, entry->vstorages[vmStorageIndex - 1].mtypestr, len);

    return 0;
}
int
gh_getVstorageTable_vmStorageResourceID(long vmIndex, long vstorageIndex,
                                       char *resid, size_t len)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return -1;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vstorageIndex - 1 < 0 || vstorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return -1;
    }

    assert ( entry->vstorages[vstorageIndex - 1].index == vstorageIndex );

    (void)strncpy(resid, entry->vstorages[vstorageIndex - 1].resourceid, len);

    return 0;
}
long
gh_getVstorageTable_vmStorageSizeUnit(long vmIndex, long vstorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vstorageIndex - 1 < 0 || vstorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vstorageIndex - 1].index == vstorageIndex );

    return entry->vstorages[vstorageIndex - 1].sizeunit;
}
long
gh_getVstorageTable_vmStorageDefinedSize(long vmIndex, long vstorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vstorageIndex - 1 < 0 || vstorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vstorageIndex - 1].index == vstorageIndex );

    return entry->vstorages[vstorageIndex - 1].defsize;
}
long
gh_getVstorageTable_vmStorageAllocatedSize(long vmIndex, long vstorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vstorageIndex - 1 < 0 || vstorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vstorageIndex - 1].index == vstorageIndex );

    return entry->vstorages[vstorageIndex - 1].allocsize;
}
uint64_t
gh_getVstorageTable_vmStorageReadIOs(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;
	vm_storage_t *storage;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].readios;
}
uint64_t
gh_getVstorageTable_vmStorageWriteIOs(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].writeios;
}
uint64_t
gh_getVstorageTable_vmStorageReadOctets(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;
	vm_storage_t *storage;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].readoctets;
}
uint64_t
gh_getVstorageTable_vmStorageWriteOctets(long vmIndex, long vmStorageIndex)
{
    vm_entry_t *entry;

    entry = _search_vm_entry(vmIndex, gh.vms);
    if ( NULL == entry ) {
        return 0;
    }
    /* TODO: Implement cache */
    _update_vm_entry_for_vstorage(entry);

    if ( vmStorageIndex - 1 < 0 || vmStorageIndex - 1 >= entry->nvstorage ) {
        /* Out of range */
        return 0;
    }

    assert ( entry->vstorages[vmStorageIndex - 1].index == vmStorageIndex );

    return entry->vstorages[vmStorageIndex - 1].writeoctets;
}

/*
 * Shutdown the global handler
 */
void
shutdown_globalHandler(void)
{
    /* ToDo: Implement something, e.g., free() etc. */
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
