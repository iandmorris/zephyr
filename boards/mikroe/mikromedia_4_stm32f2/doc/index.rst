.. _mikromedia_4_stm32f2_cap:

Mikroe Mikromedia 4 for STM32F2 Capacitive FPI
##############################################

Overview
********

The Mikroe Mikromedia 4 for STM32F2 is a front panel interface (FPI) with a
bezel, driven by a `STM32F207`_ microcontroller.

.. figure:: img/mikromedia-4-stm32f2-cap.jpg
   :align: center
   :alt: Mikromedia 4 for STM32F2 Capacitive FPI

   Mikromedia 4 for STM32F2 Capacitive FPI (Credit: MikroElektronika d.o.o.)

Hardware
********

The Mikromedia 4 for STM32F2 Capacitive FPI board contains a 4.3" TFT display
with capacitive touch screen, a DSP-powered embedded sound CODEC, a microSD
card slot, a USB interface and two mikroBUS Shuttle connectors.

For more information about the development board see the
`Mikromedia 4 for STM32F2 Capacitive FPI website`_.

Supported Features
==================

The Zephyr Mikromedia 4 for STM32F2 Capacitive FPI configuration supports the
following hardware features:

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| NVIC      | on-chip    | nested vector interrupt controller  |
+-----------+------------+-------------------------------------+
| UART      | on-chip    | serial port-polling;                |
|           |            | serial port-interrupt               |
+-----------+------------+-------------------------------------+
| PINMUX    | on-chip    | pinmux                              |
+-----------+------------+-------------------------------------+
| I2C       | on-chip    | i2c                                 |
+-----------+------------+-------------------------------------+
| SPI       | on-chip    | spi                                 |
+-----------+------------+-------------------------------------+
| ADC       | on-chip    | adc                                 |
+-----------+------------+-------------------------------------+
| USB       | on-chip    | usb                                 |
+-----------+------------+-------------------------------------+

Other hardware features have not been enabled yet for this board.

The default configuration can be found in
:zephyr_file:`boards/mikroe/mikromedia_4_stm32f2_cap/mikroe_clicker_2_defconfig`

Connections and IOs
===================

The two mikroBUS interfaces are aliased in the device tree so that their
peripherals can be accessed using ``mikrobus_N_INTERFACE`` so e.g. the spi on
bus 2 can be found by the alias ``mikrobus_2_spi``. The counting corresponds
with the marking on the board.

For connections on the edge connectors, please refer to `Clicker 2 for STM32 User Manual`_.

Programming and Debugging
*************************
Applications for the ``mikroe_clicker_2`` board configuration can
be built and flashed in the usual way (see :ref:`build_an_application` and
:ref:`application_run` for more details).


Flashing
========
The initial state of the board is set to lock.
When you flash, it will fail with the message:

.. code-block:: console

   Error: stm32x device protected

Unlocking with openocd makes it possible to flash.

.. code-block:: console

   $ openocd -f /usr/share/openocd/scripts/interface/stlink-v2.cfg \
       -f /usr/share/openocd/scripts/target/stm32f4x.cfg -c init\
       -c "reset halt" -c "stm32f4x unlock 0" -c "reset run" -c shutdown

Here is an example for the :ref:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: mikroe_clicker_2
   :goals: build flash

You should see the following message on the console:

.. code-block:: console

   Hello World! mikroe_clicker_2


Debugging
=========

You can debug an application in the usual way.  Here is an example for the
:ref:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: mikroe_clicker_2
   :maybe-skip-config:
   :goals: debug

References
**********
.. _Mikromedia 4 for STM32F2 Capacitive FPI website:
	https://www.mikroe.com/mikromedia-4-for-stm32f2-capacitive-fpi-with-bezel

.. _Clicker 2 website:
    https://www.mikroe.com/clicker-2-stm32f4
.. _Clicker 2 for STM32 User Manual:
    https://download.mikroe.com/documents/starter-boards/clicker-2/stm32f4/clicker2-stm32-manual-v100.pdf
.. _STM32F407VG Website:
    https://www.st.com/content/st_com/en/products/microcontrollers-microprocessors/stm32-32-bit-arm-cortex-mcus/stm32-high-performance-mcus/stm32f4-series/stm32f407-417/stm32f407vg.html
.. _STM32F407:
    https://www.st.com/resource/en/datasheet/stm32f407vg.pdf