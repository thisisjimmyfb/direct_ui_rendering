#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------
// CommandLineTest — --timeout parameter parsing
// ---------------------------------------------------------------------------

// Test helper: parse --timeout parameter from argv array
// This mirrors the parsing logic in main.cpp for testing purposes
int parseTimeout(int argc, char* argv[])
{
    int timeoutSeconds = 0;  // 0 = no timeout (default)
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0) {
                timeoutSeconds = parsed;
            }
            ++i;  // Skip the next argument
        }
    }
    return timeoutSeconds;
}

// Test: Default timeout is 0 when no --timeout parameter is provided
TEST(CommandLineTest, DefaultTimeout_NoParameter_ReturnsZero)
{
    const char* argv[] = {"app"};
    int argc = 1;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Default timeout should be 0 when --timeout parameter is not provided";
}

// Test: --timeout with valid positive integer
TEST(CommandLineTest, Timeout_ValidInteger_ParsesCorrectly)
{
    const char* argv[] = {"app", "--timeout", "30"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 30)
        << "Timeout should be parsed as 30 from --timeout 30";
}

// Test: --timeout with zero (should remain default)
TEST(CommandLineTest, Timeout_Zero_RemainsDefault)
{
    const char* argv[] = {"app", "--timeout", "0"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Timeout should remain 0 when --timeout 0 is provided (invalid/disabled)";
}

// Test: --timeout with large value
TEST(CommandLineTest, Timeout_LargeValue_ParsesCorrectly)
{
    const char* argv[] = {"app", "--timeout", "3600"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 3600)
        << "Timeout should be parsed as 3600 from --timeout 3600";
}

// Test: --timeout with negative value (should remain default)
TEST(CommandLineTest, Timeout_NegativeValue_RemainsDefault)
{
    const char* argv[] = {"app", "--timeout", "-5"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Timeout should remain 0 when --timeout -5 is provided (negative values invalid)";
}

// Test: --timeout missing value (should remain default)
TEST(CommandLineTest, Timeout_MissingValue_RemainsDefault)
{
    const char* argv[] = {"app", "--timeout"};
    int argc = 2;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Timeout should remain 0 when --timeout is provided without a value";
}

// Test: Multiple --timeout parameters (last one wins)
TEST(CommandLineTest, Timeout_MultipleParameters_LastWins)
{
    const char* argv[] = {"app", "--timeout", "10", "--timeout", "60"};
    int argc = 5;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 60)
        << "When multiple --timeout parameters are provided, the last one should be used";
}

// Test: --timeout with non-numeric value
TEST(CommandLineTest, Timeout_NonNumericValue_RemainsDefault)
{
    const char* argv[] = {"app", "--timeout", "abc"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Timeout should remain 0 when --timeout is provided with non-numeric value";
}

// Test: --timeout at end of argument list without value
TEST(CommandLineTest, Timeout_AtEnd_NoValue_RemainsDefault)
{
    const char* argv[] = {"app", "other", "--timeout"};
    int argc = 3;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 0)
        << "Timeout should remain 0 when --timeout is at end without value";
}

// Test: Other arguments before --timeout
TEST(CommandLineTest, Timeout_WithOtherArgs_ParsesCorrectly)
{
    const char* argv[] = {"app", "--verbose", "--timeout", "45", "--debug"};
    int argc = 5;

    int timeout = parseTimeout(argc, const_cast<char**>(argv));
    EXPECT_EQ(timeout, 45)
        << "Timeout should be parsed correctly even with other arguments present";
}
