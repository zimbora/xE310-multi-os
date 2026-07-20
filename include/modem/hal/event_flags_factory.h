#pragma once

#include "modem/hal/event_flags_interface.h"

#include <memory>

namespace modem {

struct EventFlagsHandleDeleter {
    bool owns = true;
    void operator()(EventFlagsInterface* ptr) const {
        if (owns) {
            std::default_delete<EventFlagsInterface>{}(ptr);
        }
    }
};

using EventFlagsHandle = std::unique_ptr<EventFlagsInterface, EventFlagsHandleDeleter>;

/// Creates the platform-appropriate event flags implementation.
EventFlagsHandle create_platform_event_flags();

} // namespace modem