/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "../src/stream/FFmpegStream.h"
#include "../src/utils/Log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
std::string caPath;
int allocations = 0;
int opens = 0;
int closes = 0;
int configuredOpens = 0;
int translatedPaths = 0;

void Check(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

bool GetBooleanSetting(KODI_ADDON_BACKEND_HDL, const char*, bool* value)
{
  // Disable fast opening so the real stream code closes and recreates the context.
  *value = false;
  return true;
}

char* TranslatePath(void*, const char* source)
{
  Check(std::strcmp(source, "special://xbmc/system/certs/cacert.pem") == 0,
        "Unexpected CA bundle special path");
  ++translatedPaths;
  char* result = static_cast<char*>(std::malloc(caPath.size() + 1));
  Check(result != nullptr, "Could not allocate translated CA path");
  std::memcpy(result, caPath.c_str(), caPath.size() + 1);
  return result;
}

void FreeString(KODI_ADDON_BACKEND_HDL, char* string)
{
  std::free(string);
}

int ObserveIO(AVFormatContext*, AVIOContext** io, const char* url, int,
              AVDictionary** options)
{
  Check(std::strcmp(url, "https://test.invalid/master.m3u8") == 0,
        "Unexpected resource URL");
  const AVDictionaryEntry* ca = av_dict_get(*options, "ca_file", nullptr, 0);
  Check(ca != nullptr && caPath == ca->value,
        "An FFmpegStream open did not receive the CA bundle");
  ++configuredOpens;
  *io = nullptr;
  return 0;
}

AddonToKodiFuncTable_kodi_addon addonCallbacks{};
AddonToKodiFuncTable_kodi_filesystem filesystemCallbacks{};
AddonToKodiFuncTable_Addon kodiCallbacks{};
AddonGlobalInterface kodiInterface{};
} // namespace

AddonGlobalInterface* kodi::addon::CPrivateBase::m_interface = &kodiInterface;

void Log(LogLevel, const char*, ...)
{
}

// Only FFmpegStream.cpp redirects these API calls to the test shims. All other
// FFmpeg APIs and the stream's own implementation are compiled and linked normally.
extern "C" AVFormatContext* TestAvformatAllocContext()
{
  AVFormatContext* context = avformat_alloc_context();
  Check(context != nullptr, "Could not allocate format context");
  context->io_open = ObserveIO;
  ++allocations;
  return context;
}

extern "C" int TestAvformatOpenInput(AVFormatContext** context, const char* url,
                                    const AVInputFormat*, AVDictionary** options)
{
  ++opens;
  Check(opens <= 2 && allocations == opens, "Unexpected context allocation/open sequence");
  Check(closes == opens - 1, "The preliminary context was not closed before reopening");
  AVIOContext* io = nullptr;
  Check((*context)->io_open(*context, &io, url, AVIO_FLAG_READ, options) == 0,
        "Configured IO callback failed");
  // The preliminary open succeeds. Stop the second open after checking its IO
  // configuration, before public Open() proceeds to unrelated stream discovery.
  return opens == 1 ? 0 : AVERROR(EIO);
}

extern "C" void TestAvformatCloseInput(AVFormatContext** context)
{
  if (*context)
    ++closes;
  avformat_close_input(context);
}

int main(int argc, char** argv)
{
  try
  {
    Check(argc == 2, "Pass a readable CA bundle fixture path");
    caPath = argv[1];
    addonCallbacks.get_setting_bool = GetBooleanSetting;
    filesystemCallbacks.translate_special_protocol = TranslatePath;
    kodiCallbacks.free_string = FreeString;
    kodiCallbacks.kodi_addon = &addonCallbacks;
    kodiCallbacks.kodi_filesystem = &filesystemCallbacks;
    kodiInterface.toKodi = &kodiCallbacks;

    ffmpegdirect::Properties properties;
    properties.m_openMode = ffmpegdirect::OpenMode::FFMPEG;
    properties.m_manifestType = "hls";
    ffmpegdirect::HttpProxy proxy;
    {
      ffmpegdirect::FFmpegStream stream(nullptr, properties, proxy);
      Check(!stream.Open("https://test.invalid/master.m3u8|user-agent=Test&cookies=Test",
                         "application/x-mpegURL", true, ""),
            "The controlled second-open failure was not returned");
      Check(stream.m_pFormatContext == nullptr, "Failed open did not release its context");
    }
    Check(allocations == 2 && opens == 2 && closes == 2,
          "The preliminary open, close, reallocation and second open were not exercised");
    Check(configuredOpens == 2 && translatedPaths == 2,
          "TLS configuration was not installed for both format contexts");
    std::cout << "FFmpegStream TLS: initial and recreated contexts received the CA bundle\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FFmpegStream TLS integration test failed: " << error.what() << '\n';
    return 1;
  }
}
