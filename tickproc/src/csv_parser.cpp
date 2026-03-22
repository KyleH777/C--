#include "csv_parser.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>

std::string_view CsvParser::next_field(
    const char*& p, const char* end) noexcept
{
    const char* comma = static_cast<const char*>(
        std::memchr(p, ',', static_cast<std::size_t>(end - p)));
    if (!comma) comma = end;
    std::string_view field(p, static_cast<std::size_t>(comma - p));
    p = (comma < end) ? comma + 1 : end;
    return field;
}

TickRowView CsvParser::parse_line(std::string_view line) {
    const char* p   = line.data();
    const char* end = p + line.size();

    TickRowView row;

    // timestamp — zero-copy view into the mmap region
    row.timestamp = next_field(p, end);

    // symbol — zero-copy view
    row.symbol = next_field(p, end);

    // price — parsed with strtod via a small stack buffer
    auto price_sv = next_field(p, end);
    {
        char buf[64];
        auto len = std::min(price_sv.size(), sizeof(buf) - 1);
        std::memcpy(buf, price_sv.data(), len);
        buf[len] = '\0';
        row.price = std::strtod(buf, nullptr);
    }

    // volume — parsed with std::from_chars (zero-alloc integer parse)
    auto vol_sv = next_field(p, end);
    std::from_chars(vol_sv.data(),
                    vol_sv.data() + vol_sv.size(),
                    row.volume);

    return row;
}

std::vector<std::span<const char>>
CsvParser::partition(std::span<const char> data, unsigned num_chunks) {
    std::vector<std::span<const char>> chunks;
    if (data.empty() || num_chunks == 0) return chunks;

    chunks.reserve(num_chunks);
    const char* begin = data.data();
    const char* end   = data.data() + data.size();
    const std::size_t approx = data.size() / num_chunks;

    for (unsigned i = 0; i < num_chunks; ++i) {
        const char* chunk_end = (i == num_chunks - 1)
            ? end
            : std::min(end, begin + approx);

        // Advance to next newline so rows are never split
        while (chunk_end < end && *chunk_end != '\n')
            ++chunk_end;
        if (chunk_end < end)
            ++chunk_end;  // include the '\n' in this chunk

        if (begin < chunk_end)
            chunks.emplace_back(begin, static_cast<std::size_t>(chunk_end - begin));

        begin = chunk_end;
    }
    return chunks;
}
