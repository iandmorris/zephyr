/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_rpc_socket_offload, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <zephyr/posix/fcntl.h>

#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/socket_offload.h>

#include "sockets_internal.h"

#include "ra_rpc.h"
#include "c_wifi_client.h"

// TODO - can these come from wifi_common file in service?
#define RA_RPC_AF_INET		2
#define RA_RPC_AF_INET6		10

static struct ra_rpc_socket ra_rpc_socket_1;

static int ra_rpc_socket_family_from_posix(int family, int *family_ra_rpc)
{
	switch (family) {
	case AF_INET:
		*family_ra_rpc = RA_RPC_AF_INET;
		break;
	case AF_INET6:
		*family_ra_rpc = RA_RPC_AF_INET6;
		break;
	default:
		return -EAFNOSUPPORT;
	}

	return 0;
}

static int ra_rpc_socket_listen(struct net_context *context, int backlog)
{
	LOG_DBG("ra_rpc_socket_listen");

	return -ENOTSUP;
}

static int ra_rpc_socket_bind(struct net_context *context, const struct sockaddr *addr,
		    socklen_t addrlen)
{
	LOG_DBG("ra_rpc_socket_bind");

	return 0;
}

static const struct socket_op_vtable ra_rpc_socket_fd_op_vtable;

static int ra_rpc_socket_connect(void *obj, const struct sockaddr *addr,
		       socklen_t addrlen)
{
	int ret;
	int err;
	//int sd = OBJ_TO_SD(obj);

	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_connect");
	LOG_DBG("addr->sa_family: %d", addr->sa_family);
	LOG_DBG("addrlen: %d", addrlen);
	LOG_DBG("sd: %d", sock->sd);

	if (addr->sa_family == AF_INET) {
		char addr_str[NET_IPV4_ADDR_LEN];
		struct sockaddr_in * s_addr;

		s_addr = net_sin(addr);

		net_addr_ntop(addr->sa_family, &s_addr->sin_addr, addr_str, sizeof(addr_str));

		LOG_DBG("sin: addr: %s port: %d", addr_str, ntohs(s_addr->sin_port));
	}

	struct ra_erpc_sockaddr ra_sockaddr;

	err = ra_rpc_socket_family_from_posix(addr->sa_family, &ra_sockaddr.sa_family);
	if (err) {
		LOG_ERR("unsupported family: %d", addr->sa_family);
		// TODO - better error code
		return -1;
	}

	//ra_sockaddr.sa_family = addr->sa_family;
	// TODO - use MIN macro to find best length to copy...?
	memcpy(ra_sockaddr.sa_data, addr->data, sizeof(addr->data));

	// TODO - must be a better way to get this...?
	ra_sockaddr.sa_len = sizeof(addr->data);

	ret = ra6w1_connect(sock->sd, &ra_sockaddr, sizeof(struct ra_erpc_sockaddr));

	LOG_DBG("ra6w1_connect: %d", ret);

	return ret;
}

static int ra_rpc_socket_accept(struct net_context *context,
			     net_tcp_accept_cb_t cb, int32_t timeout,
			     void *user_data)
{
	LOG_DBG("ra_rpc_socket_accept");

	return -ENOTSUP;
}

static int ra_rpc_socket_sendto(void *obj, const void *buf, size_t len, int flags,
			  const struct sockaddr *dest_addr, socklen_t addrlen)
{
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_sendto");
	LOG_DBG("len: %d", len);
	LOG_DBG("sd: %d", sock->sd);

	//int32_t ra6w1_sendto(int32_t s, const uint8_t * dataptr, int32_t size, int32_t flags, const ra_erpc_sockaddr * to, uint32_t tolen);

	return 0;
}

static int ra_rpc_socket_send(struct net_pkt *pkt,
		    net_context_send_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	LOG_DBG("ra_rpc_socket_send");

	return 0;
}

static int ra_rpc_socket_recv(struct net_context *context,
		    net_context_recv_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	LOG_DBG("ra_rpc_socket_recv");

	return 0;
}

static int ra_rpc_socket_put(struct net_context *context)
{
	LOG_DBG("ra_rpc_socket_put");

	return 0;
}

static int ra_rpc_socket_get(sa_family_t family,
		   enum net_sock_type type,
		   enum net_ip_protocol ip_proto,
		   struct net_context **context)
{
	LOG_DBG("ra_rpc_socket_get");

	return 0;
}

static bool ra_rpc_socket_is_supported(int family, int type, int proto)
{
	LOG_DBG("ra_rpc_socket_is_supported");
	LOG_DBG("family: %d", family);
	LOG_DBG("type: %d", type);
	LOG_DBG("proto: %d", proto);

	if (family != AF_INET &&
	    family != AF_INET6) {
		return false;
	}

	if (type != SOCK_DGRAM &&
	    type != SOCK_STREAM) {
		return false;
	}

	if (proto != IPPROTO_TCP &&
	    proto != IPPROTO_UDP) {
		return false;
	}

	return true;
}

static const struct socket_op_vtable ra_rpc_socket_fd_op_vtable = {
//	.fd_vtable = {
//		.read = simplelink_read,
//		.write = simplelink_write,
//		.close = simplelink_close,
//		.ioctl = simplelink_ioctl,
//	},
	.bind = ra_rpc_socket_bind,
	.connect = ra_rpc_socket_connect,
	.listen = ra_rpc_socket_listen,
	.accept = ra_rpc_socket_accept,
	.sendto = ra_rpc_socket_sendto,
	//.sendmsg = simplelink_sendmsg,
	//.recvfrom = simplelink_recvfrom,
	//.getsockopt = simplelink_getsockopt,
	//.setsockopt = simplelink_setsockopt,
};

int ra_rpc_socket_create(int family, int type, int proto)
{
	int err;
	int sock;

	int fd = zvfs_reserve_fd();

	LOG_DBG("ra_rpc_socket_create");
	LOG_DBG("family: %d", family);
	LOG_DBG("type: %d", type);
	LOG_DBG("proto: %d", proto);

	/* Map Zephyr socket.h family to RA RPC's */
	err = ra_rpc_socket_family_from_posix(family, &family);
	if (err) {
		LOG_ERR("unsupported family: %d", family);
		// TODO - better error code
		return -1;
	}

	sock = ra6w1_socket(family, type, proto);

	LOG_DBG("ra6w1_socket: %d", sock);

	if (sock < 0) {
		zvfs_free_fd(fd);
		return -1;
	}

	ra_rpc_socket_1.sd = sock;

	zvfs_finalize_typed_fd(fd, &ra_rpc_socket_1,
			    (const struct fd_op_vtable *)&ra_rpc_socket_fd_op_vtable,
			    ZVFS_MODE_IFSOCK);

	return fd;
}

static struct net_offload ra_rpc_socket_offload = {
	.get	       = ra_rpc_socket_get,
	.bind	       = ra_rpc_socket_bind,
	.listen	       = ra_rpc_socket_listen,
	.connect       = ra_rpc_socket_connect,
	.accept	       = ra_rpc_socket_accept,
	.send	       = ra_rpc_socket_send,
	.sendto	       = ra_rpc_socket_sendto,
	.recv	       = ra_rpc_socket_recv,
	.put	       = ra_rpc_socket_put,
};

int ra_rpc_socket_offload_init(struct net_if *iface)
{
	iface->if_dev->offload = &ra_rpc_socket_offload;

	return 0;
}

// TODO - fix magic number below and ra_rpc_1 name
#ifdef CONFIG_NET_SOCKETS_OFFLOAD
NET_SOCKET_OFFLOAD_REGISTER(ra_rpc_1, 10,
				AF_UNSPEC, ra_rpc_socket_is_supported, ra_rpc_socket_create);
#endif
