// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "reference_browser.hpp"

#include "reference_icons.hpp"

#include <gui_forms/event/event/event.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace gf = gui_forms;

namespace pocket_reference {
namespace {

std::span<const std::byte> bytes_of(std::span<const std::uint8_t> data) {
    return std::as_bytes(data);
}

char lower_ascii(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool contains_folded(std::string_view text, std::string_view needle) {
    if (needle.empty())
        return true;
    for (std::size_t at = 0U; at + needle.size() <= text.size(); ++at) {
        std::size_t index = 0U;
        while (index < needle.size() && lower_ascii(text[at + index]) == lower_ascii(needle[index]))
            ++index;
        if (index == needle.size())
            return true;
    }
    return false;
}

constexpr std::string_view search_prefix = "search:";

} // namespace

ReferenceBrowser::ReferenceBrowser(Book book, ReferenceWindow &window) : book_(std::move(book)), window_(window) {}

std::shared_ptr<gf::Command> ReferenceBrowser::command(std::string id, std::string label, std::string shortcut) {
    std::shared_ptr<gf::Command> made = std::make_shared<gf::Command>(std::move(id), std::move(label));
    if (!shortcut.empty())
        (*made).set_shortcut(std::move(shortcut));
    gf::on((*made).invoked(), *this, &ReferenceBrowser::on_command);
    commands_.push_back(made);
    return made;
}

void ReferenceBrowser::bind_key(gf::Command &bound, std::uint32_t key, gf::Modifier modifiers) {
    accelerators_.push_back(bound.bind_shortcut(window_, gf::KeyGesture{key, modifiers}));
}

void ReferenceBrowser::attach(std::string_view start) {
    contents_ = ContentsPane::make();
    page_pane_ = PagePane::make();
    page_view_ = (*page_pane_).page_view;
    (*page_view_).set_listener(this);
    std::shared_ptr<gf::SplitContainer> split = window_.reference_split;
    (*(*split).first_panel()).add_child(contents_);
    (*(*split).second_panel()).add_child(page_pane_);
    (*split).fixed_panel = gf::SplitFixedPanel::first;
    (*split).splitter_distance = 290.0;
    (*split).splitter_width = 5.0;
    (*(*page_pane_).page_find_bar).visible = false;

    build_menus();
    build_status();
    load_images();
    fill_tree();

    gf::on((*(*contents_).contents_tree).selection_changed(), *this, &ReferenceBrowser::on_tree_selection);
    gf::on((*(*contents_).contents_filter).text_changed(), *this, &ReferenceBrowser::on_filter);
    gf::on((*window_.reference_address).segment_activated(), *this, &ReferenceBrowser::on_crumb);
    gf::on((*window_.reference_search).search_requested(), *this, &ReferenceBrowser::on_search);
    gf::on((*window_.reference_address).edit_committed(), *this, &ReferenceBrowser::on_address);
    gf::on((*(*page_pane_).page_find_text).text_changed(), *this, &ReferenceBrowser::on_find_text);
    gf::on((*(*page_pane_).page_match_case).checked_changed(), *this, &ReferenceBrowser::on_match_case);
    navigate(start, true);
}

void ReferenceBrowser::build_menus() {
    std::shared_ptr<gf::Command> back = command("back", "&Back", "Alt+Left");
    std::shared_ptr<gf::Command> forward = command("forward", "&Forward", "Alt+Right");
    std::shared_ptr<gf::Command> home = command("home", "&Home", "Alt+Home");
    std::shared_ptr<gf::Command> find = command("find", "&Find in Page", "Ctrl+F");
    std::shared_ptr<gf::Command> find_next = command("find-next", "Find &Next", "F3");
    std::shared_ptr<gf::Command> find_previous = command("find-previous", "Find &Previous", "Shift+F3");
    std::shared_ptr<gf::Command> find_close = command("find-close", "Close Find", "");
    std::shared_ptr<gf::Command> search = command("search", "&Search", "Ctrl+E");
    std::shared_ptr<gf::Command> about = command("about", "&About Pocket Reference", "");
    std::shared_ptr<gf::Command> contents = command("contents", "&Contents", "");
    bind_key(*back, gf::PhysicalKey::left, gf::Modifier::alt);
    bind_key(*forward, gf::PhysicalKey::right, gf::Modifier::alt);
    bind_key(*home, gf::PhysicalKey::home, gf::Modifier::alt);
    bind_key(*find, gf::PhysicalKey::f, gf::Modifier::control);
    bind_key(*find, gf::PhysicalKey::f, gf::Modifier::meta);
    bind_key(*find_next, gf::PhysicalKey::f3, gf::Modifier::none);
    bind_key(*find_previous, gf::PhysicalKey::f3, gf::Modifier::shift);
    bind_key(*find_close, gf::PhysicalKey::escape, gf::Modifier::none);
    (*window_.reference_back).bind(*back);
    (*window_.reference_forward).bind(*forward);
    (*window_.reference_home).bind(*home);
    (*window_.reference_find).bind(*find);
    (*(*page_pane_).page_find_next).bind(*find_next);
    (*(*page_pane_).page_find_previous).bind(*find_previous);
    (*(*page_pane_).page_find_close).bind(*find_close);

    std::vector<gf::MenuStripItemSpec> menus;
    gf::MenuStripItemSpec edit{};
    edit.stable_id = "menu.edit";
    edit.text = "&Edit";
    edit.items.push_back(gf::MenuItemSpec{"find", gf::MenuItemKind::command, find, "&Find in Page"});
    edit.items.push_back(gf::MenuItemSpec{"find-next", gf::MenuItemKind::command, find_next, "Find &Next"});
    edit.items.push_back(gf::MenuItemSpec{"find-previous", gf::MenuItemKind::command, find_previous, "Find &Previous"});
    edit.items.push_back(gf::MenuItemSpec{"search", gf::MenuItemKind::command, search, "&Search"});
    menus.push_back(std::move(edit));
    gf::MenuStripItemSpec view{};
    view.stable_id = "menu.view";
    view.text = "&View";
    view.items.push_back(gf::MenuItemSpec{"contents", gf::MenuItemKind::command, contents, "&Contents Pane"});
    menus.push_back(std::move(view));
    gf::MenuStripItemSpec go{};
    go.stable_id = "menu.go";
    go.text = "&Go";
    go.items.push_back(gf::MenuItemSpec{"back", gf::MenuItemKind::command, back, "&Back"});
    go.items.push_back(gf::MenuItemSpec{"forward", gf::MenuItemKind::command, forward, "&Forward"});
    go.items.push_back(gf::MenuItemSpec{"home", gf::MenuItemKind::command, home, "&Home"});
    menus.push_back(std::move(go));
    gf::MenuStripItemSpec help{};
    help.stable_id = "menu.help";
    help.text = "&Help";
    help.items.push_back(gf::MenuItemSpec{"about", gf::MenuItemKind::command, about, "&About Pocket Reference"});
    menus.push_back(std::move(help));
    (*window_.reference_menu).set_items(std::move(menus));
}

void ReferenceBrowser::build_status() {
    std::vector<gf::ToolStripItemSpec> items;
    gf::ToolStripItemSpec text{};
    text.stable_id = "status.text";
    text.kind = gf::ToolStripItemKind::label;
    text.text = "Ready";
    text.spring = true;
    items.push_back(std::move(text));
    gf::ToolStripItemSpec source{};
    source.stable_id = "status.source";
    source.kind = gf::ToolStripItemKind::label;
    items.push_back(std::move(source));
    gf::ToolStripItemSpec pack{};
    pack.stable_id = "status.pack";
    pack.kind = gf::ToolStripItemKind::label;
    pack.text = "GUI.Forms " + book_.version() + " · " + book_.commit() + " · " +
                std::to_string(book_.page_count()) + " pages";
    items.push_back(std::move(pack));
    (*window_.reference_status).set_items(std::move(items));
}

void ReferenceBrowser::load_images() {
    tree_images_ = std::make_shared<gf::ImageList>(window_, gf::Size{16.0, 16.0});
    figure_images_ = std::make_shared<gf::ImageList>(window_, gf::Size{16.0, 16.0});
    (*page_view_).set_figures(&book_, figure_images_);
    static_cast<void>((*tree_images_).add_png("root", bytes_of(icons::root_png), 2.0));
    static_cast<void>((*tree_images_).add_png("book", bytes_of(icons::book_png), 2.0));
    static_cast<void>((*tree_images_).add_png("control", bytes_of(icons::control_png), 2.0));
    static_cast<void>((*tree_images_).add_png("type", bytes_of(icons::type_png), 2.0));
    static_cast<void>((*tree_images_).add_png("page", bytes_of(icons::page_png), 2.0));
    (*(*contents_).contents_tree).image_list = tree_images_;
    static_cast<void>((*tree_images_).add_png("back", bytes_of(icons::back_png), 2.0));
    static_cast<void>((*tree_images_).add_png("forward", bytes_of(icons::forward_png), 2.0));
    static_cast<void>((*tree_images_).add_png("home", bytes_of(icons::home_png), 2.0));
    static_cast<void>((*tree_images_).add_png("find", bytes_of(icons::find_png), 2.0));
    (*window_.reference_back).set_image_list(tree_images_);
    (*window_.reference_back).set_image_key("back");
    (*window_.reference_forward).set_image_list(tree_images_);
    (*window_.reference_forward).set_image_key("forward");
    (*window_.reference_home).set_image_list(tree_images_);
    (*window_.reference_home).set_image_key("home");
    (*window_.reference_find).set_image_list(tree_images_);
    (*window_.reference_find).set_image_key("find");
    (*window_.reference_home).text_image_relation = gf::TextImageRelation::image_before_text;
    (*window_.reference_find).text_image_relation = gf::TextImageRelation::image_before_text;
}

void ReferenceBrowser::fill_tree() {
    const std::vector<ContentsRow> &rows = book_.contents();
    std::vector<bool> shown(rows.size(), filter_.empty());
    if (!filter_.empty()) {
        // A matching row shows, with every row above it on its path.
        std::vector<std::size_t> path;
        for (std::size_t index = 0U; index < rows.size(); ++index) {
            while (!path.empty() && rows[path.back()].depth >= rows[index].depth)
                path.pop_back();
            if (contains_folded(rows[index].label, filter_)) {
                shown[index] = true;
                for (const std::size_t above : path)
                    shown[above] = true;
            }
            path.push_back(index);
        }
    }
    std::vector<gf::TreeViewItem> items;
    row_pages_.clear();
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        if (!shown[index])
            continue;
        std::size_t next = index + 1U;
        while (next < rows.size() && !shown[next])
            ++next;
        const bool expandable = next < rows.size() && rows[next].depth > rows[index].depth;
        gf::TreeViewItem item{};
        item.stable_id = "row." + std::to_string(index);
        item.text = rows[index].label;
        item.depth = rows[index].depth;
        item.expandable = expandable;
        item.expanded = expandable && (!filter_.empty() || rows[index].depth == 0U || rows[index].id == "group/controls");
        item.image_key = rows[index].icon;
        items.push_back(std::move(item));
        row_pages_.push_back(rows[index].id);
    }
    (*(*contents_).contents_tree).set_items(std::move(items));
}

void ReferenceBrowser::select_in_tree(std::string_view page_id) {
    const std::vector<ContentsRow> &rows = book_.contents();
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        if (rows[index].id != page_id)
            continue;
        const std::string row_id = "row." + std::to_string(index);
        // Open every row above it, so it can show.
        std::vector<std::size_t> path;
        for (std::size_t above = 0U; above < index; ++above) {
            while (!path.empty() && rows[path.back()].depth >= rows[above].depth)
                path.pop_back();
            path.push_back(above);
        }
        while (!path.empty() && rows[path.back()].depth >= rows[index].depth)
            path.pop_back();
        selecting_ = true;
        for (const std::size_t above : path)
            (*(*contents_).contents_tree).set_expanded("row." + std::to_string(above), true);
        (*(*contents_).contents_tree).selected_id = row_id;
        selecting_ = false;
        return;
    }
    // A page the contents do not hold (a search): nothing is selected.
    selecting_ = true;
    (*(*contents_).contents_tree).selected_id = std::string();
    selecting_ = false;
}

Page ReferenceBrowser::search_page(std::string_view query) const {
    const std::vector<SearchHit> hits = book_.search(query, 200U);
    Page page{};
    page.id = std::string(search_prefix) + std::string(query);
    page.title = "Search: " + std::string(query);
    page.crumbs.push_back(Crumb{"home", "GUI.Forms " + book_.version()});
    page.crumbs.push_back(Crumb{page.id, "Search: " + std::string(query)});
    Block heading{};
    heading.kind = BlockKind::heading1;
    heading.runs.push_back(Run{"Search results", false, false, false, no_link});
    page.blocks.push_back(std::move(heading));
    Block summary{};
    summary.kind = BlockKind::paragraph;
    summary.runs.push_back(Run{std::to_string(hits.size()) + (hits.size() == 1U ? " page holds " : " pages hold "),
                               false, false, false, no_link});
    summary.runs.push_back(Run{std::string(query), true, false, false, no_link});
    summary.runs.push_back(Run{hits.empty() ? "." : "; pages whose title holds it come first.", false, false, false,
                               no_link});
    page.blocks.push_back(std::move(summary));
    for (const SearchHit &hit : hits) {
        Block card{};
        card.kind = BlockKind::card;
        card.target = hit.id;
        page.links.push_back(hit.id);
        card.runs.push_back(Run{hit.title, false, true, false, page.links.size() - 1U});
        std::vector<Run> purpose;
        purpose.push_back(Run{std::to_string(hit.count) + (hit.count == 1U ? " match" : " matches"), false, false,
                              false, no_link});
        card.cells.push_back(std::move(purpose));
        page.blocks.push_back(std::move(card));
    }
    return page;
}

void ReferenceBrowser::navigate(std::string_view target, bool record) {
    std::string_view page_id = target;
    std::string_view anchor{};
    const std::size_t hash = target.find('#');
    if (hash != std::string_view::npos && !target.starts_with(search_prefix)) {
        page_id = target.substr(0U, hash);
        anchor = target.substr(hash + 1U);
    }
    if (record) {
        if (!history_.empty())
            history_.resize(position_ + 1U);
        if (history_.empty() || history_.back() != target)
            history_.push_back(std::string(target));
        position_ = history_.size() - 1U;
    }
    if (page_id == current_page_ && !page_id.empty()) {
        if (!anchor.empty())
            static_cast<void>((*page_view_).scroll_to_anchor(anchor));
        else
            (*page_view_).scroll_to_top();
        update_commands();
        return;
    }
    if (page_id.starts_with(search_prefix))
        show_page(search_page(page_id.substr(search_prefix.size())), anchor);
    else
        show_page(book_.page(page_id), anchor);
    update_commands();
}

void ReferenceBrowser::show_page(Page page, std::string_view anchor) {
    current_page_ = page.id;
    // Segment ids are stable IDs, which page ids are not: a segment is
    // named by its place, and crumb_targets_ holds the page it opens.
    std::vector<gf::BreadcrumbSegment> segments;
    crumb_targets_.clear();
    for (const Crumb &crumb : page.crumbs) {
        segments.push_back(
            gf::BreadcrumbSegment{"crumb." + std::to_string(crumb_targets_.size()), crumb.label, crumb.label, true});
        crumb_targets_.push_back(crumb.id);
    }
    (*window_.reference_address).set_segments(std::move(segments));
    window_.text = page.title + " - Pocket Reference";
    (*window_.reference_status).set_item_text("status.source", page.source);
    page_status_ = page.title;
    (*page_view_).set_page(std::move(page));
    if (!anchor.empty())
        static_cast<void>((*page_view_).scroll_to_anchor(anchor));
    set_status(page_status_);
    select_in_tree(current_page_);
    run_find();
}

void ReferenceBrowser::go_back() {
    if (position_ == 0U || history_.empty())
        return;
    --position_;
    const std::string target = history_[position_];
    navigate(target, false);
}

void ReferenceBrowser::go_forward() {
    if (position_ + 1U >= history_.size())
        return;
    ++position_;
    const std::string target = history_[position_];
    navigate(target, false);
}

void ReferenceBrowser::update_commands() {
    for (const std::shared_ptr<gf::Command> &each : commands_) {
        if ((*each).stable_id() == "back")
            (*each).set_enabled(position_ > 0U);
        else if ((*each).stable_id() == "forward")
            (*each).set_enabled(position_ + 1U < history_.size());
    }
}

void ReferenceBrowser::set_status(std::string_view text) {
    (*window_.reference_status).set_item_text("status.text", std::string(text));
}

void ReferenceBrowser::show_find(std::string_view query) {
    (*(*page_pane_).page_find_bar).visible = true;
    if (!query.empty())
        (*(*page_pane_).page_find_text).text = std::string(query);
    window_.request_focus((*page_pane_).page_find_text);
    run_find();
}

void ReferenceBrowser::hide_find() {
    (*(*page_pane_).page_find_bar).visible = false;
    (*page_view_).clear_find();
    window_.request_focus(page_view_);
}

void ReferenceBrowser::run_find() {
    if (!(*(*page_pane_).page_find_bar).visible.read())
        return;
    const std::string query = (*(*page_pane_).page_find_text).text.read();
    const bool match_case = (*(*page_pane_).page_match_case).checked.read();
    const std::size_t count = (*page_view_).find(query, match_case);
    std::string shown;
    if (query.empty())
        shown = "";
    else if (count == 0U)
        shown = "No matches";
    else
        shown = std::to_string((*page_view_).find_position()) + " of " + std::to_string(count);
    (*(*page_pane_).page_find_count).text = shown;
}

void ReferenceBrowser::set_filter(std::string_view filter) {
    (*(*contents_).contents_filter).text = std::string(filter);
    on_filter(std::string(filter));
}

void ReferenceBrowser::open_link(std::string_view target) { navigate(target, true); }

void ReferenceBrowser::hover_link(std::string_view target) {
    if (target.empty()) {
        set_status(page_status_);
        return;
    }
    std::string_view page_id = target.substr(0U, target.find('#'));
    const std::string_view title = book_.title_of(page_id);
    std::string shown = std::string(target);
    if (!title.empty())
        shown = std::string(title) + "  (" + std::string(target) + ")";
    set_status(shown);
}

void ReferenceBrowser::on_command(const gf::CommandInvocation &invocation) {
    const std::string &id = invocation.command_id;
    if (id == "back")
        go_back();
    else if (id == "forward")
        go_forward();
    else if (id == "home" || id == "about")
        navigate("home", true);
    else if (id == "find")
        show_find("");
    else if (id == "contents")
        (*window_.reference_split).set_first_collapsed(!(*window_.reference_split).first_collapsed());
    else if (id == "find-close")
        hide_find();
    else if (id == "search")
        window_.request_focus(window_.reference_search);
    else if (id == "find-next" || id == "find-previous") {
        if (!(*(*page_pane_).page_find_bar).visible.read()) {
            show_find("");
            return;
        }
        if (id == "find-next")
            (*page_view_).find_next();
        else
            (*page_view_).find_previous();
        const std::size_t count = (*page_view_).find_count();
        if (count > 0U)
            (*(*page_pane_).page_find_count).text =
                std::to_string((*page_view_).find_position()) + " of " + std::to_string(count);
    }
}

void ReferenceBrowser::on_tree_selection(const gf::TreeSelectionChange &change) {
    if (selecting_ || !change.current_id.starts_with("row."))
        return;
    const std::size_t row = static_cast<std::size_t>(std::stoul(change.current_id.substr(4U)));
    const std::vector<ContentsRow> &rows = book_.contents();
    if (row < rows.size() && rows[row].id != current_page_)
        navigate(rows[row].id, true);
}

void ReferenceBrowser::on_crumb(const std::string &id) {
    if (!id.starts_with("crumb."))
        return;
    const std::size_t index = static_cast<std::size_t>(std::stoul(id.substr(6U)));
    if (index < crumb_targets_.size()) {
        const std::string target = crumb_targets_[index];
        navigate(target, true);
    }
}

void ReferenceBrowser::on_address(const std::string &text) {
    // A typed address: a page id, a type's name, else a search.
    if (text.empty())
        return;
    const std::string_view typed(text);
    const std::string_view page_id = typed.substr(0U, typed.find('#'));
    if (book_.has_page(page_id)) {
        navigate(typed, true);
        return;
    }
    const std::string as_type = "type/" + text;
    const std::string_view as_type_view(as_type);
    if (book_.has_page(as_type_view.substr(0U, as_type_view.find('#')))) {
        navigate(as_type, true);
        return;
    }
    navigate(std::string(search_prefix) + text, true);
}

void ReferenceBrowser::on_search(const std::string &query) {
    if (query.empty())
        return;
    navigate(std::string(search_prefix) + query, true);
}

void ReferenceBrowser::on_filter(const std::string &text) {
    filter_ = text;
    fill_tree();
    select_in_tree(current_page_);
}

void ReferenceBrowser::on_find_text(const std::string &) { run_find(); }

void ReferenceBrowser::on_match_case(bool) { run_find(); }

} // namespace pocket_reference
