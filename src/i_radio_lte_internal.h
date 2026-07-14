#pragma once

#include "modem/i_radio_lte.h"

namespace modem {

class NetworkLte;

/// Process pending radio LTE requests from channels and dispatch them on the network thread.
void process_radio_requests(RadioLteChannels& channels, NetworkLte& network);

} // namespace modem