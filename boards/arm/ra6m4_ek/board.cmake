# Copyright (c) 2023 Ian Morris
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=r7fa6m4af")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
