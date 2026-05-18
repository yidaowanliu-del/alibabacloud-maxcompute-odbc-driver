#include "maxcompute_odbc/odbc_api/encoding.h"
#include <cstring>
#include <gtest/gtest.h>
#include <string>

using maxcompute_odbc::encoding::WriteUtf8AsCharset;
using maxcompute_odbc::encoding::WriteUtf8AsUtf16;

namespace {

// "你好" in UTF-8 is E4 BD A0  E5 A5 BD (6 bytes).
// In GBK it is C4 E3  BA C3 (4 bytes).
const std::string kNiHaoUtf8 = "\xE4\xBD\xA0\xE5\xA5\xBD";
const std::string kNiHaoGbk = "\xC4\xE3\xBA\xC3";

}  // namespace

TEST(EncodingTest, Utf8PassThrough) {
  char buf[16] = {};
  size_t total = WriteUtf8AsCharset("hello", "UTF-8", buf, sizeof(buf));
  EXPECT_EQ(total, 5u);
  EXPECT_STREQ(buf, "hello");
}

TEST(EncodingTest, EmptyCharsetTreatedAsUtf8) {
  char buf[16] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "", buf, sizeof(buf));
  EXPECT_EQ(total, kNiHaoUtf8.size());
  EXPECT_EQ(0, std::memcmp(buf, kNiHaoUtf8.data(), kNiHaoUtf8.size()));
  EXPECT_EQ('\0', buf[kNiHaoUtf8.size()]);
}

TEST(EncodingTest, Utf8ByteBoundaryTruncation) {
  // "你好" is 6 bytes UTF-8. Buffer can hold 5 bytes (4 chars + NUL).
  // Truncating after 4 bytes would split 好 (3-byte sequence E5 A5 BD), so
  // helper should stop after 你 (3 bytes) at the last complete char boundary.
  char buf[5] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "UTF-8", buf, sizeof(buf));
  EXPECT_EQ(total, 6u);  // total length unaffected by buffer
  // bytes written: 3 ("你"), then NUL
  EXPECT_EQ(buf[3], '\0');
  EXPECT_EQ(0, std::memcmp(buf, kNiHaoUtf8.data(), 3));
}

TEST(EncodingTest, Utf8NoTruncationWhenFits) {
  char buf[16] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "UTF-8", buf, sizeof(buf));
  EXPECT_EQ(total, kNiHaoUtf8.size());
  EXPECT_EQ('\0', buf[kNiHaoUtf8.size()]);
}

TEST(EncodingTest, GbkConversionRoundTrip) {
  // Skip on platforms where GBK is unavailable: helper falls back to UTF-8 and
  // returns the UTF-8 byte length, so we can detect that case.
  char buf[16] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "GBK", buf, sizeof(buf));
  if (total == kNiHaoUtf8.size()) {
    GTEST_SKIP() << "GBK not available on this platform; helper fell back to "
                    "UTF-8.";
  }
  EXPECT_EQ(total, kNiHaoGbk.size());
  EXPECT_EQ(0, std::memcmp(buf, kNiHaoGbk.data(), kNiHaoGbk.size()));
  EXPECT_EQ('\0', buf[kNiHaoGbk.size()]);
}

TEST(EncodingTest, GbkCharsetCaseInsensitive) {
  char a[16] = {};
  char b[16] = {};
  size_t ta = WriteUtf8AsCharset(kNiHaoUtf8, "GBK", a, sizeof(a));
  size_t tb = WriteUtf8AsCharset(kNiHaoUtf8, "gbk", b, sizeof(b));
  EXPECT_EQ(ta, tb);
  if (ta == kNiHaoGbk.size()) {
    EXPECT_EQ(0, std::memcmp(a, b, ta));
  }
}

TEST(EncodingTest, GbkBoundaryTruncation) {
  // "你好" → 4 bytes in GBK. Buffer holds 3 bytes (room for 1 char + NUL).
  // The helper must stop after 1 char (2 bytes) and not split a multi-byte
  // sequence.
  char buf[3] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "GBK", buf, sizeof(buf));
  if (total == kNiHaoUtf8.size()) {
    GTEST_SKIP() << "GBK not available on this platform.";
  }
  EXPECT_EQ(total, kNiHaoGbk.size());  // total reported, not capped
  // Buffer should contain "你" (2 bytes) + NUL — never the partial 3 bytes.
  EXPECT_EQ('\0', buf[2]);
  EXPECT_EQ(0, std::memcmp(buf, kNiHaoGbk.data(), 2));
}

TEST(EncodingTest, EmptyInputProducesEmptyOutput) {
  char buf[8];
  std::memset(buf, 'X', sizeof(buf));
  size_t total = WriteUtf8AsCharset("", "GBK", buf, sizeof(buf));
  EXPECT_EQ(total, 0u);
  EXPECT_EQ('\0', buf[0]);
}

TEST(EncodingTest, UnsupportedCharsetFallsBackToUtf8) {
  char buf[16] = {};
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "DEFINITELY-NOT-A-REAL-CHARSET",
                                    buf, sizeof(buf));
  // Fallback writes UTF-8 bytes verbatim.
  EXPECT_EQ(total, kNiHaoUtf8.size());
  EXPECT_EQ(0, std::memcmp(buf, kNiHaoUtf8.data(), kNiHaoUtf8.size()));
}

TEST(EncodingTest, ZeroSizedBufferReportsLengthOnly) {
  // Passing buffer_size == 0 must not write anything but should still return
  // the converted length so callers can size a buffer.
  size_t total = WriteUtf8AsCharset(kNiHaoUtf8, "UTF-8", nullptr, 0);
  EXPECT_EQ(total, kNiHaoUtf8.size());
}

// ---------------------------------------------------------------------------
// WriteUtf8AsUtf16 — UTF-8 → UTF-16/UTF-32 (host SQLWCHAR) writer
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kWcSize = sizeof(SQLWCHAR);

// "你好" = U+4F60 U+597D, both in BMP → 2 SQLWCHAR units = 2 * kWcSize bytes.
const std::string kNiHaoUtf8Wide = "\xE4\xBD\xA0\xE5\xA5\xBD";

// "😀" = U+1F600, supplementary plane → 2 SQLWCHAR on UTF-16 hosts (surrogate
// pair), 1 SQLWCHAR on UTF-32 hosts.
const std::string kSmileyUtf8 = "\xF0\x9F\x98\x80";

}  // namespace

TEST(EncodingTest, WriteUtf8AsUtf16_AsciiRoundTrip) {
  SQLWCHAR buf[16] = {};
  size_t total = WriteUtf8AsUtf16("hello", buf, sizeof(buf));
  EXPECT_EQ(total, 5u * kWcSize);
  // Verify UTF-16/UTF-32 contents
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(buf[i], static_cast<SQLWCHAR>("hello"[i]));
  }
  EXPECT_EQ(buf[5], 0);
}

TEST(EncodingTest, WriteUtf8AsUtf16_BmpChinese) {
  SQLWCHAR buf[16] = {};
  size_t total = WriteUtf8AsUtf16(kNiHaoUtf8Wide, buf, sizeof(buf));
  EXPECT_EQ(total, 2u * kWcSize);
  EXPECT_EQ(buf[0], 0x4F60);  // 你
  EXPECT_EQ(buf[1], 0x597D);  // 好
  EXPECT_EQ(buf[2], 0);
}

TEST(EncodingTest, WriteUtf8AsUtf16_SupplementaryPlane) {
  SQLWCHAR buf[8] = {};
  size_t total = WriteUtf8AsUtf16(kSmileyUtf8, buf, sizeof(buf));
  if (kWcSize == 2) {
    // Surrogate pair: 0xD83D 0xDE00
    EXPECT_EQ(total, 2u * kWcSize);
    EXPECT_EQ(buf[0], 0xD83D);
    EXPECT_EQ(buf[1], 0xDE00);
    EXPECT_EQ(buf[2], 0);
  } else {
    // UTF-32: single code unit
    EXPECT_EQ(total, 1u * kWcSize);
    EXPECT_EQ(buf[0], 0x1F600u);
    EXPECT_EQ(buf[1], 0);
  }
}

TEST(EncodingTest, WriteUtf8AsUtf16_NullTargetSizing) {
  // buffer == nullptr / size == 0 must not write but still report total bytes.
  size_t total = WriteUtf8AsUtf16(kNiHaoUtf8Wide, nullptr, 0);
  EXPECT_EQ(total, 2u * kWcSize);
}

TEST(EncodingTest, WriteUtf8AsUtf16_ZeroSizeWithNonNullBuffer) {
  // Some callers pass a non-null buffer with size 0; must not write either.
  SQLWCHAR sentinel[2] = {0xAAAA, 0xBBBB};
  size_t total = WriteUtf8AsUtf16(kNiHaoUtf8Wide, sentinel, 0);
  EXPECT_EQ(total, 2u * kWcSize);
  EXPECT_EQ(sentinel[0], 0xAAAA);  // unchanged
  EXPECT_EQ(sentinel[1], 0xBBBB);
}

TEST(EncodingTest, WriteUtf8AsUtf16_TinyBufferOneByte) {
  // buffer_size_bytes < sizeof(SQLWCHAR): cannot even fit a NUL. Helper must
  // not touch the buffer; total still reports full required bytes.
  SQLWCHAR sentinel[2] = {0xAAAA, 0xBBBB};
  size_t total = WriteUtf8AsUtf16(kNiHaoUtf8Wide, sentinel, 1);
  EXPECT_EQ(total, 2u * kWcSize);
  EXPECT_EQ(sentinel[0], 0xAAAA);
  EXPECT_EQ(sentinel[1], 0xBBBB);
}

TEST(EncodingTest, WriteUtf8AsUtf16_BufferFitsOnlyNul) {
  // buffer_size_bytes == sizeof(SQLWCHAR): only room for NUL, no payload.
  SQLWCHAR buf[4] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
  size_t total = WriteUtf8AsUtf16(kNiHaoUtf8Wide, buf, kWcSize);
  EXPECT_EQ(total, 2u * kWcSize);  // total still reflects full output
  EXPECT_EQ(buf[0], 0);            // NUL written
  EXPECT_EQ(buf[1], 0xBBBB);       // beyond NUL untouched
}

TEST(EncodingTest, WriteUtf8AsUtf16_TruncationAtCharBoundary) {
  // 4 SQLWCHAR slots (3 chars + NUL). Source has 6 BMP chars.
  // "你好世界你好" — 6 BMP chars. Helper must stop at 3 chars + NUL.
  std::string src;
  for (int i = 0; i < 3; ++i) src += kNiHaoUtf8Wide;  // 6 BMP chars
  SQLWCHAR buf[4] = {};
  size_t total = WriteUtf8AsUtf16(src, buf, sizeof(buf));
  EXPECT_EQ(total, 6u * kWcSize);  // full required size, not capped
  // 3 chars written: 你 好 你 (or first three from the repeated string)
  EXPECT_EQ(buf[0], 0x4F60);
  EXPECT_EQ(buf[1], 0x597D);
  EXPECT_EQ(buf[2], 0x4F60);
  EXPECT_EQ(buf[3], 0);  // NUL at boundary
}

TEST(EncodingTest, WriteUtf8AsUtf16_TruncationBeforeSurrogatePair) {
  if (kWcSize != 2) {
    GTEST_SKIP() << "Surrogate-pair semantics only apply on UTF-16 hosts.";
  }
  // 3 SQLWCHAR slots: room for 1 BMP char + 1 surrogate-pair char + NUL? No.
  // (1 BMP + NUL == 2 units used; surrogate pair needs 2; total 4 -> doesn't
  // fit.) So we set capacity to 3 units and feed "A😀": A is 1 unit, 😀 needs
  // 2; out_capacity_units = 3 - 1 = 2 (BMP A + 1) -> 😀 needs 2 more, won't
  // fit, must NOT write a half surrogate.
  SQLWCHAR buf[3] = {0xAAAA, 0xAAAA, 0xAAAA};
  std::string src = "A" + kSmileyUtf8;
  size_t total = WriteUtf8AsUtf16(src, buf, sizeof(buf));
  EXPECT_EQ(total, 3u * kWcSize);  // 'A' + surrogate pair = 3 units
  EXPECT_EQ(buf[0], static_cast<SQLWCHAR>('A'));
  EXPECT_EQ(buf[1], 0);  // NUL — surrogate not split
  // buf[2] is now the NUL terminator slot; the helper writes NUL at out_pos
}

TEST(EncodingTest, WriteUtf8AsUtf16_EmbeddedNul) {
  // Source contains a NUL byte: passes through as a U+0000 code unit.
  std::string src("a\0b", 3);
  SQLWCHAR buf[4] = {0xAAAA, 0xAAAA, 0xAAAA, 0xAAAA};
  size_t total = WriteUtf8AsUtf16(src, buf, sizeof(buf));
  EXPECT_EQ(total, 3u * kWcSize);
  EXPECT_EQ(buf[0], static_cast<SQLWCHAR>('a'));
  EXPECT_EQ(buf[1], 0);  // embedded NUL
  EXPECT_EQ(buf[2], static_cast<SQLWCHAR>('b'));
  EXPECT_EQ(buf[3], 0);  // explicit terminator
}

TEST(EncodingTest, WriteUtf8AsUtf16_EmptyInput) {
  SQLWCHAR buf[4] = {0xAAAA, 0xAAAA, 0xAAAA, 0xAAAA};
  size_t total = WriteUtf8AsUtf16("", buf, sizeof(buf));
  EXPECT_EQ(total, 0u);
  EXPECT_EQ(buf[0], 0);  // NUL-terminated
}
