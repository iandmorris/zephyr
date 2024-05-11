# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=STM32F207VG" "--speed=4000")
board_runner_args(openocd --cmd-pre-init "source [find interface/jlink.cfg]")
board_runner_args(openocd --cmd-pre-init "source [find target/stm32f2x.cfg]")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
