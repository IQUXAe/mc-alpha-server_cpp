#include <gtest/gtest.h>
#include "alpha_bridge.h"
#include <string>

static std::string fromFfi(FfiString s) {
    if (!s.ptr || s.len == 0) return "";
    return std::string(s.ptr, s.len);
}

TEST(ConsoleCommandsTest, ParseHelp) {
    std::string cmd = "help";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Help);
    
    cmd = "?";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Help);
}

TEST(ConsoleCommandsTest, ParseStopAndSaveAll) {
    std::string cmd = "stop";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Stop);

    cmd = "save-all";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_SaveAll);
}

TEST(ConsoleCommandsTest, ParseOpAndDeop) {
    std::string cmd = "op Notch";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Op);
    EXPECT_EQ(fromFfi(parsed.arg1), "Notch");

    cmd = "deop Herobrine";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Deop);
    EXPECT_EQ(fromFfi(parsed.arg1), "Herobrine");
}

TEST(ConsoleCommandsTest, ParseBanAndPardon) {
    std::string cmd = "ban-ip 127.0.0.1";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_BanIp);
    EXPECT_EQ(fromFfi(parsed.arg1), "127.0.0.1");

    cmd = "pardon-ip 192.168.1.1";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_PardonIp);
    EXPECT_EQ(fromFfi(parsed.arg1), "192.168.1.1");
}

TEST(ConsoleCommandsTest, ParseTp) {
    std::string cmd = "tp Notch Herobrine";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Tp);
    EXPECT_EQ(fromFfi(parsed.arg1), "Notch");
    EXPECT_EQ(fromFfi(parsed.arg2), "Herobrine");
}

TEST(ConsoleCommandsTest, ParseSummon) {
    // summon <mob> [count] [player]
    std::string cmd = "summon zombie 10 Steve";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Summon);
    EXPECT_EQ(fromFfi(parsed.arg1), "zombie");
    EXPECT_EQ(parsed.count, 10);
    EXPECT_EQ(fromFfi(parsed.arg2), "Steve");

    cmd = "summon pig";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Summon);
    EXPECT_EQ(fromFfi(parsed.arg1), "pig");
    EXPECT_EQ(parsed.count, 1);
    EXPECT_EQ(fromFfi(parsed.arg2), "");

    cmd = "summon creeper Steve";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Summon);
    EXPECT_EQ(fromFfi(parsed.arg1), "creeper");
    EXPECT_EQ(parsed.count, 1);
    EXPECT_EQ(fromFfi(parsed.arg2), "Steve");
}

TEST(ConsoleCommandsTest, ParseTellAndSay) {
    std::string cmd = "tell Notch Hello World!";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Tell);
    EXPECT_EQ(fromFfi(parsed.arg1), "Notch");
    EXPECT_EQ(fromFfi(parsed.arg2), "Hello World!");

    cmd = "say Testing server broadcast";
    parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Say);
    EXPECT_EQ(fromFfi(parsed.arg1), "Testing server broadcast");
}

TEST(ConsoleCommandsTest, ParseUnknown) {
    std::string cmd = "unknowncommand";
    auto parsed = rust_parse_console_command(cmd.c_str(), cmd.size());
    EXPECT_EQ(parsed.tag, ConsoleCommand_Unknown);
}
