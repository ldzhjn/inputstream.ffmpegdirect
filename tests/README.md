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

The helper tests callbacks and option propagation without making network
requests, validating server certificate chains, or testing playback.

An optional integration target also compiles the production `FFmpegStream.cpp`
and its stream dependencies. It calls `FFmpegStream::Open()` with fast manifest
opening disabled, so the first open succeeds, its context is closed, and a new
context is opened. Test replacements for FFmpeg's format I/O functions check
that both opens receive the CA configuration; the second open then returns an
error to stop before media analysis. This covers the `ConfigureTlsIO()` call in
the actual close/reallocate/reopen path.

A second integration case lets FFmpeg's real HLS demuxer read a master playlist,
child playlist, AES-128 key and encrypted MPEG-2 segment. A Python controller
generates synthetic media using the host `ffmpeg` executable and serves only
those four resources on `127.0.0.1` at an ephemeral port. The test checks that
every demuxer I/O callback receives `ca_file`, and that the server received all
four resource requests. The controller stops the server and removes the
generated fixtures after the test.

The integration targets run on POSIX hosts (macOS/Linux). They need a C++20
compiler, the Kodi SDK headers, matching host FFmpeg 9 development libraries,
Python 3.8 or newer, and a host `ffmpeg` executable with the `lavfi` color source,
`mpeg2video` encoder and HLS muxer with AES-128 support.
`KODI_INCLUDE_DIR` is the directory containing `kodi/AddonBase.h`:

```sh
cmake -S tests -B build-stream-tests -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_STREAM_INTEGRATION_TESTS=ON -DKODI_INCLUDE_DIR=/path/to/kodi/include
cmake --build build-stream-tests
ctest --test-dir build-stream-tests --output-on-failure
```

Both integration cases enable the Apple embedded TLS setup on the host and
supply Kodi callbacks and a readable CA-path fixture. The context-recreation
case uses no network; the HLS case makes loopback HTTP requests only. Neither
requires a running Kodi instance or validates a real TLS certificate chain.
