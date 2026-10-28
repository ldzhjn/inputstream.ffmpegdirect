/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "../src/stream/FFmpegStream.h"
#include "../src/utils/Log.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
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
bool useRealDemuxer = false;
std::string baseUrl;
std::string observationFailure;
std::set<std::string> openedResources;
decltype(AVFormatContext::io_open) defaultIOOpen = nullptr;

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

bool GetIntegerSetting(KODI_ADDON_BACKEND_HDL, const char*, int* value)
{
  *value = 0;
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

int ObserveIO(AVFormatContext* context, AVIOContext** io, const char* url, int flags,
              AVDictionary** options)
{
  if (useRealDemuxer)
  {
    // Do not throw through FFmpeg's C frames. Report a callback failure through
    // the normal FFmpeg error path and assert its diagnostic after Open returns.
    std::string resourceUrl(url);
    if (resourceUrl.compare(0, 7, "crypto+") == 0)
      resourceUrl.erase(0, 7);
    const AVDictionaryEntry* ca =
        options ? av_dict_get(*options, "ca_file", nullptr, 0) : nullptr;
    if (resourceUrl.compare(0, baseUrl.size() + 1, baseUrl + '/') != 0)
      observationFailure = "A demuxer request escaped the loopback fixture server";
    else if (!ca || caPath != ca->value)
      observationFailure = "An HLS demuxer resource did not receive the CA bundle";
    if (!observationFailure.empty())
      return AVERROR(EINVAL);

    const int result = defaultIOOpen(context, io, url, flags, options);
    if (result >= 0)
    {
      openedResources.insert(resourceUrl.substr(baseUrl.size()));
      ++configuredOpens;
    }
    return result;
  }

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
  defaultIOOpen = context->io_open;
  context->io_open = ObserveIO;
  ++allocations;
  return context;
}

extern "C" int TestAvformatOpenInput(AVFormatContext** context, const char* url,
                                    const AVInputFormat* format, AVDictionary** options)
{
  ++opens;
  Check(opens <= 2 && allocations == opens, "Unexpected context allocation/open sequence");
  Check(closes == opens - 1, "The preliminary context was not closed before reopening");
  if (useRealDemuxer)
    return avformat_open_input(context, url, format, options);
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
    Check(argc == 2 || argc == 3, "Pass a CA fixture path and optional loopback HLS base URL");
    caPath = argv[1];
    if (argc == 3)
    {
      useRealDemuxer = true;
      baseUrl = argv[2];
      const std::string prefix = "http://127.0.0.1:";
      Check(baseUrl.compare(0, prefix.size(), prefix) == 0,
            "HLS fixture must use the IPv4 loopback interface");
      const std::string port = baseUrl.substr(prefix.size());
      Check(!port.empty() && std::all_of(port.begin(), port.end(),
                                        [](unsigned char c) { return std::isdigit(c); }),
            "HLS fixture URL must contain only a loopback host and port");
      Check(std::stoi(port) > 0 && std::stoi(port) <= 65535, "Invalid loopback fixture port");
    }
    addonCallbacks.get_setting_bool = GetBooleanSetting;
    addonCallbacks.get_setting_int = GetIntegerSetting;
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
      const std::string url = useRealDemuxer ? baseUrl + "/master.m3u8" :
                                               "https://test.invalid/master.m3u8";
      const bool opened = stream.Open(url + "|user-agent=Test&cookies=Test",
                                      "application/x-mpegURL", true, "");
      if (useRealDemuxer)
      {
        Check(observationFailure.empty(), observationFailure.c_str());
        Check(opened, "FFmpeg's HLS demuxer did not open the loopback fixture");
        Check(stream.m_pFormatContext && stream.m_pFormatContext->iformat &&
                  std::strcmp(stream.m_pFormatContext->iformat->name, "hls") == 0,
              "The fixture did not use FFmpeg's real HLS demuxer");
        for (const char* resource : {"/master.m3u8", "/child.m3u8", "/key.bin", "/segment.ts"})
          Check(openedResources.count(resource) == 1,
                "The HLS demuxer did not open every playlist, key and media resource");
      }
      else
      {
        Check(!opened, "The controlled second-open failure was not returned");
        Check(stream.m_pFormatContext == nullptr, "Failed open did not release its context");
      }
    }
    Check(allocations == 2 && opens == 2 && closes == 2,
          "The preliminary open, close, reallocation and second open were not exercised");
    Check(translatedPaths == 2 && (useRealDemuxer ? configuredOpens >= 8 : configuredOpens == 2),
          "TLS configuration was not installed for both format contexts");
    if (useRealDemuxer)
      std::cout << "FFmpegStream HLS: real demuxer opened master, child, key and segment with CA options\n";
    else
      std::cout << "FFmpegStream TLS: initial and recreated contexts received the CA bundle\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FFmpegStream TLS integration test failed: " << error.what() << '\n';
    return 1;
  }
}
