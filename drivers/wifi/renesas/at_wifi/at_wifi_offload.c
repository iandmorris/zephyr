/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(at_wifi_offload, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>

#include "at_wifi.h"

extern struct at_wifi_data at_wifi_driver_data;

static int at_wifi_offload_get(sa_family_t family,
		   enum net_sock_type type,
		   enum net_ip_protocol ip_proto,
		   struct net_context **context)
{
	struct at_wifi_socket *sock = NULL;
	struct at_wifi_data *dev;

	LOG_DBG("");

	if (family != AF_INET) {
		return -EAFNOSUPPORT;
	}

	/* TODO:
	 * iface has not yet been assigned to context so there is currently
	 * no way to know which interface to operate on. Therefore this driver
	 * only supports one device node.
	 */
	dev = &at_wifi_driver_data;

	for (int i = 0; i < WIFI_AT_WIFI_SOCKETS_MAX; i++) {
		if (!dev->sockets[i].in_use) {
			sock = &dev->sockets[i];

			sock->context = *context;
			(*context)->offload_context = sock;
			dev->sockets[i].in_use = true;

			LOG_DBG("alloc socket: %d", i);

			break;
		}
	}

	if (!sock) {
		LOG_ERR("No socket available!");
		return -ENOMEM;
	}

	return 0;
}

static int at_wifi_offload_bind(struct net_context *context, const struct sockaddr *addr,
		    socklen_t addrlen)
{
	LOG_DBG("sa_family: %d", addr->sa_family);
	LOG_DBG("%d", ((struct in_addr *)addr->data)->s4_addr[0]);
	LOG_DBG("addrlen: %d", addrlen);

	return 0;
}

static int at_wifi_offload_listen(struct net_context *context, int backlog)
{
	LOG_DBG("");
	return -ENOTSUP;
}

MODEM_CMD_DIRECT_DEFINE(on_cmd_tcp_server_connect)
{
	return 0;
}

static int at_wifi_offload_connect(struct net_context *context,
		       const struct sockaddr *addr,
		       socklen_t addrlen,
		       net_context_connect_cb_t cb,
		       int32_t timeout,
		       void *user_data)
{
	int ret;
	struct at_wifi_socket *sock = NULL;
	struct at_wifi_data *dev;
	char cmd_buf[sizeof("AT+TRTC=255.255.255.255,65535")];
	char addr_s[NET_IPV4_ADDR_LEN];

	static const struct modem_cmd cmd =
		MODEM_CMD_DIRECT("+TRTC:", on_cmd_tcp_server_connect);

	if (!context || !addr) {
		return -EINVAL;
	}

	// TODO - must be a better way to do this?
	dev = &at_wifi_driver_data;

	net_addr_ntop(AF_INET, &net_sin(addr)->sin_addr, addr_s, sizeof(addr_s));

	LOG_DBG("proto: %d", context->proto);
	LOG_DBG("addr: %s", addr_s);
	LOG_DBG("port: %d", ((struct sockaddr_in *)addr)->sin_port);
	LOG_DBG("timeout: %d", timeout);

	sock = (struct at_wifi_socket *)context->offload_context;
	if (!sock) {
		LOG_ERR("Can't locate socket for net_ctx: %p", context);
		return -EINVAL;
	}

	if (context->proto == IPPROTO_TCP) {

		snprintk(cmd_buf, sizeof(cmd_buf), "AT+WFJAP=%s,%d", addr_s, ((struct sockaddr_in *)addr)->sin_port);

		ret = modem_cmd_send(&dev->modem.ctx.iface, &dev->modem.ctx.cmd_handler,
						     &cmd, 1, cmd_buf, &dev->sem_response,
						     K_SECONDS(10)); // TODO - use macro

	} else if (context->proto == IPPROTO_UDP) {

	} else {
		return -ENOTSUP;
	}

	return 0;
}

static int at_wifi_offload_accept(struct net_context *context,
			     net_tcp_accept_cb_t cb, int32_t timeout,
			     void *user_data)
{
	LOG_DBG("");
	return -ENOTSUP;
}

static int at_wifi_offload_send(struct net_pkt *pkt,
		    net_context_send_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	LOG_DBG("");
	return 0;
}

static int at_wifi_offload_sendto(struct net_pkt *pkt,
		      const struct sockaddr *dst_addr,
		      socklen_t addrlen,
		      net_context_send_cb_t cb,
		      int32_t timeout,
		      void *user_data)
{
	LOG_DBG("");
	return 0;
}

static int at_wifi_offload_recv(struct net_context *context,
		    net_context_recv_cb_t cb,
		    int32_t timeout,
		    void *user_data)
{
	LOG_DBG("proto: %d", context->proto);
	return 0;
}

static int at_wifi_offload_put(struct net_context *context)
{
	LOG_DBG("");
	return 0;
}

static struct net_offload at_wifi_offload = {
	.get	       = at_wifi_offload_get,
	.bind	       = at_wifi_offload_bind,
	.listen	       = at_wifi_offload_listen,
	.connect       = at_wifi_offload_connect,
	.accept	       = at_wifi_offload_accept,
	.send	       = at_wifi_offload_send,
	.sendto	       = at_wifi_offload_sendto,
	.recv	       = at_wifi_offload_recv,
	.put	       = at_wifi_offload_put,
};

int at_wifi_offload_init(struct net_if *iface)
{
	LOG_DBG("");

	iface->if_dev->offload = &at_wifi_offload;

	return 0;
}
