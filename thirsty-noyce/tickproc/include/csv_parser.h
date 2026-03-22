#pragma once

#include "tick_row.h"
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

/// Fast CSV field splitter.  Uses memchr to locate commas —
/// significantly faster than std::getline or std::string::find
/// because memchr is SIMD-optimised on glibc/musl/macOS.
///
/// All returned string_views point into the original `line` memory.
/// No allocations.
class CsvParser {
public:
    /// Parse a single CSV line into a TickRowView.
    /// Expected format: timestamp,symbol,price,volume
    [[nodiscard]] static TickRowView parse_line(std::string_view line);

    /// Partition a memory region into line-aligned chunks suitable
    /// for parallel processing.  Each chunk ends on a '\n' boundary
    /// so no row is ever split across chunks.
    [[nodiscard]] static std::vector<std::span<const char>>
    partition(std::span<const char> data, unsigned num_chunks);

    /// Iterate all lines in a chunk, invoking `callback(TickRowView)`
    /// for each parsed row.  Returns the number of rows processed.
    template <typename Callback>
    static std::size_t for_each_row(std::span<const char> chunk, Callback&& callback) {
        std::size_t count = 0;
        const char* p   = chunk.data();
        const char* end = p + chunk.size();

        while (p < end) {
            const char* nl = static_cast<const char*>(
                std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (!nl) nl = end;

            std::string_view line(p, static_cast<std::size_t>(nl - p));
            // Strip trailing \r for Windows line endings
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if (!line.empty()) {
                callback(parse_line(line));
                ++count;
            }
            p = nl + 1;
        }
        return count;
    }

private:
    /// Extract the next comma-delimited field, advancing `p` past it.
    [[nodiscard]] static std::string_view next_field(
        const char*& p, const char* end) noexcept;
};
