#pragma once

#include "modem/uart_interface.h"
#include <memory>

namespace modem {

/// Creates the platform-appropriate UART implementation.
std::unique_ptr<UartInterface> create_platform_uart();

} // namespace modem
