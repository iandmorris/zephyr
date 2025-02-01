/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_rrq_at_offload, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/net/net_offload.h>

static int rrq_get(sa_family_t family,
		   enum net_sock_type type,
		   enum net_ip_protocol ip_proto,
		   struct net_context **context)
{
	return 0;
}

static int rrq_bind(struct net_context *context,
			const struct sockaddr *addr,
		    socklen_t addrlen)
{
	return 0;
}

static int rrq_listen(struct net_context *context, int backlog)
{
	return -ENOTSUP;
}

static int rrq_connect(struct net_context *context,
		       const struct sockaddr *addr,
		       socklen_t addrlen,
		       net_context_connect_cb_t cb,
		       int32_t timeout,
		       void *user_data)
{
	return 0;
}

static int rrq_accept(struct net_context *context,
			     net_tcp_accept_cb_t cb, int32_t timeout,
			     void *user_data)
{
	return -ENOTSUP;
}

static int rrq_sendto(struct net_pkt *pkt,
		      const struct sockaddr *dst_addr,
		      socklen_t addrlen,
		      net_context_send_cb_t cb,
		      int32_t timeout,
		      void *user_data)
{
	return 0;
}

static int rrq_send(struct net_pkt *pkt,
		    net_context_send_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return rrq_sendto(pkt, NULL, 0, cb, timeout, user_data);
}

static int rrq_recv(struct net_context *context,
		    net_context_recv_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	return 0;
}

static int rrq_put(struct net_context *context)
{
	return 0;
}

static struct net_offload rrq_offload = {
	.get	       = rrq_get,
	.bind	       = rrq_bind,
	.listen	       = rrq_listen,
	.connect       = rrq_connect,
	.accept	       = rrq_accept,
	.send	       = rrq_send,
	.sendto	       = rrq_sendto,
	.recv	       = rrq_recv,
	.put	       = rrq_put,
};

int rrq_offload_init(struct net_if *iface)
{
	iface->if_dev->offload = &rrq_offload;

	return 0;
}