# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_RA_AT)
  zephyr_library_include_directories(
    ${ZEPHYR_BASE}/drivers/modem
  )
  zephyr_library_sources(
	ra_at.c
	ra_at_socket_offload.c
  )
endif()
