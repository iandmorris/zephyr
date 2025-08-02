/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(at_wifi_socket_offload, CONFIG_WIFI_LOG_LEVEL);

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
#include "at_wifi.h"

#include <zephyr/net/socket.h>

static const struct socket_op_vtable at_wifi_socket_fd_op_vtable;

static ssize_t at_wifi_socket_read(void *obj, void *buf, size_t sz)
{
	return 0;
}

static ssize_t at_wifi_socket_write(void *obj, const void *buf, size_t sz)
{
	return 0;
}

static int at_wifi_socket_close(void *obj)
{
	return 0;
}

static int at_wifi_socket_ioctl(void *obj, unsigned int request, va_list args)
{
	return 0;
}

static int at_wifi_socket_shutdown(void *obj, int how)
{
	return 0;
}

static int at_wifi_socket_bind(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
	return 0;
}

static int at_wifi_socket_connect(void *obj, const struct sockaddr *addr,
		       socklen_t addrlen)
{
	return 0;
}

static int at_wifi_socket_listen(void *obj, int backlog)
{
	return 0;
}

static int at_wifi_socket_accept(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
	return 0;
}

static ssize_t at_wifi_socket_sendto(void *obj, const void *buf, size_t len, int flags,
			  const struct sockaddr *dest_addr, socklen_t addrlen)
{
	return 0;
}

static ssize_t at_wifi_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
			    struct sockaddr *src_addr, socklen_t *addrlen)
{
	return 0;
}

static int at_wifi_socket_getsockopt(void *obj, int level, int optname,
			  void *optval, socklen_t *optlen)
{
	return 0;
}

static int at_wifi_socket_setsockopt(void *obj, int level, int optname,
			  const void *optval, socklen_t optlen)
{
	return 0;
}

static ssize_t at_wifi_socket_sendmsg(void *obj, const struct msghdr *msg, int flags)
{
	return 0;
}

static ssize_t at_wifi_socket_recvmsg(void *obj, struct msghdr *msg, int flags)
{
	return 0;
}

static int at_wifi_socket_getpeername(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	return 0;
}

static int at_wifi_socket_getsockname(void *obj, struct sockaddr *addr,
			   socklen_t *addrlen)
{
	return 0;
}

static const struct socket_op_vtable at_wifi_socket_fd_op_vtable = {
	.fd_vtable = {
		.read = at_wifi_socket_read,
		.write = at_wifi_socket_write,
		.close = at_wifi_socket_close,
		.ioctl = at_wifi_socket_ioctl,
	},

	.shutdown = at_wifi_socket_shutdown,
	.bind = at_wifi_socket_bind,
	.connect = at_wifi_socket_connect,
	.listen = at_wifi_socket_listen,
	.accept = at_wifi_socket_accept,
	.sendto = at_wifi_socket_sendto,
	.recvfrom = at_wifi_socket_recvfrom,
	.getsockopt = at_wifi_socket_getsockopt,
	.setsockopt = at_wifi_socket_setsockopt,
	.sendmsg = at_wifi_socket_sendmsg,
	.recvmsg = at_wifi_socket_recvmsg,
	.getpeername = at_wifi_socket_getpeername,
	.getsockname = at_wifi_socket_getsockname,
};

static int at_wifi_socket_create(int family, int type, int proto)
{
	return 0;
}

static bool at_wifi_socket_is_supported(int family, int type, int proto)
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

static struct net_offload at_wifi_offload = {
	.get	       = NULL,
	.bind	       = NULL,
	.listen	       = NULL,
	.connect       = NULL,
	.accept	       = NULL,
	.send	       = NULL,
	.sendto	       = NULL,
	.recv	       = NULL,
	.put	       = NULL,
};

int at_wifi_socket_offload_init(struct net_if *iface)
{
	iface->if_dev->offload = &at_wifi_offload;

	return 0;
}

#ifdef CONFIG_NET_SOCKETS_OFFLOAD
NET_SOCKET_OFFLOAD_REGISTER(at_wifi, CONFIG_NET_SOCKETS_OFFLOAD_PRIORITY,
				AF_UNSPEC, at_wifi_socket_is_supported, at_wifi_socket_create);
#endif
