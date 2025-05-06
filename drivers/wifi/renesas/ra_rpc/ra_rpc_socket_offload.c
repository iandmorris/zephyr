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

#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>

#include "ra_rpc.h"

static int ra_rpc_socket_listen(struct net_context *context, int backlog)
{
	return -ENOTSUP;
}

static int ra_rpc_socket_bind(struct net_context *context, const struct sockaddr *addr,
		    socklen_t addrlen)
{
	return 0;
}

static int ra_rpc_socket_connect(struct net_context *context,
		       const struct sockaddr *addr,
		       socklen_t addrlen,
		       net_context_connect_cb_t cb,
		       int32_t timeout,
		       void *user_data)
{
	return 0;
}

static int ra_rpc_socket_accept(struct net_context *context,
			     net_tcp_accept_cb_t cb, int32_t timeout,
			     void *user_data)
{
	return -ENOTSUP;
}

static int ra_rpc_socket_sendto(struct net_pkt *pkt,
		      const struct sockaddr *dst_addr,
		      socklen_t addrlen,
		      net_context_send_cb_t cb,
		      int32_t timeout,
		      void *user_data)
{
	return 0;
}

static int ra_rpc_socket_send(struct net_pkt *pkt,
		    net_context_send_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return 0;
}

static int ra_rpc_socket_recv(struct net_context *context,
		    net_context_recv_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return 0;
}

static int ra_rpc_socket_put(struct net_context *context)
{
	return 0;
}

static int ra_rpc_socket_get(sa_family_t family,
		   enum net_sock_type type,
		   enum net_ip_protocol ip_proto,
		   struct net_context **context)
{
	return 0;
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
