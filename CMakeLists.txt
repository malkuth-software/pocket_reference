# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Pocket Reference: GUI.Forms' API reference as a program. Its pages are
# generated from a GUI.Forms checkout's docs (POCKET_REFERENCE_DOCS), each
# control page's figure is that control's stock specimen rendered through
# the SDK, and the whole is coded into reference.pack with the coder in
# pack_coder/ (copied from Pocket Library).
cmake_minimum_required(VERSION 3.25)
project(PocketReference VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GUI.Forms, as Plan Paint takes it: the pinned source
# (third_party/gui-forms.lock.json, scripts/fetch-gui-forms.py) built in
# this tree, or an installed SDK found on CMAKE_PREFIX_PATH.
set(POCKET_REFERENCE_TOOLKIT_SOURCE_DIR "" CACHE PATH "Pinned GUI.Forms source, built in this tree")
if(POCKET_REFERENCE_TOOLKIT_SOURCE_DIR)
  set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_ENABLE_HARFBUZZ_TEXT ON CACHE BOOL "" FORCE)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GUI_FORMS_ENABLE_LINUX_HOST ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${POCKET_REFERENCE_TOOLKIT_SOURCE_DIR}" toolkit EXCLUDE_FROM_ALL)
  set(GUIForms_FONT_DIR "${POCKET_REFERENCE_TOOLKIT_SOURCE_DIR}/assets/fonts")
  # The toolkit's add_subdirectory scope keeps the theme embed script's
  # directory to itself, so name it here as well.
  set(GUI_FORMS_THEME_EMBED_SCRIPT "${POCKET_REFERENCE_TOOLKIT_SOURCE_DIR}/cmake/GUIFormsThemeEmbed.cmake")
  set(pocket_reference_default_docs "${POCKET_REFERENCE_TOOLKIT_SOURCE_DIR}")
else()
  # Keep the selected SDK ahead of ambient headers from another SDK.
  set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
  find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application)
  set_property(TARGET GUIForms::Application PROPERTY IMPORTED_NO_SYSTEM TRUE)
  set(pocket_reference_default_docs "")
endif()
if(NOT TARGET stxlib::algorithms)
  find_package(stxlib CONFIG REQUIRED)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(POCKET_REFERENCE_DOCS "${pocket_reference_default_docs}" CACHE PATH
    "The GUI.Forms checkout whose docs/api the pack is made from (the toolkit source by default)")

add_library(pocket_reference_coder STATIC pack_coder/lz_coder.cpp pack_coder/sha256.cpp)
target_compile_features(pocket_reference_coder PUBLIC cxx_std_20)

add_executable(pocket-reference-pack tools/pack_main.cpp)
target_link_libraries(pocket-reference-pack PRIVATE pocket_reference_coder)

# Renders the stock specimen of each control a page shows a figure of.
add_executable(pocket-reference-figures tools/figures_main.cpp)
target_link_libraries(pocket-reference-figures PRIVATE GUIForms::Application)

set(pocket_reference_sources src/main.cpp src/reference_document.cpp src/reference_page.cpp
                             src/reference_browser.cpp)
if(APPLE)
  add_executable(pocket-reference MACOSX_BUNDLE ${pocket_reference_sources})
  set_target_properties(pocket-reference PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.malkuthsoft.pocketreference
    MACOSX_BUNDLE_BUNDLE_NAME "Pocket Reference"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
    BUILD_WITH_INSTALL_RPATH TRUE)
else()
  add_executable(pocket-reference WIN32 ${pocket_reference_sources})
endif()
set_target_properties(pocket-reference PROPERTIES OUTPUT_NAME PocketReference)
target_link_libraries(pocket-reference PRIVATE GUIForms::Application stxlib::algorithms pocket_reference_coder)

# The forms: the window, and the two panes the browser puts in its split.
# The page pane names the application's own control, ReferencePage.
gui_forms_add_form(pocket-reference forms/reference_window.form CLASS ReferenceWindow NAMESPACE pocket_reference
                   DERIVED)
gui_forms_add_form(pocket-reference forms/contents_pane.form CLASS ContentsPane NAMESPACE pocket_reference
                   DERIVED)
gui_forms_add_form(pocket-reference forms/page_pane.form CLASS PagePane NAMESPACE pocket_reference DERIVED
                   TYPES "reference_page,pocket_reference::ReferencePage,scrollable_control,reference_page.hpp")
target_include_directories(pocket-reference PRIVATE src)
gui_forms_add_theme(pocket-reference themes/pocket_reference FUNCTION pocket_reference_theme
                    NAMESPACE pocket_reference
                    FORMS forms/reference_window.form forms/contents_pane.form forms/page_pane.form)

# The pack: the page text from the docs, the figures it names, then the
# coded pack, copied into the bundle's Resources (beside the program
# elsewhere).
if(POCKET_REFERENCE_DOCS)
  set(pocket_reference_text "${CMAKE_CURRENT_BINARY_DIR}/reference.txt")
  set(pocket_reference_figures "${CMAKE_CURRENT_BINARY_DIR}/figures")
  set(pocket_reference_figures_list "${pocket_reference_figures}/figures.txt")
  set(pocket_reference_pack "${CMAKE_CURRENT_BINARY_DIR}/reference.pack")
  file(GLOB pocket_reference_rst CONFIGURE_DEPENDS "${POCKET_REFERENCE_DOCS}/docs/api/*.rst"
       "${POCKET_REFERENCE_DOCS}/docs/api/generated/*.rst")
  add_custom_command(OUTPUT "${pocket_reference_text}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_reference_text.py"
            --gui-forms "${POCKET_REFERENCE_DOCS}" --out "${pocket_reference_text}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_reference_text.py" ${pocket_reference_rst}
            "${POCKET_REFERENCE_DOCS}/manifests/controls.json"
    COMMENT "Pocket Reference: writing the page text"
    VERBATIM)
  add_custom_command(OUTPUT "${pocket_reference_figures_list}"
    COMMAND pocket-reference-figures "${pocket_reference_text}" "${pocket_reference_figures}"
    DEPENDS pocket-reference-figures "${pocket_reference_text}"
    COMMENT "Pocket Reference: rendering the figures"
    VERBATIM)
  add_custom_command(OUTPUT "${pocket_reference_pack}"
    COMMAND pocket-reference-pack "${pocket_reference_text}" "${pocket_reference_figures_list}"
            "${pocket_reference_pack}"
    DEPENDS pocket-reference-pack "${pocket_reference_text}" "${pocket_reference_figures_list}"
    COMMENT "Pocket Reference: coding the pack"
    VERBATIM)
  add_custom_target(pocket-reference-pack-file DEPENDS "${pocket_reference_pack}")
  add_dependencies(pocket-reference pocket-reference-pack-file)
  if(APPLE)
    set(pocket_reference_pack_destination "$<TARGET_BUNDLE_CONTENT_DIR:pocket-reference>/Resources")
  else()
    set(pocket_reference_pack_destination "$<TARGET_FILE_DIR:pocket-reference>")
  endif()
  add_custom_command(TARGET pocket-reference POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${pocket_reference_pack_destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pocket_reference_pack}" "${pocket_reference_pack_destination}/reference.pack"
    VERBATIM)
endif()
