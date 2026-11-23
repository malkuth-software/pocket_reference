// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "reference_page.hpp"

#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/theme/part_painting/part_painting.hpp>
#include <gui_forms/theme/stock_anatomies/stock_anatomies.hpp>
#include <gui_forms/typography/types/typography_types.hpp>

#include <stxlib/optional.hpp>
#include <stxlib/syntax.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace gf = gui_forms;

namespace pocket_reference {
namespace {

constexpr std::size_t part_count = 26U;

// The parts, in the order of ReferencePage::Part.
constexpr std::array<std::string_view, part_count> part_names{{
    "paper", "heading", "heading_rule", "section_rule", "text", "code_text", "code_span", "link", "signature", "signature_bar",
    "code_block", "keyword", "type", "function", "string", "number", "comment", "directive",
    "table_header", "table_cell", "find_match", "find_current", "preview", "caption", "card", "purpose",
}};
constexpr std::array<gf::PartRole, part_count> part_roles{{
    gf::PartRole::face, gf::PartRole::label, gf::PartRole::face, gf::PartRole::face, gf::PartRole::label,
    gf::PartRole::label, gf::PartRole::face, gf::PartRole::label, gf::PartRole::face, gf::PartRole::face,
    gf::PartRole::face, gf::PartRole::label, gf::PartRole::label, gf::PartRole::label, gf::PartRole::label,
    gf::PartRole::label, gf::PartRole::label, gf::PartRole::label, gf::PartRole::face, gf::PartRole::face,
    gf::PartRole::face, gf::PartRole::face, gf::PartRole::face, gf::PartRole::label, gf::PartRole::face,
    gf::PartRole::label,
}};
constexpr std::array<gf::AnatomyState, 2> link_states{{gf::AnatomyState::hot, gf::AnatomyState::pressed}};
constexpr std::array<gf::AnatomyState, 1> card_states{{gf::AnatomyState::hot}};

// The page's typography, in units of the mockup (mockup.html): the body is
// 13 units with lines of 1.5, the code 12, headings 22, 16 and 13.
constexpr double page_padding_x = 28.0;
constexpr double page_padding_top = 18.0;
constexpr double page_padding_bottom = 40.0;
constexpr double page_maximum_width = 860.0;
constexpr double nested_indent = 18.0;
constexpr double definition_indent = 22.0;
constexpr double card_minimum_width = 220.0;
constexpr double card_gap = 8.0;

// The anatomy: ScrollableControl's parts, then the page's.
struct AnatomyStorage final {
    std::vector<gf::PartDeclaration> parts;
    gf::ControlAnatomy anatomy{};
};

AnatomyStorage make_anatomy() {
    AnatomyStorage storage;
    const gf::ControlAnatomy *base = nullptr;
    for (const gf::ControlAnatomy &stock : gf::stock_control_anatomies()) {
        if (stock.control_id == "scrollable_control")
            base = &stock;
    }
    if (base != nullptr)
        storage.parts.assign((*base).parts.begin(), (*base).parts.end());
    for (std::size_t index = 0U; index < part_count; ++index) {
        gf::PartDeclaration declaration{};
        declaration.name = part_names[index];
        declaration.role = part_roles[index];
        if (part_names[index] == "link")
            declaration.states = std::span<const gf::AnatomyState>(link_states);
        else if (part_names[index] == "card")
            declaration.states = std::span<const gf::AnatomyState>(card_states);
        storage.parts.push_back(declaration);
    }
    storage.anatomy.control_id = "reference_page";
    storage.anatomy.parts = std::span<const gf::PartDeclaration>(storage.parts.data(), storage.parts.size());
    if (base != nullptr)
        storage.anatomy.states = (*base).states;
    return storage;
}

bool is_space(char c) noexcept { return c == ' '; }

char fold(char c, bool match_case) noexcept {
    if (match_case)
        return c;
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

ReferencePage::ReferencePage(gf::StableId stable_id) : ScrollableControl(std::move(stable_id)) {
    auto_scroll = true;
    focusable = true;
    tab_stop = true;
    accessible_name = "Reference page";
}

const gf::ControlAnatomy &ReferencePage::page_anatomy() noexcept {
    static const AnatomyStorage storage = make_anatomy();
    return storage.anatomy;
}

const gf::ControlAnatomy &ReferencePage::anatomy() const noexcept { return page_anatomy(); }

// --- Content -------------------------------------------------------------------------

void ReferencePage::set_page(Page page) {
    page_ = std::move(page);
    matches_.clear();
    query_.clear();
    pending_anchor_.clear();
    pending_reveal_ = false;
    current_match_ = 0U;
    hovered_link_ = no_link;
    pressed_link_ = no_link;
    relayout();
    arrange_scroll_viewport(gf::Size{committed_arranged_bounds().width, committed_arranged_bounds().height},
                            gf::Size{content_width_, content_height_});
    scroll_to(gf::Point{0.0, 0.0});
    invalidate(gf::Dirty::paint);
}

bool ReferencePage::scroll_to_anchor(std::string_view anchor) {
    pending_anchor_ = std::string(anchor);
    return apply_anchor(anchor);
}

bool ReferencePage::apply_anchor(std::string_view anchor) {
    for (const Anchor &entry : anchors_) {
        if (entry.name == anchor) {
            scroll_to(gf::Point{0.0, std::max(0.0, entry.top - 8.0)});
            invalidate(gf::Dirty::paint);
            return true;
        }
    }
    return false;
}

void ReferencePage::scroll_to_top() {
    pending_anchor_.clear();
    scroll_to(gf::Point{0.0, 0.0});
    invalidate(gf::Dirty::paint);
}

// --- Find ----------------------------------------------------------------------------

std::size_t ReferencePage::find(std::string_view query, bool match_case) {
    query_ = std::string(query);
    match_case_ = match_case;
    matches_.clear();
    current_match_ = 0U;
    if (!query_.empty()) {
        for (std::size_t index = 0U; index < pieces_.size(); ++index) {
            const std::string_view text = pieces_[index].text;
            if (text.size() < query_.size())
                continue;
            for (std::size_t at = 0U; at + query_.size() <= text.size(); ++at) {
                std::size_t matched = 0U;
                while (matched < query_.size() &&
                       fold(text[at + matched], match_case_) == fold(query_[matched], match_case_))
                    ++matched;
                if (matched == query_.size()) {
                    matches_.push_back(Match{index, at, query_.size()});
                    at += query_.size() - 1U;
                }
            }
        }
    }
    reveal_current_match();
    invalidate(gf::Dirty::paint);
    return matches_.size();
}

void ReferencePage::find_next() {
    if (matches_.empty())
        return;
    current_match_ = (current_match_ + 1U) % matches_.size();
    reveal_current_match();
    invalidate(gf::Dirty::paint);
}

void ReferencePage::find_previous() {
    if (matches_.empty())
        return;
    current_match_ = current_match_ == 0U ? matches_.size() - 1U : current_match_ - 1U;
    reveal_current_match();
    invalidate(gf::Dirty::paint);
}

void ReferencePage::clear_find() {
    query_.clear();
    matches_.clear();
    current_match_ = 0U;
    invalidate(gf::Dirty::paint);
}

std::size_t ReferencePage::find_position() const noexcept { return matches_.empty() ? 0U : current_match_ + 1U; }

void ReferencePage::reveal_current_match() {
    pending_reveal_ = true;
    if (matches_.empty())
        return;
    const Piece &piece = pieces_[matches_[current_match_].piece];
    const gf::Rect viewport = viewport_rectangle();
    if (viewport.height <= 0.0)
        return;
    const gf::Point scrolled = scroll_position();
    const double top = piece.bounds.y;
    const double bottom = piece.bounds.y + piece.bounds.height;
    if (top >= scrolled.y && bottom <= scrolled.y + viewport.height)
        return;
    scroll_to(gf::Point{scrolled.x, std::max(0.0, top - viewport.height / 3.0)});
}

// --- Layout --------------------------------------------------------------------------

ReferencePage::Fonts ReferencePage::fonts() const noexcept {
    Fonts fonts{};
    fonts.body = text_font(gf::FontSpec{gf::FontRole::content, 13.0, 400, false});
    const double unit = fonts.body.size / 13.0;
    fonts.bold = fonts.body;
    fonts.bold.weight = 700;
    fonts.italic = fonts.body;
    fonts.italic.italic = true;
    // The code font is the monospace role's own family, not the body's.
    fonts.code = gf::FontSpec{};
    fonts.code.role = gf::FontRole::monospace;
    fonts.code.size = 12.0 * unit;
    fonts.code.language = fonts.body.language;
    fonts.small = fonts.body;
    fonts.small.size = 12.0 * unit;
    fonts.small_code = fonts.code;
    fonts.small_code.size = 11.5 * unit;
    fonts.caption = fonts.body;
    fonts.caption.size = 11.0 * unit;
    fonts.heading1 = fonts.body;
    fonts.heading1.size = 22.0 * unit;
    fonts.heading1.weight = 600;
    fonts.heading2 = fonts.body;
    fonts.heading2.size = 16.0 * unit;
    fonts.heading2.weight = 600;
    fonts.heading3 = fonts.body;
    fonts.heading3.weight = 600;
    return fonts;
}

double ReferencePage::width_of(std::string_view text, const gf::FontSpec &font) const {
    if (text.empty())
        return 0.0;
    return measure_text_extent_utf8(text, font).logical_size.width;
}

double ReferencePage::baseline_in(double line_height, const gf::FontSpec &font) const {
    const gf::LineBoxMetrics metrics =
        gf::line_box_metrics(resolve_text_layout_utf8(" ", font), stxlib::Optional<double>::none());
    return std::round((line_height - metrics.height) / 2.0 + metrics.baseline);
}

double ReferencePage::flow_runs(const std::vector<Run> &runs, const Flow &flow, double top) {
    const std::size_t first_piece = pieces_.size();
    double x = 0.0;
    double line_top = top;
    bool placed = false;
    std::size_t last_run = static_cast<std::size_t>(-1);
    for (std::size_t run_index = 0U; run_index < runs.size(); ++run_index) {
        const Run &run = runs[run_index];
        gf::FontSpec font = run.code ? flow.code : flow.body;
        if (run.bold)
            font.weight = 700;
        if (run.italic)
            font.italic = true;
        Part part = flow.part;
        if (run.link != no_link)
            part = Part::link;
        else if (run.code && flow.part == Part::text)
            part = Part::code_text;
        const double baseline = baseline_in(flow.line_height, font);
        const std::string_view text(run.text);
        std::size_t at = 0U;
        while (at < text.size()) {
            std::size_t end = at;
            while (end < text.size() && !is_space(text[end]))
                ++end;
            const std::size_t word_end = end;
            while (end < text.size() && is_space(text[end]))
                ++end;
            const std::string_view word = text.substr(at, end - at);
            const double full = width_of(word, font);
            const double trimmed = word_end == end ? full : width_of(text.substr(at, word_end - at), font);
            if (x > 0.0 && x + trimmed > flow.width) {
                line_top += flow.line_height;
                x = 0.0;
            }
            const bool extends = placed && last_run == run_index && pieces_.back().bounds.y == line_top &&
                                 pieces_.back().text.data() + pieces_.back().text.size() == word.data();
            if (extends) {
                Piece &piece = pieces_.back();
                piece.text = std::string_view(piece.text.data(), piece.text.size() + word.size());
                piece.bounds.width += full;
            } else {
                Piece piece{};
                piece.bounds = gf::Rect{flow.left + x, line_top, full, flow.line_height};
                piece.baseline = line_top + baseline;
                piece.font = font;
                piece.text = word;
                piece.part = part;
                piece.link = run.link;
                pieces_.push_back(piece);
            }
            placed = true;
            last_run = run_index;
            x += full;
            at = end;
        }
    }
    // Inline code sits on a tint, as in the mockup (p code).
    for (std::size_t index = first_piece; index < pieces_.size(); ++index) {
        const Piece &piece = pieces_[index];
        if (piece.part != Part::code_text)
            continue;
        const double inset = std::round(flow.line_height * 0.12);
        boxes_.push_back(Box{gf::Rect{piece.bounds.x - 1.0, piece.bounds.y + inset, piece.bounds.width + 2.0,
                                      piece.bounds.height - 2.0 * inset},
                             Part::code_span, no_link});
    }
    return placed ? line_top + flow.line_height : top;
}

double ReferencePage::place_code(std::string_view line, double left, double top, double line_height,
                                 const gf::FontSpec &font, double wrap_width, std::uint64_t &state, double &bottom) {
    namespace sx = stxlib::syntax;
    const double space = width_of(" ", font);
    const double baseline = baseline_in(line_height, font);
    gf::FontSpec comment_font = font;
    comment_font.italic = true;
    double x = left;
    double line_top = top;
    sx::LineCursor cursor = sx::LineCursor::at_line_start(sx::LineState{state});
    std::array<sx::LineToken, 64> tokens{};
    double right = left;
    std::size_t column = 0U;
    while (!cursor.done) {
        const std::size_t count =
            sx::lex_line(sx::Language::cpp, stxlib::View<char>(line.data(), line.size()), cursor,
                         stxlib::MutableView<sx::LineToken>(tokens.data(), tokens.size()));
        for (std::size_t index = 0U; index < count; ++index) {
            const sx::LineToken &token = tokens[index];
            // The lexer leaves gaps between tokens untold: they are spaces.
            if (token.column > column)
                x += space * static_cast<double>(token.column - column);
            column = token.column + token.length;
            const std::string_view text = line.substr(token.column, token.length);
            std::size_t spaces = 0U;
            while (spaces < text.size() && text[spaces] == ' ')
                ++spaces;
            if (spaces == text.size()) {
                x += space * static_cast<double>(spaces);
                continue;
            }
            Part part = Part::code_text;
            switch (token.kind) {
            case sx::TokenKind::comment:
            case sx::TokenKind::documentation:
                part = Part::comment;
                break;
            case sx::TokenKind::string:
            case sx::TokenKind::character:
                part = Part::string;
                break;
            case sx::TokenKind::number:
            case sx::TokenKind::constant:
                part = Part::number;
                break;
            case sx::TokenKind::keyword:
            case sx::TokenKind::control:
                part = Part::keyword;
                break;
            case sx::TokenKind::type:
                part = Part::type;
                break;
            case sx::TokenKind::function:
                part = Part::function;
                break;
            case sx::TokenKind::directive:
            case sx::TokenKind::macro:
                part = Part::directive;
                break;
            default:
                part = Part::code_text;
                break;
            }
            const gf::FontSpec &token_font = part == Part::comment ? comment_font : font;
            const double width = width_of(text, token_font);
            if (wrap_width > 0.0 && x > left && x + width - left > wrap_width) {
                line_top += line_height;
                x = left + 4.0 * space;
            }
            Piece piece{};
            piece.bounds = gf::Rect{x, line_top, width, line_height};
            piece.baseline = line_top + baseline;
            piece.font = token_font;
            piece.text = text;
            piece.part = part;
            pieces_.push_back(piece);
            x += width;
            right = std::max(right, x);
        }
    }
    state = cursor.state.value;
    bottom = line_top + line_height;
    return right;
}

double ReferencePage::lay_out_table(std::size_t first, std::size_t end, double left, double width, double top) {
    const Fonts f = fonts();
    const double line_height = std::round(f.small.size * 1.45);
    std::size_t columns = 1U;
    for (std::size_t index = first; index < end; ++index)
        columns = std::max(columns, page_.blocks[index].cells.size());
    std::vector<double> natural(columns, 0.0);
    for (std::size_t index = first; index < end; ++index) {
        const std::vector<std::vector<Run>> &cells = page_.blocks[index].cells;
        for (std::size_t column = 0U; column < cells.size(); ++column)
            natural[column] = std::max(natural[column], width_of(runs_text(cells[column]), f.small) + 16.0);
    }
    double total = 0.0;
    for (double &column : natural) {
        column = std::max(column, 48.0);
        total += column;
    }
    // The table fills the column, as the mockup's does: spare width goes to
    // each column by its natural share; a table too wide shrinks the same way.
    std::vector<double> widths(columns, 0.0);
    for (std::size_t column = 0U; column < columns; ++column)
        widths[column] = std::floor(width * natural[column] / total);
    double y = top;
    for (std::size_t index = first; index < end; ++index) {
        const Block &block = page_.blocks[index];
        const bool header = block.kind == BlockKind::table_header;
        Flow flow{};
        flow.line_height = line_height;
        flow.body = header ? f.small : f.small;
        if (header)
            flow.body.weight = 600;
        flow.code = f.small_code;
        flow.part = Part::text;
        double x = left;
        double row_bottom = y + line_height + 8.0;
        for (std::size_t column = 0U; column < columns; ++column) {
            if (column < block.cells.size()) {
                flow.left = x + 8.0;
                flow.width = std::max(16.0, widths[column] - 16.0);
                const double bottom = flow_runs(block.cells[column], flow, y + 4.0);
                row_bottom = std::max(row_bottom, bottom + 4.0);
            }
            x += widths[column];
        }
        x = left;
        for (std::size_t column = 0U; column < columns; ++column) {
            boxes_.push_back(Box{gf::Rect{x, y, widths[column] + 1.0, row_bottom - y + 1.0},
                                 header ? Part::table_header : Part::table_cell, no_link});
            x += widths[column];
        }
        y = row_bottom;
    }
    return y + 1.0;
}

double ReferencePage::lay_out_cards(std::size_t first, std::size_t end, double left, double width, double top) {
    const Fonts f = fonts();
    const double line_height = std::round(f.body.size * 1.45);
    const std::size_t columns =
        std::max<std::size_t>(1U, static_cast<std::size_t>((width + card_gap) / (card_minimum_width + card_gap)));
    const double card_width = std::floor((width - card_gap * static_cast<double>(columns - 1U)) /
                                         static_cast<double>(columns));
    double y = top;
    for (std::size_t row = first; row < end; row += columns) {
        const std::size_t row_end = std::min(end, row + columns);
        const std::size_t first_box = boxes_.size();
        double row_bottom = y;
        for (std::size_t index = row; index < row_end; ++index) {
            const Block &block = page_.blocks[index];
            const double x = left + static_cast<double>(index - row) * (card_width + card_gap);
            const std::size_t box_index = boxes_.size();
            boxes_.push_back(Box{gf::Rect{x, y, card_width, 0.0}, Part::card,
                                 block.runs.empty() ? no_link : block.runs.front().link});
            Flow flow{};
            flow.left = x + 10.0;
            flow.width = card_width - 20.0;
            flow.line_height = line_height;
            flow.body = f.body;
            flow.code = f.code;
            flow.part = Part::text;
            double bottom = flow_runs(block.runs, flow, y + 7.0);
            if (!block.cells.empty() && !block.cells.front().empty()) {
                flow.part = Part::purpose;
                flow.body = f.small;
                flow.line_height = std::round(f.small.size * 1.45);
                bottom = flow_runs(block.cells.front(), flow, bottom + 1.0);
            }
            boxes_[box_index].bounds.height = bottom + 7.0 - y;
            row_bottom = std::max(row_bottom, bottom + 7.0);
        }
        for (std::size_t index = first_box; index < boxes_.size(); ++index) {
            if (boxes_[index].part == Part::card)
                boxes_[index].bounds.height = row_bottom - y;
        }
        y = row_bottom + card_gap;
    }
    return y - card_gap;
}

void ReferencePage::relayout() {
    const gf::Rect viewport = viewport_rectangle();
    const double width = viewport.width > 0.0 ? viewport.width : client_rectangle().width;
    laid_width_ = width;
    pieces_.clear();
    boxes_.clear();
    anchors_.clear();
    const Fonts f = fonts();
    const double unit = f.body.size / 13.0;
    const double body_line = std::round(f.body.size * 1.5);
    const double code_line = std::round(f.code.size * 1.5);
    const double paragraph_width = 80.0 * width_of("0", f.body);
    const double column = std::max(120.0, std::min(width, page_maximum_width * unit) - 2.0 * page_padding_x);
    double right = width;
    double y = page_padding_top;
    double pending = 0.0;
    bool first_block = true;
    std::size_t index = 0U;
    while (index < page_.blocks.size()) {
        const Block &block = page_.blocks[index];
        const double indent = nested_indent * static_cast<double>(block.depth);
        const double left = page_padding_x + indent;
        const double available = std::max(80.0, column - indent);
        if (block.kind == BlockKind::figure && figure_size(block.target).width <= 0.0) {
            // No specimen of this control: no figure, and no space for one.
            ++index;
            continue;
        }
        double top_margin = 0.0;
        double bottom_margin = 0.0;
        switch (block.kind) {
        case BlockKind::heading1: top_margin = 0.0; bottom_margin = 4.0; break;
        case BlockKind::heading2: top_margin = 22.0; bottom_margin = 8.0; break;
        case BlockKind::heading3: top_margin = 16.0; bottom_margin = 6.0; break;
        case BlockKind::paragraph: top_margin = 6.0; bottom_margin = 8.0; break;
        case BlockKind::signature: top_margin = 14.0; bottom_margin = 4.0; break;
        case BlockKind::term: top_margin = 8.0; bottom_margin = 0.0; break;
        case BlockKind::definition: top_margin = 2.0; bottom_margin = 0.0; break;
        case BlockKind::code: top_margin = 8.0; bottom_margin = 12.0; break;
        case BlockKind::table_header:
        case BlockKind::table_row: top_margin = 8.0; bottom_margin = 14.0; break;
        case BlockKind::figure: top_margin = 10.0; bottom_margin = 12.0; break;
        case BlockKind::card: top_margin = 10.0; bottom_margin = 10.0; break;
        }
        y += first_block ? 0.0 : std::max(top_margin, pending);
        first_block = false;
        pending = bottom_margin;
        if (!block.anchor.empty())
            anchors_.push_back(Anchor{block.anchor, y});
        Flow flow{};
        flow.left = left;
        flow.width = available;
        flow.line_height = body_line;
        flow.body = f.body;
        flow.code = f.code;
        flow.part = Part::text;
        std::size_t next = index + 1U;
        switch (block.kind) {
        case BlockKind::heading1:
        case BlockKind::heading2: {
            const bool first_level = block.kind == BlockKind::heading1;
            flow.body = first_level ? f.heading1 : f.heading2;
            flow.code = f.code;
            flow.code.size = flow.body.size;
            flow.code.weight = flow.body.weight;
            flow.line_height = std::round(flow.body.size * 1.3);
            flow.part = Part::heading;
            y = flow_runs(block.runs, flow, y);
            y += first_level ? 6.0 : 3.0;
            boxes_.push_back(Box{gf::Rect{left, y, available, 1.0}, first_level ? Part::heading_rule : Part::section_rule, no_link});
            y += 1.0;
            break;
        }
        case BlockKind::heading3:
            flow.body = f.heading3;
            flow.part = Part::heading;
            y = flow_runs(block.runs, flow, y);
            break;
        case BlockKind::paragraph:
            flow.width = std::min(available, paragraph_width);
            y = flow_runs(block.runs, flow, y);
            break;
        case BlockKind::term:
            y = flow_runs(block.runs, flow, y);
            break;
        case BlockKind::definition:
            flow.left = left + definition_indent;
            flow.width = std::min(available - definition_indent, paragraph_width);
            y = flow_runs(block.runs, flow, y);
            break;
        case BlockKind::signature: {
            const std::size_t box_index = boxes_.size();
            boxes_.push_back(Box{gf::Rect{left, y, available, 0.0}, Part::signature, no_link});
            boxes_.push_back(Box{gf::Rect{left, y, 3.0, 0.0}, Part::signature_bar, no_link});
            std::uint64_t state = 0U;
            double bottom = y;
            const std::string_view line = block.lines.empty() ? std::string_view{} : std::string_view(block.lines[0]);
            place_code(line, left + 11.0, y + 5.0, code_line, f.code, available - 19.0, state, bottom);
            boxes_[box_index].bounds.height = bottom + 5.0 - y;
            boxes_[box_index + 1U].bounds.height = bottom + 5.0 - y;
            y = bottom + 5.0;
            break;
        }
        case BlockKind::code: {
            const std::size_t box_index = boxes_.size();
            boxes_.push_back(Box{gf::Rect{left, y, available, 0.0}, Part::code_block, no_link});
            std::uint64_t state = 0U;
            double line_top = y + 10.0;
            double widest = left + available;
            for (const std::string &line : block.lines) {
                double bottom = line_top;
                const double line_right =
                    place_code(line, left + 12.0, line_top, code_line, f.code, 0.0, state, bottom);
                widest = std::max(widest, line_right + 12.0);
                line_top = bottom;
            }
            boxes_[box_index].bounds.width = widest - left;
            boxes_[box_index].bounds.height = line_top + 10.0 - y;
            right = std::max(right, widest + page_padding_x);
            y = line_top + 10.0;
            break;
        }
        case BlockKind::table_header:
        case BlockKind::table_row:
            while (next < page_.blocks.size() &&
                   (page_.blocks[next].kind == BlockKind::table_row) && page_.blocks[next].depth == block.depth)
                ++next;
            y = lay_out_table(index, next, left, available, y);
            break;
        case BlockKind::figure: {
            // The control's specimen at its own size, shrunk to fit. A
            // control with no specimen (a component, a dialog) has no
            // figure, so the block shows nothing.
            const gf::Size image = figure_size(block.target);
            const double fit = std::min(1.0, (available - 2.0) / image.width);
            const double box_width = std::round(image.width * fit) + 2.0;
            const double box_height = std::round(image.height * fit) + 2.0;
            boxes_.push_back(Box{gf::Rect{left, y, box_width, box_height}, Part::preview, no_link, block.target});
            flow.body = f.caption;
            flow.line_height = std::round(f.caption.size * 1.4);
            flow.part = Part::caption;
            flow.width = std::min(available, 420.0 * unit);
            y = flow_runs(block.runs, flow, y + box_height + 4.0);
            break;
        }
        case BlockKind::card:
            while (next < page_.blocks.size() && page_.blocks[next].kind == BlockKind::card)
                ++next;
            y = lay_out_cards(index, next, left, available, y);
            break;
        }
        index = next;
    }
    content_height_ = y + page_padding_bottom;
    content_width_ = right;
    if (!query_.empty()) {
        const std::size_t keep = current_match_;
        const std::string query = query_;
        find(query, match_case_);
        if (keep < matches_.size())
            current_match_ = keep;
    }
}

gf::Size ReferencePage::measure(gf::Size available) {
    const gf::Rect requested = requested_bounds();
    const gf::Size result{requested.width > 0.0 ? requested.width : std::min(available.width, 640.0),
                          requested.height > 0.0 ? requested.height : std::min(available.height, 480.0)};
    return result;
}

void ReferencePage::arrange(gf::Rect final_bounds) {
    // ScrollableControl::arrange measures no children here and so clamps
    // the position to nothing; the page's own extent is set below, and the
    // position it had is put back within it.
    const gf::Point kept = scroll_position();
    ScrollableControl::arrange(final_bounds);
    relayout();
    arrange_scroll_viewport(gf::Size{final_bounds.width, final_bounds.height},
                            gf::Size{content_width_, content_height_});
    const gf::Rect viewport = viewport_rectangle();
    if (viewport.width > 0.0 && std::abs(viewport.width - laid_width_) > 0.5) {
        relayout();
        arrange_scroll_viewport(gf::Size{final_bounds.width, final_bounds.height},
                                gf::Size{content_width_, content_height_});
    }
    scroll_to(kept);
    if (final_bounds.height > 0.0) {
        if (!pending_anchor_.empty()) {
            const std::string anchor = pending_anchor_;
            pending_anchor_.clear();
            static_cast<void>(apply_anchor(anchor));
        }
        if (pending_reveal_) {
            reveal_current_match();
            pending_reveal_ = false;
        }
    }
}

gf::Point ReferencePage::content_offset() const noexcept {
    const gf::Rect viewport = viewport_rectangle();
    const gf::Point scrolled = scroll_position();
    return gf::Point{viewport.x - scrolled.x, viewport.y - scrolled.y};
}

// --- Input ---------------------------------------------------------------------------

std::size_t ReferencePage::link_at(gf::Point local) const noexcept {
    const gf::Point offset = content_offset();
    const gf::Point content{local.x - offset.x, local.y - offset.y};
    for (const Piece &piece : pieces_) {
        if (piece.link != no_link && piece.bounds.contains(content))
            return piece.link;
    }
    for (const Box &box : boxes_) {
        if (box.link != no_link && box.bounds.contains(content))
            return box.link;
    }
    return no_link;
}

void ReferencePage::handle_mouse(gf::MouseEventArgs &arguments) {
    scroll_pointer(arguments);
    if (arguments.handled)
        return;
    const gf::Point local = arguments.window_position;
    const std::size_t link = link_at(local);
    if (arguments.action == gf::PointerAction::move) {
        if (link != hovered_link_) {
            hovered_link_ = link;
            cursor = link != no_link ? stxlib::Optional<gf::CursorKind>::of(gf::CursorKind::hand)
                                     : stxlib::Optional<gf::CursorKind>::none();
            if (listener_ != nullptr)
                (*listener_).hover_link(link != no_link ? std::string_view(page_.links[link]) : std::string_view{});
            invalidate(gf::Dirty::paint);
        }
    } else if (arguments.action == gf::PointerAction::down && arguments.button == gf::PointerButton::primary) {
        pressed_link_ = link;
        if (link != no_link)
            arguments.handled = true;
        invalidate(gf::Dirty::paint);
    } else if (arguments.action == gf::PointerAction::up && arguments.button == gf::PointerButton::primary) {
        const std::size_t pressed = pressed_link_;
        pressed_link_ = no_link;
        invalidate(gf::Dirty::paint);
        if (pressed != no_link && pressed == link && listener_ != nullptr) {
            arguments.handled = true;
            const std::string target = page_.links[link];
            (*listener_).open_link(target);
        }
    }
}

void ReferencePage::on_mouse_down(gf::MouseEventArgs &arguments) {
    handle_mouse(arguments);
    Control::on_mouse_down(arguments);
}

void ReferencePage::on_mouse_up(gf::MouseEventArgs &arguments) {
    handle_mouse(arguments);
    Control::on_mouse_up(arguments);
}

void ReferencePage::on_mouse_move(gf::MouseEventArgs &arguments) {
    handle_mouse(arguments);
    Control::on_mouse_move(arguments);
}

void ReferencePage::on_mouse_wheel(gf::MouseEventArgs &arguments) {
    handle_mouse(arguments);
    Control::on_mouse_wheel(arguments);
}

void ReferencePage::on_mouse_leave() {
    scroll_pointer_left();
    if (hovered_link_ != no_link) {
        hovered_link_ = no_link;
        cursor = stxlib::Optional<gf::CursorKind>::none();
        if (listener_ != nullptr)
            (*listener_).hover_link(std::string_view{});
        invalidate(gf::Dirty::paint);
    }
    Control::on_mouse_leave();
}

// --- Paint ---------------------------------------------------------------------------

void ReferencePage::set_figures(const Book *book, std::shared_ptr<gf::ImageList> images) {
    figure_book_ = book;
    figure_images_ = std::move(images);
    relayout();
}

gf::Size ReferencePage::figure_size(const std::string &id) {
    if (figure_book_ == nullptr || figure_images_ == nullptr || id.empty())
        return gf::Size{};
    gf::ImageList &images = *figure_images_;
    if (!images.contains_key(id)) {
        for (const FigureImage &figure : (*figure_book_).figures()) {
            if (figure.id != id)
                continue;
            const std::span<const std::byte> png(reinterpret_cast<const std::byte *>(figure.png.data()),
                                                 figure.png.size());
            if (!images.contains_key(id))
                static_cast<void>(images.add_png(id, png, static_cast<double>(figure.density)));
            else
                static_cast<void>(images.set_variant_png(id, gf::ImageVisualState::normal,
                                                         static_cast<double>(figure.density), png));
        }
        if (!images.contains_key(id))
            return gf::Size{};
    }
    const gf::ImageListResolution resolved = images.resolve(std::string_view(id), gf::ImageVisualState::normal, 1.0);
    if (!resolved || resolved.resolved_scale <= 0.0)
        return gf::Size{};
    return gf::Size{resolved.source_size.width / resolved.resolved_scale,
                    resolved.source_size.height / resolved.resolved_scale};
}

void ReferencePage::paint_box(gf::Painter &painter, const Box &box, gf::Point offset) const {
    gf::PartPaint part{};
    part.family = gf::ControlVisualRole::editor;
    part.role = part_roles[static_cast<std::size_t>(box.part)];
    part.bounds = gf::Rect{box.bounds.x + offset.x, box.bounds.y + offset.y, box.bounds.width, box.bounds.height};
    if (box.link != no_link && box.link == hovered_link_)
        part.states.insert(gf::AnatomyState::hot);
    part.key.set(gf::PartKey{part_names[static_cast<std::size_t>(box.part)], {}, {}});
    gf::paint_part(painter, *this, part);
    if (!box.figure.empty() && figure_images_ != nullptr) {
        double scale = 1.0;
        const gf::BorrowedPointer<gf::SurfaceRoot> root = attached_surface_root();
        if (root)
            scale = (*root).scale();
        const gf::ImageListResolution resolved =
            (*figure_images_).resolve(std::string_view(box.figure), gf::ImageVisualState::normal, scale);
        if (resolved)
            painter.draw_image(resolved.image, gf::Rect{part.bounds.x + 1.0, part.bounds.y + 1.0,
                                                        part.bounds.width - 2.0, part.bounds.height - 2.0});
    }
}

void ReferencePage::paint_piece(gf::Painter &painter, const Piece &piece, gf::Point offset) const {
    gf::PartPaint part{};
    part.family = piece.part == Part::link ? gf::ControlVisualRole::link : gf::ControlVisualRole::editor;
    part.role = gf::PartRole::label;
    part.font = piece.font;
    part.text = gf::lend_paint_text(piece.text);
    if (piece.link != no_link && piece.link == hovered_link_)
        part.states.insert(gf::AnatomyState::hot);
    if (piece.link != no_link && piece.link == pressed_link_)
        part.states.insert(gf::AnatomyState::pressed);
    part.key.set(gf::PartKey{part_names[static_cast<std::size_t>(piece.part)], {}, {}});
    const gf::Rect bounds{piece.bounds.x + offset.x, piece.bounds.y + offset.y, piece.bounds.width + 2.0,
                          piece.bounds.height};
    gf::paint_text_part_at(painter, *this, part, gf::Point{bounds.x, piece.baseline + offset.y}, bounds.width);
    if (piece.link != no_link && piece.link == hovered_link_) {
        gf::PartPaint underline{};
        underline.family = gf::ControlVisualRole::link;
        underline.role = gf::PartRole::glyph;
        underline.mark = gf::PartMark::underline;
        underline.bounds = gf::Rect{bounds.x, bounds.y, piece.bounds.width, piece.baseline + offset.y + 2.0 - bounds.y};
        underline.states = part.states;
        gf::paint_part(painter, *this, underline);
    }
}

void ReferencePage::on_paint(gf::Painter &painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    gf::PartPaint paper{};
    paper.family = gf::ControlVisualRole::editor;
    paper.role = gf::PartRole::face;
    paper.bounds = bounds;
    paper.corner_radius.set(0.0);
    paper.key.set(gf::PartKey{"paper", {}, {}});
    gf::paint_part(painter, *this, paper);
    const gf::Rect viewport = viewport_rectangle();
    const gf::Point offset = content_offset();
    const double visible_top = viewport.y - offset.y;
    const double visible_bottom = visible_top + viewport.height;
    painter.save();
    painter.clip_rect(viewport);
    for (const Box &box : boxes_) {
        if (box.bounds.y + box.bounds.height < visible_top || box.bounds.y > visible_bottom)
            continue;
        paint_box(painter, box, offset);
    }
    for (std::size_t index = 0U; index < matches_.size(); ++index) {
        const Match &match = matches_[index];
        const Piece &piece = pieces_[match.piece];
        if (piece.bounds.y + piece.bounds.height < visible_top || piece.bounds.y > visible_bottom)
            continue;
        const double before = width_of(piece.text.substr(0U, match.begin), piece.font);
        const double width = width_of(piece.text.substr(match.begin, match.length), piece.font);
        Box mark{};
        mark.bounds = gf::Rect{piece.bounds.x + before - 1.0, piece.bounds.y + 1.0, width + 2.0, piece.bounds.height - 2.0};
        mark.part = index == current_match_ ? Part::find_current : Part::find_match;
        paint_box(painter, mark, offset);
    }
    for (const Piece &piece : pieces_) {
        if (piece.bounds.y + piece.bounds.height < visible_top || piece.bounds.y > visible_bottom)
            continue;
        paint_piece(painter, piece, offset);
    }
    painter.restore();
}

} // namespace pocket_reference
