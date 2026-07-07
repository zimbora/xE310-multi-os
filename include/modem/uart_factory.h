#pragma once

#include "modem/uart_interface.h"
#include <memory>

namespace modem {

struct UartHandleDeleter {
    bool owns = true;
    void operator()(UartInterface* ptr) const {
        if (owns) {
            std::default_delete<UartInterface>{}(ptr);
        }
    }
};
using UartHandle = std::unique_ptr<UartInterface, UartHandleDeleter>;

/// Creates the platform-appropriate UART implementation.
UartHandle create_platform_uart();

} // namespace modem
