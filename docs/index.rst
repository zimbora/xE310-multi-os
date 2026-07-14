xE310 Modem Library
===================

A cross-platform C++ library for controlling the Telit xE310 (ME310M1) LTE modem
over a UART interface using AT commands. Designed to run on both desktop
(Windows, macOS, Linux) and embedded targets (Zephyr OS / nRF54).

The library provides:

* A **hardware abstraction layer (HAL)** that isolates platform-specific serial/UART code.
* Core AT command parsing and state machine logic that is fully **platform-independent**.
* A thread-safe **cross-thread messaging interface** (``IRadioLte`` / ``RadioLteChannels``)
  for querying the last known modem state without touching the modem directly.

.. toctree::
   :maxdepth: 2
   :caption: Contents

   api_reference
   examples

Indices and tables
------------------

* :ref:`genindex`
* :ref:`search`
