cmake_minimum_required(VERSION 3.12)
project(ffmpegdirect_tls_tests LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavutil)

enable_testing()
add_executable(test_tls_io TestTlsIo.cpp)
set_target_properties(test_tls_io PROPERTIES
                      CXX_STANDARD 11
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(test_tls_io PRIVATE ../src/utils)
target_link_libraries(test_tls_io PRIVATE PkgConfig::FFMPEG)
add_test(NAME tls_io COMMAND test_tls_io)
