cmake_minimum_required(VERSION 3.21)
project(Keyflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia Xml Svg Pdf PdfWidgets Concurrent)
find_package(ALSA REQUIRED)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc)
find_package(ZLIB REQUIRED)

qt_add_executable(keyflow
    src/main.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/musicdocument.h src/musicdocument.cpp
    src/musicwidgets.h src/musicwidgets.cpp
    src/synthesizer.h src/synthesizer.cpp
    src/midiinput.h src/midiinput.cpp
)

target_link_libraries(keyflow PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::Xml Qt6::Svg
    Qt6::Pdf Qt6::PdfWidgets Qt6::Concurrent ALSA::ALSA
    ${OpenCV_LIBS} ZLIB::ZLIB
)
target_include_directories(keyflow PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(keyflow PRIVATE third_party/tinysoundfont)

target_compile_definitions(keyflow PRIVATE KEYFLOW_VERSION="${PROJECT_VERSION}")
configure_file(tools/keyflow_score.py ${CMAKE_BINARY_DIR}/keyflow_score.py COPYONLY)
file(COPY ${CMAKE_SOURCE_DIR}/tools/python DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/soundfonts DESTINATION ${CMAKE_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/licenses)
configure_file(third_party/tinysoundfont/LICENSE ${CMAKE_BINARY_DIR}/licenses/TinySoundFont-LICENSE COPYONLY)
install(TARGETS keyflow RUNTIME DESTINATION bin)
install(PROGRAMS tools/keyflow_score.py DESTINATION bin)
install(DIRECTORY tools/python DESTINATION bin)
install(DIRECTORY soundfonts DESTINATION bin)
install(FILES third_party/tinysoundfont/LICENSE DESTINATION share/licenses/keyflow RENAME TinySoundFont-LICENSE)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(keyflow_tests tests/musicdocument_test.cpp src/musicdocument.h src/musicdocument.cpp)
    target_include_directories(keyflow_tests PRIVATE src)
    target_link_libraries(keyflow_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Xml ${OpenCV_LIBS} ZLIB::ZLIB)
    target_include_directories(keyflow_tests PRIVATE src ${OpenCV_INCLUDE_DIRS} third_party/tinysoundfont)
    add_test(NAME keyflow_musicdocument COMMAND keyflow_tests)
    set_tests_properties(keyflow_musicdocument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(keyflow_synth_tests tests/synthesizer_test.cpp src/synthesizer.h src/synthesizer.cpp)
    target_include_directories(keyflow_synth_tests PRIVATE src)
    target_include_directories(keyflow_synth_tests PRIVATE third_party/tinysoundfont)
    target_link_libraries(keyflow_synth_tests PRIVATE Qt6::Core Qt6::Multimedia)
    add_test(NAME keyflow_synthesizer COMMAND keyflow_synth_tests)
endif()
