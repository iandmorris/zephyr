/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_ra_at_offload, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/net_offload.h>

static int ra_get(sa_family_t family,
		   enum net_sock_type type,
		   enum net_ip_protocol ip_proto,
		   struct net_context **context)
{
	return 0;
}

static int ra_bind(struct net_context *context,
			const struct sockaddr *addr,
		    socklen_t addrlen)
{
	return 0;
}

static int ra_listen(struct net_context *context, int backlog)
{
	return -ENOTSUP;
}

static int ra_connect(struct net_context *context,
		       const struct sockaddr *addr,
		       socklen_t addrlen,
		       net_context_connect_cb_t cb,
		       int32_t timeout,
		       void *user_data)
{
	LOG_INF("ra_connect");

	return 0;
}

static int ra_accept(struct net_context *context,
			     net_tcp_accept_cb_t cb, int32_t timeout,
			     void *user_data)
{
	return -ENOTSUP;
}

static int ra_sendto(struct net_pkt *pkt,
		      const struct sockaddr *dst_addr,
		      socklen_t addrlen,
		      net_context_send_cb_t cb,
		      int32_t timeout,
		      void *user_data)
{
	LOG_INF("ra_sendto");

	return 0;
}

static int ra_send(struct net_pkt *pkt,
		    net_context_send_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return ra_sendto(pkt, NULL, 0, cb, timeout, user_data);
}

static int ra_recv(struct net_context *context,
		    net_context_recv_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return 0;
}

static int ra_put(struct net_context *context)
{
	return 0;
}

static struct net_offload ra_offload = {
	.get	       = ra_get,
	.bind	       = ra_bind,
	.listen	       = ra_listen,
	.connect       = ra_connect,
	.accept	       = ra_accept,
	.send	       = ra_send,
	.sendto	       = ra_sendto,
	.recv	       = ra_recv,
	.put	       = ra_put,
};

int ra_at_socket_offload_init(struct net_if *iface)
{
	iface->if_dev->offload = &ra_offload;

	return 0;
}
