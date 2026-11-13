// ps5cc - Tests for the line editor of the Dateien area: which files may be edited, and that a text
// taken apart into lines and put together again is the same bytes.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/files_edit.hpp"

#include <gtest/gtest.h>

namespace
{

using namespace hui::ps5cc;

std::string round_trip(const std::string &text)
{
    return join_edit_text(split_edit_text(text));
}

FsEntry file(const char *name, std::uint64_t size, bool writable)
{
    FsEntry e;
    e.name = name;
    e.type = "file";
    e.size = size;
    e.writable = writable;
    return e;
}

TEST(FilesEdit, TextNamesFollowTheWebList)
{
    for (const char *n : {"config.ini", "a.TXT", "x.json", "run.sh", "playlist.m3u", ".gitignore"})
        EXPECT_TRUE(is_text_name(n)) << n;
    for (const char *n : {"eboot.bin", "readme", "image.png", "archive.zip", "dot.", "ini"})
        EXPECT_FALSE(is_text_name(n)) << n;
}

TEST(FilesEdit, RefusalsNameTheReason)
{
    EXPECT_EQ(edit_refusal(file("a.ini", 100, true)), "");
    EXPECT_EQ(edit_refusal(file("a.ini", kEditMaxBytes, true)), "");
    EXPECT_NE(edit_refusal(file("a.ini", kEditMaxBytes + 1, true)), "");
    EXPECT_NE(edit_refusal(file("a.ini", 100, false)), "");
    EXPECT_NE(edit_refusal(file("a.bin", 100, true)), "");
    FsEntry dir = file("a.ini", 0, true);
    dir.type = "dir";
    EXPECT_NE(edit_refusal(dir), "");
}

TEST(FilesEdit, ExactRoundTrips)
{
    for (const char *t : {"", "\n", "a", "a\n", "a\nb", "a\nb\n", "\n\n", "a\n\nb\n", "a\r\nb\r\n",
                          "a\r\nb", "a\r\n\r\nb\r\n", "a\nb\r\nc\n", "x\r", "tab\tin\tline\n",
                          "umlaut \xC3\xA4\xC3\xB6\xC3\xBC\n"})
        EXPECT_EQ(round_trip(t), t) << "text: " << t;
}

TEST(FilesEdit, LinesWithoutLineEndings)
{
    const EditText t = split_edit_text("one\r\ntwo\r\nthree\r\n");
    ASSERT_EQ(t.lines.size(), 3u);
    EXPECT_EQ(t.lines[0], "one");
    EXPECT_EQ(t.lines[2], "three");
    EXPECT_TRUE(t.crlf);
    EXPECT_TRUE(t.final_newline);
    const EditText u = split_edit_text("one\ntwo");
    ASSERT_EQ(u.lines.size(), 2u);
    EXPECT_FALSE(u.crlf);
    EXPECT_FALSE(u.final_newline);
    // mixed endings: not CRLF, the CR stays in its line
    const EditText m = split_edit_text("a\r\nb\n");
    EXPECT_FALSE(m.crlf);
    EXPECT_EQ(m.lines[0], "a\r");
}

TEST(FilesEdit, EditedLinesKeepTheRest)
{
    EditText t = split_edit_text("[fan]\r\nmode=auto\r\ntarget=77\r\n");
    t.lines[1] = "mode=manual";
    t.lines.insert(t.lines.begin() + 2, "speed=40");
    EXPECT_EQ(join_edit_text(t), "[fan]\r\nmode=manual\r\nspeed=40\r\ntarget=77\r\n");
    t.lines.erase(t.lines.begin());
    EXPECT_EQ(join_edit_text(t), "mode=manual\r\nspeed=40\r\ntarget=77\r\n");
}

TEST(FilesEdit, NoTextAndTooManyLinesAreRefused)
{
    EXPECT_FALSE(split_edit_text(std::string("ab\0cd", 5)).error.empty());
    std::string many;
    for (std::size_t i = 0; i <= kEditMaxLines; ++i)
        many += "x\n";
    EXPECT_FALSE(split_edit_text(many).error.empty());
    std::string fits;
    for (std::size_t i = 0; i < kEditMaxLines; ++i)
        fits += "x\n";
    EXPECT_TRUE(split_edit_text(fits).error.empty());
}

TEST(FilesEdit, DisplayShowsTabsAsSpacesAndNoCr)
{
    EXPECT_EQ(edit_display("a\tb\r"), "a    b");
    EXPECT_EQ(edit_display(""), "");
}

TEST(FilesEdit, OnlyTheAnswerForTheOpenFileIsTaken)
{
    FsView v;
    v.path = "/mnt/usb0/a.ini";
    EXPECT_TRUE(view_matches(v, "/mnt/usb0/a.ini"));
    EXPECT_FALSE(view_matches(v, "/mnt/usb0/b.ini"));
    EXPECT_FALSE(view_matches(v, ""));
    EXPECT_FALSE(view_matches(FsView{}, "")); // nothing asked, nothing taken
}

TEST(FilesEdit, MarksOnlyWhatChanged)
{
    const std::vector<std::string> before{"a", "b", "c", "d"};
    EXPECT_EQ(edit_marks(before, before), (std::vector<bool>{false, false, false, false}));
    EXPECT_EQ(edit_marks({"a", "B", "c", "d"}, before),
              (std::vector<bool>{false, true, false, false}));
    // a line put in marks only itself
    EXPECT_EQ(edit_marks({"a", "b", "x", "c", "d"}, before),
              (std::vector<bool>{false, false, true, false, false}));
    // a line taken out marks nothing that is left
    EXPECT_EQ(edit_marks({"a", "c", "d"}, before), (std::vector<bool>{false, false, false}));
    EXPECT_EQ(edit_marks({}, before), std::vector<bool>{});
}

} // namespace
