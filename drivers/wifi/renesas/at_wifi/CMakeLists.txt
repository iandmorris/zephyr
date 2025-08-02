# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_AT_WIFI)

  zephyr_include_directories(
    ${ZEPHYR_BASE}/drivers/modem
	#${ZEPHYR_BASE}/subsys/net/lib/sockets
  )

  zephyr_library_sources(
    at_wifi.c
    at_wifi_offload.c
  )

  zephyr_library_sources_ifdef(CONFIG_WIFI_AT_WIFI_TRANSPORT_UART at_wifi_transport_uart.c)
  zephyr_library_sources_ifdef(CONFIG_WIFI_AT_WIFI_TRANSPORT_SPI at_wifi_transport_spi.c)

  #zephyr_include_directories(
#	.
#	${ZEPHYR_BASE}/subsys/net/lib/sockets
#  )
endif()
