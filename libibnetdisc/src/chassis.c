/*
 * Copyright (c) 2004-2007 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2007 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2008 Lawrence Livermore National Lab.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*========================================================*/
/*               FABRIC SCANNER SPECIFIC DATA             */
/*========================================================*/

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <inttypes.h>

#include <infiniband/mad.h>

#include "internal.h"
#include "chassis.h"

static char *ChassisTypeStr[5] = { "", "ISR9288", "ISR9096", "ISR2012", "ISR2004" };
static char *ChassisSlotTypeStr[4] = { "", "Line", "Spine", "SRBD" };

char *ibnd_get_chassis_type(ibnd_node_t *node)
{
	/* Currently, only if Voltaire chassis */
	if (mad_get_field(node->info, 0, IB_NODE_VENDORID_F) != VTR_VENDOR_ID)
		return (NULL);
	if (!node->chassis)
		return (NULL);
	if (node->ch_type == UNRESOLVED_CT
		|| node->ch_type > ISR2004_CT)
		return (NULL);
	return ChassisTypeStr[node->ch_type];
}

char *ibnd_get_chassis_slot_str(ibnd_node_t *node, char *str, size_t size)
{
	/* Currently, only if Voltaire chassis */
	if (mad_get_field(node->info, 0, IB_NODE_VENDORID_F) != VTR_VENDOR_ID)
		return (NULL);
	if (!node->chassis)
		return (NULL);
	if (node->ch_slot == UNRESOLVED_CS
		|| node->ch_slot > SRBD_CS)
		return (NULL);
	if (!str)
		return (NULL);
	snprintf(str, size, "%s %d Chip %d",
			ChassisSlotTypeStr[node->ch_slot],
			node->ch_slotnum,
			node->ch_anafanum);
	return (str);
}

static ibnd_chassis_t *find_chassisnum(struct ibnd_fabric *fabric, unsigned char chassisnum)
{
	ibnd_chassis_t *current;

	for (current = fabric->first_chassis; current; current = current->next) {
		if (current->chassisnum == chassisnum)
			return current;
	}

	return NULL;
}

static uint64_t topspin_chassisguid(uint64_t guid)
{
	/* Byte 3 in system image GUID is chassis type, and */
	/* Byte 4 is location ID (slot) so just mask off byte 4 */
	return guid & 0xffffffff00ffffffULL;
}

int ibnd_is_xsigo_guid(uint64_t guid)
{
	if ((guid & 0xffffff0000000000ULL) == 0x0013970000000000ULL)
		return 1;
	else
		return 0;
}

static int is_xsigo_leafone(uint64_t guid)
{
	if ((guid & 0xffffffffff000000ULL) == 0x0013970102000000ULL)
		return 1;
	else
		return 0;
}

int ibnd_is_xsigo_hca(uint64_t guid)
{
	/* NodeType 2 is HCA */
	if ((guid & 0xffffffff00000000ULL) == 0x0013970200000000ULL)
		return 1;
	else
		return 0;
}

int ibnd_is_xsigo_tca(uint64_t guid)
{
	/* NodeType 3 is TCA */
	if ((guid & 0xffffffff00000000ULL) == 0x0013970300000000ULL)
		return 1;
	else
		return 0;
}

static int is_xsigo_ca(uint64_t guid)
{
	if (ibnd_is_xsigo_hca(guid) || ibnd_is_xsigo_tca(guid))
		return 1;
	else
		return 0;
}

static int is_xsigo_switch(uint64_t guid)
{
	if ((guid & 0xffffffff00000000ULL) == 0x0013970100000000ULL)
		return 1;
	else
		return 0;
}

static uint64_t xsigo_chassisguid(ibnd_node_t *node)
{
	uint64_t sysimgguid = mad_get_field64(node->info, 0, IB_NODE_SYSTEM_GUID_F);
	uint64_t remote_sysimgguid;

	if (!is_xsigo_ca(sysimgguid)) {
		/* Byte 3 is NodeType and byte 4 is PortType */
		/* If NodeType is 1 (switch), PortType is masked */
		if (is_xsigo_switch(sysimgguid))
			return sysimgguid & 0xffffffff00ffffffULL;
		else
			return sysimgguid;
	} else {
		if (!node->ports || !node->ports[1])
			return (0);

		/* Is there a peer port ? */
		if (!node->ports[1]->remoteport)
			return sysimgguid;

		/* If peer port is Leaf 1, use its chassis GUID */
		remote_sysimgguid = mad_get_field64(
					node->ports[1]->remoteport->node->info,
					0, IB_NODE_SYSTEM_GUID_F);
		if (is_xsigo_leafone(remote_sysimgguid))
			return remote_sysimgguid & 0xffffffff00ffffffULL;
		else
			return sysimgguid;
	}
}

static uint64_t get_chassisguid(ibnd_node_t *node)
{
	uint32_t vendid = mad_get_field(node->info, 0, IB_NODE_VENDORID_F);
	uint64_t sysimgguid = mad_get_field64(node->info, 0, IB_NODE_SYSTEM_GUID_F);

	if (vendid == TS_VENDOR_ID || vendid == SS_VENDOR_ID)
		return topspin_chassisguid(sysimgguid);
	else if (vendid == XS_VENDOR_ID || ibnd_is_xsigo_guid(sysimgguid))
		return xsigo_chassisguid(node);
	else
		return sysimgguid;
}

static ibnd_chassis_t *find_chassisguid(ibnd_node_t *node)
{
	struct ibnd_fabric *f = CONV_FABRIC_INTERNAL(node->fabric);
	ibnd_chassis_t *current;
	uint64_t chguid;

	chguid = get_chassisguid(node);
	for (current = f->first_chassis; current; current = current->next) {
		if (current->chassisguid == chguid)
			return current;
	}

	return NULL;
}

uint64_t ibnd_get_chassis_guid(ibnd_fabric_t *fabric, unsigned char chassisnum)
{
	struct ibnd_fabric *f = CONV_FABRIC_INTERNAL(fabric);
	ibnd_chassis_t *chassis;

	chassis = find_chassisnum(f, chassisnum);
	if (chassis)
		return chassis->chassisguid;
	else
		return 0;
}

static int is_router(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_IB_FC_ROUTER ||
		devid == VTR_DEVID_IB_IP_ROUTER);
}

static int is_spine_9096(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SFB4 ||
		devid == VTR_DEVID_SFB4_DDR);
}

static int is_spine_9288(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SFB12 ||
		devid == VTR_DEVID_SFB12_DDR);
}

static int is_spine_2004(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SFB2004);
}

static int is_spine_2012(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SFB2012);
}

static int is_spine(struct ibnd_node *n)
{
	return (is_spine_9096(n) || is_spine_9288(n) ||
		is_spine_2004(n) || is_spine_2012(n));
}

static int is_line_24(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SLB24 ||
		devid == VTR_DEVID_SLB24_DDR ||
		devid == VTR_DEVID_SRB2004);
}

static int is_line_8(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SLB8);
}

static int is_line_2024(struct ibnd_node *n)
{
	uint32_t devid = mad_get_field(n->node.info, 0, IB_NODE_DEVID_F);
	return (devid == VTR_DEVID_SLB2024);
}

static int is_line(struct ibnd_node *n)
{
	return (is_line_24(n) || is_line_8(n) || is_line_2024(n));
}

int is_chassis_switch(struct ibnd_node *n)
{
    return (is_spine(n) || is_line(n));
}

/* these structs help find Line (Anafa) slot number while using spine portnum */
char line_slot_2_sfb4[25]        = { 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4 };
char anafa_line_slot_2_sfb4[25]  = { 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2 };
char line_slot_2_sfb12[25]       = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,10, 10, 11, 11, 12, 12 };
char anafa_line_slot_2_sfb12[25] = { 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2 };

/* IPR FCR modules connectivity while using sFB4 port as reference */
char ipr_slot_2_sfb4_port[25]    = { 0, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1 };

/* these structs help find Spine (Anafa) slot number while using spine portnum */
char spine12_slot_2_slb[25]      = { 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char anafa_spine12_slot_2_slb[25]= { 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char spine4_slot_2_slb[25]       = { 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char anafa_spine4_slot_2_slb[25] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
/*	reference                     { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 }; */

static void get_sfb_slot(struct ibnd_node *node, ibnd_port_t *lineport)
{
	ibnd_node_t *n = (ibnd_node_t *)node;

	n->ch_slot = SPINE_CS;
	if (is_spine_9096(node)) {
		n->ch_type = ISR9096_CT;
		n->ch_slotnum = spine4_slot_2_slb[lineport->portnum];
		n->ch_anafanum = anafa_spine4_slot_2_slb[lineport->portnum];
	} else if (is_spine_9288(node)) {
		n->ch_type = ISR9288_CT;
		n->ch_slotnum = spine12_slot_2_slb[lineport->portnum];
		n->ch_anafanum = anafa_spine12_slot_2_slb[lineport->portnum];
	} else if (is_spine_2012(node)) {
		n->ch_type = ISR2012_CT;
		n->ch_slotnum = spine12_slot_2_slb[lineport->portnum];
		n->ch_anafanum = anafa_spine12_slot_2_slb[lineport->portnum];
	} else if (is_spine_2004(node)) {
		n->ch_type = ISR2004_CT;
		n->ch_slotnum = spine4_slot_2_slb[lineport->portnum];
		n->ch_anafanum = anafa_spine4_slot_2_slb[lineport->portnum];
	} else {
		IBPANIC("Unexpected node found: guid 0x%016" PRIx64,
		node->node.guid);
	}
}

static void get_router_slot(struct ibnd_node *node, ibnd_port_t *spineport)
{
	ibnd_node_t *n = (ibnd_node_t *)node;
	uint64_t guessnum = 0;

	node->ch_found = 1;

	n->ch_slot = SRBD_CS;
	if (is_spine_9096(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR9096_CT;
		n->ch_slotnum = line_slot_2_sfb4[spineport->portnum];
		n->ch_anafanum = ipr_slot_2_sfb4_port[spineport->portnum];
	} else if (is_spine_9288(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR9288_CT;
		n->ch_slotnum = line_slot_2_sfb12[spineport->portnum];
		/* this is a smart guess based on nodeguids order on sFB-12 module */
		guessnum = spineport->node->guid % 4;
		/* module 1 <--> remote anafa 3 */
		/* module 2 <--> remote anafa 2 */
		/* module 3 <--> remote anafa 1 */
		n->ch_anafanum = (guessnum == 3 ? 1 : (guessnum == 1 ? 3 : 2));
	} else if (is_spine_2012(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR2012_CT;
		n->ch_slotnum = line_slot_2_sfb12[spineport->portnum];
		/* this is a smart guess based on nodeguids order on sFB-12 module */
		guessnum = spineport->node->guid % 4;
		// module 1 <--> remote anafa 3
		// module 2 <--> remote anafa 2
		// module 3 <--> remote anafa 1
		n->ch_anafanum = (guessnum == 3? 1 : (guessnum == 1 ? 3 : 2));
	} else if (is_spine_2004(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR2004_CT;
		n->ch_slotnum = line_slot_2_sfb4[spineport->portnum];
		n->ch_anafanum = ipr_slot_2_sfb4_port[spineport->portnum];
	} else {
		IBPANIC("Unexpected node found: guid 0x%016" PRIx64,
		spineport->node->guid);
	}
}

static void get_slb_slot(ibnd_node_t *n, ibnd_port_t *spineport)
{
	n->ch_slot = LINE_CS;
	if (is_spine_9096(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR9096_CT;
		n->ch_slotnum = line_slot_2_sfb4[spineport->portnum];
		n->ch_anafanum = anafa_line_slot_2_sfb4[spineport->portnum];
	} else if (is_spine_9288(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR9288_CT;
		n->ch_slotnum = line_slot_2_sfb12[spineport->portnum];
		n->ch_anafanum = anafa_line_slot_2_sfb12[spineport->portnum];
	} else if (is_spine_2012(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR2012_CT;
		n->ch_slotnum = line_slot_2_sfb12[spineport->portnum];
		n->ch_anafanum = anafa_line_slot_2_sfb12[spineport->portnum];
	} else if (is_spine_2004(CONV_NODE_INTERNAL(spineport->node))) {
		n->ch_type = ISR2004_CT;
		n->ch_slotnum = line_slot_2_sfb4[spineport->portnum];
		n->ch_anafanum = anafa_line_slot_2_sfb4[spineport->portnum];
	} else {
		IBPANIC("Unexpected node found: guid 0x%016" PRIx64,
		spineport->node->guid);
	}
}

/* forward declare this */
static void voltaire_portmap(ibnd_port_t *port);
/*
	This function called for every Voltaire node in fabric
	It could be optimized so, but time overhead is very small
	and its only diag.util
*/
static void fill_voltaire_chassis_record(struct ibnd_node *node)
{
	ibnd_node_t *n = (ibnd_node_t *)node;
	int p = 0;
	ibnd_port_t *port;
	struct ibnd_node *remnode = 0;

	if (node->ch_found) /* somehow this node has already been passed */
		return;
	node->ch_found = 1;

	/* node is router only in case of using unique lid */
	/* (which is lid of chassis router port) */
	/* in such case node->ports is actually a requested port... */
	if (is_router(node)) {
		/* find the remote node */
		for (p = 1; p <= node->node.numports; p++) {
			port = node->node.ports[p];
			if (port && is_spine(CONV_NODE_INTERNAL(port->remoteport->node)))
				get_router_slot(node, port->remoteport);
		}
	} else if (is_spine(node)) {
		for (p = 1; p <= node->node.numports; p++) {
			port = node->node.ports[p];
			if (!port || !port->remoteport)
				continue;
			remnode = CONV_NODE_INTERNAL(port->remoteport->node);
			if (remnode->node.type != IB_NODE_SWITCH) {
				if (!remnode->ch_found)
					get_router_slot(remnode, port);
				continue;
			}
			if (!n->ch_type)
				/* we assume here that remoteport belongs to line */
				get_sfb_slot(node, port->remoteport);

				/* we could break here, but need to find if more routers connected */
		}

	} else if (is_line(node)) {
		for (p = 1; p <= node->node.numports; p++) {
			port = node->node.ports[p];
			if (!port || port->portnum > 12 || !port->remoteport)
				continue;
			/* we assume here that remoteport belongs to spine */
			get_slb_slot(n, port->remoteport);
			break;
		}
	}

	/* for each port of this node, map external ports */
	for (p = 1; p <= node->node.numports; p++) {
		port = node->node.ports[p];
		if (!port)
			continue;
		voltaire_portmap(port);
	}

	return;
}

static int get_line_index(ibnd_node_t *node)
{
	int retval = 3 * (node->ch_slotnum - 1) + node->ch_anafanum;

	if (retval > LINES_MAX_NUM || retval < 1)
		IBPANIC("Internal error");
	return retval;
}

static int get_spine_index(ibnd_node_t *node)
{
	int retval;

	if (is_spine_9288(CONV_NODE_INTERNAL(node)) || is_spine_2012(CONV_NODE_INTERNAL(node)))
		retval = 3 * (node->ch_slotnum - 1) + node->ch_anafanum;
	else
		retval = node->ch_slotnum;

	if (retval > SPINES_MAX_NUM || retval < 1)
		IBPANIC("Internal error");
	return retval;
}

static void insert_line_router(ibnd_node_t *node, ibnd_chassis_t *chassis)
{
	int i = get_line_index(node);

	if (chassis->linenode[i])
		return;		/* already filled slot */

	chassis->linenode[i] = node;
	node->chassis = chassis;
}

static void insert_spine(ibnd_node_t *node, ibnd_chassis_t *chassis)
{
	int i = get_spine_index(node);

	if (chassis->spinenode[i])
		return;		/* already filled slot */

	chassis->spinenode[i] = node;
	node->chassis = chassis;
}

static void pass_on_lines_catch_spines(ibnd_chassis_t *chassis)
{
	ibnd_node_t *node, *remnode;
	ibnd_port_t *port;
	int i, p;

	for (i = 1; i <= LINES_MAX_NUM; i++) {
		node = chassis->linenode[i];

		if (!(node && is_line(CONV_NODE_INTERNAL(node))))
			continue;	/* empty slot or router */

		for (p = 1; p <= node->numports; p++) {
			port = node->ports[p];
			if (!port || port->portnum > 12 || !port->remoteport)
				continue;

			remnode = port->remoteport->node;

			if (!CONV_NODE_INTERNAL(remnode)->ch_found)
				continue;	/* some error - spine not initialized ? FIXME */
			insert_spine(remnode, chassis);
		}
	}
}

static void pass_on_spines_catch_lines(ibnd_chassis_t *chassis)
{
	ibnd_node_t *node, *remnode;
	ibnd_port_t *port;
	int i, p;

	for (i = 1; i <= SPINES_MAX_NUM; i++) {
		node = chassis->spinenode[i];
		if (!node)
			continue;	/* empty slot */
		for (p = 1; p <= node->numports; p++) {
			port = node->ports[p];
			if (!port || !port->remoteport)
				continue;
			remnode = port->remoteport->node;

			if (!CONV_NODE_INTERNAL(remnode)->ch_found)
				continue;	/* some error - line/router not initialized ? FIXME */
			insert_line_router(remnode, chassis);
		}
	}
}

/*
	Stupid interpolation algorithm...
	But nothing to do - have to be compliant with VoltaireSM/NMS
*/
static void pass_on_spines_interpolate_chguid(ibnd_chassis_t *chassis)
{
	ibnd_node_t *node;
	int i;

	for (i = 1; i <= SPINES_MAX_NUM; i++) {
		node = chassis->spinenode[i];
		if (!node)
			continue;	/* skip the empty slots */

		/* take first guid minus one to be consistent with SM */
		chassis->chassisguid = node->guid - 1;
		break;
	}
}

/*
	This function fills chassis structure with all nodes
	in that chassis
	chassis structure = structure of one standalone chassis
*/
static void build_chassis(struct ibnd_node *node, ibnd_chassis_t *chassis)
{
	int p = 0;
	struct ibnd_node *remnode = 0;
	ibnd_port_t *port = 0;

	/* we get here with node = chassis_spine */
	insert_spine((ibnd_node_t *)node, chassis);

	/* loop: pass on all ports of node */
	for (p = 1; p <= node->node.numports; p++ ) {
		port = node->node.ports[p];
		if (!port || !port->remoteport)
			continue;
		remnode = CONV_NODE_INTERNAL(port->remoteport->node);

		if (!remnode->ch_found)
			continue; /* some error - line or router not initialized ? FIXME */

		insert_line_router(&(remnode->node), chassis);
	}

	pass_on_lines_catch_spines(chassis);
	/* this pass needed for to catch routers, since routers connected only */
	/* to spines in slot 1 or 4 and we could miss them first time */
	pass_on_spines_catch_lines(chassis);

	/* additional 2 passes needed for to overcome a problem of pure "in-chassis" */
	/* connectivity - extra pass to ensure that all related chips/modules */
	/* inserted into the chassis */
	pass_on_lines_catch_spines(chassis);
	pass_on_spines_catch_lines(chassis);
	pass_on_spines_interpolate_chguid(chassis);
}

/*========================================================*/
/*                INTERNAL TO EXTERNAL PORT MAPPING       */
/*========================================================*/

/*
Description : On ISR9288/9096 external ports indexing
              is not matching the internal ( anafa ) port
              indexes. Use this MAP to translate the data you get from
              the OpenIB diagnostics (smpquery, ibroute, ibtracert, etc.)


Module : sLB-24
                anafa 1             anafa 2
ext port | 13 14 15 16 17 18 | 19 20 21 22 23 24
int port | 22 23 24 18 17 16 | 22 23 24 18 17 16
ext port | 1  2  3  4  5  6  | 7  8  9  10 11 12
int port | 19 20 21 15 14 13 | 19 20 21 15 14 13
------------------------------------------------

Module : sLB-8
                anafa 1             anafa 2
ext port | 13 14 15 16 17 18 | 19 20 21 22 23 24
int port | 24 23 22 18 17 16 | 24 23 22 18 17 16
ext port | 1  2  3  4  5  6  | 7  8  9  10 11 12
int port | 21 20 19 15 14 13 | 21 20 19 15 14 13

----------->
                anafa 1             anafa 2
ext port | -  -  5  -  -  6  | -  -  7  -  -  8
int port | 24 23 22 18 17 16 | 24 23 22 18 17 16
ext port | -  -  1  -  -  2  | -  -  3  -  -  4
int port | 21 20 19 15 14 13 | 21 20 19 15 14 13
------------------------------------------------

Module : sLB-2024

ext port | 13 14 15 16 17 18 19 20 21 22 23 24
A1 int port| 13 14 15 16 17 18 19 20 21 22 23 24
ext port | 1 2 3 4 5 6 7 8 9 10 11 12
A2 int port| 13 14 15 16 17 18 19 20 21 22 23 24
---------------------------------------------------

*/

int int2ext_map_slb24[2][25] = {
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 5, 4, 18, 17, 16, 1, 2, 3, 13, 14, 15 },
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 11, 10, 24, 23, 22, 7, 8, 9, 19, 20, 21 }
				};
int int2ext_map_slb8[2][25] = {
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 6, 6, 6, 1, 1, 1, 5, 5, 5 },
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 8, 8, 8, 3, 3, 3, 7, 7, 7 }
				};
int int2ext_map_slb2024[2][25] = {
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 },
					{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }
				};
/*	reference			{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 }; */

/* map internal ports to external ports if appropriate */
static void
voltaire_portmap(ibnd_port_t *port)
{
	struct ibnd_node *n = CONV_NODE_INTERNAL(port->node);
	int portnum = port->portnum;
	int chipnum = 0;
	ibnd_node_t *node = port->node;

	if (!n->ch_found || !is_line(CONV_NODE_INTERNAL(node)) || (portnum < 13 || portnum > 24)) {
		port->ext_portnum = 0;
		return;
	}

	if (port->node->ch_anafanum < 1 || port->node->ch_anafanum > 2) {
		port->ext_portnum = 0;
		return;
	}

	chipnum = port->node->ch_anafanum - 1;

	if (is_line_24(CONV_NODE_INTERNAL(node)))
		port->ext_portnum = int2ext_map_slb24[chipnum][portnum];
	else if (is_line_2024(CONV_NODE_INTERNAL(node)))
		port->ext_portnum = int2ext_map_slb2024[chipnum][portnum];
	else
		port->ext_portnum = int2ext_map_slb8[chipnum][portnum];
}

static void add_chassis(struct ibnd_fabric *fabric)
{
	if (!(fabric->current_chassis = calloc(1, sizeof(ibnd_chassis_t))))
		IBPANIC("out of mem");

	if (fabric->first_chassis == NULL) {
		fabric->first_chassis = fabric->current_chassis;
		fabric->last_chassis = fabric->current_chassis;
	} else {
		fabric->last_chassis->next = fabric->current_chassis;
		fabric->last_chassis = fabric->current_chassis;
	}
}

static void
add_node_to_chassis(ibnd_chassis_t *chassis, ibnd_node_t *node)
{
	node->chassis = chassis;
	node->next_chassis_node = chassis->nodes;
	chassis->nodes = node;
}

/*
	Main grouping function
	Algorithm:
	1. pass on every Voltaire node
	2. catch spine chip for every Voltaire node
		2.1 build/interpolate chassis around this chip
		2.2 go to 1.
	3. pass on non Voltaire nodes (SystemImageGUID based grouping)
	4. now group non Voltaire nodes by SystemImageGUID
	Returns:
	Pointer to the first chassis in a NULL terminated list of chassis in
	the fabric specified.
*/
ibnd_chassis_t *group_nodes(struct ibnd_fabric *fabric)
{
	struct ibnd_node *node;
	int dist;
	int chassisnum = 0;
	ibnd_chassis_t *chassis;

	fabric->first_chassis = NULL;
	fabric->current_chassis = NULL;

	/* first pass on switches and build for every Voltaire node */
	/* an appropriate chassis record (slotnum and position) */
	/* according to internal connectivity */
	/* not very efficient but clear code so... */
	for (dist = 0; dist <= fabric->fabric.maxhops_discovered; dist++) {
		for (node = fabric->nodesdist[dist]; node; node = node->dnext) {
			if (mad_get_field(node->node.info, 0, IB_NODE_VENDORID_F) == VTR_VENDOR_ID)
				fill_voltaire_chassis_record(node);
		}
	}

	/* separate every Voltaire chassis from each other and build linked list of them */
	/* algorithm: catch spine and find all surrounding nodes */
	for (dist = 0; dist <= fabric->fabric.maxhops_discovered; dist++) {
		for (node = fabric->nodesdist[dist]; node; node = node->dnext) {
			if (mad_get_field(node->node.info, 0, IB_NODE_VENDORID_F) != VTR_VENDOR_ID)
				continue;
			//if (!node->node.chrecord || node->node.chrecord->chassisnum || !is_spine(node))
			if (!node->ch_found
					|| (node->node.chassis && node->node.chassis->chassisnum)
					|| !is_spine(node))
				continue;
			add_chassis(fabric);
			fabric->current_chassis->chassisnum = ++chassisnum;
			build_chassis(node, fabric->current_chassis);
		}
	}

	/* now make pass on nodes for chassis which are not Voltaire */
	/* grouped by common SystemImageGUID */
	for (dist = 0; dist <= fabric->fabric.maxhops_discovered; dist++) {
		for (node = fabric->nodesdist[dist]; node; node = node->dnext) {
			if (mad_get_field(node->node.info, 0, IB_NODE_VENDORID_F) == VTR_VENDOR_ID)
				continue;
			if (mad_get_field64(node->node.info, 0, IB_NODE_SYSTEM_GUID_F)) {
				chassis = find_chassisguid((ibnd_node_t *)node);
				if (chassis)
					chassis->nodecount++;
				else {
					/* Possible new chassis */
					add_chassis(fabric);
					fabric->current_chassis->chassisguid =
							get_chassisguid((ibnd_node_t *)node);
					fabric->current_chassis->nodecount = 1;
				}
			}
		}
	}

	/* now, make another pass to see which nodes are part of chassis */
	/* (defined as chassis->nodecount > 1) */
	for (dist = 0; dist <= MAXHOPS; ) {
		for (node = fabric->nodesdist[dist]; node; node = node->dnext) {
			if (mad_get_field(node->node.info, 0, IB_NODE_VENDORID_F) == VTR_VENDOR_ID)
				continue;
			if (mad_get_field64(node->node.info, 0, IB_NODE_SYSTEM_GUID_F)) {
				chassis = find_chassisguid((ibnd_node_t *)node);
				if (chassis && chassis->nodecount > 1) {
					if (!chassis->chassisnum)
						chassis->chassisnum = ++chassisnum;
					if (!node->ch_found) {
						node->ch_found = 1;
						add_node_to_chassis(chassis, (ibnd_node_t *)node);
					}
				}
			}
		}
		if (dist == fabric->fabric.maxhops_discovered)
			dist = MAXHOPS;	/* skip to CAs */
		else
			dist++;
	}

	return (fabric->first_chassis);
}