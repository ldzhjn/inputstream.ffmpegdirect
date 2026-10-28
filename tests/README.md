# TLS IO regression test

The helper test requires CMake, a C++11 compiler, pkg-config, and the host
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

An optional integration target also compiles the production `FFmpegStream.cpp`
and its stream dependencies. It calls `FFmpegStream::Open()` with fast manifest
opening disabled, so the first open succeeds, its context is closed, and a new
context is opened. Test replacements for FFmpeg's format I/O functions check
that both opens receive the CA configuration; the second open then returns an
error to stop before media analysis. This covers the `ConfigureTlsIO()` call in
the actual close/reallocate/reopen path.

The integration target runs on POSIX hosts (macOS/Linux). It needs a C++20
compiler, the Kodi SDK headers, and matching host `libavcodec` headers and
libraries in addition to the helper dependencies.
`KODI_INCLUDE_DIR` is the directory containing `kodi/AddonBase.h`:

```sh
cmake -S tests -B build-stream-tests -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_STREAM_INTEGRATION_TESTS=ON -DKODI_INCLUDE_DIR=/path/to/kodi/include
cmake --build build-stream-tests
ctest --test-dir build-stream-tests --output-on-failure
```

The integration target enables the Apple embedded TLS setup on the host and
supplies Kodi callbacks and a readable CA-path fixture. It does not require a
running Kodi instance, contact a server, or validate a real certificate chain.
