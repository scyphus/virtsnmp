#
# $Id$
#
# Copyright (c) 2012 Scyphus Solutions Co. Ltd.  All rights reserved.
# Authors:
#      Hirochika Asai  <asai@scyphus.co.jp>
#

#EXTRA_DIST = VMM-MIB.txt
EXTRA_DIST = LICENSE
INCLUDES = `$(XML2_CONFIG) --cflags` `$(NET_SNMP_CONFIG) --cflags`

bin_PROGRAMS = virtsnmpd
virtsnmpd_SOURCES = virtsnmpd.c \
	globalHandler.c vmHypervisor.c vmInfo.c vmTable.c \
	vmCpuTable.c vmCpuAffinityTable.c vmStorageTable.c vmNetworkTable.c \
	globalHandler.h vmHypervisor.h vmInfo.h vmTable.h \
	vmCpuTable.h vmCpuAffinityTable.h vmStorageTable.h vmNetworkTable.h
virtsnmpd_LDFLAGS = -lvirt \
	`$(XML2_CONFIG) --libs` \
	`$(NET_SNMP_CONFIG) --agent-libs`

CLEANFILES = *~
