cmake_minimum_required(VERSION 3.21)
project(VArpeggio VERSION 0.2.45 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia Xml Svg Pdf PdfWidgets Concurrent Network)
find_package(ALSA REQUIRED)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

qt_add_executable(varpeggio
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/helpmanual.h src/helpmanual.cpp
    src/musicdocument.h src/musicdocument.cpp
    src/pdfrecognizer.h src/pdfrecognizer.cpp
    src/audiverisbridge.h src/audiverisbridge.cpp
    src/musicwidgets.h src/musicwidgets.cpp
    src/synthesizer.h src/synthesizer.cpp
    src/midiinput.h src/midiinput.cpp
)

target_link_libraries(varpeggio PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::Xml Qt6::Svg
    Qt6::Pdf Qt6::PdfWidgets Qt6::Concurrent Qt6::Network ALSA::ALSA
    ${OpenCV_LIBS} ZLIB::ZLIB PkgConfig::SNDFILE
)
target_include_directories(varpeggio PRIVATE ${OpenCV_INCLUDE_DIRS})

target_compile_definitions(varpeggio PRIVATE
    VARPEGGIO_VERSION="${PROJECT_VERSION}"
    VARPEGGIO_LIBEXECDIR="${CMAKE_INSTALL_LIBEXECDIR}/varpeggio"
    VARPEGGIO_DATADIR="${CMAKE_INSTALL_DATADIR}/varpeggio"
)
configure_file(tools/varpeggio_score.py ${CMAKE_BINARY_DIR}/varpeggio_score.py COPYONLY)
file(COPY ${CMAKE_SOURCE_DIR}/tools/python DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/soundfonts DESTINATION ${CMAKE_BINARY_DIR})
install(TARGETS varpeggio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS tools/varpeggio_score.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/varpeggio)
install(DIRECTORY tools/python DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/varpeggio)
install(DIRECTORY soundfonts DESTINATION ${CMAKE_INSTALL_DATADIR}/varpeggio)
install(FILES com.voncloft.VArpeggio.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES com.voncloft.VArpeggio.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/varpeggio)
install(DIRECTORY LICENSES DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/varpeggio)
install(FILES
    tools/python/verovio-6.3.0.dist-info/licenses/COPYING
    tools/python/verovio-6.3.0.dist-info/licenses/COPYING.LESSER
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/varpeggio/verovio
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(varpeggio_tests tests/musicdocument_test.cpp src/musicdocument.h src/musicdocument.cpp)
    target_include_directories(varpeggio_tests PRIVATE src)
    target_link_libraries(varpeggio_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Xml ${OpenCV_LIBS} ZLIB::ZLIB)
    target_include_directories(varpeggio_tests PRIVATE src ${OpenCV_INCLUDE_DIRS})
    add_test(NAME varpeggio_musicdocument COMMAND varpeggio_tests)
    set_tests_properties(varpeggio_musicdocument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(varpeggio_synth_tests tests/synthesizer_test.cpp src/synthesizer.h src/synthesizer.cpp)
    target_include_directories(varpeggio_synth_tests PRIVATE src)
    target_link_libraries(varpeggio_synth_tests PRIVATE Qt6::Core Qt6::Multimedia PkgConfig::SNDFILE)
    add_test(NAME varpeggio_synthesizer COMMAND varpeggio_synth_tests)

    qt_add_executable(varpeggio_pdf_tests tests/pdfrecognizer_test.cpp src/pdfrecognizer.h src/pdfrecognizer.cpp)
    target_include_directories(varpeggio_pdf_tests PRIVATE src ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(varpeggio_pdf_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Pdf ${OpenCV_LIBS})
    add_test(NAME varpeggio_pdf_recognizer COMMAND varpeggio_pdf_tests)
    set_tests_properties(varpeggio_pdf_recognizer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(varpeggio_fingering_tests tests/fingering_test.cpp
        src/musicwidgets.h src/musicwidgets.cpp src/musicdocument.h src/musicdocument.cpp)
    target_include_directories(varpeggio_fingering_tests PRIVATE src ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(varpeggio_fingering_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml ${OpenCV_LIBS} ZLIB::ZLIB)
    add_test(NAME varpeggio_fingering COMMAND varpeggio_fingering_tests)
    set_tests_properties(varpeggio_fingering PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
