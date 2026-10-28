# TLS IO regression test

This standalone test requires CMake, a C++11 compiler, pkg-config, and the host
FFmpeg development packages for `libavformat` and `libavutil`. It does not require
Kodi or the add-on's cross-compilation toolchain.

From the repository root:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The test checks CA options for HLS-style child requests, redirects and encrypted
segments; existing CA and HTTP options; unchanged non-HTTP requests; null options;
caller-visible IO output, success and failure return values, protocol restrictions
and interrupt callbacks; repeated configuration, copied child contexts and
replacement contexts. Checks remain active in Release builds.

These are callback and option-propagation tests. They do not make network
requests, validate server certificate chains, or test playback.

The replacement-context case exercises `TlsIo` directly. It does not compile
`FFmpegStream.cpp` or cover the retry call site in `FFmpegStream::OpenWithFFmpeg`.
Testing that integration requires the Kodi SDK and stream dependencies, which
are outside this standalone test's scope.
