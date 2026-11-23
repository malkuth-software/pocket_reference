// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
// ReferencePage: one reference page, read only, on paper. It lays out the
// blocks of a Page (reference_document.hpp): headings with rules, wrapped
// text with code spans and links, signatures and code blocks coloured by
// stxlib::syntax, tables, figures and cards, and marks the matches of a find.
//
// Everything it paints is a named part of its own anatomy (control id
// reference_page), so the theme package colours it: reference-page::part(
// keyword), reference-page::part(signature) and the rest. The sizes are the
// page's typography and live here.
//
// It does what HelpBook (GUI.Forms 0.6) does not: tables, coloured code,
// find in page, anchors and a listener for links. It is written against
// GUI.Forms' public API only, so it can move into the library as HelpBook's
// renderer later.
#include "reference_document.hpp"

#include <gui_forms/controls/scrollable_control.hpp>
#include <gui_forms/anatomy.hpp>
#include <gui_forms/image_list/image_list/image_list.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pocket_reference {

// What the page tells its owner. Borrowed: the owner outlives the page's
// use of it, or clears it.
class ReferencePageListener {
  public:
    virtual ~ReferencePageListener() = default;
    // A link was clicked: a page id, then "#anchor" or nothing.
    virtual void open_link(std::string_view target) = 0;
    // The pointer moved onto a link (its target) or off every link (empty).
    virtual void hover_link(std::string_view target) = 0;
};

class ReferencePage final : public gui_forms::ScrollableControl {
  public:
    explicit ReferencePage(gui_forms::StableId stable_id);

    // The anatomy, for ApplicationThemeConfig::controls, so a theme package
    // can name reference-page and its parts.
    [[nodiscard]] static const gui_forms::ControlAnatomy &page_anatomy() noexcept;

    void set_listener(ReferencePageListener *listener) noexcept { listener_ = listener; }
    // Where figures come from: `book`'s figure images (borrowed: the book
    // outlives the page's use of it), loaded into `images` the first time a
    // page shows them. A figure without an image keeps an empty box.
    void set_figures(const Book *book, std::shared_ptr<gui_forms::ImageList> images);
    // Shows `page` from its top, dropping any find.
    void set_page(Page page);
    [[nodiscard]] const Page &page() const noexcept { return page_; }
    // Scrolls so the block with `anchor` is at the top. False when the page
    // has no such anchor.
    bool scroll_to_anchor(std::string_view anchor);
    void scroll_to_top();

    // Find in page: marks every match of `query` and makes the first the
    // current one, scrolled into view. Returns the number of matches.
    std::size_t find(std::string_view query, bool match_case);
    void find_next();
    void find_previous();
    void clear_find();
    [[nodiscard]] std::size_t find_count() const noexcept { return matches_.size(); }
    // The current match, from 1, or 0 for none.
    [[nodiscard]] std::size_t find_position() const noexcept;

    [[nodiscard]] const gui_forms::ControlAnatomy &anatomy() const noexcept override;
    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

  protected:
    void on_paint(gui_forms::Painter &painter, gui_forms::Rect local_damage) override;
    void on_mouse_down(gui_forms::MouseEventArgs &arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs &arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs &arguments) override;
    void on_mouse_wheel(gui_forms::MouseEventArgs &arguments) override;
    void on_mouse_leave() override;
    [[nodiscard]] bool paints_scrolled_content() const noexcept override { return true; }

  private:
    // A part of the anatomy, by its index in part_names.
    enum class Part : std::uint8_t {
        paper,
        heading,
        heading_rule,
        section_rule,
        text,
        code_text,
        code_span,
        link,
        signature,
        signature_bar,
        code_block,
        keyword,
        type,
        function,
        string,
        number,
        comment,
        directive,
        table_header,
        table_cell,
        find_match,
        find_current,
        preview,
        caption,
        card,
        purpose,
    };
    // A stretch of text on one line, in content coordinates.
    struct Piece final {
        gui_forms::Rect bounds{};
        double baseline{};
        gui_forms::FontSpec font{};
        std::string_view text;
        Part part{Part::text};
        std::size_t link{no_link};
    };
    // A box painted behind text.
    struct Box final {
        gui_forms::Rect bounds{};
        Part part{Part::paper};
        std::size_t link{no_link};
        // A figure's image key in the figure list, drawn inside the box.
        std::string figure;
    };
    struct Anchor final {
        std::string name;
        double top{};
    };
    struct Match final {
        std::size_t piece{};
        std::size_t begin{};
        std::size_t length{};
    };
    // The fonts of one layout, from the control's text font.
    struct Fonts final {
        gui_forms::FontSpec body{};
        gui_forms::FontSpec bold{};
        gui_forms::FontSpec italic{};
        gui_forms::FontSpec code{};
        gui_forms::FontSpec small{};
        gui_forms::FontSpec small_code{};
        gui_forms::FontSpec caption{};
        gui_forms::FontSpec heading1{};
        gui_forms::FontSpec heading2{};
        gui_forms::FontSpec heading3{};
    };
    // Where a flow of runs places its text.
    struct Flow final {
        double left{};
        double width{};
        double line_height{};
        gui_forms::FontSpec body{};
        gui_forms::FontSpec code{};
        Part part{Part::text};
    };

    void relayout();
    [[nodiscard]] Fonts fonts() const noexcept;
    [[nodiscard]] double width_of(std::string_view text, const gui_forms::FontSpec &font) const;
    [[nodiscard]] double baseline_in(double line_height, const gui_forms::FontSpec &font) const;
    // Places `runs` in lines from `top`; returns the bottom of the last line.
    double flow_runs(const std::vector<Run> &runs, const Flow &flow, double top);
    // Places one line of code from (`left`, `top`), coloured by kind; returns
    // its right edge. `wrap_width` > 0 wraps at spaces and commas.
    // `state` is the lexer's line state: the one the line enters in, then
    // the one it leaves (stxlib::syntax::LineState::value).
    double place_code(std::string_view line, double left, double top, double line_height,
                      const gui_forms::FontSpec &font, double wrap_width, std::uint64_t &state, double &bottom);
    double lay_out_table(std::size_t first, std::size_t end, double left, double width, double top);
    double lay_out_cards(std::size_t first, std::size_t end, double left, double width, double top);
    [[nodiscard]] gui_forms::Point content_offset() const noexcept;
    [[nodiscard]] std::size_t link_at(gui_forms::Point local) const noexcept;
    void reveal_current_match();
    bool apply_anchor(std::string_view anchor);
    void handle_mouse(gui_forms::MouseEventArgs &arguments);
    // The logical size of figure `id`, loading it on first use; empty when
    // there is no image for it.
    [[nodiscard]] gui_forms::Size figure_size(const std::string &id);
    void paint_box(gui_forms::Painter &painter, const Box &box, gui_forms::Point offset) const;
    void paint_piece(gui_forms::Painter &painter, const Piece &piece, gui_forms::Point offset) const;

    ReferencePageListener *listener_{};
    const Book *figure_book_{};
    std::shared_ptr<gui_forms::ImageList> figure_images_;
    Page page_;
    std::vector<Piece> pieces_;
    std::vector<Box> boxes_;
    std::vector<Anchor> anchors_;
    std::vector<Match> matches_;
    std::size_t current_match_{};
    std::string query_;
    // An anchor or a match asked for before the page had its size, shown at
    // the next arrange.
    std::string pending_anchor_;
    bool pending_reveal_{};
    bool match_case_{};
    double laid_width_{};
    double content_width_{};
    double content_height_{};
    std::size_t hovered_link_{no_link};
    std::size_t pressed_link_{no_link};
};

} // namespace pocket_reference
