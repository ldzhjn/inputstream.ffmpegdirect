#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Generate encrypted HLS and run the real-demuxer test against a loopback server."""

import argparse
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import urllib.parse


def generate_hls(ffmpeg, directory):
    """Create synthetic MPEG-2 video and an AES-128 key without external inputs."""
    key = directory / "key.bin"
    key.write_bytes(bytes(range(16)))
    key_info = directory / "key.info"
    key_info.write_text("key.bin\n" + str(key) + "\n" + "0" * 32 + "\n")
    subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
         "-f", "lavfi", "-i", "color=c=black:s=16x16:r=25", "-t", "1", "-an",
         "-c:v", "mpeg2video", "-g", "25", "-bf", "0", "-f", "hls",
         "-hls_time", "1", "-hls_list_size", "0", "-hls_segment_filename",
         str(directory / "segment%03d.ts"), "-hls_key_info_file", str(key_info),
         str(directory / "child.m3u8")],
        check=True, timeout=30,
    )
    child = (directory / "child.m3u8").read_text()
    child = child.replace("segment000.ts", "segment.ts")
    return {
        "/master.m3u8": (
            b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100000\nchild.m3u8\n",
            "application/vnd.apple.mpegurl",
        ),
        "/child.m3u8": (child.encode(), "application/vnd.apple.mpegurl"),
        "/key.bin": (key.read_bytes(), "application/octet-stream"),
        "/segment.ts": ((directory / "segment000.ts").read_bytes(), "video/mp2t"),
    }


def run_test(args, resources):
    """Serve only the generated resources and require every HLS layer to be read."""
    requested = set()
    request_lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            resource = resources.get(path)
            if resource is None:
                self.send_error(404)
                return
            with request_lock:
                requested.add(path)
            data, content_type = resource
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, format, *values):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        base_url = "http://127.0.0.1:" + str(server.server_port)
        result = subprocess.run(
            [args.test_binary, args.ca_file, base_url],
            capture_output=True, text=True, timeout=30,
        )
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            return 1
        with request_lock:
            missing = resources.keys() - requested
        if missing:
            raise RuntimeError("HLS server resources were not requested: " + ", ".join(sorted(missing)))
        print("Loopback HLS server served master, child playlist, AES key and encrypted segment")
        return 0
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--test-binary", required=True)
    parser.add_argument("--ca-file", required=True)
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="ffmpegdirect-hls-") as temporary:
            resources = generate_hls(args.ffmpeg, pathlib.Path(temporary))
            return run_test(args, resources)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("HLS demux integration test failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
