#pragma once

#include "maxcompute_odbc/platform.h"
#include <cstddef>
#include <sql.h>
#include <string>

namespace maxcompute_odbc::encoding {

/**
 * Convert a UTF-8 string into a target charset and write it to a SQL_C_CHAR
 * buffer.
 *
 * Behavior:
 * - Truncates at character boundaries (never writes a partial multi-byte
 *   character; the next byte after the last complete character is the NUL).
 * - Always NUL-terminates the buffer when buffer_size > 0.
 * - Returns the *total* converted byte length (excluding NUL), so callers
 *   can fill SQL_LEN_or_Ind correctly. If the return value is greater than
 *   buffer_size - 1, the data was truncated.
 *
 * Recognized charset values (case-insensitive, normalized to UPPER):
 *   "UTF-8" / "UTF8"  -> no conversion (UTF-8 byte truncation only)
 *   "GBK" / "CP936" / "GB2312" -> Windows code page 936
 *   "GB18030"
 *   "BIG5" / "CP950"
 *   "SHIFT_JIS" / "SJIS" / "CP932"
 *   "EUC-KR" / "CP949"
 * Unknown values fall through to iconv (Unix) / numeric code-page parsing
 * (Windows). On unsupported charset or conversion error, the function falls
 * back to writing UTF-8 bytes (with character-boundary truncation) and logs a
 * warning at most once per process per unsupported charset.
 *
 * @param utf8        Source UTF-8 string.
 * @param charset     Target charset name.
 * @param buffer      Destination buffer (may be nullptr if buffer_size == 0).
 * @param buffer_size Size of the destination buffer in bytes.
 * @return Total byte length the converted output requires (excluding NUL).
 */
size_t WriteUtf8AsCharset(const std::string &utf8, const std::string &charset,
                          char *buffer, size_t buffer_size);

/**
 * Convert a UTF-8 string to UTF-16 (or UTF-32, depending on host SQLWCHAR size)
 * and write it to a SQL_C_WCHAR buffer.
 *
 * Behavior:
 * - Truncates at code-point boundaries; never splits a UTF-16 surrogate pair.
 * - Writes a NUL terminator iff buffer_size_bytes >= sizeof(SQLWCHAR) and
 *   buffer != nullptr.
 * - Pass buffer = nullptr (with buffer_size_bytes = 0) to size-only.
 * - Returns the *total* byte length the full UTF-16 (or UTF-32) output
 *   requires, excluding the NUL. Callers compare to buffer_size_bytes to
 *   decide truncation:
 *     truncated == (return_value >= buffer_size_bytes) when buffer_size_bytes
 *     was non-zero, or (return_value > 0) when sizing.
 *   The driver-manager / client uses this value as StrLen_or_Ind so apps can
 *   detect truncation per ODBC spec (SQLGetData "01004").
 *
 * Host-width handling:
 * - sizeof(SQLWCHAR) == 2: UTF-16, supplementary code points encoded as
 *   surrogate pairs (BMP code points emit one SQLWCHAR; U+10000+ emit two).
 * - sizeof(SQLWCHAR) == 4: UTF-32, every code point emits one SQLWCHAR
 *   (iODBC convention).
 *
 * @param utf8              Source UTF-8 string.
 * @param buffer            Destination buffer (may be nullptr if size is 0).
 * @param buffer_size_bytes Size of the destination buffer in bytes (matches
 *                          the ODBC `BufferLength` parameter, which for
 *                          SQL_C_WCHAR is bytes, not characters).
 * @return Total byte length of the converted output, excluding the NUL
 *         terminator. Always a multiple of sizeof(SQLWCHAR).
 */
size_t WriteUtf8AsUtf16(const std::string &utf8, SQLWCHAR *buffer,
                        size_t buffer_size_bytes);

}  // namespace maxcompute_odbc::encoding
