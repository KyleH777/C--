#pragma once

#include <cstdint>
#include <string_view>

/// Zero-copy view into a parsed CSV row.
/// Every field is a std::string_view pointing directly into the
/// memory-mapped file region — no heap allocation.
/// Only numeric fields (price, volume) are materialized.
struct TickRowView {
    std::string_view timestamp;  // points into mmap region
    std::string_view symbol;     // points into mmap region
    double           price  = 0.0;
    std::uint64_t    volume = 0;
};

/// Owning version for cross-thread transfer via the queue.
/// The producer copies only the small numeric + short-string data;
/// this avoids dangling views when chunks are processed independently.
struct TickRow {
    char     timestamp[32] {};
    char     symbol[16]    {};
    double   price  = 0.0;
    uint64_t volume = 0;

    /// Materialise from a zero-copy view.
    static TickRow from_view(const TickRowView& v) {
        TickRow r;
        auto ts_len = std::min(v.timestamp.size(), sizeof(r.timestamp) - 1);
        v.timestamp.copy(r.timestamp, ts_len);

        auto sym_len = std::min(v.symbol.size(), sizeof(r.symbol) - 1);
        v.symbol.copy(r.symbol, sym_len);

        r.price  = v.price;
        r.volume = v.volume;
        return r;
    }
};
