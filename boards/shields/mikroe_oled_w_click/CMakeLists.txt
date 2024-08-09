# Copyright (c) 2025 Ian Morris
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_I2C board.c)
