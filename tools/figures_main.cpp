// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// pocket-reference-figures: renders the figure of every control page.
// Usage: pocket-reference-figures <reference.txt> <figures directory>
//
// Each "fig" line of the page text names a control id (tools/
// make_reference_text.py). Its figure is that control's stock specimen
// (gui_forms/controls/stock_specimens.hpp) in the windows-professional
// theme, light, at rest, captured headless through an OffscreenRaster at
// scale 1 and 2, as GUI.Forms' own preview capture takes it
// (tools/preview_capture). The directory gets <id>@1x.png and <id>@2x.png
// for each, and figures.txt listing them (id, tab, density, tab, file) for
// pocket-reference-pack. A base whose specimen is blank by design
// (StockSpecimen::blank_reason) is shown as it is, since that is its look;
// a type the catalogue has no specimen of (a component, a dialog) gets no
// figure, and the page leaves its figure out.
#include "gui_forms/application.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/controls/stock_specimens.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <stxlib/optional.hpp>

namespace gf = gui_forms;

namespace {

// Idle frames a specimen may take to settle, as GUI.Forms' preview capture
// allows.
constexpr std::size_t maximum_settle_frames = 64U;
// Logical units of the window's backplane kept around the control.
constexpr double figure_margin = 8.0;

// The control ids the page text shows figures of, each once, in order.
std::vector<std::string> figure_ids(const std::filesystem::path &text_path) {
    std::ifstream input(text_path, std::ios::binary);
    std::vector<std::string> ids;
    std::set<std::string> seen;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with("fig\t"))
            continue;
        const std::size_t end = line.find('\t', 4U);
        const std::string id = line.substr(4U, end == std::string::npos ? std::string::npos : end - 4U);
        if (!id.empty() && seen.insert(id).second)
            ids.push_back(id);
    }
    return ids;
}

// A stable ID for a specimen of `control_id`: stable IDs refuse '_'.
gf::StableId specimen_id(std::string_view control_id) {
    std::string segment(control_id);
    std::replace(segment.begin(), segment.end(), '_', '-');
    gf::StableId id("figure." + segment);
    return id;
}

bool paint_frame(gf::Window &window, gf::PaintFramebuffer &framebuffer) {
    window.perform_layout();
    static_cast<void>(window.take_damage());
    const gf::Rect whole{0.0, 0.0, window.client_size().width, window.client_size().height};
    if (!framebuffer.begin(window.image_resources(), whole))
        return false;
    const stxlib::Optional<gf::PaintReceipt> receipt = window.paint(framebuffer.painter(), whole);
    framebuffer.end();
    return receipt.present();
}

bool engine_work_waiting(const gf::Window &window) {
    return window.engine_parts().patterns_waiting() || window.engine_parts().finish_tiles_waiting();
}

bool write_file(const std::filesystem::path &path, const std::vector<std::byte> &bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return !file.fail();
}

// One specimen at one scale, written to `path`. The reason for a refusal
// goes to stderr.
bool capture(const gf::StockSpecimen &row, gf::OffscreenRaster &raster, double scale,
             const std::filesystem::path &path) {
    const gf::StockSpecimenInstance instance = gf::make_stock_specimen(row, specimen_id(row.control_id));
    gf::Window window(instance.root, row.size);
    window.set_active(true);
    gf::PresentationOverrides still{};
    still.colour_scheme.set(gf::ColourScheme::light);
    still.reduced_motion.set(true);
    still.ambient_motion.set(false);
    window.set_presentation_overrides(still);
    // Declared after the window it borrows, so it ends first.
    gf::OffscreenAttachment attached(raster, window);
    window.set_scale(scale);
    window.perform_layout();
    gf::present_stock_specimen(row, instance, window);
    const std::unique_ptr<gf::PaintFramebuffer> framebuffer = window.create_framebuffer(window.client_size(), scale);
    if (framebuffer == nullptr) {
        std::fprintf(stderr, "%s: the raster made no framebuffer\n", std::string(row.control_id).c_str());
        return false;
    }
    gf::FrameTime now = gf::FrameClock::now();
    std::size_t frame = 0U;
    bool painted = paint_frame(window, *framebuffer);
    while (painted && frame < maximum_settle_frames && engine_work_waiting(window)) {
        now += std::chrono::milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(now));
        painted = paint_frame(window, *framebuffer);
        ++frame;
    }
    gf::OffscreenCaptureOptions options{};
    options.scale = scale;
    options.colour_scheme.set(gf::ColourScheme::light);
    options.margin = figure_margin;
    options.include_backplane = true;
    options.include_overlays = true;
    const std::optional<gf::WindowCapture> picture = attached.capture_control(instance.frame, options);
    std::vector<std::byte> png;
    if (!picture.has_value()) {
        std::fprintf(stderr, "%s: the capture was refused\n", std::string(row.control_id).c_str());
        return false;
    }
    if (!gf::encode_window_capture_png(*picture, png)) {
        std::fprintf(stderr, "%s: the PNG encoding was refused\n", std::string(row.control_id).c_str());
        return false;
    }
    if (!write_file(path, png)) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

std::unique_ptr<gf::Application> create_figure_application() {
    gf::install_built_in_effect_registry();
    gf::ApplicationConfig config{};
    config.theme.base = gf::ThemeBase::windows_professional;
    config.theme.effects = &gf::built_in_effect_registry();
    gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
    return std::move(created.application);
}

int run(const std::filesystem::path &text_path, const std::filesystem::path &out) {
    const std::vector<std::string> ids = figure_ids(text_path);
    std::error_code made_error;
    std::filesystem::create_directories(out, made_error);
    if (made_error) {
        std::fprintf(stderr, "cannot make %s: %s\n", out.string().c_str(), made_error.message().c_str());
        return 2;
    }
    // The application before any window: each specimen window joins it.
    const std::unique_ptr<gf::Application> application = create_figure_application();
    if (application == nullptr) {
        std::fprintf(stderr, "pocket-reference-figures: the application was refused\n");
        return 2;
    }
    gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create();
    if (!made.succeeded()) {
        std::fprintf(stderr, "pocket-reference-figures: no raster: %s\n", made.detail().c_str());
        return 2;
    }
    // Declared before every attachment, which borrow it and end first.
    const std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
    std::string listing;
    std::size_t rendered = 0U;
    std::vector<std::string> empty;
    std::size_t failed = 0U;
    for (const std::string &id : ids) {
        const gf::StockSpecimen *row = gf::find_stock_specimen(id);
        if (row == nullptr) {
            empty.push_back(id);
            continue;
        }
        bool saved = true;
        for (int density = 1; density <= 2 && saved; ++density) {
            const std::string file = id + "@" + std::to_string(density) + "x.png";
            try {
                saved = capture(*row, *raster, static_cast<double>(density), out / file);
            } catch (const std::exception &error) {
                std::fprintf(stderr, "%s: %s\n", id.c_str(), error.what());
                saved = false;
            }
            if (saved)
                listing += id + "\t" + std::to_string(density) + "\t" + file + "\n";
        }
        if (saved)
            ++rendered;
        else
            ++failed;
    }
    std::ofstream list(out / "figures.txt", std::ios::binary | std::ios::trunc);
    list << listing;
    list.close();
    if (list.fail()) {
        std::fprintf(stderr, "cannot write %s\n", (out / "figures.txt").string().c_str());
        return 1;
    }
    std::string names;
    for (const std::string &id : empty)
        names += " " + id;
    std::fprintf(stdout, "pocket-reference-figures: %zu of %zu figures rendered, %zu failed; no specimen:%s\n", rendered,
                 ids.size(), failed, names.empty() ? " none" : names.c_str());
    return failed == 0U ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: pocket-reference-figures <reference.txt> <figures directory>\n");
        return 2;
    }
    return run(argv[1], argv[2]);
}
