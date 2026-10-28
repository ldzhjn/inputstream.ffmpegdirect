find_path(KODI_INCLUDE_DIR kodi/AddonBase.h)
if(NOT EXISTS "${KODI_INCLUDE_DIR}/kodi/AddonBase.h")
  message(FATAL_ERROR "Set KODI_INCLUDE_DIR to the include directory containing kodi/AddonBase.h")
endif()
find_package(Threads REQUIRED)
pkg_check_modules(STREAM_FFMPEG REQUIRED IMPORTED_TARGET
                  libavformat>=63.1.102 libavcodec>=63.1.102 libavutil>=61.1.102)

add_library(ffmpeg_stream_tls_production OBJECT ../src/stream/FFmpegStream.cpp)
target_compile_definitions(ffmpeg_stream_tls_production PRIVATE
                           TARGET_DARWIN_EMBEDDED TARGET_POSIX
                           avformat_alloc_context=TestAvformatAllocContext
                           avformat_open_input=TestAvformatOpenInput
                           avformat_close_input=TestAvformatCloseInput)
target_include_directories(ffmpeg_stream_tls_production PRIVATE ${KODI_INCLUDE_DIR})
target_link_libraries(ffmpeg_stream_tls_production PRIVATE PkgConfig::STREAM_FFMPEG Threads::Threads)
set_target_properties(ffmpeg_stream_tls_production PROPERTIES
                      CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

add_executable(test_ffmpeg_stream_tls TestFFmpegStreamTls.cpp
               $<TARGET_OBJECTS:ffmpeg_stream_tls_production>
               ../src/stream/DemuxStream.cpp
               ../src/stream/CurlInput.cpp
               ../src/stream/FFmpegLog.cpp
               ../src/stream/url/URL.cpp
               ../src/stream/url/UrlOptions.cpp
               ../src/stream/url/Variant.cpp
               ../src/utils/FilenameUtils.cpp)
target_include_directories(test_ffmpeg_stream_tls PRIVATE ${KODI_INCLUDE_DIR})
target_compile_definitions(test_ffmpeg_stream_tls PRIVATE TARGET_POSIX)
target_link_libraries(test_ffmpeg_stream_tls PRIVATE PkgConfig::STREAM_FFMPEG Threads::Threads)
set_target_properties(test_ffmpeg_stream_tls PROPERTIES
                      CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test-cacert.pem" "Readable CA path fixture\n")
add_test(NAME ffmpeg_stream_tls
         COMMAND test_ffmpeg_stream_tls "${CMAKE_CURRENT_BINARY_DIR}/test-cacert.pem")

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(NOT FFMPEG_EXECUTABLE)
  message(FATAL_ERROR "The HLS demux integration test requires a host ffmpeg executable")
endif()
add_test(NAME ffmpeg_stream_hls
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/RunHlsDemuxTest.py"
                 --ffmpeg "${FFMPEG_EXECUTABLE}"
                 --test-binary $<TARGET_FILE:test_ffmpeg_stream_tls>
                 --ca-file "${CMAKE_CURRENT_BINARY_DIR}/test-cacert.pem")
set_tests_properties(ffmpeg_stream_hls PROPERTIES TIMEOUT 75)
