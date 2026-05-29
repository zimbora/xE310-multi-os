#include "modem/timer_interface.h"

#ifdef MODEM_PLATFORM_ZEPHYR

#include <zephyr/kernel.h>

namespace modem {

class ZephyrTimer : public TimerInterface {
public:
    ZephyrTimer() {
        k_timer_init(&timer_, ZephyrTimer::expiry_fn, nullptr);
        k_timer_user_data_set(&timer_, this);
    }

    ~ZephyrTimer() override {
        k_timer_stop(&timer_);
    }

    ZephyrTimer(const ZephyrTimer&) = delete;
    ZephyrTimer& operator=(const ZephyrTimer&) = delete;

    TimerError start(uint32_t timeout_ms, Callback cb) override {
        if (running_) {
            return TimerError::already_running;
        }
        cb_       = std::move(cb);
        running_  = true;
        start_ms_ = k_uptime_get();
        k_timer_start(&timer_, K_MSEC(timeout_ms), K_NO_WAIT);
        return TimerError::ok;
    }

    TimerError stop() override {
        if (!running_) {
            return TimerError::not_running;
        }
        k_timer_stop(&timer_);
        running_ = false;
        return TimerError::ok;
    }

    TimerError reset(uint32_t timeout_ms) override {
        // k_timer_start restarts even if already running
        if (!cb_) {
            return TimerError::not_running;
        }
        running_  = true;
        start_ms_ = k_uptime_get();
        k_timer_start(&timer_, K_MSEC(timeout_ms), K_NO_WAIT);
        return TimerError::ok;
    }

    bool is_running() const override {
        return running_;
    }

    uint32_t elapsed_ms() const override {
        if (start_ms_ < 0) {
            return 0;
        }
        int64_t diff = k_uptime_get() - start_ms_;
        return static_cast<uint32_t>(diff < 0 ? 0 : diff);
    }

private:
    static void expiry_fn(struct k_timer* t) {
        auto* self = static_cast<ZephyrTimer*>(k_timer_user_data_get(t));
        self->running_ = false;
        if (self->cb_) {
            self->cb_();
        }
    }

    struct k_timer timer_;
    Callback       cb_;
    bool           running_  = false;
    int64_t        start_ms_ = -1;  ///< k_uptime_get() at last start/reset, -1 if never started
};

} // namespace modem

#include "modem/timer_factory.h"

namespace modem {

std::unique_ptr<TimerInterface> create_platform_timer() {
    return std::make_unique<ZephyrTimer>();
}

void delay_ms(uint32_t ms) {
    k_msleep(static_cast<int32_t>(ms));
}

} // namespace modem

#endif // MODEM_PLATFORM_ZEPHYR
