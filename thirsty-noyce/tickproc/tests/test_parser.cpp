#include "csv_parser.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

static void test_parse_line() {
    std::string line = "2024-01-15T09:30:00.123,AAPL,185.32,1500";
    auto row = CsvParser::parse_line(line);

    assert(row.timestamp == "2024-01-15T09:30:00.123");
    assert(row.symbol    == "AAPL");
    assert(std::abs(row.price - 185.32) < 1e-9);
    assert(row.volume    == 1500);

    std::cout << "  parse_line:       PASS\n";
}

static void test_parse_line_crlf() {
    std::string line = "2024-01-15T09:30:00.000,MSFT,410.55,3200\r";
    // Trim \r as the parser does
    std::string_view sv = line;
    if (!sv.empty() && sv.back() == '\r') sv.remove_suffix(1);

    auto row = CsvParser::parse_line(sv);

    assert(row.symbol == "MSFT");
    assert(row.volume == 3200);

    std::cout << "  parse_line_crlf:  PASS\n";
}

static void test_partition() {
    std::string data = "row1\nrow2\nrow3\nrow4\nrow5\n";
    std::span<const char> sp(data.data(), data.size());
    auto chunks = CsvParser::partition(sp, 2);

    // Must produce exactly 2 chunks
    assert(chunks.size() == 2);

    // Each chunk must end on a newline
    for (auto& c : chunks) {
        assert(!c.empty());
        assert(c.back() == '\n');
    }

    // Concatenation must equal original
    std::string reconstructed;
    for (auto& c : chunks)
        reconstructed.append(c.data(), c.size());
    assert(reconstructed == data);

    std::cout << "  partition:        PASS\n";
}

static void test_for_each_row() {
    std::string data =
        "2024-01-15T09:30:00.000,AAPL,185.32,1500\n"
        "2024-01-15T09:30:00.001,GOOG,141.80,800\n"
        "2024-01-15T09:30:00.002,TSLA,245.10,2200\n";

    std::span<const char> sp(data.data(), data.size());
    std::size_t count = 0;
    double price_sum = 0;

    CsvParser::for_each_row(sp, [&](const TickRowView& row) {
        price_sum += row.price;
        ++count;
    });

    assert(count == 3);
    assert(std::abs(price_sum - (185.32 + 141.80 + 245.10)) < 1e-6);

    std::cout << "  for_each_row:     PASS\n";
}

int main() {
    std::cout << "test_parser:\n";
    test_parse_line();
    test_parse_line_crlf();
    test_partition();
    test_for_each_row();
    std::cout << "All parser tests passed.\n";
    return 0;
}
