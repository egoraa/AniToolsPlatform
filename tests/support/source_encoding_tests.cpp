// SPDX-License-Identifier: Apache-2.0
#include <string>

#include <gtest/gtest.h>

TEST(SourceEncoding, ANarrowLiteralIsEncodedAsUtf8) {
    EXPECT_EQ(std::string("\u2014").size(), 3u)
        << "the execution charset is not UTF-8. A universal character name has to be encoded into it, "
           "so this is one byte under a code page and three under UTF-8 — the one assertion here that "
           "tells the two apart. A plain em dash literal does not: its UTF-8 bytes all survive a round "
           "trip through CP1252, so it reads as three bytes either way and guards nothing. MSVC needs "
           "/utf-8, which sets the source and execution charsets together, so this also stands for the "
           "source side; gcc and clang are UTF-8 by default";
    EXPECT_EQ(std::string("\u2014"), std::string("\xE2\x80\x94"));
}
