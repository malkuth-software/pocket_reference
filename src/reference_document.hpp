// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
// Pocket Reference's pages: the book of pages a pack holds, and one page
// parsed into blocks and runs for ReferencePage to lay out. The markup is
// written by tools/make_reference_text.py, which documents it.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pocket_reference {

// A stretch of text in one style. `link` indexes the page's links, or is
// no_link.
inline constexpr std::size_t no_link = static_cast<std::size_t>(-1);
struct Run final {
    std::string text;
    bool code{};
    bool bold{};
    bool italic{};
    std::size_t link{no_link};
};

enum class BlockKind : std::uint8_t {
    heading1,
    heading2,
    heading3,
    paragraph,
    signature,
    term,
    definition,
    code,
    table_header,
    table_row,
    figure,
    card,
};

// One block. `depth` is how many signatures hold it. A signature's or a
// code block's text is in `lines` (a signature has one line); a table row's
// cells and a card's purpose are in `cells`; a card's title is `runs` and
// its page `target`.
struct Block final {
    BlockKind kind{BlockKind::paragraph};
    std::size_t depth{};
    std::string anchor;
    std::vector<Run> runs;
    std::vector<std::vector<Run>> cells;
    std::vector<std::string> lines;
    // A card's page, or a figure's control id.
    std::string target;
};

// A figure's PNG at one density, from the pack.
struct FigureImage final {
    std::string id;
    std::uint32_t density{};
    std::vector<std::uint8_t> png;
};

struct Crumb final {
    std::string id;
    std::string label;
};

struct Page final {
    std::string id;
    std::string title;
    std::string source;
    std::vector<Crumb> crumbs;
    std::vector<Block> blocks;
    // Each link's target: a page id, then "#anchor" or nothing.
    std::vector<std::string> links;
};

// A row of the contents: its depth, the page it opens, its label and the
// image key of its icon.
struct ContentsRow final {
    std::size_t depth{};
    std::string id;
    std::string label;
    std::string icon;
};

// A search hit: the page, its title, and how many times the query occurs in
// its text.
struct SearchHit final {
    std::string id;
    std::string title;
    std::size_t count{};
    bool in_title{};
};

// The pages of one pack, held as their markup; a page is parsed when it is
// asked for.
class Book final {
  public:
    // Reads a pack (tools/pack_main.cpp). Throws std::runtime_error, saying
    // why, when the file is missing or damaged.
    [[nodiscard]] static Book open(const std::filesystem::path &pack);
    // Reads markup that is not packed (a test, or a pack's own text).
    [[nodiscard]] static Book from_text(std::string text);

    [[nodiscard]] const std::string &version() const noexcept { return version_; }
    [[nodiscard]] const std::string &commit() const noexcept { return commit_; }
    [[nodiscard]] const std::vector<ContentsRow> &contents() const noexcept { return contents_; }
    [[nodiscard]] std::size_t page_count() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t text_size() const noexcept { return text_.size(); }
    [[nodiscard]] bool has_page(std::string_view id) const noexcept;
    [[nodiscard]] std::string_view title_of(std::string_view id) const noexcept;
    // The page `id`, parsed; a page saying it is missing when there is none.
    [[nodiscard]] Page page(std::string_view id) const;
    // Pages whose title or text holds `query`, ignoring ASCII case: title
    // matches first, then by count. At most `limit`.
    [[nodiscard]] std::vector<SearchHit> search(std::string_view query, std::size_t limit) const;
    // The figures' images: each control id at densities 1 and 2.
    [[nodiscard]] const std::vector<FigureImage> &figures() const noexcept { return figures_; }

  private:
    struct Entry final {
        std::string id;
        std::string title;
        std::size_t begin{};
        std::size_t end{};
    };
    void index();
    [[nodiscard]] const Entry *find(std::string_view id) const noexcept;
    std::string text_;
    std::string version_;
    std::string commit_;
    std::vector<ContentsRow> contents_;
    std::vector<FigureImage> figures_;
    // Sorted by id.
    std::vector<Entry> entries_;
};

// Parses inline markup into runs, adding link targets to `links`.
void parse_inline(std::string_view text, std::vector<Run> &runs, std::vector<std::string> &links);
// The plain text of runs, without marks.
[[nodiscard]] std::string runs_text(const std::vector<Run> &runs);

} // namespace pocket_reference
