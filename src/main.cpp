// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Pocket Reference: GUI.Forms' API reference, read from a pack.
//
//   PocketReference [--pack <file>] [--page <id>]
//   PocketReference --capture <file.png> [--size <w>x<h>] [--scale <s>]
//                   [--page <id>] [--find <text>] [--filter <text>]
//                   [--hover <target>] [--search <query>]
//
// Without --pack, the pack beside the program (the bundle's Resources on
// macOS). --capture draws the window off screen into a PNG and exits.
#include "pocket_reference_theme.hpp"  // generated from themes/pocket_reference
#include "reference_browser.hpp"
#include "reference_page.hpp"

#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace gf = gui_forms;

namespace {

struct Options final {
    std::filesystem::path pack;
    std::filesystem::path capture;
    std::string page{"home"};
    std::string find;
    std::string filter;
    std::string hover;
    std::string search;
    gf::Size size{1180.0, 748.0};
    double scale{1.0};
};

std::filesystem::path executable_directory(const char *argv0) {
#ifdef __APPLE__
    std::array<char, 4096> buffer{};
    std::uint32_t length = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &length) == 0)
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data())).parent_path();
#endif
    return std::filesystem::absolute(std::filesystem::path(argv0)).parent_path();
}

std::filesystem::path default_pack(const char *argv0) {
    const std::filesystem::path here = executable_directory(argv0);
    const std::filesystem::path bundled = here / ".." / "Resources" / "reference.pack";
    if (std::filesystem::exists(bundled))
        return bundled;
    return here / "reference.pack";
}

Options read_options(int argc, char **argv) {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view name(argv[index]);
        if (index + 1 >= argc)
            throw std::runtime_error("Missing a value after " + std::string(name) + ".");
        const std::string value(argv[++index]);
        if (name == "--pack")
            options.pack = value;
        else if (name == "--capture")
            options.capture = value;
        else if (name == "--page")
            options.page = value;
        else if (name == "--find")
            options.find = value;
        else if (name == "--filter")
            options.filter = value;
        else if (name == "--hover")
            options.hover = value;
        else if (name == "--search")
            options.search = value;
        else if (name == "--scale")
            options.scale = std::stod(value);
        else if (name == "--size") {
            const std::size_t cross = value.find('x');
            if (cross == std::string::npos)
                throw std::runtime_error("--size takes <width>x<height>.");
            options.size = gf::Size{std::stod(value.substr(0U, cross)), std::stod(value.substr(cross + 1U))};
        } else
            throw std::runtime_error("Unknown option " + std::string(name) + ".");
    }
    if (options.pack.empty())
        options.pack = default_pack(argv[0]);
    return options;
}

// Draws the window off screen at the options' size and scale into a PNG.
int capture(gf::Window &window, const Options &options) {
    gf::OffscreenRasterCreateResult made = gf::OffscreenRaster::create();
    std::unique_ptr<gf::OffscreenRaster> raster = made.take_raster();
    if (raster == nullptr)
        throw std::runtime_error("Cannot make the off-screen raster.");
    gf::OffscreenAttachment attached(*raster, window);
    window.resize(options.size);
    gf::OffscreenCaptureOptions capture_options{};
    capture_options.scale = options.scale;
    const std::optional<gf::WindowCapture> picture = attached.capture_window(capture_options);
    if (!picture.has_value())
        throw std::runtime_error("The capture failed.");
    std::vector<std::byte> png;
    if (!gf::encode_window_capture_png(*picture, png))
        throw std::runtime_error("Cannot encode the capture.");
    std::ofstream output(options.capture, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!output)
        throw std::runtime_error("Cannot write " + options.capture.string() + ".");
    std::cout << options.capture.string() << '\n';
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = read_options(argc, argv);
        pocket_reference::Book book = pocket_reference::Book::open(options.pack);
        // The application first: its theme is the windows-professional base
        // with Pocket Reference's package, which may name the page's parts.
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::windows_professional;
        config.theme.packages.push_back(pocket_reference::pocket_reference_theme());
        const std::array<gf::ControlAnatomy, 1> controls{{pocket_reference::ReferencePage::page_anatomy()}};
        config.theme.controls = std::span<const gf::ControlAnatomy>(controls);
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        for (const gf::ThemeCompileDiagnostic &finding : created.findings)
            std::cerr << "Pocket Reference theme: " << finding.code << ' ' << finding.file << ':' << finding.line
                      << ':' << finding.column << ' ' << finding.detail << '\n';
        if (!created)
            throw std::runtime_error("Cannot create the application.");
        int exit_code = 1;
        {
            gf::Application &application = *created.application;
            std::unique_ptr<pocket_reference::ReferenceWindow> window =
                std::make_unique<pocket_reference::ReferenceWindow>(application);
            std::unique_ptr<pocket_reference::ReferenceBrowser> browser =
                std::make_unique<pocket_reference::ReferenceBrowser>(std::move(book), *window);
            (*browser).attach(options.page);
            if (!options.search.empty())
                (*browser).navigate("search:" + options.search, true);
            if (!options.filter.empty())
                (*browser).set_filter(options.filter);
            if (!options.find.empty())
                (*browser).show_find(options.find);
            if (!options.hover.empty())
                (*browser).show_hover(options.hover);
            if (!options.capture.empty()) {
                exit_code = capture(*window, options);
            } else {
                const gf::ApplicationResult result = application.run(std::move(window));
                if (result.callback_exception)
                    std::rethrow_exception(result.callback_exception);
                exit_code = result.accepted() ? 0 : 1;
            }
            browser.reset();
        }
        const gf::ApplicationResult ended = gf::Application::shutdown();
        static_cast<void>(ended);
        return exit_code;
    } catch (const std::exception &e) {
        std::cerr << "Pocket Reference: " << e.what() << '\n';
        return 1;
    }
}
