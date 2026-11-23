// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
// ReferenceBrowser: Pocket Reference's behaviour over the forms. The window
// (forms/reference_window.form) holds the menus, the navigation bar, the
// split and the status bar; the contents pane (forms/contents_pane.form) and
// the page pane (forms/page_pane.form) go into the split's two panels. The
// browser keeps the history, follows links, filters the contents, searches
// the pack and runs find in page.
#include "contents_pane_form.hpp"     // generated from forms/contents_pane.form
#include "page_pane_form.hpp"         // generated from forms/page_pane.form
#include "reference_document.hpp"
#include "reference_page.hpp"
#include "reference_window_form.hpp"  // generated from forms/reference_window.form

#include <gui_forms/component/component/component.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pocket_reference {

class ReferenceBrowser final : public gui_forms::Component, public ReferencePageListener {
  public:
    // Takes the book; borrows the window, which outlives the browser's use
    // of it (the run owns the window; the browser is ended after the run).
    ReferenceBrowser(Book book, ReferenceWindow &window);
    ReferenceBrowser(const ReferenceBrowser &) = delete;
    ReferenceBrowser &operator=(const ReferenceBrowser &) = delete;

    // Builds the panes, menus, images and connections; then shows `start`.
    void attach(std::string_view start);

    // Opens a target: a page id, then "#anchor" or nothing, or
    // "search:<query>". `record` adds it to the history.
    void navigate(std::string_view target, bool record);
    void go_back();
    void go_forward();
    void show_find(std::string_view query);
    void hide_find();
    void set_filter(std::string_view filter);
    // The pointer stands on the link `target` (for a capture).
    void show_hover(std::string_view target) { hover_link(target); }

    void open_link(std::string_view target) override;
    void hover_link(std::string_view target) override;

  private:
    void on_command(const gui_forms::CommandInvocation &invocation);
    void on_tree_selection(const gui_forms::TreeSelectionChange &change);
    void on_crumb(const std::string &id);
    void on_search(const std::string &query);
    void on_address(const std::string &text);
    void on_filter(const std::string &text);
    void on_find_text(const std::string &text);
    void on_match_case(bool checked);
    std::shared_ptr<gui_forms::Command> command(std::string id, std::string label, std::string shortcut);
    void bind_key(gui_forms::Command &command, std::uint32_t key, gui_forms::Modifier modifiers);
    void build_menus();
    void build_status();
    void load_images();
    void fill_tree();
    void select_in_tree(std::string_view page_id);
    void show_page(Page page, std::string_view anchor);
    [[nodiscard]] Page search_page(std::string_view query) const;
    void run_find();
    void update_commands();
    void set_status(std::string_view text);

    Book book_;
    ReferenceWindow &window_;
    std::shared_ptr<ContentsPane> contents_;
    std::shared_ptr<PagePane> page_pane_;
    std::shared_ptr<ReferencePage> page_view_;
    std::vector<std::shared_ptr<gui_forms::Command>> commands_;
    std::vector<gui_forms::AcceleratorToken> accelerators_;
    std::shared_ptr<gui_forms::ImageList> tree_images_;
    // The figures' images, loaded as pages show them.
    std::shared_ptr<gui_forms::ImageList> figure_images_;
    // The tree's rows as shown, and the page each opens (rows repeat pages:
    // a control is under its category, its topics and the A to Z list).
    std::vector<std::string> row_pages_;
    std::vector<std::string> crumb_targets_;
    std::string filter_;
    std::vector<std::string> history_;
    std::size_t position_{};
    std::string current_page_;
    std::string page_status_;
    bool selecting_{};
};

} // namespace pocket_reference
