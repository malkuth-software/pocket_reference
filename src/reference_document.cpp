// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "reference_document.hpp"

#include "../pack_coder/lz_coder.hpp"
#include "../pack_coder/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>

namespace pocket_reference {
namespace {

constexpr char code_on = '\x01';
constexpr char code_off = '\x02';
constexpr char link_on = '\x03';
constexpr char link_middle = '\x04';
constexpr char link_off = '\x05';
constexpr char bold_mark = '\x06';
constexpr char italic_mark = '\x07';

// The fields of one markup line, split at tabs. The views point into `line`.
std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1U;
    }
    return fields;
}

std::string_view field(const std::vector<std::string_view> &fields, std::size_t index) {
    return index < fields.size() ? fields[index] : std::string_view{};
}

std::uint32_t read_word(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
        value |= static_cast<std::uint32_t>(bytes[at + index]) << (8U * index);
    return value;
}

// The figures section (tools/pack_main.cpp), its hash already checked.
std::vector<FigureImage> read_figures(std::span<const std::uint8_t> section, const std::filesystem::path &pack) {
    std::vector<FigureImage> figures;
    const std::runtime_error damaged(pack.string() + " is damaged: its figures are cut short.");
    if (section.size() < 4U)
        throw damaged;
    const std::size_t count = read_word(section, 0U);
    std::size_t at = 4U;
    for (std::size_t index = 0U; index < count; ++index) {
        if (section.size() - at < 4U)
            throw damaged;
        const std::size_t id_length = read_word(section, at);
        at += 4U;
        if (section.size() - at < id_length + 8U)
            throw damaged;
        FigureImage figure;
        figure.id.assign(reinterpret_cast<const char *>(section.data() + at), id_length);
        at += id_length;
        figure.density = read_word(section, at);
        const std::size_t png_length = read_word(section, at + 4U);
        at += 8U;
        if (section.size() - at < png_length)
            throw damaged;
        figure.png.assign(section.begin() + static_cast<std::ptrdiff_t>(at),
                          section.begin() + static_cast<std::ptrdiff_t>(at + png_length));
        at += png_length;
        figures.push_back(std::move(figure));
    }
    if (at != section.size())
        throw damaged;
    return figures;
}

char lower_ascii(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// How many times `needle` (already lower case) occurs in `hay`, ignoring
// ASCII case.
std::size_t count_folded(std::string_view hay, std::string_view needle) {
    if (needle.empty() || hay.size() < needle.size())
        return 0U;
    std::size_t count = 0U;
    const char first = needle[0];
    const char first_upper = first >= 'a' && first <= 'z' ? static_cast<char>(first - 'a' + 'A') : first;
    for (std::size_t at = 0U; at + needle.size() <= hay.size(); ++at) {
        if (hay[at] != first && hay[at] != first_upper)
            continue;
        std::size_t index = 1U;
        while (index < needle.size() && lower_ascii(hay[at + index]) == needle[index])
            ++index;
        if (index == needle.size()) {
            ++count;
            at += needle.size() - 1U;
        }
    }
    return count;
}

bool search_hit_before(const SearchHit &left, const SearchHit &right) {
    if (left.in_title != right.in_title)
        return left.in_title;
    if (left.count != right.count)
        return left.count > right.count;
    return left.title < right.title;
}

bool entry_before(const std::string &left_id, const std::string &right_id) { return left_id < right_id; }

} // namespace

void parse_inline(std::string_view text, std::vector<Run> &runs, std::vector<std::string> &links) {
    Run current{};
    bool in_target = false;
    std::string target;
    for (const char c : text) {
        if (in_target) {
            if (c == link_middle) {
                in_target = false;
                links.push_back(target);
                current.link = links.size() - 1U;
            } else {
                target.push_back(c);
            }
            continue;
        }
        if (c == code_on || c == code_off || c == link_on || c == link_off || c == bold_mark || c == italic_mark) {
            if (!current.text.empty()) {
                runs.push_back(current);
                current.text.clear();
            }
            if (c == code_on)
                current.code = true;
            else if (c == code_off)
                current.code = false;
            else if (c == bold_mark)
                current.bold = !current.bold;
            else if (c == italic_mark)
                current.italic = !current.italic;
            else if (c == link_on) {
                in_target = true;
                target.clear();
            } else
                current.link = no_link;
            continue;
        }
        current.text.push_back(c);
    }
    if (!current.text.empty())
        runs.push_back(current);
}

std::string runs_text(const std::vector<Run> &runs) {
    std::string text;
    for (const Run &run : runs)
        text += run.text;
    return text;
}

Book Book::from_text(std::string text) {
    Book book;
    book.text_ = std::move(text);
    book.index();
    return book;
}

Book Book::open(const std::filesystem::path &pack) {
    std::ifstream input(pack, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read the pack " + pack.string() + ".");
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    constexpr std::size_t header = 4U + 12U + 32U;
    if (bytes.size() < header || std::memcmp(bytes.data(), "PRPK", 4U) != 0)
        throw std::runtime_error(pack.string() + " is not a Pocket Reference pack.");
    if (read_word(bytes, 4U) != 2U)
        throw std::runtime_error(pack.string() + " is a pack of another format.");
    const std::size_t length = read_word(bytes, 8U);
    const std::size_t code_length = read_word(bytes, 12U);
    const std::size_t figures_at = header + code_length + 4U + 32U;
    if (bytes.size() < figures_at)
        throw std::runtime_error(pack.string() + " is cut short.");
    const std::size_t figures_length = read_word(bytes, header + code_length);
    if (bytes.size() != figures_at + figures_length)
        throw std::runtime_error(pack.string() + " is cut short.");
    std::vector<std::uint8_t> text;
    const std::span<const std::uint8_t> code(bytes.data() + header, code_length);
    if (!pl::lz_decode(code, length, text))
        throw std::runtime_error(pack.string() + " is damaged: its code does not decode.");
    pl::Sha256 hash;
    hash.update(std::span<const std::uint8_t>(text.data(), text.size()));
    const pl::Sha256Digest digest = hash.finish();
    if (!std::equal(digest.begin(), digest.end(), bytes.begin() + 16))
        throw std::runtime_error(pack.string() + " is damaged: its text does not match its hash.");
    const std::span<const std::uint8_t> figures(bytes.data() + figures_at, figures_length);
    pl::Sha256 figures_hash;
    figures_hash.update(figures);
    const pl::Sha256Digest figures_digest = figures_hash.finish();
    if (!std::equal(figures_digest.begin(), figures_digest.end(), bytes.begin() + static_cast<std::ptrdiff_t>(header + code_length + 4U)))
        throw std::runtime_error(pack.string() + " is damaged: its figures do not match their hash.");
    std::string decoded(text.begin(), text.end());
    Book book = from_text(std::move(decoded));
    book.figures_ = read_figures(figures, pack);
    return book;
}

void Book::index() {
    const std::string_view all(text_);
    std::size_t at = 0U;
    bool in_tree = false;
    constexpr std::size_t none = static_cast<std::size_t>(-1);
    std::size_t open_entry = none;
    std::vector<Entry> entries;
    while (at < all.size()) {
        std::size_t end = all.find('\n', at);
        if (end == std::string_view::npos)
            end = all.size();
        const std::string_view line = all.substr(at, end - at);
        const std::size_t line_start = at;
        at = end + 1U;
        if (line.starts_with("@@")) {
            const std::vector<std::string_view> fields = fields_of(line);
            in_tree = fields[0] == "@@tree";
            if (fields[0] == "@@pack") {
                version_ = std::string(field(fields, 1U));
                commit_ = std::string(field(fields, 2U));
            } else if (fields[0] == "@@page") {
                if (open_entry != none)
                    entries[open_entry].end = line_start;
                Entry entry{};
                entry.id = std::string(field(fields, 1U));
                entry.begin = at;
                entry.end = all.size();
                entries.push_back(std::move(entry));
                open_entry = entries.size() - 1U;
            }
            continue;
        }
        if (in_tree) {
            const std::vector<std::string_view> fields = fields_of(line);
            ContentsRow row{};
            row.depth = static_cast<std::size_t>(std::max(0, std::atoi(std::string(field(fields, 0U)).c_str())));
            row.id = std::string(field(fields, 1U));
            row.label = std::string(field(fields, 2U));
            row.icon = std::string(field(fields, 3U));
            contents_.push_back(std::move(row));
        } else if (open_entry != none && line.starts_with("title\t")) {
            entries[open_entry].title = std::string(line.substr(6U));
        }
    }
    std::vector<std::string> ids;
    ids.reserve(entries.size());
    for (const Entry &entry : entries)
        ids.push_back(entry.id);
    entries_ = std::move(entries);
    std::vector<std::size_t> order(entries_.size());
    for (std::size_t index = 0U; index < order.size(); ++index)
        order[index] = index;
    struct ById final {
        const std::vector<std::string> *ids{};
        bool operator()(std::size_t left, std::size_t right) const { return entry_before((*ids)[left], (*ids)[right]); }
    };
    std::sort(order.begin(), order.end(), ById{&ids});
    std::vector<Entry> sorted;
    sorted.reserve(entries_.size());
    for (const std::size_t index : order)
        sorted.push_back(std::move(entries_[index]));
    entries_ = std::move(sorted);
}

const Book::Entry *Book::find(std::string_view id) const noexcept {
    std::size_t low = 0U;
    std::size_t high = entries_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (std::string_view(entries_[middle].id) < id)
            low = middle + 1U;
        else
            high = middle;
    }
    if (low < entries_.size() && entries_[low].id == id)
        return &entries_[low];
    return nullptr;
}

bool Book::has_page(std::string_view id) const noexcept { return find(id) != nullptr; }

std::string_view Book::title_of(std::string_view id) const noexcept {
    const Entry *const entry = find(id);
    return entry != nullptr ? std::string_view((*entry).title) : std::string_view{};
}

Page Book::page(std::string_view id) const {
    Page page{};
    page.id = std::string(id);
    const Entry *const entry = find(id);
    if (entry == nullptr) {
        page.title = "Page not found";
        Block heading{};
        heading.kind = BlockKind::heading1;
        heading.runs.push_back(Run{"Page not found", false, false, false, no_link});
        page.blocks.push_back(std::move(heading));
        Block paragraph{};
        paragraph.kind = BlockKind::paragraph;
        paragraph.runs.push_back(Run{"This pack has no page ", false, false, false, no_link});
        paragraph.runs.push_back(Run{std::string(id), true, false, false, no_link});
        paragraph.runs.push_back(Run{".", false, false, false, no_link});
        page.blocks.push_back(std::move(paragraph));
        return page;
    }
    const std::string_view body = std::string_view(text_).substr((*entry).begin, (*entry).end - (*entry).begin);
    std::size_t at = 0U;
    std::size_t depth = 0U;
    Block *code_block = nullptr;
    while (at < body.size()) {
        std::size_t end = body.find('\n', at);
        if (end == std::string_view::npos)
            end = body.size();
        const std::string_view line = body.substr(at, end - at);
        at = end + 1U;
        const std::vector<std::string_view> fields = fields_of(line);
        const std::string_view key = fields[0];
        if (key == "code") {
            if (code_block == nullptr) {
                Block block{};
                block.kind = BlockKind::code;
                block.depth = depth;
                page.blocks.push_back(std::move(block));
                code_block = &page.blocks.back();
            }
            (*code_block).lines.push_back(std::string(line.size() > 5U ? line.substr(5U) : std::string_view{}));
            continue;
        }
        code_block = nullptr;
        if (key == "codeend") {
            continue;
        }
        if (key == "title") {
            page.title = std::string(field(fields, 1U));
            continue;
        }
        if (key == "source") {
            page.source = std::string(field(fields, 1U));
            continue;
        }
        if (key == "crumb") {
            page.crumbs.push_back(Crumb{std::string(field(fields, 1U)), std::string(field(fields, 2U))});
            continue;
        }
        if (key == "in") {
            ++depth;
            continue;
        }
        if (key == "out") {
            if (depth > 0U)
                --depth;
            continue;
        }
        Block block{};
        block.depth = depth;
        if (key == "h1") {
            block.kind = BlockKind::heading1;
            parse_inline(field(fields, 1U), block.runs, page.links);
        } else if (key == "h2") {
            block.kind = BlockKind::heading2;
            block.anchor = std::string(field(fields, 1U));
            parse_inline(field(fields, 2U), block.runs, page.links);
        } else if (key == "h3") {
            block.kind = BlockKind::heading3;
            parse_inline(field(fields, 1U), block.runs, page.links);
        } else if (key == "p") {
            block.kind = BlockKind::paragraph;
            parse_inline(field(fields, 1U), block.runs, page.links);
        } else if (key == "sig") {
            block.kind = BlockKind::signature;
            block.anchor = std::string(field(fields, 1U));
            block.lines.push_back(std::string(field(fields, 2U)));
        } else if (key == "dt") {
            block.kind = BlockKind::term;
            block.anchor = std::string(field(fields, 1U));
            parse_inline(field(fields, 2U), block.runs, page.links);
        } else if (key == "dd") {
            block.kind = BlockKind::definition;
            parse_inline(field(fields, 1U), block.runs, page.links);
        } else if (key == "th" || key == "tr") {
            block.kind = key == "th" ? BlockKind::table_header : BlockKind::table_row;
            for (std::size_t index = 1U; index < fields.size(); ++index) {
                std::vector<Run> cell;
                parse_inline(fields[index], cell, page.links);
                block.cells.push_back(std::move(cell));
            }
        } else if (key == "fig") {
            block.kind = BlockKind::figure;
            block.target = std::string(field(fields, 1U));
            parse_inline(field(fields, 2U), block.runs, page.links);
        } else if (key == "card") {
            block.kind = BlockKind::card;
            block.target = std::string(field(fields, 1U));
            page.links.push_back(block.target);
            block.runs.push_back(Run{std::string(field(fields, 2U)), false, true, false, page.links.size() - 1U});
            std::vector<Run> purpose;
            parse_inline(field(fields, 3U), purpose, page.links);
            block.cells.push_back(std::move(purpose));
        } else {
            continue;
        }
        page.blocks.push_back(std::move(block));
    }
    return page;
}

std::vector<SearchHit> Book::search(std::string_view query, std::size_t limit) const {
    std::string needle;
    for (const char c : query)
        needle.push_back(lower_ascii(c));
    std::vector<SearchHit> hits;
    if (needle.empty())
        return hits;
    for (const Entry &entry : entries_) {
        const std::string_view body = std::string_view(text_).substr(entry.begin, entry.end - entry.begin);
        const std::size_t count = count_folded(body, needle);
        const bool in_title = count_folded(entry.title, needle) > 0U;
        if (count == 0U && !in_title)
            continue;
        hits.push_back(SearchHit{entry.id, entry.title, count, in_title});
    }
    std::sort(hits.begin(), hits.end(), search_hit_before);
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

} // namespace pocket_reference
