#include "modem/at_command.h"

#include <gtest/gtest.h>

using namespace modem;

TEST(AtCommandTest, ConstructsWithDefaults) {
    AtCommand cmd("AT");
    EXPECT_EQ(cmd.command_string(), "AT");
    EXPECT_EQ(cmd.timeout_ms(), 1000);
}

TEST(AtCommandTest, ConstructsWithCustomTimeout) {
    AtCommand cmd("AT+CSQ", 5000);
    EXPECT_EQ(cmd.command_string(), "AT+CSQ");
    EXPECT_EQ(cmd.timeout_ms(), 5000);
}

TEST(AtCommandTest, ParseOkResponse) {
    auto resp = AtCommand::parse_response("\r\nOK\r\n");
    EXPECT_EQ(resp.status, AtStatus::ok);
}

TEST(AtCommandTest, ParseErrorResponse) {
    auto resp = AtCommand::parse_response("\r\nERROR\r\n");
    EXPECT_EQ(resp.status, AtStatus::error);
}

TEST(AtCommandTest, ParseBusyResponse) {
    auto resp = AtCommand::parse_response("\r\nBUSY\r\n");
    EXPECT_EQ(resp.status, AtStatus::busy);
}

TEST(AtCommandTest, ParseEmptyResponse) {
    auto resp = AtCommand::parse_response("");
    EXPECT_EQ(resp.status, AtStatus::timeout);
}
