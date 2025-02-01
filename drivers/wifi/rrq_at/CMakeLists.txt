# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_RRQ_AT)

  zephyr_library_include_directories(
    ${ZEPHYR_BASE}/drivers/modem
  )

  zephyr_library_sources(
    rrq.c
    rrq_socket.c
    rrq_offload.c
  )
endif()
