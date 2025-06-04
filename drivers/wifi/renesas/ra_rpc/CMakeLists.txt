# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_RA_RPC)
  zephyr_library_sources(
    ra_rpc.c
	ra_rpc_socket_offload.c
	service/c_wifi_client.cpp
	service/wifi_client.cpp
	service/wifi_interface.cpp
  )
  zephyr_include_directories(
	.
	service
	${ZEPHYR_BASE}/subsys/net/lib/sockets
  )
endif()
