API Reference — ``i_radio_lte.h``
=================================

This page documents the public API defined in ``include/modem/i_radio_lte.h``.
It covers the thread-safe messaging interface (``RadioLteChannels``),
the data-plane queue interface (``IRadioDataQueue``), all message/event
types, and every data structure they reference.

.. contents:: Contents
   :local:
   :depth: 2

---------------------------------------------------------------------------

Event Flag Constants
--------------------

These bit flags are posted on the ``modem_evt`` event-flag object inside
``RadioLteChannels`` to synchronise requests, responses, and notifications
across threads.

.. doxygenvariable:: modem::MODEM_EVT_REQUEST
   :project: xE310ModemLibrary

.. doxygenvariable:: modem::MODEM_EVT_RESPONSE
   :project: xE310ModemLibrary

.. doxygenvariable:: modem::MODEM_EVT_STATE
   :project: xE310ModemLibrary

.. doxygenvariable:: modem::MODEM_EVT_LOG
   :project: xE310ModemLibrary

---------------------------------------------------------------------------

Request Type Enumeration
-------------------------

.. doxygenenum:: modem::RadioLteRequestType
   :project: xE310ModemLibrary

---------------------------------------------------------------------------

Message Structures
------------------

ModemTxMsg
~~~~~~~~~~

.. doxygenstruct:: modem::ModemTxMsg
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

.. note::
   Use ``ModemTxMsg`` (aliased as ``RadioLteRequestMsg``) to enqueue a typed
   request on the transmit queue of ``RadioLteChannels``.

ModemSetConfigMsg
~~~~~~~~~~~~~~~~~

.. doxygenstruct:: modem::ModemSetConfigMsg
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

ModemTypedResponseMsg
~~~~~~~~~~~~~~~~~~~~~

A generic response wrapper.  The boolean ``ok`` field indicates whether the
network thread successfully fulfilled the request, and ``value`` carries the
result payload.

.. doxygenstruct:: modem::ModemTypedResponseMsg
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

ModemStateMsg
~~~~~~~~~~~~~

Carries the current LTE state-machine snapshot.  Published on every state
transition via ``RadioLteChannels::publish_state()``.

.. doxygenstruct:: modem::ModemStateMsg
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

ModemLogMsg
~~~~~~~~~~~

A fixed-size log line forwarded from the network thread to observers via
``RadioLteChannels::publish_log()``.

.. doxygenstruct:: modem::ModemLogMsg
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

StateTimers
~~~~~~~~~~~

Accumulated time counters (in milliseconds) for each timed state of the LTE
state machine. Every time the state machine exits a timed state the elapsed
time is added to the corresponding counter. Tracked states: ``network_attaching``,
``pdp_context_opening``, ``data_ready``, ``transparent_mode``, ``sleep_mode``,
and ``off_mode``. Request this data with ``RadioLteRequestType::get_timers``.

.. doxygenstruct:: modem::StateTimers
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

---------------------------------------------------------------------------

RadioLteChannels
----------------

``RadioLteChannels`` owns the three lock-free message channels and the event
flags object used for cross-thread communication.  One instance is shared
between the *application thread* (which calls ``send_request`` /
``recv_typed_response``) and the *network thread* (which calls
``recv_request_frame`` / ``publish_typed_response``).

.. doxygenclass:: modem::RadioLteChannels
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

---------------------------------------------------------------------------

IRadioDataQueue
---------------

``IRadioDataQueue`` exposes the thread-safe payload queues used outside the
radio request dispatcher. Implementations provide ``tx_write()`` for outbound
payload buffering and ``rx_read()`` for draining received payloads.

.. doxygenclass:: modem::IRadioDataQueue
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

---------------------------------------------------------------------------

Referenced Data Structures
---------------------------

The following structures and enumerations are defined in the supporting
headers included by ``i_radio_lte.h``.

NetworkLteConfig
~~~~~~~~~~~~~~~~

Holds the full LTE network state-machine configuration. Embed it in a
``ModemSetConfigMsg`` to request a configuration update through the channel.

.. doxygenstruct:: modem::NetworkLteConfig
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

RegistrationInfo
~~~~~~~~~~~~~~~~

Populated on every ``AT+CEREG?`` response or URC.

.. doxygenstruct:: modem::RegistrationInfo
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

SignalQuality
~~~~~~~~~~~~~

Populated by ``AT+CESQ``.

.. doxygenstruct:: modem::SignalQuality
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

NetworkInfo
~~~~~~~~~~~

PDP context state and IP address.

.. doxygenstruct:: modem::NetworkInfo
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

ModemInfo
~~~~~~~~~

Full modem identification, read at power-on.

.. doxygenstruct:: modem::ModemInfo
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

SoftwarePackageVersion
~~~~~~~~~~~~~~~~~~~~~~

.. doxygenstruct:: modem::SoftwarePackageVersion
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

CpsmsConfig
~~~~~~~~~~~

3GPP standard PSM configuration (``AT+CPSMS``).

.. doxygenstruct:: modem::CpsmsConfig
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

TelitCpsmsConfig
~~~~~~~~~~~~~~~~

Telit-specific PSM configuration (``AT#CPSMS``).

.. doxygenstruct:: modem::TelitCpsmsConfig
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

TelitCpsmsStatus
~~~~~~~~~~~~~~~~

Network-reported PSM status (``AT#CPSMS?``).

.. doxygenstruct:: modem::TelitCpsmsStatus
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

NetworkSurveyResult
~~~~~~~~~~~~~~~~~~~

Result of ``AT#CSURVC``.

.. doxygenstruct:: modem::NetworkSurveyResult
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

SurvCell
~~~~~~~~

A single cell entry from a numeric network survey.

.. doxygenstruct:: modem::SurvCell
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

CsurvResult
~~~~~~~~~~~

Result of ``AT#CSURV`` (with ``AT#CSURVF=2`` pre-set).

.. doxygenstruct:: modem::CsurvResult
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

CsurvCell
~~~~~~~~~

A single labeled/hex cell entry from ``AT#CSURV``.

.. doxygenstruct:: modem::CsurvCell
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

Operator
~~~~~~~~

An operator entry returned by ``AT+COPS=?``.

.. doxygenstruct:: modem::Operator
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

---------------------------------------------------------------------------

Supporting Enumerations
-----------------------

.. doxygenenum:: modem::RegStatus
   :project: xE310ModemLibrary

.. doxygenenum:: modem::RadioTech
   :project: xE310ModemLibrary

.. doxygenenum:: modem::SimStatus
   :project: xE310ModemLibrary

.. doxygenenum:: modem::PsmMode
   :project: xE310ModemLibrary

.. doxygenenum:: modem::PsmVersion
   :project: xE310ModemLibrary

.. doxygenenum:: modem::ContextState
   :project: xE310ModemLibrary

.. doxygenenum:: modem::SurvCellType
   :project: xE310ModemLibrary

---------------------------------------------------------------------------

Supporting Templates
--------------------

FixedString
~~~~~~~~~~~

Fixed-capacity, heap-free string used throughout the library.

.. doxygenclass:: modem::FixedString
   :project: xE310ModemLibrary
   :members:
   :undoc-members:

StaticVector
~~~~~~~~~~~~

Fixed-capacity, heap-free vector used for operator and cell lists.

.. doxygenclass:: modem::StaticVector
   :project: xE310ModemLibrary
   :members:
   :undoc-members:
