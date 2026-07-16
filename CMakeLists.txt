cmake_minimum_required(VERSION 3.21)
project(OpenFDTD_X
    VERSION 1.0.0
    DESCRIPTION "Multi-Domain FDTD GUI (EM / Optical / Acoustic / Underwater)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt6 Widgets is the only hard requirement (classic C++ Qt, NOT QML/Quick).
find_package(Qt6 REQUIRED COMPONENTS Widgets)

option(USE_HDF5   "Enable HDF5 time-series export"            OFF)
option(USE_LIBIGL "Use libigl for STL import + voxelization"  OFF)
option(BUILD_TESTS "Build the .ofd round-trip self test"      ON)

if(USE_HDF5)
    find_package(HDF5 REQUIRED COMPONENTS C)
endif()

if(USE_LIBIGL)
    include(FetchContent)
    set(LIBIGL_GLFW OFF CACHE BOOL "")
    set(LIBIGL_IMGUI OFF CACHE BOOL "")
    set(LIBIGL_OPENGL OFF CACHE BOOL "")
    set(LIBIGL_PNG OFF CACHE BOOL "")
    FetchContent_Declare(libigl
        GIT_REPOSITORY https://github.com/libigl/libigl.git
        GIT_TAG v2.5.0
    )
    FetchContent_MakeAvailable(libigl)
endif()

set(CORE_SOURCES
    src/core/Project.cpp
    src/core/Project.h
    src/core/Domain.h
    src/core/Material.h
    src/core/Geometry.h
    src/core/Source.h
    src/core/MeshAxis.h
    src/core/PostOpts.h
    src/core/GlassCatalog.cpp
    src/core/GlassCatalog.h
    src/core/RoomAcoustics.cpp
    src/core/RoomAcoustics.h

    src/io/OfdIO.cpp
    src/io/OfdIO.h
    src/io/Touchstone.cpp
    src/io/Touchstone.h
    src/io/Tidy3dExporter.cpp
    src/io/Tidy3dExporter.h
    src/io/H5Writer.cpp
    src/io/H5Writer.h
    src/io/EvReader.h
    src/io/StlImporter.cpp
    src/io/StlImporter.h
    src/io/Voxelizer.cpp
    src/io/Voxelizer.h

    src/kernel/Runner.cpp
    src/kernel/Runner.h
)

set(GUI_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/DomainBar.cpp
    src/DomainBar.h
    src/RightDock.cpp
    src/RightDock.h
    src/I18n.cpp
    src/I18n.h

    src/widgets/SectionBox.cpp
    src/widgets/SectionBox.h
    src/widgets/MiniPlot.cpp
    src/widgets/MiniPlot.h
    src/widgets/UnitNav.cpp
    src/widgets/UnitNav.h
    src/widgets/Viewport3D.cpp
    src/widgets/Viewport3D.h
    src/widgets/PlotPanel.cpp
    src/widgets/PlotPanel.h
    src/widgets/LogConsole.cpp
    src/widgets/LogConsole.h
    src/widgets/EvViewer.cpp
    src/widgets/EvViewer.h

    src/tabs/GeneralTab.cpp
    src/tabs/GeneralTab.h
    src/tabs/MeshTab.cpp
    src/tabs/MeshTab.h
    src/tabs/MaterialTab.cpp
    src/tabs/MaterialTab.h
    src/tabs/GeometryTab.cpp
    src/tabs/GeometryTab.h
    src/tabs/SourceTab.cpp
    src/tabs/SourceTab.h
    src/tabs/Post1Tab.cpp
    src/tabs/Post1Tab.h
    src/tabs/Post2Tab.cpp
    src/tabs/Post2Tab.h
    src/tabs/OpticalTab.cpp
    src/tabs/OpticalTab.h
    src/tabs/AcousticTab.cpp
    src/tabs/AcousticTab.h
    src/tabs/UnderwaterTab.cpp
    src/tabs/UnderwaterTab.h
    src/tabs/Tidy3dTab.cpp
    src/tabs/Tidy3dTab.h
    src/tabs/GlassCatalogTab.cpp
    src/tabs/GlassCatalogTab.h
    src/tabs/RoomAcousticsTab.cpp
    src/tabs/RoomAcousticsTab.h

    # オペラ音響分析 (実測RIR分析) — Qt アダプターと分析タブ
    src/acoustics/qt/QtAcousticAdapter.cpp
    src/acoustics/qt/QtAcousticAdapter.h
    src/acoustics/qt/AcousticResultModel.cpp
    src/acoustics/qt/AcousticResultModel.h
    src/tabs/RirAnalysisTab.cpp
    src/tabs/RirAnalysisTab.h

    resources/openfdtd.qrc
)

qt_add_executable(openfdtd_x ${CORE_SOURCES} ${GUI_SOURCES})

target_include_directories(openfdtd_x PRIVATE src)
target_link_libraries(openfdtd_x PRIVATE Qt6::Widgets)

if(USE_HDF5)
    target_link_libraries(openfdtd_x PRIVATE ${HDF5_C_LIBRARIES})
    target_include_directories(openfdtd_x PRIVATE ${HDF5_INCLUDE_DIRS})
    target_compile_definitions(openfdtd_x PRIVATE OFD_USE_HDF5)
endif()

if(USE_LIBIGL)
    target_link_libraries(openfdtd_x PRIVATE igl::core)
    target_compile_definitions(openfdtd_x PRIVATE OFD_USE_LIBIGL)
endif()

if(MSVC)
    target_compile_options(openfdtd_x PRIVATE /utf-8 /W3)
else()
    target_compile_options(openfdtd_x PRIVATE -Wall -Wextra)
endif()

set_target_properties(openfdtd_x PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE    ON
)

# ── Acoustic analysis core (Qt-free, strict C++14) ──────────────────────────
# 音響コアは意図的に C++14 に固定する (プロジェクト全体の C++17 とは独立)。
# Qt には一切リンクしない。
add_library(ofdx_acoustic_core STATIC
    src/acoustics/core/AcousticMetrics.cpp
    src/acoustics/core/BandFilter.cpp
    src/acoustics/core/DirectSoundDetector.cpp
    src/acoustics/core/NoiseFloorEstimator.cpp
    src/acoustics/core/ReflectionDetector.cpp
    src/acoustics/core/RirAnalyzer.cpp
    src/acoustics/core/SchroederDecay.cpp
    src/acoustics/io/WavReader.cpp
    src/acoustics/io/WavWriter.cpp
)
target_include_directories(ofdx_acoustic_core PUBLIC src)

# GUI (QtAcousticAdapter 経由) から C++14 コアを利用する
target_link_libraries(openfdtd_x PRIVATE ofdx_acoustic_core)
set_target_properties(ofdx_acoustic_core PROPERTIES
    AUTOMOC               OFF
    CXX_STANDARD          14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)
if(MSVC)
    target_compile_options(ofdx_acoustic_core PRIVATE /utf-8 /W3)
else()
    target_compile_options(ofdx_acoustic_core PRIVATE -Wall -Wextra)
endif()

# ── Acoustic C API (stable C ABI over the core, C++14) ──────────────────────
add_library(ofdx_acoustic_c_api STATIC
    src/acoustics/c_api/openfdtd_x_acoustics.cpp
    src/acoustics/c_api/openfdtd_x_acoustics.h
)
target_link_libraries(ofdx_acoustic_c_api PUBLIC ofdx_acoustic_core)
set_target_properties(ofdx_acoustic_c_api PROPERTIES
    AUTOMOC               OFF
    CXX_STANDARD          14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)
if(MSVC)
    target_compile_options(ofdx_acoustic_c_api PRIVATE /utf-8 /W3)
else()
    target_compile_options(ofdx_acoustic_c_api PRIVATE -Wall -Wextra)
endif()

# ── Self test: .ofd round-trip against ../data/sample ───────────────────────
if(BUILD_TESTS)
    qt_add_executable(ofdx_selftest ${CORE_SOURCES} tests/selftest.cpp)
    target_include_directories(ofdx_selftest PRIVATE src)
    target_link_libraries(ofdx_selftest PRIVATE Qt6::Widgets)
    set_target_properties(ofdx_selftest PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE    OFF
    )
endif()

# ── Acoustics tests (CTest) ──────────────────────────────────────────────────
if(BUILD_TESTS)
    enable_language(C)  # test_c_api.c は純 C (C99) としてコンパイルする
    enable_testing()

    # C++ 単体テスト (各 tests/acoustics/test_*.cpp → 個別実行ファイル)
    set(OFDX_ACOUSTIC_TESTS
        clarity
        direct_sound
        reflections
        reverberation
        schroeder
        wav
    )
    foreach(_t IN LISTS OFDX_ACOUSTIC_TESTS)
        add_executable(ofdx_test_${_t} tests/acoustics/test_${_t}.cpp)
        target_link_libraries(ofdx_test_${_t} PRIVATE ofdx_acoustic_core)
        set_target_properties(ofdx_test_${_t} PROPERTIES
            AUTOMOC               OFF
            CXX_STANDARD          14
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS        OFF
        )
        add_test(NAME acoustics.${_t} COMMAND ofdx_test_${_t})
    endforeach()

    # C API テスト — 純 C (C99) としてコンパイルされることが検証項目
    add_executable(ofdx_test_c_api tests/acoustics/test_c_api.c)
    target_link_libraries(ofdx_test_c_api PRIVATE ofdx_acoustic_c_api)
    set_target_properties(ofdx_test_c_api PROPERTIES
        AUTOMOC             OFF
        C_STANDARD          99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS        OFF
    )
    add_test(NAME acoustics.c_api COMMAND ofdx_test_c_api)

    # 人工 RIR 生成 CLI (wav はコミットせず実行時に生成する)
    add_executable(ofdx_generate_synthetic_rir
        tests/generators/generate_synthetic_rir.cpp)
    target_link_libraries(ofdx_generate_synthetic_rir PRIVATE ofdx_acoustic_core)
    set_target_properties(ofdx_generate_synthetic_rir PROPERTIES
        AUTOMOC               OFF
        CXX_STANDARD          14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
endif()
