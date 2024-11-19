# Copyright (c) 2025 Ian Morris
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=r7fa6m5ah")
board_runner_args(jlink "--device=R7FA6M5AH" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
