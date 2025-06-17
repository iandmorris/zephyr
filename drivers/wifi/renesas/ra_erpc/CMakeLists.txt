# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_RA_ERPC)
  zephyr_library_sources(
    ra_erpc.c
	ra_erpc_socket_offload.c
	service/c_wifi_client.cpp
	service/wifi_client.cpp
	service/wifi_interface.cpp

    service/c_wifi_async_server.cpp
	service/wifi_async_server.cpp
	service/wifi_async_interface.cpp
  )
  zephyr_include_directories(
	.
	service
	${ZEPHYR_BASE}/subsys/net/lib/sockets
  )
endif()
