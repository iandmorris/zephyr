# Copyright (c) 2026 Ian Morris
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=R7FA4M2AD")
board_runner_args(jlink "--device=R7FA4M2AD")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
