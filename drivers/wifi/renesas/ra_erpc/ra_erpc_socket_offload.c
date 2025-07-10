/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_erpc_socket_offload, CONFIG_WIFI_LOG_LEVEL);

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

#include "ra_erpc.h"
#include "c_wifi_host_to_ra_client.h"
#include "c_wifi_ra_to_host_client.h"

#include <zephyr/net/socket.h>

// TODO - can these come from wifi_common file in service?
#define RA_ERPC_AF_INET		2
#define RA_ERPC_AF_INET6		10

#define RA_ERPC_MAX_SOCKETS 4

struct ra_erpc_socket {
	int fd;
	bool in_use;
};

static struct ra_erpc_socket sockets[RA_ERPC_MAX_SOCKETS];
static const struct socket_op_vtable ra_erpc_socket_fd_op_vtable;

static int ra_erpc_socket_family_to_posix(uint8_t family_ra_erpc, int *family)
{
	switch (family_ra_erpc) {
	case RA_ERPC_AF_INET:
		*family = AF_INET;
		break;
	case RA_ERPC_AF_INET6:
		*family = AF_INET6;
		break;
	default:
		return -EAFNOSUPPORT;
		break;
	}

	return 0;
}

static int ra_erpc_socket_family_from_posix(int family, uint8_t *family_ra_erpc)
{
	switch (family) {
	case AF_INET:
		*family_ra_erpc = RA_ERPC_AF_INET;
		break;
	case AF_INET6:
		*family_ra_erpc = RA_ERPC_AF_INET6;
		break;
	default:
		// TODO - family supplied by sendto function is not valid, figure out why?
		*family_ra_erpc = RA_ERPC_AF_INET;
//		return -EAFNOSUPPORT;
		break;
	}

	return 0;
}

static int ra_erpc_socket_addr_from_posix(const struct sockaddr *addr,
				struct ra_erpc_sockaddr *addr_ra_erpc)
{
	int err;

	err = ra_erpc_socket_family_from_posix(addr->sa_family, &addr_ra_erpc->sa_family);
	if (err) {
		LOG_ERR("unsupported family: %d", addr->sa_family);
		// TODO - better error code
		return -1;
	}

	// TODO - use MIN macro to find best length to copy...?
	memcpy(addr_ra_erpc->sa_data, addr->data, sizeof(addr->data));

	// TODO - must be a better way to get this...?
	addr_ra_erpc->sa_len = sizeof(addr->data);

	return err;
}

static int ra_erpc_socket_addr_to_posix(const struct sockaddr *addr,
				struct ra_erpc_sockaddr *addr_ra_erpc)
{
	int err;

	err = ra_erpc_socket_family_to_posix(addr_ra_erpc->sa_family, &addr->sa_family);
	if (err) {
		LOG_ERR("unsupported family: %d", addr_ra_erpc->sa_family);
		// TODO - better error code
		return -1;
	}

	// TODO - use MIN macro to find best length to copy...?
	memcpy(addr->data, addr_ra_erpc->sa_data, sizeof(addr->data));

	return err;
}

static ssize_t ra_erpc_socket_read(void *obj, void *buf, size_t sz)
{
	LOG_DBG("ra_erpc_socket_read");

	return -1;
}

static ssize_t ra_erpc_socket_write(void *obj, const void *buf, size_t sz)
{
	LOG_DBG("ra_erpc_socket_write");

	return -1;
}

static int ra_erpc_socket_close(void *obj)
{
	int ret;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_close");

	for (int i = 0; i < RA_ERPC_MAX_SOCKETS; i++) {
		if (sockets[i].fd == sock->fd) {
			sockets[i].in_use = false;
			break;
		}
	}

	ret = ra6w1_close(sock->fd);

	LOG_DBG("ra6w1_close: %d", ret);

	return ret;
}

static int ra_erpc_socket_ioctl(void *obj, unsigned int request, va_list args)
{
	LOG_DBG("ra_erpc_socket_ioctl");
	LOG_DBG("request: %d", request);

	return -1;
}

static int ra_erpc_socket_shutdown(void *obj, int how)
{
	LOG_DBG("ra_erpc_socket_shutdown");

	return -1;
}

static int ra_erpc_socket_bind(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_erpc;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_bind");
	LOG_DBG("fd: %d", sock->fd);

	ret = ra_erpc_socket_addr_from_posix(addr, &addr_ra_erpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	// TODO - pass addrlen instead?
	ret = ra6w1_bind(sock->fd, &addr_ra_erpc, sizeof(struct ra_erpc_sockaddr));

	LOG_DBG("ra6w1_bind: %d", ret);

	return ret;
}

static int ra_erpc_socket_connect(void *obj, const struct sockaddr *addr,
		       socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_erpc;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_connect");
	LOG_DBG("addr->sa_family: %d", addr->sa_family);
	LOG_DBG("addrlen: %d", addrlen);
	LOG_DBG("fd: %d", sock->fd);

	if (addr->sa_family == AF_INET) {
		char addr_str[NET_IPV4_ADDR_LEN];
		struct sockaddr_in * s_addr;

		s_addr = net_sin(addr);

		net_addr_ntop(addr->sa_family, &s_addr->sin_addr, addr_str, sizeof(addr_str));

		LOG_DBG("sin: addr: %s port: %d", addr_str, ntohs(s_addr->sin_port));
	}

	ret = ra_erpc_socket_addr_from_posix(addr, &addr_ra_erpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

	ret = ra6w1_connect(sock->fd, &addr_ra_erpc, sizeof(struct ra_erpc_sockaddr));

	LOG_DBG("ra6w1_connect: %d", ret);

	return ret;
}

static int ra_erpc_socket_listen(void *obj, int backlog)
{
	int ret;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_listen");
	LOG_DBG("fd: %d", sock->fd);
	LOG_DBG("backlog: %d", backlog);

	ret = ra6w1_listen(sock->fd, backlog);

	LOG_DBG("ra6w1_listen: %d", ret);

	return ret;
}

static int ra_erpc_socket_accept(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
	int conn_fd;
	struct ra_erpc_sockaddr addr_ra_erpc;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_accept");
	LOG_DBG("fd: %d", sock->fd);

    // reserve a file descriptor
    int fd = zvfs_reserve_fd();

    // Accept returns file descriptor for new connected socket
	conn_fd = ra6w1_accept(sock->fd, &addr_ra_erpc, addrlen);
    LOG_DBG("ra6w1_accept: %d", conn_fd);
	if (conn_fd < 0) {
        zvfs_free_fd(fd);
		return conn_fd;
	}

	int ret = ra_erpc_socket_addr_to_posix(addr, &addr_ra_erpc);
    if (ret < 0) {
        zvfs_free_fd(fd);
        LOG_DBG("ra_erpc_socket_addr_to_posix error: %d", ret);
		return ret;
	}

    // keep track of the new fd returned by accept
    struct ra_erpc_socket *socket = NULL;
    for (int i = 0; i < RA_ERPC_MAX_SOCKETS; i++) {
        if (sockets[i].in_use == false) {
            sockets[i].fd = conn_fd;
            sockets[i].in_use = true;
            socket = &sockets[i];
            break;
        }
	}

    if (socket == NULL){
		zvfs_free_fd(fd);
		return -1;
	}

    // associate zephyr file descriptor with the socket obj and offload table
    // ensures subsequent socket operations using zephyr fd will use the appropriate RA6W1 fd
	zvfs_finalize_typed_fd(fd, socket,
			    (const struct fd_op_vtable *)&ra_erpc_socket_fd_op_vtable,
			    ZVFS_MODE_IFSOCK);
            
    return fd;
}

static ssize_t ra_erpc_socket_sendto(void *obj, const void *buf, size_t len, int flags,
			  const struct sockaddr *dest_addr, socklen_t addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_erpc;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_sendto");
	LOG_DBG("len: %d", len);
	LOG_DBG("fd: %d", sock->fd);

	ret = ra_erpc_socket_addr_from_posix(dest_addr, &addr_ra_erpc);
	if (ret) {
		// TODO - better error code
		return -1;
	}

    // TODO this is handling IPV4, need to extend for IPV6
    ret = ra6w1_sendto(sock->fd, buf, len, flags, &addr_ra_erpc, sizeof(ra_erpc_sockaddr));

	LOG_DBG("ra6w1_sendto: %d", ret);

	return ret;
}

static ssize_t ra_erpc_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
			    struct sockaddr *src_addr, socklen_t *addrlen)
{
	int ret;
	struct ra_erpc_sockaddr addr_ra_erpc;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	LOG_DBG("ra_erpc_socket_recvfrom");
	LOG_DBG("fd: %d", sock->fd);
	LOG_DBG("max_len: %d", max_len);

    *addrlen = sizeof(struct ra_erpc_sockaddr);

	ret = ra6w1_recvfrom(sock->fd, buf, max_len, flags, &addr_ra_erpc, addrlen);

	LOG_DBG("ra6w1_recvfrom: %d", ret);

    if (ret > 0 && src_addr != NULL) {
        int err = ra_erpc_socket_addr_to_posix(src_addr, &addr_ra_erpc);
        if (err != 0) {
            return err;
        }
    }

	return ret;
}

static int ra_erpc_socket_getsockopt(void *obj, int level, int optname,
			  void *optval, socklen_t *optlen)
{
	LOG_DBG("ra_erpc_socket_getsockopt");

	return -1;
}

static int ra_erpc_socket_setsockopt(void *obj, int level, int optname,
			  const void *optval, socklen_t optlen)
{
	LOG_DBG("ra_erpc_socket_setsockopt");
    LOG_DBG("level=%d", level);
    LOG_DBG("optname=%d", level);

    int ret;
	struct ra_erpc_socket *sock = (struct ra_erpc_socket *)obj;

	ret = ra6w1_setsockopt(sock->fd, level, optname, optval, optlen);

	LOG_DBG("ra6w1_setsockopt: %d", ret);

	return ret;
}

static ssize_t ra_erpc_socket_sendmsg(void *obj, const struct msghdr *msg, int flags)
{
	LOG_DBG("ra_erpc_socket_sendmsg");

	return -1;
}

static ssize_t ra_erpc_socket_recvmsg(void *obj, struct msghdr *msg, int flags)
{
	LOG_DBG("ra_erpc_socket_recvmsg");

	return -1;
}

static int ra_erpc_socket_getpeername(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	LOG_DBG("ra_erpc_socket_getpeername");

	return -1;
}

static int ra_erpc_socket_getsockname(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	LOG_DBG("ra_erpc_socket_getsockname");

	return -1;
}

static const struct socket_op_vtable ra_erpc_socket_fd_op_vtable = {
	.fd_vtable = {
		.read = ra_erpc_socket_read,
		.write = ra_erpc_socket_write,
		.close = ra_erpc_socket_close,
		.ioctl = ra_erpc_socket_ioctl,
	},

	.shutdown = ra_erpc_socket_shutdown,
	.bind = ra_erpc_socket_bind,
	.connect = ra_erpc_socket_connect,
	.listen = ra_erpc_socket_listen,
	.accept = ra_erpc_socket_accept,
	.sendto = ra_erpc_socket_sendto,
	.recvfrom = ra_erpc_socket_recvfrom,
	.getsockopt = ra_erpc_socket_getsockopt,
	.setsockopt = ra_erpc_socket_setsockopt,
	.sendmsg = ra_erpc_socket_sendmsg,
	.recvmsg = ra_erpc_socket_recvmsg,
	.getpeername = ra_erpc_socket_getpeername,
	.getsockname = ra_erpc_socket_getsockname,
};

static int ra_erpc_socket_create(int family, int type, int proto)
{
	int err;
	int sock;
	uint8_t family_ra_erpc;
	int fd = zvfs_reserve_fd();
	struct ra_erpc_socket *socket = NULL;

	LOG_DBG("ra_erpc_socket_create");
	LOG_DBG("family: %d", family);
	LOG_DBG("type: %d", type);
	LOG_DBG("proto: %d", proto);

	/* Map Zephyr socket.h family to RA RPC's */
	err = ra_erpc_socket_family_from_posix(family, &family_ra_erpc);
	if (err) {
		LOG_ERR("unsupported family: %d", family);
		// TODO - better error code
		return -1;
	}

	sock = ra6w1_socket(family_ra_erpc, type, proto);

	LOG_DBG("ra6w1_socket: %d", sock);

	if (sock < 0) {
		zvfs_free_fd(fd);
		return -1;
	}

	for (int i = 0; i < RA_ERPC_MAX_SOCKETS; i++) {
		if (sockets[i].in_use == false) {
			sockets[i].fd = sock;
			sockets[i].in_use = true;
			socket = &sockets[i];
			break;
		}
	}

	if (socket == NULL){
		zvfs_free_fd(fd);
		return -1;
	}

	zvfs_finalize_typed_fd(fd, socket,
			    (const struct fd_op_vtable *)&ra_erpc_socket_fd_op_vtable,
			    ZVFS_MODE_IFSOCK);

	return fd;
}

static bool ra_erpc_socket_is_supported(int family, int type, int proto)
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

int ra_erpc_socket_offload_init(struct net_if *iface)
{
	memset(sockets, 0, sizeof(sockets));

	net_if_socket_offload_set(iface, ra_erpc_socket_create);

	return 0;
}

#ifdef CONFIG_NET_SOCKETS_OFFLOAD
NET_SOCKET_OFFLOAD_REGISTER(ra_erpc, CONFIG_NET_SOCKETS_OFFLOAD_PRIORITY,
				AF_UNSPEC, ra_erpc_socket_is_supported, ra_erpc_socket_create);
#endif
