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

static int ra_rpc_socket_family_to_posix(uint8_t family_ra_rpc, int *family)
{
	switch (family_ra_rpc) {
	case RA_RPC_AF_INET:
		*family = AF_INET;
		break;
	case RA_RPC_AF_INET6:
		*family = AF_INET6;
		break;
	default:
		return -EAFNOSUPPORT;
		break;
	}

	return 0;
}

static int ra_rpc_socket_family_from_posix(int family, uint8_t *family_ra_rpc)
{
	switch (family) {
	case AF_INET:
		*family_ra_rpc = RA_RPC_AF_INET;
		break;
	case AF_INET6:
		*family_ra_rpc = RA_RPC_AF_INET6;
		break;
	default:
		// TODO - family supplied by sendto function is not valid, figure out why?
		*family_ra_rpc = RA_RPC_AF_INET;
//		return -EAFNOSUPPORT;
		break;
	}

	return 0;
}

static int ra_rpc_socket_addr_from_posix(const struct sockaddr *addr,
				struct ra_erpc_sockaddr *addr_ra_rpc)
{
	int err;

	err = ra_rpc_socket_family_from_posix(addr->sa_family, &addr_ra_rpc->sa_family);
	if (err) {
		LOG_ERR("unsupported family: %d", addr->sa_family);
		// TODO - better error code
		return -1;
	}

	// TODO - use MIN macro to find best length to copy...?
	memcpy(addr_ra_rpc->sa_data, addr->data, sizeof(addr->data));

	// TODO - must be a better way to get this...?
	addr_ra_rpc->sa_len = sizeof(addr->data);

	return err;
}

static int ra_rpc_socket_addr_to_posix(const struct sockaddr *addr,
				struct ra_erpc_sockaddr *addr_ra_rpc)
{
	int err;

	err = ra_rpc_socket_family_to_posix(addr_ra_rpc->sa_family, &addr->sa_family);
	if (err) {
		LOG_ERR("unsupported family: %d", addr_ra_rpc->sa_family);
		// TODO - better error code
		return -1;
	}

	// TODO - use MIN macro to find best length to copy...?
	memcpy(addr->data, addr_ra_rpc->sa_data, sizeof(addr->data));

	return err;
}

static ssize_t ra_rpc_socket_read(void *obj, void *buf, size_t sz)
{
	LOG_DBG("ra_rpc_socket_read");

	return -1;
}

static ssize_t ra_rpc_socket_write(void *obj, const void *buf, size_t sz)
{
	LOG_DBG("ra_rpc_socket_write");

	return -1;
}

static int ra_rpc_socket_close(void *obj)
{
	int ret;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_close");

	ret = ra6w1_close(sock->sd);

	LOG_DBG("ra6w1_close: %d", ret);

	return ret;
}

static int ra_rpc_socket_ioctl(void *obj, unsigned int request, va_list args)
{
	LOG_DBG("ra_rpc_socket_ioctl");
	LOG_DBG("request: %d", request);

	return 0;
}

static int ra_rpc_socket_shutdown(void *obj, int how)
{
	LOG_DBG("ra_rpc_socket_shutdown");

	return 0;
}

static int ra_rpc_socket_bind(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_rpc;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_bind");
	LOG_DBG("sd: %d", sock->sd);

	ret = ra_rpc_socket_addr_from_posix(addr, &addr_ra_rpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	// TODO - pass addrlen instead?
	ret = ra6w1_bind(sock->sd, &addr_ra_rpc, sizeof(struct ra_erpc_sockaddr));

	LOG_DBG("ra6w1_bind: %d", ret);

	return ret;
}

static int ra_rpc_socket_connect(void *obj, const struct sockaddr *addr,
		       socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_rpc;
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

	ret = ra_rpc_socket_addr_from_posix(addr, &addr_ra_rpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	ret = ra6w1_connect(sock->sd, &addr_ra_rpc, sizeof(struct ra_erpc_sockaddr));

	LOG_DBG("ra6w1_connect: %d", ret);

	return ret;
}

static int ra_rpc_socket_listen(void *obj, int backlog)
{
	int ret;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_listen");
	LOG_DBG("sd: %d", sock->sd);
	LOG_DBG("backlog: %d", backlog);

	ret = ra6w1_listen(sock->sd, backlog);

	LOG_DBG("ra6w1_listen: %d", ret);

	return ret;
}

static int ra_rpc_socket_accept(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_rpc;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_accept");
	LOG_DBG("sd: %d", sock->sd);

	ret = ra6w1_accept(sock->sd, &addr_ra_rpc, addrlen);
	if (ret < 0) {
		return ret;
	}

	ret = ra_rpc_socket_addr_to_posix(addr, &addr_ra_rpc);

	return ret;
}

static ssize_t ra_rpc_socket_sendto(void *obj, const void *buf, size_t len, int flags,
			  const struct sockaddr *dest_addr, socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_rpc;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;

	LOG_DBG("ra_rpc_socket_sendto");
	LOG_DBG("len: %d", len);
	LOG_DBG("sd: %d", sock->sd);

	ret = ra_rpc_socket_addr_from_posix(dest_addr, &addr_ra_rpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	ret = ra6w1_sendto(sock->sd, buf, len, flags, &addr_ra_rpc, addrlen);

	LOG_DBG("ra6w1_sendto: %d", ret);

	return ret;
}

static ssize_t ra_rpc_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
			    struct sockaddr *src_addr, socklen_t *addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_rpc;
	struct ra_rpc_socket *sock = (struct ra_rpc_socket *)obj;
	uint32_t addr_len = 0;

	LOG_DBG("ra_rpc_socket_recvfrom");
	LOG_DBG("max_len: %d", max_len);
	LOG_DBG("addrlen: %d", *addrlen);
	LOG_DBG("sd: %d", sock->sd);

	ret = ra_rpc_socket_addr_from_posix(src_addr, &addr_ra_rpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	// TODO - fixme!! why is the value passed incorrect...
	//*addrlen = sizeof(struct ra_erpc_sockaddr);
	addr_len = sizeof(struct ra_erpc_sockaddr);

	// TODO - fix this!
	//ret = ra6w1_recvfrom(sock->sd, buf, max_len, flags, &addr_ra_rpc, &addr_len);
	ret = ra6w1_recv(sock->sd, buf, max_len, flags);

	LOG_DBG("ra6w1_recvfrom: %d", ret);

	return ret;
}

static int ra_rpc_socket_getsockopt(void *obj, int level, int optname,
			  void *optval, socklen_t *optlen)
{
	LOG_DBG("ra_rpc_socket_getsockopt");

	return -1;
}

static int ra_rpc_socket_setsockopt(void *obj, int level, int optname,
			  const void *optval, socklen_t optlen)
{
	LOG_DBG("ra_rpc_socket_setsockopt");

	return -1;
}

static ssize_t ra_rpc_socket_sendmsg(void *obj, const struct msghdr *msg, int flags)
{
	LOG_DBG("ra_rpc_socket_sendmsg");

	return -1;
}

static ssize_t ra_rpc_socket_recvmsg(void *obj, struct msghdr *msg, int flags)
{
	LOG_DBG("ra_rpc_socket_recvmsg");

	return -1;
}

static int ra_rpc_socket_getpeername(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	LOG_DBG("ra_rpc_socket_getpeername");

	return -1;
}

static int ra_rpc_socket_getsockname(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	LOG_DBG("ra_rpc_socket_getsockname");

	return -1;
}

static const struct socket_op_vtable ra_rpc_socket_fd_op_vtable = {
	.fd_vtable = {
		.read = ra_rpc_socket_read,
		.write = ra_rpc_socket_write,
		.close = ra_rpc_socket_close,
		.ioctl = ra_rpc_socket_ioctl,
	},

	.shutdown = ra_rpc_socket_shutdown,
	.bind = ra_rpc_socket_bind,
	.connect = ra_rpc_socket_connect,
	.listen = ra_rpc_socket_listen,
	.accept = ra_rpc_socket_accept,
	.sendto = ra_rpc_socket_sendto,
	.recvfrom = ra_rpc_socket_recvfrom,
	.getsockopt = ra_rpc_socket_getsockopt,
	.setsockopt = ra_rpc_socket_setsockopt,
	.sendmsg = ra_rpc_socket_sendmsg,
	.recvmsg = ra_rpc_socket_recvmsg,
	.getpeername = ra_rpc_socket_getpeername,
	.getsockname = ra_rpc_socket_getsockname,
};

static int ra_rpc_socket_create(int family, int type, int proto)
{
	int err;
	int sock;
	uint8_t family_ra_rpc;
	int fd = zvfs_reserve_fd();

	LOG_DBG("ra_rpc_socket_create");
	LOG_DBG("family: %d", family);
	LOG_DBG("type: %d", type);
	LOG_DBG("proto: %d", proto);

	/* Map Zephyr socket.h family to RA RPC's */
	err = ra_rpc_socket_family_from_posix(family, &family_ra_rpc);
	if (err) {
		LOG_ERR("unsupported family: %d", family);
		// TODO - better error code
		return -1;
	}

	sock = ra6w1_socket(family_ra_rpc, type, proto);

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

static bool ra_rpc_socket_is_supported(int family, int type, int proto)
{
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

int ra_rpc_socket_offload_init(struct net_if *iface)
{
	net_if_socket_offload_set(iface, ra_rpc_socket_create);

	return 0;
}

#ifdef CONFIG_NET_SOCKETS_OFFLOAD
NET_SOCKET_OFFLOAD_REGISTER(ra_rpc, CONFIG_NET_SOCKETS_OFFLOAD_PRIORITY,
				AF_UNSPEC, ra_rpc_socket_is_supported, ra_rpc_socket_create);
#endif
