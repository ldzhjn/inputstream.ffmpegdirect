/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/dict.h>
}

namespace ffmpegdirect
{

// HLS does not propagate ca_file from the master playlist to its child requests.
// Supply it at the IO boundary, while retaining FFmpeg's normal IO implementation.
// This object must outlive its contexts and occupies their otherwise unused opaque field.
class TlsIo
{
public:
  TlsIo() = default;
  TlsIo(const TlsIo&) = delete;
  TlsIo& operator=(const TlsIo&) = delete;
  TlsIo(TlsIo&&) = delete;
  TlsIo& operator=(TlsIo&&) = delete;

  void Configure(AVFormatContext* context, const std::string& caFile)
  {
    if (caFile.empty())
      return;

    m_caFile = caFile;
    if (context->io_open == Open)
      return;
    m_originalOpen = context->io_open;
    context->opaque = this;
    context->io_open = Open;
  }

private:
  static int Open(AVFormatContext* context, AVIOContext** io, const char* url,
                  int flags, AVDictionary** options)
  {
    auto& self = *static_cast<TlsIo*>(context->opaque);
    AVDictionary* localOptions = nullptr;
    AVDictionary** effectiveOptions = options ? options : &localOptions;

    // Include HTTP for redirects to HTTPS, and encrypted HLS segment URLs.
    if (av_stristart(url, "http://", nullptr) || av_stristart(url, "https://", nullptr) ||
        av_stristart(url, "crypto+http://", nullptr) ||
        av_stristart(url, "crypto+https://", nullptr))
    {
      const int result = av_dict_set(effectiveOptions, "ca_file", self.m_caFile.c_str(),
                                     AV_DICT_DONT_OVERWRITE);
      if (result < 0)
      {
        av_dict_free(&localOptions);
        return result;
      }
    }

    const int result = self.m_originalOpen(context, io, url, flags, effectiveOptions);
    av_dict_free(&localOptions);
    return result;
  }

  decltype(AVFormatContext::io_open) m_originalOpen = nullptr;
  std::string m_caFile;
};

} // namespace ffmpegdirect
