// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// pocket-reference-pack: codes the reference text and its figures into the
// pack the viewer reads.
// Usage: pocket-reference-pack <reference.txt> <figures.txt> <reference.pack>
//
// The pack, its words little-endian 32-bit: "PRPK", the format (2), the
// text's length and the code's length, the text's SHA-256, the code
// (pack_coder/lz_coder.hpp); then the figures: their section's length, its
// SHA-256, and the section, which is a count, then for each figure its
// control id's length, the id, its density (1 or 2), its PNG's length and
// the PNG. figures.txt is pocket-reference-figures' listing; the PNGs are
// compressed already, so they go in as they are.
#include "../pack_coder/lz_coder.hpp"
#include "../pack_coder/sha256.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

void append_word(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

bool read_file(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

// The figures section from pocket-reference-figures' listing, or false
// with the reason on stderr.
bool figure_section(const std::filesystem::path &listing_path, std::vector<std::uint8_t> &section) {
    std::ifstream listing(listing_path, std::ios::binary);
    if (!listing) {
        std::cerr << "pocket-reference-pack: cannot read " << listing_path.string() << '\n';
        return false;
    }
    std::vector<std::uint8_t> entries;
    std::uint32_t count = 0U;
    std::string line;
    while (std::getline(listing, line)) {
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1U);
        if (second == std::string::npos) {
            std::cerr << "pocket-reference-pack: a figure line without three fields: " << line << '\n';
            return false;
        }
        const std::string id = line.substr(0U, first);
        const std::uint32_t density = static_cast<std::uint32_t>(std::stoul(line.substr(first + 1U, second - first - 1U)));
        std::vector<std::uint8_t> png;
        const std::filesystem::path png_path = listing_path.parent_path() / line.substr(second + 1U);
        if (!read_file(png_path, png)) {
            std::cerr << "pocket-reference-pack: cannot read " << png_path.string() << '\n';
            return false;
        }
        append_word(entries, static_cast<std::uint32_t>(id.size()));
        entries.insert(entries.end(), id.begin(), id.end());
        append_word(entries, density);
        append_word(entries, static_cast<std::uint32_t>(png.size()));
        entries.insert(entries.end(), png.begin(), png.end());
        ++count;
    }
    section.clear();
    append_word(section, count);
    section.insert(section.end(), entries.begin(), entries.end());
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: pocket-reference-pack <reference.txt> <figures.txt> <reference.pack>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "pocket-reference-pack: cannot read " << argv[1] << '\n';
        return 1;
    }
    const std::vector<std::uint8_t> text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::uint8_t> code;
    pl::lz_encode(std::span<const std::uint8_t>(text.data(), text.size()), code, 48);
    std::vector<std::uint8_t> check;
    if (!pl::lz_decode(std::span<const std::uint8_t>(code.data(), code.size()), text.size(), check) || check != text) {
        std::cerr << "pocket-reference-pack: the code does not decode to the text\n";
        return 1;
    }
    pl::Sha256 hash;
    hash.update(std::span<const std::uint8_t>(text.data(), text.size()));
    const pl::Sha256Digest digest = hash.finish();
    std::vector<std::uint8_t> pack{'P', 'R', 'P', 'K'};
    append_word(pack, 2U);
    append_word(pack, static_cast<std::uint32_t>(text.size()));
    append_word(pack, static_cast<std::uint32_t>(code.size()));
    pack.insert(pack.end(), digest.begin(), digest.end());
    pack.insert(pack.end(), code.begin(), code.end());
    std::vector<std::uint8_t> figures;
    if (!figure_section(argv[2], figures))
        return 1;
    pl::Sha256 figures_hash;
    figures_hash.update(std::span<const std::uint8_t>(figures.data(), figures.size()));
    const pl::Sha256Digest figures_digest = figures_hash.finish();
    append_word(pack, static_cast<std::uint32_t>(figures.size()));
    pack.insert(pack.end(), figures_digest.begin(), figures_digest.end());
    pack.insert(pack.end(), figures.begin(), figures.end());
    std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(pack.data()), static_cast<std::streamsize>(pack.size()));
    if (!output) {
        std::cerr << "pocket-reference-pack: cannot write " << argv[3] << '\n';
        return 1;
    }
    std::cout << argv[3] << ": " << text.size() << " bytes of text and " << figures.size() << " of figures in "
              << pack.size() << '\n';
    return 0;
}
