/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "TlsIo.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible<ffmpegdirect::TlsIo>::value,
              "An installed callback must retain its TlsIo owner");
static_assert(!std::is_copy_assignable<ffmpegdirect::TlsIo>::value,
              "An installed callback must retain its TlsIo owner");
static_assert(!std::is_move_constructible<ffmpegdirect::TlsIo>::value,
              "Moving TlsIo would invalidate context->opaque");
static_assert(!std::is_move_assignable<ffmpegdirect::TlsIo>::value,
              "Moving TlsIo would invalidate context->opaque");

namespace
{
constexpr char CA_FILE[] = "/bundle/system/certs/cacert.pem";
constexpr char CUSTOM_CA_FILE[] = "/custom/ca.pem";
constexpr char HEADERS[] = "User-Agent: Test\r\n";
constexpr char PROTOCOL_WHITELIST[] = "file,http,https,tcp,tls,crypto";
constexpr char PROTOCOL_BLACKLIST[] = "concat";
constexpr int OPEN_FLAGS = AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK;
constexpr int OPEN_RESULT = -123;

int interruptToken = 0;
int calls = 0;
const char* expectedUrl = nullptr;
const char* expectedCa = nullptr;
bool expectHttpOptions = false;

void Check(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void CheckOption(AVDictionary* options, const char* name, const char* expected)
{
  const AVDictionaryEntry* option = av_dict_get(options, name, nullptr, 0);
  Check(expected ? option && std::strcmp(option->value, expected) == 0 : !option,
        std::string("Unexpected option: ") + name);
}

void SetOption(AVDictionary** options, const char* name, const char* value)
{
  Check(av_dict_set(options, name, value, 0) >= 0, "Could not allocate test option");
}

int Interrupt(void* opaque)
{
  return opaque == &interruptToken ? 0 : 1;
}

int OriginalOpen(AVFormatContext* context, AVIOContext** io, const char* url,
                 int flags, AVDictionary** options)
{
  ++calls;
  Check(io != nullptr, "IO output pointer was not forwarded");
  Check(std::strcmp(url, expectedUrl) == 0, "URL was not forwarded");
  Check(flags == OPEN_FLAGS, "IO flags were changed");
  Check(context->interrupt_callback.callback == Interrupt, "Interrupt callback was changed");
  Check(context->interrupt_callback.opaque == &interruptToken, "Interrupt data was changed");
  Check(std::strcmp(context->protocol_whitelist, PROTOCOL_WHITELIST) == 0,
        "Protocol whitelist was changed");
  Check(std::strcmp(context->protocol_blacklist, PROTOCOL_BLACKLIST) == 0,
        "Protocol blacklist was changed");
  Check(options != nullptr, "Null options were not normalized for the original callback");
  CheckOption(*options, "ca_file", expectedCa);
  CheckOption(*options, "tls_verify", expectHttpOptions ? "1" : nullptr);
  CheckOption(*options, "headers", expectHttpOptions ? HEADERS : nullptr);
  CheckOption(*options, "user_agent", expectHttpOptions ? "TestAgent" : nullptr);
  return OPEN_RESULT;
}

void InitializeContext(AVFormatContext& context)
{
  context.io_open = OriginalOpen;
  context.interrupt_callback.callback = Interrupt;
  context.interrupt_callback.opaque = &interruptToken;
  context.protocol_whitelist = const_cast<char*>(PROTOCOL_WHITELIST);
  context.protocol_blacklist = const_cast<char*>(PROTOCOL_BLACKLIST);
}

enum class OptionsMode
{
  HTTP,
  CUSTOM_CA,
  EMPTY_DICTIONARY,
  NULL_POINTER,
};

void RunOpen(AVFormatContext& context, const char* url, const char* ca,
             OptionsMode mode = OptionsMode::HTTP)
{
  expectedUrl = url;
  expectedCa = ca;
  expectHttpOptions = mode == OptionsMode::HTTP || mode == OptionsMode::CUSTOM_CA;
  AVDictionary* options = nullptr;
  try
  {
    if (expectHttpOptions)
    {
      SetOption(&options, "tls_verify", "1");
      SetOption(&options, "headers", HEADERS);
      SetOption(&options, "user_agent", "TestAgent");
    }
    if (mode == OptionsMode::CUSTOM_CA)
      SetOption(&options, "ca_file", CUSTOM_CA_FILE);

    AVIOContext* io = nullptr;
    const int before = calls;
    const int result = context.io_open(&context, &io, url, OPEN_FLAGS,
                                       mode == OptionsMode::NULL_POINTER ? nullptr : &options);
    Check(result == OPEN_RESULT, "Original callback result was changed");
    Check(calls == before + 1, "Original callback was not called exactly once");
    if (mode != OptionsMode::NULL_POINTER)
      CheckOption(options, "ca_file", ca);
  }
  catch (...)
  {
    av_dict_free(&options);
    throw;
  }
  av_dict_free(&options);
}
} // namespace

int main()
{
  try
  {
    AVFormatContext context{};
    InitializeContext(context);
    ffmpegdirect::TlsIo tls;
    tls.Configure(&context, "");
    Check(context.io_open == OriginalOpen && context.opaque == nullptr,
          "An empty CA path must not install a callback");

    tls.Configure(&context, CA_FILE);
    Check(context.io_open != OriginalOpen && context.opaque != nullptr,
          "A CA path did not install the TLS callback");
    RunOpen(context, "https://test.invalid/master.m3u8", CA_FILE);
    RunOpen(context, "https://test.invalid/child.m3u8", CA_FILE);
    RunOpen(context, "https://test.invalid/segment.ts", CA_FILE);
    RunOpen(context, "https://test.invalid/key", CA_FILE);
    RunOpen(context, "http://test.invalid/redirect", CA_FILE);
    RunOpen(context, "crypto+https://test.invalid/segment.ts", CA_FILE);
    RunOpen(context, "crypto+http://test.invalid/segment.ts", CA_FILE);
    RunOpen(context, "HTTPS://test.invalid/master.m3u8", CA_FILE);
    RunOpen(context, "file:///tmp/segment.ts", nullptr);
    RunOpen(context, "udp://test.invalid:1234", nullptr);
    RunOpen(context, "httpsomething://test.invalid/segment.ts", nullptr);
    RunOpen(context, "https://test.invalid/custom", CUSTOM_CA_FILE, OptionsMode::CUSTOM_CA);
    RunOpen(context, "https://test.invalid/empty", CA_FILE, OptionsMode::EMPTY_DICTIONARY);
    RunOpen(context, "https://test.invalid/null", CA_FILE, OptionsMode::NULL_POINTER);

    const auto installedCallback = context.io_open;
    void* const installedOpaque = context.opaque;
    tls.Configure(&context, CA_FILE);
    Check(context.io_open == installedCallback && context.opaque == installedOpaque,
          "Repeated configuration changed callback ownership");
    RunOpen(context, "https://test.invalid/repeated.m3u8", CA_FILE);

    // HLS may copy the parent callback and its opaque data into a child context.
    AVFormatContext child = context;
    RunOpen(child, "https://test.invalid/copied-child.m3u8", CA_FILE);

    AVFormatContext reopened{};
    InitializeContext(reopened);
    tls.Configure(&reopened, CA_FILE);
    RunOpen(reopened, "https://test.invalid/reopened.m3u8", CA_FILE);

    std::cout << "TLS IO: " << calls << " resource opens passed\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "TLS IO test failed: " << error.what() << '\n';
    return 1;
  }
}
