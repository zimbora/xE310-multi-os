#include <gtest/gtest.h>

#include "modem/hal/event_flags_factory.h"

#include <thread>

using namespace modem;

class EventFlagsTest : public ::testing::Test {
protected:
    void SetUp() override { events = create_platform_event_flags(); }

    EventFlagsHandle events;
};

TEST_F(EventFlagsTest, SetAndGetFlags) {
    events->set(0x01U);
    events->set(0x04U);
    EXPECT_EQ(events->get(), 0x05U);
}

TEST_F(EventFlagsTest, WaitReturnsMatchedFlagsWithoutClearingWhenRequested) {
    events->set(0x02U);
    EXPECT_EQ(events->wait(0x02U, false, 0), 0x02U);
    EXPECT_EQ(events->get(), 0x02U);
}

TEST_F(EventFlagsTest, WaitClearsMatchedFlagsWhenRequested) {
    events->set(0x08U);
    EXPECT_EQ(events->wait(0x08U, true, 0), 0x08U);
    EXPECT_EQ(events->get(), 0U);
}

TEST_F(EventFlagsTest, ClearRemovesRequestedFlags) {
    events->set(0x03U);
    events->clear(0x01U);
    EXPECT_EQ(events->get(), 0x02U);
}

TEST_F(EventFlagsTest, WaitWithTimeoutWaitsForSetter) {
    std::thread setter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        events->set(0x10U);
    });

    uint32_t matched = events->wait(0x10U, true, 500);
    setter.join();

    EXPECT_EQ(matched, 0x10U);
    EXPECT_EQ(events->get(), 0U);
}

TEST_F(EventFlagsTest, WaitTimeoutExpiresWhenNothingArrives) {
    EXPECT_EQ(events->wait(0x20U, true, 50), 0U);
}
