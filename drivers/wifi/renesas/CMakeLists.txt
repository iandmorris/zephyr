# Copyright (c) 2025 Renesas Electronics Corporation
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_WIFI_RA_AT        ra_at)
add_subdirectory_ifdef(CONFIG_WIFI_RA_ERPC      ra_erpc)
