Usage Examples
==============

This page shows how to use ``IRadioDataQueue`` and ``RadioLteChannels`` for
cross-thread communication in a desktop
(POSIX/Win32) or Zephyr application.

.. contents:: Contents
   :local:
   :depth: 2

---------------------------------------------------------------------------

Architecture Overview
---------------------

The library separates modem I/O (network thread) from the application
logic (application thread) using three platform-abstracted message channels
and an event-flag object bundled in ``RadioLteChannels``:

.. code-block:: text

   ┌─────────────────────────┐     modem_tx_q      ┌───────────────────────────┐
    │    Application Thread   │ ─────────────────▶  │     Network Thread        │
    │                         │     modem_rx_q       │  (runs the modem state    │
    │                         │                      │   machine and queues)     │
   │  RadioLteChannels::     │ ◀─────────────────  │                           │
   │    send_request()       │     modem_evt        │  process_radio_requests() │
   │    recv_typed_response()│ ◀── event flags ───  │  publish_typed_response() │
   └─────────────────────────┘                      └───────────────────────────┘

The application thread uses ``send_request()`` / ``recv_typed_response()`` /
``wait()`` for control-plane operations. Payload TX/RX queue access is a
separate data-plane concern exposed by ``IRadioDataQueue``.

The network thread calls ``process_radio_requests()`` once per loop iteration
to drain queued requests and dispatch them to the modem state-machine owner.

---------------------------------------------------------------------------

Example 1 — Reading Registration Info
--------------------------------------

This example shows how an application thread queries the last known
registration information without directly touching the modem.

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   // channels is shared between the application thread and the network thread.
   void app_thread(modem::RadioLteChannels& channels) {
       modem::ModemTxMsg req{};
       req.type = modem::RadioLteRequestType::get_registration_info;

       // Send request to the network thread.
       auto err = channels.send_request(req);
       if (err != modem::MessageChannelError::ok) {
           // handle error
           return;
       }

       // Wait for the network thread to post MODEM_EVT_RESPONSE (max 2 s).
       uint32_t events = channels.wait(modem::MODEM_EVT_RESPONSE, /*reset=*/true, 2000);
       if ((events & modem::MODEM_EVT_RESPONSE) == 0) {
           // timeout
           return;
       }

       // Retrieve the typed response.
       modem::ModemTypedResponseMsg<modem::RegistrationInfo> resp{};
       err = channels.recv_typed_response(resp);
       if (err != modem::MessageChannelError::ok || !resp.ok) {
           // handle error
           return;
       }

       const modem::RegistrationInfo& info = resp.value;
       // Use info.operator_name, info.stat, info.act, info.ip_address, etc.
   }

---------------------------------------------------------------------------

Example 2 — Reading Signal Quality
------------------------------------

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   void read_signal_quality(modem::RadioLteChannels& channels) {
       modem::ModemTxMsg req{};
       req.type = modem::RadioLteRequestType::get_signal_quality;

       if (channels.send_request(req) != modem::MessageChannelError::ok) return;
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 2000);

       modem::ModemTypedResponseMsg<modem::SignalQuality> resp{};
       if (channels.recv_typed_response(resp) != modem::MessageChannelError::ok || !resp.ok) return;

       const modem::SignalQuality& sq = resp.value;
       int rsrp_dbm = sq.rsrp_dbm();  // e.g. -95 dBm
       int rsrq_tenth = sq.rsrq_tenth_db();  // e.g. -150 = -15.0 dB
   }

---------------------------------------------------------------------------

Example 3 — Querying SIM ICCID and IMSI
-----------------------------------------

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   void read_sim_ids(modem::RadioLteChannels& channels) {
       // ICCID
       {
           modem::ModemTxMsg req{modem::RadioLteRequestType::get_iccid};
           channels.send_request(req);
           channels.wait(modem::MODEM_EVT_RESPONSE, true, 2000);
           modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
           channels.recv_typed_response(resp);
           // resp.value.c_str() is the ICCID string
       }

       // IMSI
       {
           modem::ModemTxMsg req{modem::RadioLteRequestType::get_imsi};
           channels.send_request(req);
           channels.wait(modem::MODEM_EVT_RESPONSE, true, 2000);
           modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
           channels.recv_typed_response(resp);
           // resp.value.c_str() is the IMSI string
       }
   }

---------------------------------------------------------------------------

Example 4 — Updating the Active Configuration
-----------------------------------------------

The ``set_config`` request carries the full ``NetworkLteConfig`` payload in a
``ModemSetConfigMsg``.  The new configuration takes effect on the next network
state-machine cycle.

.. code-block:: cpp

   #include "modem/i_radio_lte.h"
   #include "modem/network_lte_config.h"

   void update_config(modem::RadioLteChannels& channels) {
       modem::NetworkLteConfig cfg{};
       cfg.default_apn = "iot.example.com";
       cfg.default_iot_tech = modem::RadioTech::cat_m1;
       cfg.fPsmEnable = true;
       cfg.psm_t3412 = 3600;  // 1 hour sleep
       cfg.psm_t3324 = 30;    // 30 s active window

       modem::ModemSetConfigMsg msg{};
       msg.config = cfg;

       channels.send_request(msg);
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 2000);

       modem::ModemTypedResponseMsg<bool> resp{};
       channels.recv_typed_response(resp);
       // resp.ok == true when the network thread accepted the config
   }

---------------------------------------------------------------------------

Example 5 — Network Connect / Disconnect
-----------------------------------------

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   bool network_connect(modem::RadioLteChannels& channels) {
       modem::ModemTxMsg req{modem::RadioLteRequestType::network_connect};
       channels.send_request(req);
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 30000);  // 30 s timeout

       modem::ModemTypedResponseMsg<bool> resp{};
       if (channels.recv_typed_response(resp) != modem::MessageChannelError::ok) return false;
       return resp.ok && resp.value;
   }

   void network_disconnect(modem::RadioLteChannels& channels) {
       modem::ModemTxMsg req{modem::RadioLteRequestType::network_disconnect};
       channels.send_request(req);
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 5000);
   }

---------------------------------------------------------------------------

Example 6 — Scanning Networks
-------------------------------

``scan_networks()`` triggers ``AT#CSURVF=2`` + ``AT#CSURV`` on the modem and
stores the result internally.  Retrieve it with ``get_csurv_result``.

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   void scan_and_print(modem::RadioLteChannels& channels) {
       // Start scan (may take tens of seconds).
       modem::ModemTxMsg req{modem::RadioLteRequestType::scan_networks};
       channels.send_request(req);
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 60000);  // up to 60 s

       modem::ModemTypedResponseMsg<bool> scan_resp{};
       channels.recv_typed_response(scan_resp);

       if (!scan_resp.ok || !scan_resp.value) return;  // scan failed

       // Retrieve the result.
       modem::ModemTxMsg fetch{modem::RadioLteRequestType::get_csurv_result};
       channels.send_request(fetch);
       channels.wait(modem::MODEM_EVT_RESPONSE, true, 2000);

       modem::ModemTypedResponseMsg<modem::CsurvResult> resp{};
       channels.recv_typed_response(resp);

       for (size_t i = 0; i < resp.value.cells.size(); ++i) {
           const auto& cell = resp.value.cells[i];
           // cell.earfcn, cell.rx_lev, cell.mcc, cell.mnc, cell.tac, etc.
       }
   }

---------------------------------------------------------------------------

Example 7 — Monitoring State Changes and Logs
----------------------------------------------

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   void monitor_loop(modem::RadioLteChannels& channels) {
       while (true) {
           // Wait for any event (state, log, response) — 5 s timeout.
           uint32_t events = channels.wait(
               modem::MODEM_EVT_STATE | modem::MODEM_EVT_LOG,
               /*reset=*/false,
               5000);

           if (events & modem::MODEM_EVT_STATE) {
               channels.clear(modem::MODEM_EVT_STATE);
               const modem::ModemStateMsg& s = channels.current_state();
               // Inspect s.state and s.event.
           }

           if (events & modem::MODEM_EVT_LOG) {
               channels.clear(modem::MODEM_EVT_LOG);
               modem::ModemLogMsg log{};
               while (channels.recv_log(log) == modem::MessageChannelError::ok) {
                   // Process log.text.c_str()
               }
           }
       }
   }

---------------------------------------------------------------------------

Example 8 — Thread-Safe Payload Queue Access
---------------------------------------------

Use ``IRadioDataQueue`` when you need to push TX payloads or drain RX payloads
without routing the operation through ``process_radio_requests()``.

.. code-block:: cpp

   #include "modem/i_radio_lte.h"

   void queue_payload(modem::IRadioDataQueue& data_queue,
                      uint8_t conn_id,
                      const uint8_t* data,
                      size_t size) {
       modem::QueueError err = data_queue.tx_write(conn_id, data, size);
       if (err != modem::QueueError::ok) {
           // handle queue full / invalid connection id / unsupported state
       }
   }

   void drain_rx(modem::IRadioDataQueue& data_queue, uint8_t conn_id) {
       modem::QueueMessage msg{};
       while (data_queue.rx_read(conn_id, msg) == modem::QueueError::ok) {
           // process msg.data
       }
   }

---------------------------------------------------------------------------

Example 9 — Network Thread Integration
--------------------------------------

The following skeleton shows how a network thread integrates
``process_radio_requests()`` to service application requests every cycle.

.. code-block:: cpp

   #include "modem/i_radio_lte.h"
    #include "modem/network_lte.h"

   void network_thread_main(modem::RadioLteChannels& channels,
                             modem::NetworkLte& network) {
       while (true) {
           // Wait for work (request, tick, etc.) — 100 ms timeout.
           uint32_t events = channels.wait(
               modem::MODEM_EVT_REQUEST,
               /*reset=*/true,
               100);

           if (events & modem::MODEM_EVT_REQUEST) {
               // Drain all pending requests from the application thread.
               modem::process_radio_requests(channels, network);
           }

           // Run one step of the network state machine.
           network.loop();
       }
   }
