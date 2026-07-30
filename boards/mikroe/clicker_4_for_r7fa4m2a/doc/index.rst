.. zephyr:board:: mikroe_clicker_4_for_r7fa4m2a

Overview
********

The Mikroe Clicker 4 for R7FA4M2A development board contains a Renesas
Cortex-M33 based RA4M2 Microcontroller operating at up to 100 MHz with
512 KB of Flash memory and 128 KB of SRAM.

Hardware
********

The Clicker 4 for R7FA4M2A board contains two USB Type-C connectors, six LEDs and
push buttons, and a reset button. It has a CMSIS-DAP compliant debugger and four
mikroBUS sockets for interfacing with external electronics. For more information
about the development board see the `Clicker 4 for R7FA4M2A website`_.

Supported Features
==================

.. zephyr:board-supported-hw::

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Flashing
========

You can build and flash an application in the usual way (See
:ref:`build_an_application` and
:ref:`application_run` for more details).

Here is an example for building and flashing the :zephyr:code-sample:`blinky` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: mikroe_clicker_4_for_r7fa4m2a
   :goals: build flash

Debugging
=========

Debugging also can be done in the usual way.
The following command is debugging the :zephyr:code-sample:`blinky` application.
Also, see the instructions specific to the debug server that you use.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: mikroe_clicker_4_for_r7fa4m2a
   :maybe-skip-config:
   :goals: debug

References
**********

.. target-notes::

.. _Clicker 4 for R7FA4M2A website:
   https://www.mikroe.com/clicker-4-for-r7fa4m2ad3cfp
