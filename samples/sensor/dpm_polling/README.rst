.. _dpm_polling:

Generic Digital Power Monitor polling sample
############################################

Overview
********

This sample application demonstrates how to use digital power monitor sensors.

Building and Running
********************

This sample supports up to 10 power monitoring sensors. Each sensor needs to
be aliased as ``dpmN`` where ``N`` goes from ``0`` to ``9``. For example:

.. code-block:: devicetree

  / {
	aliases {
			dpm0 = &ina219;
		};
	};

Make sure the aliases are in devicetree.

Then build and run with:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/dpm_polling
   :board: <board to use>
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

       hs300x@44: temp is 25.31 °C humidity is 30.39 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.44 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.37 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.39 %RH
       hs300x@44: temp is 25.31 °C humidity is 30.37 %RH
       hs300x@44: temp is 25.31 °C humidity is 30.35 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.37 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.37 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.39 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.44 %RH
       hs300x@44: temp is 25.51 °C humidity is 30.53 %RH
