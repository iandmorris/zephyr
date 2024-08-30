# Copyright (c) 2024 Ian Morris
# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=R7FA6M5BH")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
