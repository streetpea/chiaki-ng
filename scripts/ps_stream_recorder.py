#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PlayStation Stream Recorder - chiaki-ng Python Interface

Connects to a PlayStation 4/5 on the local network and records the
Remote Play stream (video + audio) to a file using FFmpeg.

Requirements (Fedora):
    sudo dnf install python3 ffmpeg
    # Build chiaki-ng first to get libchiaki.so:
    #   cd chiaki-ng && mkdir build && cd build
    #   cmake -DCMAKE_BUILD_TYPE=Release ..
    #   make -j$(nproc)
    # The library will be at build/lib/libchiaki.so

Usage:
    # 1) Discover your PlayStation on the network
    python3 ps_stream_recorder.py discover --host 192.168.1.X

    # 2) Register this device with your PlayStation (one-time)
    #    Go to PS Settings > Remote Play > Link Device to get a PIN
    python3 ps_stream_recorder.py register \\
        --host 192.168.1.X \\
        --psn-account-id <base64_account_id> \\
        --pin 12345678

    # 3) Record the stream
    python3 ps_stream_recorder.py record \\
        --host 192.168.1.X \\
        --regist-key <hex_regist_key> \\
        --morning <hex_morning> \\
        --output recording.mp4 \\
        --duration 60

    # Or use a config file
    python3 ps_stream_recorder.py record --config ps_config.json --output recording.mp4

License: AGPL-3.0-only-OpenSSL (same as chiaki-ng)
"""

import argparse
import base64
import ctypes
import ctypes.util
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Opus decoder wrapper (for decoding audio frames to PCM)
# ---------------------------------------------------------------------------

_opus_lib = None
_opus_lib_searched = False


def _load_opus_lib():
    """Load libopus.so for Opus decoding."""
    global _opus_lib, _opus_lib_searched
    if _opus_lib_searched:
        return _opus_lib
    _opus_lib_searched = True
    for name in ["libopus.so.0", "libopus.so", "opus"]:
        try:
            _opus_lib = ctypes.CDLL(name)
            return _opus_lib
        except OSError:
            continue
    found = ctypes.util.find_library("opus")
    if found:
        try:
            _opus_lib = ctypes.CDLL(found)
            return _opus_lib
        except OSError:
            pass
    return None


class OpusDecoder:
    """Decode Opus packets to PCM int16 samples using libopus."""

    def __init__(self, sample_rate=48000, channels=2):
        self._lib = _load_opus_lib()
        if not self._lib:
            raise RuntimeError(
                "libopus.so not found. Install it with: sudo dnf install opus"
            )
        self._lib.opus_decoder_create.restype = ctypes.c_void_p
        self._lib.opus_decoder_create.argtypes = [
            ctypes.c_int32, ctypes.c_int, ctypes.POINTER(ctypes.c_int)
        ]
        self._lib.opus_decode.restype = ctypes.c_int
        self._lib.opus_decode.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int32,
            ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int,
        ]
        self._lib.opus_decoder_destroy.restype = None
        self._lib.opus_decoder_destroy.argtypes = [ctypes.c_void_p]

        self._channels = channels
        self._max_frame_size = 5760  # max Opus frame size at 48kHz
        self._pcm_buf = (ctypes.c_int16 * (self._max_frame_size * channels))()

        error = ctypes.c_int(0)
        self._decoder = self._lib.opus_decoder_create(
            ctypes.c_int32(sample_rate), ctypes.c_int(channels),
            ctypes.byref(error),
        )
        if error.value != 0 or not self._decoder:
            raise RuntimeError(f"opus_decoder_create failed (error {error.value})")

    def decode(self, opus_data_ptr, opus_len):
        """Decode an Opus packet. Returns PCM bytes (int16 LE) or None."""
        samples = self._lib.opus_decode(
            self._decoder, opus_data_ptr, ctypes.c_int32(opus_len),
            self._pcm_buf, ctypes.c_int(self._max_frame_size), ctypes.c_int(0),
        )
        if samples <= 0:
            return None
        nbytes = samples * self._channels * 2
        return ctypes.string_at(self._pcm_buf, nbytes)

    def __del__(self):
        if hasattr(self, '_decoder') and self._decoder and hasattr(self, '_lib') and self._lib:
            try:
                self._lib.opus_decoder_destroy(self._decoder)
            except Exception:
                pass

# ---------------------------------------------------------------------------
# Constants from chiaki-ng headers
# ---------------------------------------------------------------------------

# Discovery ports
DISCOVERY_PORT_PS4 = 987
DISCOVERY_PORT_PS5 = 9302
DISCOVERY_PROTOCOL_VERSION_PS4 = "00020020"
DISCOVERY_PROTOCOL_VERSION_PS5 = "00030010"

# Session
SESSION_PORT = 9295
SESSION_AUTH_SIZE = 0x10
PSN_ACCOUNT_ID_SIZE = 8
RPCRYPT_KEY_SIZE = 0x10

# Video codecs
CODEC_H264 = 0
CODEC_H265 = 1
CODEC_H265_HDR = 2

# Error codes
ERR_SUCCESS = 0

# Log levels
LOG_ERROR = (1 << 0)
LOG_WARNING = (1 << 1)
LOG_INFO = (1 << 2)
LOG_VERBOSE = (1 << 3)
LOG_DEBUG = (1 << 4)
LOG_ALL = (1 << 5) - 1

# Targets
TARGET_PS4_UNKNOWN = 0
TARGET_PS4_8 = 800
TARGET_PS4_9 = 900
TARGET_PS4_10 = 1000
TARGET_PS5_UNKNOWN = 1000000
TARGET_PS5_1 = 1000100

# Event types
EVENT_CONNECTED = 0
EVENT_LOGIN_PIN_REQUEST = 1
EVENT_QUIT = 9

# Quit reasons
QUIT_REASON_NONE = 0
QUIT_REASON_STOPPED = 1

# Audio/Video disable
NONE_DISABLED = 0

# Video buffer padding
VIDEO_BUFFER_PADDING_SIZE = 64


# ---------------------------------------------------------------------------
# Pure-Python Discovery (no library needed)
# ---------------------------------------------------------------------------

def discover_consoles(host, timeout=3.0):
    """
    Send PS4 and PS5 discovery packets via UDP and collect responses.
    Works without libchiaki — uses the simple HTTP-like text protocol.
    """
    results = []

    for port, proto_ver, console_type in [
        (DISCOVERY_PORT_PS4, DISCOVERY_PROTOCOL_VERSION_PS4, "PS4"),
        (DISCOVERY_PORT_PS5, DISCOVERY_PROTOCOL_VERSION_PS5, "PS5"),
    ]:
        pkt = f"SRCH * HTTP/1.1\ndevice-discovery-protocol-version:{proto_ver}\n"

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(timeout)

            # Bind to an ephemeral port in the chiaki range (9303-9319)
            bound = False
            for bind_port in range(9303, 9320):
                try:
                    sock.bind(("", bind_port))
                    bound = True
                    break
                except OSError:
                    continue
            if not bound:
                sock.bind(("", 0))

            sock.sendto(pkt.encode("utf-8"), (host, port))

            while True:
                try:
                    data, addr = sock.recvfrom(4096)
                    response = _parse_discovery_response(data.decode("utf-8", errors="replace"), addr[0])
                    if response:
                        response["expected_type"] = console_type
                        results.append(response)
                except socket.timeout:
                    break
        except Exception as e:
            print(f"[!] Discovery error for {console_type} on port {port}: {e}", file=sys.stderr)
        finally:
            sock.close()

    return results


def _parse_discovery_response(text, addr):
    """Parse the HTTP-like discovery response from a PlayStation."""
    lines = text.strip().split("\n")
    if not lines:
        return None

    # First line: "HTTP/1.1 200 Ok" or "HTTP/1.1 620 Server Standby"
    status_line = lines[0].strip()
    parts = status_line.split(None, 2)
    if len(parts) < 2:
        return None

    try:
        status_code = int(parts[1])
    except ValueError:
        return None

    info = {
        "host_addr": addr,
        "status_code": status_code,
        "state": "ready" if status_code == 200 else ("standby" if status_code == 620 else "unknown"),
    }

    for line in lines[1:]:
        line = line.strip()
        if ":" in line:
            key, _, value = line.partition(":")
            info[key.strip().lower().replace("-", "_")] = value.strip()

    return info


def wakeup_console(host, regist_key_hex, ps5=False):
    """
    Send a wakeup packet to bring the console out of standby.

    :param host: IP address of the console
    :param regist_key_hex: registration key as hex string
    :param ps5: True for PS5, False for PS4
    """
    if ps5:
        port = DISCOVERY_PORT_PS5
        proto_ver = DISCOVERY_PROTOCOL_VERSION_PS5
    else:
        port = DISCOVERY_PORT_PS4
        proto_ver = DISCOVERY_PROTOCOL_VERSION_PS4

    # user_credential is the regist_key interpreted as hex
    user_credential = int(regist_key_hex, 16) if regist_key_hex else 0

    pkt = (
        "WAKEUP * HTTP/1.1\n"
        "client-type:vr\n"
        "auth-type:R\n"
        "model:w\n"
        "app-type:r\n"
        f"user-credential:{user_credential}\n"
        f"device-discovery-protocol-version:{proto_ver}\n"
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        sock.sendto(pkt.encode("utf-8"), (host, port))
        print(f"[+] Wakeup packet sent to {host}:{port}")
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# libchiaki ctypes bindings
# ---------------------------------------------------------------------------

_lib = None
_lib_lock = threading.Lock()


def _find_libchiaki():
    """Search for libchiaki.so in common locations."""
    search_paths = [
        # Build directory (relative to this script)
        Path(__file__).resolve().parent.parent / "build" / "lib" / "libchiaki.so",
        # Flatpak / system-wide
        Path("/usr/lib64/libchiaki.so"),
        Path("/usr/lib/libchiaki.so"),
        Path("/usr/local/lib64/libchiaki.so"),
        Path("/usr/local/lib/libchiaki.so"),
    ]

    # Also check CHIAKI_LIB_PATH env var
    env_path = os.environ.get("CHIAKI_LIB_PATH")
    if env_path:
        search_paths.insert(0, Path(env_path))

    for p in search_paths:
        if p.exists():
            return str(p)

    # Try system library search
    found = ctypes.util.find_library("chiaki")
    if found:
        return found

    return None


def load_libchiaki(lib_path=None):
    """Load libchiaki.so and set up function signatures."""
    global _lib
    with _lib_lock:
        if _lib is not None:
            return _lib

        if lib_path is None:
            lib_path = _find_libchiaki()

        if lib_path is None:
            raise RuntimeError(
                "Could not find libchiaki.so. Build chiaki-ng first:\n"
                "  cd chiaki-ng && mkdir -p build && cd build\n"
                "  cmake -DCMAKE_BUILD_TYPE=Release ..\n"
                "  make -j$(nproc)\n"
                "Or set CHIAKI_LIB_PATH=/path/to/libchiaki.so"
            )

        print(f"[+] Loading libchiaki from: {lib_path}")
        _lib = ctypes.CDLL(lib_path)

        # chiaki_lib_init
        _lib.chiaki_lib_init.restype = ctypes.c_int
        _lib.chiaki_lib_init.argtypes = []

        # chiaki_error_string
        _lib.chiaki_error_string.restype = ctypes.c_char_p
        _lib.chiaki_error_string.argtypes = [ctypes.c_int]

        # chiaki_log_init
        _lib.chiaki_log_init.restype = None
        _lib.chiaki_log_init.argtypes = [
            ctypes.c_void_p,  # ChiakiLog*
            ctypes.c_uint32,  # level_mask
            ctypes.c_void_p,  # callback
            ctypes.c_void_p,  # user
        ]

        # chiaki_log_cb_print
        _lib.chiaki_log_cb_print.restype = None

        # Initialize the library
        err = _lib.chiaki_lib_init()
        if err != ERR_SUCCESS:
            raise RuntimeError(f"chiaki_lib_init failed: {_lib.chiaki_error_string(err).decode()}")

        return _lib


# ---------------------------------------------------------------------------
# Struct definitions for ctypes (matching C struct layouts on x86_64 Linux)
# ---------------------------------------------------------------------------

class ChiakiLog(ctypes.Structure):
    _fields_ = [
        ("level_mask", ctypes.c_uint32),
        ("_pad0", ctypes.c_uint8 * 4),  # padding for alignment
        ("cb", ctypes.c_void_p),        # function pointer
        ("user", ctypes.c_void_p),
    ]


class ChiakiConnectVideoProfile(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_uint),
        ("height", ctypes.c_uint),
        ("max_fps", ctypes.c_uint),
        ("bitrate", ctypes.c_uint),
        ("codec", ctypes.c_int),  # ChiakiCodec enum
    ]


class ChiakiConnectInfo(ctypes.Structure):
    _fields_ = [
        ("ps5", ctypes.c_bool),
        ("_pad0", ctypes.c_uint8 * 7),                     # align to pointer
        ("host", ctypes.c_char_p),
        ("regist_key", ctypes.c_char * SESSION_AUTH_SIZE),
        ("morning", ctypes.c_uint8 * RPCRYPT_KEY_SIZE),
        ("video_profile", ChiakiConnectVideoProfile),
        ("video_profile_auto_downgrade", ctypes.c_bool),
        ("enable_keyboard", ctypes.c_bool),
        ("enable_dualsense", ctypes.c_bool),
        ("_pad1", ctypes.c_uint8),                          # enum padding
        ("audio_video_disabled", ctypes.c_int),
        ("auto_regist", ctypes.c_bool),
        ("_pad2", ctypes.c_uint8 * 7),                     # align to pointer
        ("holepunch_session", ctypes.c_void_p),
        ("rudp_sock", ctypes.c_void_p),
        ("psn_account_id", ctypes.c_uint8 * PSN_ACCOUNT_ID_SIZE),
        ("packet_loss_max", ctypes.c_double),
        ("enable_idr_on_fec_failure", ctypes.c_bool),
    ]


# ---------------------------------------------------------------------------
# Stream recording using FFmpeg pipe
# ---------------------------------------------------------------------------

class StreamRecorder:
    """
    Records a PlayStation Remote Play stream by wrapping chiaki-ng's
    libchiaki through ctypes.

    Video frames (H.264 / H.265 NAL units) and audio frames (Opus)
    are piped to FFmpeg for muxing into a container file.
    """

    def __init__(self, host, regist_key, morning,
                 ps5=False, codec=CODEC_H264,
                 width=1920, height=1080, fps=60, bitrate=15000,
                 output="recording.mp4", duration=None,
                 psn_account_id=None, lib_path=None,
                 verbose=False):
        self.host = host
        self.regist_key = regist_key  # bytes, 16 bytes
        self.morning = morning        # bytes, 16 bytes
        self.ps5 = ps5
        self.codec = codec
        self.width = width
        self.height = height
        self.fps = fps
        self.bitrate = bitrate
        self.output = output
        self.duration = duration
        self.psn_account_id = psn_account_id or bytes(PSN_ACCOUNT_ID_SIZE)
        self.verbose = verbose

        self._lib = load_libchiaki(lib_path)
        self._stop_event = threading.Event()
        self._tmpdir = None
        self._video_file = None
        self._audio_file = None
        self._video_frames = 0
        self._audio_frames = 0
        self._start_time = None
        self._log_buf = None
        self._audio_channels = 2
        self._audio_rate = 48000
        self._opus_decoder = None

        # Callback references (prevent GC)
        self._event_cb_ref = None
        self._video_cb_ref = None
        self._audio_header_cb_ref = None
        self._audio_frame_cb_ref = None

    def _setup_recording(self):
        """Create temp directory and files for raw stream data."""
        self._tmpdir = tempfile.mkdtemp(prefix="chiaki_rec_")
        codec_ext = "h264" if self.codec == CODEC_H264 else "h265"
        self._video_file = os.path.join(self._tmpdir, f"video.{codec_ext}")
        self._audio_file = os.path.join(self._tmpdir, "audio.pcm")

    def _mux_output(self):
        """Mux raw video and decoded PCM audio into the final output file."""
        codec_name = "h264" if self.codec == CODEC_H264 else "hevc"

        cmd = ["ffmpeg", "-y"]

        # Video input
        cmd.extend([
            "-f", codec_name,
            "-framerate", str(self.fps),
            "-i", self._video_file,
        ])

        # Audio input (decoded PCM, if available)
        has_audio = (self._audio_file
                     and os.path.exists(self._audio_file)
                     and os.path.getsize(self._audio_file) > 0)
        if has_audio:
            cmd.extend([
                "-f", "s16le",
                "-ar", str(self._audio_rate),
                "-ac", str(self._audio_channels),
                "-i", self._audio_file,
            ])

        # Output options
        cmd.extend(["-c:v", "copy"])
        if has_audio:
            cmd.extend(["-c:a", "aac", "-b:a", "192k", "-shortest"])
        cmd.append(self.output)

        print(f"[+] Muxing with FFmpeg: {' '.join(cmd)}")
        result = subprocess.run(
            cmd,
            stdout=None if self.verbose else subprocess.PIPE,
            stderr=None if self.verbose else subprocess.PIPE,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace") if result.stderr else ""
            print(f"[!] FFmpeg muxing failed (exit {result.returncode})", file=sys.stderr)
            if stderr:
                # Show last few lines of stderr
                for line in stderr.strip().splitlines()[-10:]:
                    print(f"    {line}", file=sys.stderr)
            raise RuntimeError("FFmpeg muxing failed")

    def _cleanup(self):
        """Clean up temp files."""
        if self._opus_decoder:
            del self._opus_decoder
            self._opus_decoder = None
        for f in [self._video_file, self._audio_file]:
            if f and os.path.exists(f):
                try:
                    os.unlink(f)
                except OSError:
                    pass
        if self._tmpdir and os.path.exists(self._tmpdir):
            try:
                os.rmdir(self._tmpdir)
            except OSError:
                pass

    def _make_event_callback(self):
        """Create the C callback for session events."""
        CALLBACK_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)

        def event_cb(event_ptr, user_ptr):
            if not event_ptr:
                return
            # Read the event type (first field, uint32)
            event_type = ctypes.cast(event_ptr, ctypes.POINTER(ctypes.c_int))[0]

            if event_type == EVENT_CONNECTED:
                print("[+] Connected to PlayStation!")
                self._start_time = time.time()

            elif event_type == EVENT_LOGIN_PIN_REQUEST:
                print("[!] Login PIN requested by console. Use --pin if needed.")

            elif event_type == EVENT_QUIT:
                # Quit reason is right after the event type
                quit_reason = ctypes.cast(
                    ctypes.c_void_p(event_ptr + ctypes.sizeof(ctypes.c_int)),
                    ctypes.POINTER(ctypes.c_int)
                )[0]
                reason_str = "unknown"
                try:
                    reason_str = self._lib.chiaki_quit_reason_string(quit_reason).decode()
                except Exception:
                    pass
                print(f"[+] Session ended: {reason_str}")
                self._stop_event.set()

        self._event_cb_ref = CALLBACK_TYPE(event_cb)
        return self._event_cb_ref

    def _make_video_callback(self, video_fd):
        """Create the C callback for video samples (H.264/H.265 NAL units)."""
        CALLBACK_TYPE = ctypes.CFUNCTYPE(
            ctypes.c_bool,
            ctypes.POINTER(ctypes.c_uint8),  # buf
            ctypes.c_size_t,                   # buf_size
            ctypes.c_int32,                    # frames_lost
            ctypes.c_bool,                     # frame_recovered
            ctypes.c_void_p,                   # user
        )

        def video_cb(buf, buf_size, frames_lost, frame_recovered, user):
            try:
                data = ctypes.string_at(buf, buf_size)
                os.write(video_fd, data)
                self._video_frames += 1
                if self._video_frames % 300 == 0:
                    elapsed = time.time() - (self._start_time or time.time())
                    print(f"  [video] {self._video_frames} frames ({elapsed:.1f}s)")
                return True
            except Exception as e:
                print(f"[!] Video callback error: {e}", file=sys.stderr)
                return False

        self._video_cb_ref = CALLBACK_TYPE(video_cb)
        return self._video_cb_ref

    def _make_audio_callbacks(self, audio_fd):
        """Create the C callbacks for audio (Opus frames decoded to PCM)."""
        HEADER_CB_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)
        FRAME_CB_TYPE = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_void_p)

        def audio_header_cb(header_ptr, user):
            if header_ptr:
                channels = ctypes.cast(header_ptr, ctypes.POINTER(ctypes.c_uint8))[0]
                bits = ctypes.cast(header_ptr, ctypes.POINTER(ctypes.c_uint8))[1]
                rate_bytes = ctypes.string_at(ctypes.c_void_p(header_ptr + 4), 4)
                rate = struct.unpack("<I", rate_bytes)[0]
                print(f"  [audio] Stream info: {channels}ch, {bits}bit, {rate}Hz")
                self._audio_channels = channels if channels else 2
                self._audio_rate = rate if rate else 48000
                # Initialize Opus decoder now that we know the audio params
                try:
                    self._opus_decoder = OpusDecoder(
                        sample_rate=self._audio_rate,
                        channels=self._audio_channels,
                    )
                    print(f"  [audio] Opus decoder initialized ({self._audio_rate}Hz, {self._audio_channels}ch)")
                except Exception as e:
                    print(f"  [!] Opus decoder init failed: {e} (audio will be skipped)")
                    self._opus_decoder = None

        def audio_frame_cb(buf, buf_size, user):
            try:
                if self._opus_decoder:
                    pcm_data = self._opus_decoder.decode(buf, buf_size)
                    if pcm_data:
                        os.write(audio_fd, pcm_data)
                self._audio_frames += 1
            except Exception as e:
                print(f"[!] Audio callback error: {e}", file=sys.stderr)

        self._audio_header_cb_ref = HEADER_CB_TYPE(audio_header_cb)
        self._audio_frame_cb_ref = FRAME_CB_TYPE(audio_frame_cb)
        return self._audio_header_cb_ref, self._audio_frame_cb_ref

    def record(self):
        """
        Main recording function. Connects to the PlayStation and records
        the stream to the output file.
        """
        print(f"[+] PlayStation Stream Recorder")
        print(f"    Host: {self.host}")
        print(f"    Console: {'PS5' if self.ps5 else 'PS4'}")
        print(f"    Resolution: {self.width}x{self.height} @ {self.fps}fps")
        print(f"    Codec: {'H.265' if self.codec != CODEC_H264 else 'H.264'}")
        print(f"    Output: {self.output}")
        if self.duration:
            print(f"    Duration: {self.duration}s")
        print()

        # Set up temp files for raw stream data
        self._setup_recording()

        try:
            # Open temp files for writing (no blocking, no deadlocks)
            video_fd = os.open(self._video_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
            audio_fd = os.open(self._audio_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)

            # --- Initialize chiaki session ---
            # Allocate a large buffer for the opaque ChiakiSession struct
            # (it's very large with many nested structs; 256KB is safe)
            session_buf = ctypes.create_string_buffer(256 * 1024)

            # Set up logging
            log = ChiakiLog()
            log_level = LOG_ALL if self.verbose else (LOG_INFO | LOG_WARNING | LOG_ERROR)
            self._lib.chiaki_log_init(
                ctypes.byref(log),
                ctypes.c_uint32(log_level),
                ctypes.cast(self._lib.chiaki_log_cb_print, ctypes.c_void_p),
                None,
            )

            # Set up connect info
            connect_info = ChiakiConnectInfo()
            ctypes.memset(ctypes.byref(connect_info), 0, ctypes.sizeof(connect_info))

            connect_info.ps5 = self.ps5
            connect_info.host = self.host.encode("utf-8")

            # regist_key: 16 bytes, null-padded
            rk = self.regist_key[:SESSION_AUTH_SIZE].ljust(SESSION_AUTH_SIZE, b'\x00')
            ctypes.memmove(connect_info.regist_key, rk, SESSION_AUTH_SIZE)

            # morning: 16 bytes random nonce
            m = self.morning[:RPCRYPT_KEY_SIZE]
            ctypes.memmove(connect_info.morning, m, RPCRYPT_KEY_SIZE)

            # Video profile
            connect_info.video_profile.width = self.width
            connect_info.video_profile.height = self.height
            connect_info.video_profile.max_fps = self.fps
            connect_info.video_profile.bitrate = self.bitrate
            connect_info.video_profile.codec = self.codec if self.ps5 else CODEC_H264

            connect_info.video_profile_auto_downgrade = True
            connect_info.enable_keyboard = False
            connect_info.enable_dualsense = self.ps5
            connect_info.audio_video_disabled = NONE_DISABLED
            connect_info.auto_regist = False
            connect_info.holepunch_session = None
            connect_info.rudp_sock = None
            connect_info.packet_loss_max = 0.05
            connect_info.enable_idr_on_fec_failure = True

            # PSN Account ID
            if self.psn_account_id and len(self.psn_account_id) == PSN_ACCOUNT_ID_SIZE:
                ctypes.memmove(connect_info.psn_account_id, self.psn_account_id, PSN_ACCOUNT_ID_SIZE)

            # --- Initialize session ---
            print("[+] Initializing chiaki session...")
            err = self._lib.chiaki_session_init(
                ctypes.cast(session_buf, ctypes.c_void_p),
                ctypes.byref(connect_info),
                ctypes.byref(log),
            )
            if err != ERR_SUCCESS:
                err_str = self._lib.chiaki_error_string(err).decode()
                raise RuntimeError(f"chiaki_session_init failed: {err_str}")

            session_ptr = ctypes.cast(session_buf, ctypes.c_void_p)

            # --- Set callbacks ---
            event_cb = self._make_event_callback()
            self._lib.chiaki_session_set_event_cb(session_ptr, event_cb, None)

            video_cb = self._make_video_callback(video_fd)
            self._lib.chiaki_session_set_video_sample_cb(session_ptr, video_cb, None)

            # Audio sink struct: { void *user, header_cb, frame_cb }
            audio_header_cb, audio_frame_cb = self._make_audio_callbacks(audio_fd)
            # ChiakiAudioSink: 3 pointers
            audio_sink = (ctypes.c_void_p * 3)()
            audio_sink[0] = None  # user
            audio_sink[1] = ctypes.cast(audio_header_cb, ctypes.c_void_p)
            audio_sink[2] = ctypes.cast(audio_frame_cb, ctypes.c_void_p)
            self._lib.chiaki_session_set_audio_sink(session_ptr, ctypes.byref(audio_sink))

            # --- Start session ---
            print("[+] Starting session (connecting to console)...")
            err = self._lib.chiaki_session_start(session_ptr)
            if err != ERR_SUCCESS:
                err_str = self._lib.chiaki_error_string(err).decode()
                self._lib.chiaki_session_fini(session_ptr)
                raise RuntimeError(f"chiaki_session_start failed: {err_str}")

            print("[+] Session started. Recording... (Ctrl+C to stop)")

            # --- Wait for completion ---
            def signal_handler(sig, frame):
                print("\n[+] Stopping recording...")
                self._stop_event.set()

            old_handler = signal.signal(signal.SIGINT, signal_handler)

            try:
                if self.duration:
                    self._stop_event.wait(timeout=self.duration)
                else:
                    self._stop_event.wait()
            finally:
                signal.signal(signal.SIGINT, old_handler)

            # --- Stop session ---
            print("[+] Stopping session...")
            self._lib.chiaki_session_stop(session_ptr)
            self._lib.chiaki_session_join(session_ptr)
            self._lib.chiaki_session_fini(session_ptr)

            # Close temp files
            try:
                os.close(video_fd)
            except OSError:
                pass
            try:
                os.close(audio_fd)
            except OSError:
                pass

            elapsed = time.time() - (self._start_time or time.time())
            print(f"\n[+] Recording complete!")
            print(f"    Video frames: {self._video_frames}")
            print(f"    Audio frames: {self._audio_frames}")
            print(f"    Duration: {elapsed:.1f}s")

            # Mux raw streams into final output
            video_size = os.path.getsize(self._video_file) if os.path.exists(self._video_file) else 0
            audio_size = os.path.getsize(self._audio_file) if os.path.exists(self._audio_file) else 0
            print(f"    Raw video: {video_size / 1024 / 1024:.1f} MB")
            print(f"    Raw audio: {audio_size / 1024 / 1024:.1f} MB")

            if video_size == 0:
                print("[!] No video data recorded, skipping mux")
            else:
                self._mux_output()
                print(f"    Output: {self.output}")

        except Exception as e:
            print(f"\n[!] Error: {e}", file=sys.stderr)
            raise
        finally:
            self._cleanup()


# ---------------------------------------------------------------------------
# Alternative: Record using chiaki-ng subprocess (simpler approach)
# ---------------------------------------------------------------------------

def record_with_chiaki_cli(host, regist_key, morning, output, duration=None,
                            ps5=False, resolution="1080p", fps=60):
    """
    Alternative recording approach: launch chiaki-ng GUI/CLI with
    environment settings and capture X11/Wayland output with FFmpeg.

    This is a fallback if ctypes binding doesn't work.
    """
    print("[+] Fallback: Recording via screen capture")
    print("    This method uses FFmpeg to capture the chiaki window.")
    print()

    # Determine display server
    wayland = os.environ.get("WAYLAND_DISPLAY")
    x11_display = os.environ.get("DISPLAY", ":0")

    if wayland:
        # PipeWire capture for Wayland
        ffmpeg_cmd = [
            "ffmpeg", "-y",
            "-f", "pipewire",
            "-framerate", str(fps),
            "-i", "default",
        ]
    else:
        # X11 capture
        ffmpeg_cmd = [
            "ffmpeg", "-y",
            "-video_size", f"1920x1080",
            "-framerate", str(fps),
            "-f", "x11grab",
            "-i", x11_display,
        ]

    ffmpeg_cmd.extend([
        "-f", "pulse", "-i", "default",  # audio from PulseAudio
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "23",
        "-c:a", "aac", "-b:a", "192k",
    ])

    if duration:
        ffmpeg_cmd.extend(["-t", str(duration)])

    ffmpeg_cmd.append(output)

    print(f"[+] FFmpeg command: {' '.join(ffmpeg_cmd)}")
    print("[!] Start chiaki-ng manually and connect to your PlayStation,")
    print("    then FFmpeg will capture the screen.")

    subprocess.run(ffmpeg_cmd)


# ---------------------------------------------------------------------------
# Registration helper
# ---------------------------------------------------------------------------

def register_console(host, psn_account_id_b64, pin, ps5=False, lib_path=None):
    """
    Register this device with a PlayStation console.

    This is a one-time operation. After registration you receive:
    - rp_regist_key: needed for future connections
    - rp_key: encryption key
    - morning: random nonce (generated each session, but stored for convenience)

    NOTE: Due to the complexity of the registration protocol (AES encryption,
    custom key derivation), this function requires libchiaki.so.
    """
    lib = load_libchiaki(lib_path)

    print(f"[+] Registration")
    print(f"    Host: {host}")
    print(f"    PIN: {pin}")
    print(f"    PSN Account ID: {psn_account_id_b64}")
    print()

    # Decode PSN account ID
    try:
        psn_account_id = base64.b64decode(psn_account_id_b64)
        if len(psn_account_id) != PSN_ACCOUNT_ID_SIZE:
            print(f"[!] PSN Account ID must be {PSN_ACCOUNT_ID_SIZE} bytes (got {len(psn_account_id)})")
            return None
    except Exception as e:
        print(f"[!] Invalid PSN Account ID base64: {e}")
        return None

    # Set up logging
    log = ChiakiLog()
    lib.chiaki_log_init(
        ctypes.byref(log),
        ctypes.c_uint32(LOG_ALL),
        ctypes.cast(lib.chiaki_log_cb_print, ctypes.c_void_p),
        None,
    )

    # Registration uses a complex state machine internally
    # We need ChiakiRegistInfo and ChiakiRegist structs
    # Since the structs are complex, allocate generous buffers

    target = TARGET_PS5_1 if ps5 else TARGET_PS4_10

    # ChiakiRegistInfo struct layout (approximate):
    #   int target;           // 4 bytes
    #   pad 4;
    #   char *host;           // 8 bytes
    #   bool broadcast;       // 1 byte
    #   pad 7;
    #   char *psn_online_id;  // 8 bytes
    #   uint8_t psn_account_id[8]; // 8 bytes
    #   uint32_t pin;         // 4 bytes
    #   uint32_t console_pin; // 4 bytes
    #   void *holepunch_info; // 8 bytes
    #   void *rudp;           // 8 bytes (pointer-sized, this is a pointer typedef)

    regist_info_buf = ctypes.create_string_buffer(256)
    ctypes.memset(regist_info_buf, 0, 256)

    offset = 0
    # target (int32)
    struct.pack_into("<i", regist_info_buf, offset, target)
    offset += 4
    offset += 4  # padding

    # host (char*)
    host_bytes = host.encode("utf-8") + b'\x00'
    host_buf = ctypes.create_string_buffer(host_bytes)
    host_ptr = ctypes.cast(host_buf, ctypes.c_void_p).value
    struct.pack_into("<Q", regist_info_buf, offset, host_ptr)
    offset += 8

    # broadcast (bool) - False for direct host IP, True only for broadcast addresses
    struct.pack_into("<B", regist_info_buf, offset, 0)  # broadcast = false for direct host
    offset += 1
    offset += 7  # padding

    # psn_online_id (char*) - NULL, use account_id instead
    struct.pack_into("<Q", regist_info_buf, offset, 0)
    offset += 8

    # psn_account_id[8]
    regist_info_buf[offset:offset + 8] = psn_account_id[:8]
    offset += 8

    # pin (uint32)
    struct.pack_into("<I", regist_info_buf, offset, int(pin))
    offset += 4

    # console_pin (uint32) - 0
    struct.pack_into("<I", regist_info_buf, offset, 0)
    offset += 4

    # holepunch_info (pointer) - NULL
    struct.pack_into("<Q", regist_info_buf, offset, 0)
    offset += 8

    # rudp (pointer-sized) - NULL
    struct.pack_into("<Q", regist_info_buf, offset, 0)
    offset += 8

    # Registration result storage
    result = {"success": False, "regist_key": None, "rp_key": None}
    result_event = threading.Event()

    # Callback for registration completion
    REGIST_CB_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)

    def regist_cb(event_ptr, user_ptr):
        if not event_ptr:
            return
        # ChiakiRegistEvent: { int type; ChiakiRegisteredHost *host; }
        event_type = ctypes.cast(event_ptr, ctypes.POINTER(ctypes.c_int))[0]

        if event_type == 2:  # CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS
            print("[+] Registration successful!")
            # Parse the registered host data
            host_ptr_val = ctypes.cast(
                ctypes.c_void_p(event_ptr + 8),
                ctypes.POINTER(ctypes.c_void_p)
            )[0]
            if host_ptr_val:
                # ChiakiRegisteredHost layout:
                #   int target;        // +0
                #   char ap_ssid[48];  // +4
                #   char ap_bssid[32]; // +52
                #   char ap_key[80];   // +84
                #   char ap_name[32];  // +164
                #   uint8_t mac[6];    // +196
                #   char nickname[32]; // +202 (but likely aligned to +204 or similar)
                #   char regist_key[16]; // +234
                #   uint32_t rp_key_type; // +250
                #   uint8_t rp_key[16]; // +254

                # Use a simpler approach: read the raw bytes
                host_data = ctypes.string_at(host_ptr_val, 512)

                # Extract regist_key and rp_key by scanning for them
                # The offsets depend on struct packing
                # target (4) + ap_ssid(48) + ap_bssid(32) + ap_key(80) + ap_name(32) + mac(6) = 202
                # nickname(32) = offset 234
                # regist_key(16) = offset 266 (but alignment may shift this)
                # Let's use a more robust approach - read the target first
                rh_target = struct.unpack_from("<i", host_data, 0)[0]
                # ap_ssid starts at 4, 0x30=48 bytes
                # ap_bssid at 52, 0x20=32 bytes
                # ap_key at 84, 0x50=80 bytes
                # ap_name at 164, 0x20=32 bytes
                # server_mac at 196, 6 bytes
                # pad to align: 202 -> 204 maybe? No, char arrays don't need alignment
                # server_nickname at 202, 0x20=32 bytes
                nickname = host_data[202:234].split(b'\x00')[0].decode("utf-8", errors="replace")
                # rp_regist_key at 234, 16 bytes
                regist_key = host_data[234:250]
                # rp_key_type at 250, uint32
                rp_key_type = struct.unpack_from("<I", host_data, 250)[0]
                # rp_key at 254, 16 bytes
                rp_key = host_data[254:270]

                result["success"] = True
                result["nickname"] = nickname
                result["regist_key"] = regist_key
                result["rp_key_type"] = rp_key_type
                result["rp_key"] = rp_key

                print(f"    Console nickname: {nickname}")
                print(f"    Regist key (hex): {regist_key.hex()}")
                print(f"    RP key (hex): {rp_key.hex()}")
                print(f"    RP key type: {rp_key_type}")
        elif event_type == 1:  # FAILED
            print("[!] Registration FAILED")
        elif event_type == 0:  # CANCELED
            print("[!] Registration canceled")

        result_event.set()

    cb_ref = REGIST_CB_TYPE(regist_cb)

    # Allocate ChiakiRegist struct (generous buffer)
    regist_buf = ctypes.create_string_buffer(4096)

    print("[+] Starting registration (make sure PIN is entered on console)...")
    err = lib.chiaki_regist_start(
        ctypes.cast(regist_buf, ctypes.c_void_p),
        ctypes.byref(log),
        ctypes.cast(regist_info_buf, ctypes.c_void_p),
        cb_ref,
        None,
    )

    if err != ERR_SUCCESS:
        err_str = lib.chiaki_error_string(err).decode()
        print(f"[!] Registration start failed: {err_str}")
        return None

    # Wait for registration to complete
    if not result_event.wait(timeout=30):
        print("[!] Registration timed out")
        lib.chiaki_regist_stop(ctypes.cast(regist_buf, ctypes.c_void_p))

    lib.chiaki_regist_fini(ctypes.cast(regist_buf, ctypes.c_void_p))

    if result["success"]:
        return result
    return None


# ---------------------------------------------------------------------------
# Config file management
# ---------------------------------------------------------------------------

def save_config(path, host, regist_key, morning, ps5=False,
                psn_account_id=None, nickname=None):
    """Save connection parameters to a JSON config file."""
    config = {
        "host": host,
        "ps5": ps5,
        "regist_key": regist_key.hex() if isinstance(regist_key, bytes) else regist_key,
        "morning": morning.hex() if isinstance(morning, bytes) else morning,
    }
    if psn_account_id:
        if isinstance(psn_account_id, bytes):
            config["psn_account_id"] = base64.b64encode(psn_account_id).decode()
        else:
            config["psn_account_id"] = psn_account_id
    if nickname:
        config["nickname"] = nickname

    with open(path, "w") as f:
        json.dump(config, f, indent=2)
    print(f"[+] Config saved to {path}")


def load_config(path):
    """Load connection parameters from a JSON config file."""
    with open(path) as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="PlayStation Stream Recorder (chiaki-ng Python interface)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Discover consoles on the network
  %(prog)s discover --host 192.168.1.255

  # Register with a console (one-time setup)
  %(prog)s register --host 192.168.1.100 --psn-account-id <base64> --pin 12345678

  # Record a stream
  %(prog)s record --host 192.168.1.100 \\
      --regist-key <hex> --morning <hex> \\
      --output recording.mp4 --duration 60

  # Record using a saved config
  %(prog)s record --config ps_config.json --output recording.mp4

  # Wake up a console from standby
  %(prog)s wakeup --host 192.168.1.100 --regist-key <hex> --ps5
""",
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    # --- discover ---
    discover_parser = subparsers.add_parser("discover", help="Discover PlayStation consoles on the network")
    discover_parser.add_argument("--host", required=True, help="Target IP or broadcast address (e.g. 192.168.1.255)")
    discover_parser.add_argument("--timeout", type=float, default=3.0, help="Discovery timeout in seconds (default: 3)")

    # --- wakeup ---
    wakeup_parser = subparsers.add_parser("wakeup", help="Wake up a console from standby")
    wakeup_parser.add_argument("--host", required=True, help="Console IP address")
    wakeup_parser.add_argument("--regist-key", required=True, help="Registration key (hex)")
    wakeup_parser.add_argument("--ps5", action="store_true", help="Target is PS5 (default: PS4)")

    # --- register ---
    register_parser = subparsers.add_parser("register", help="Register this device with a PlayStation (one-time)")
    register_parser.add_argument("--host", required=True, help="Console IP address")
    register_parser.add_argument("--psn-account-id", required=True, help="PSN Account ID (base64)")
    register_parser.add_argument("--pin", required=True, help="PIN from console (Settings > Remote Play > Link Device)")
    register_parser.add_argument("--ps5", action="store_true", help="Target is PS5 (default: PS4)")
    register_parser.add_argument("--lib-path", help="Path to libchiaki.so")
    register_parser.add_argument("--save-config", help="Save registration result to config file")

    # --- record ---
    record_parser = subparsers.add_parser("record", help="Connect and record the PlayStation stream")
    record_parser.add_argument("--host", help="Console IP address")
    record_parser.add_argument("--regist-key", help="Registration key (hex, 32 chars)")
    record_parser.add_argument("--morning", help="Morning nonce (hex, 32 chars). Random if not specified.")
    record_parser.add_argument("--config", help="Load settings from JSON config file")
    record_parser.add_argument("--output", "-o", default="recording.mp4", help="Output file (default: recording.mp4)")
    record_parser.add_argument("--duration", "-d", type=int, help="Recording duration in seconds (default: unlimited)")
    record_parser.add_argument("--ps5", action="store_true", help="Target is PS5 (default: PS4)")
    record_parser.add_argument("--codec", choices=["h264", "h265", "h265-hdr"], default="h264", help="Video codec")
    record_parser.add_argument("--resolution", choices=["360p", "540p", "720p", "1080p"], default="1080p", help="Resolution")
    record_parser.add_argument("--fps", type=int, choices=[30, 60], default=60, help="Frames per second")
    record_parser.add_argument("--bitrate", type=int, default=15000, help="Video bitrate in kbps")
    record_parser.add_argument("--psn-account-id", help="PSN Account ID (base64)")
    record_parser.add_argument("--lib-path", help="Path to libchiaki.so")
    record_parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    record_parser.add_argument("--fallback", action="store_true", help="Use screen capture fallback instead of direct")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    # === DISCOVER ===
    if args.command == "discover":
        print(f"[+] Discovering PlayStation consoles at {args.host}...")
        results = discover_consoles(args.host, timeout=args.timeout)

        if not results:
            print("[!] No consoles found. Make sure the console is on the same network.")
            sys.exit(1)

        for i, host in enumerate(results):
            print(f"\n--- Console #{i + 1} ---")
            print(f"  Address:        {host.get('host_addr', 'unknown')}")
            print(f"  State:          {host.get('state', 'unknown')}")
            print(f"  Host Name:      {host.get('host_name', 'N/A')}")
            print(f"  Host Type:      {host.get('host_type', 'N/A')}")
            print(f"  Host ID:        {host.get('host_id', 'N/A')}")
            print(f"  System Version: {host.get('system_version', 'N/A')}")
            print(f"  Protocol:       {host.get('device_discovery_protocol_version', 'N/A')}")
            print(f"  Running App:    {host.get('running_app_name', 'N/A')} ({host.get('running_app_titleid', 'N/A')})")
            port = host.get('host_request_port', SESSION_PORT)
            print(f"  Request Port:   {port}")

    # === WAKEUP ===
    elif args.command == "wakeup":
        wakeup_console(args.host, args.regist_key, ps5=args.ps5)

    # === REGISTER ===
    elif args.command == "register":
        result = register_console(
            args.host,
            args.psn_account_id,
            args.pin,
            ps5=args.ps5,
            lib_path=args.lib_path,
        )

        if result and result["success"]:
            # Generate a random morning for future sessions
            morning = os.urandom(16)

            print(f"\n[+] Save these values for recording:")
            print(f"    --regist-key {result['regist_key'].hex()}")
            print(f"    --morning {morning.hex()}")

            if args.save_config:
                save_config(
                    args.save_config,
                    host=args.host,
                    regist_key=result["regist_key"],
                    morning=morning,
                    ps5=args.ps5,
                    psn_account_id=args.psn_account_id,
                    nickname=result.get("nickname"),
                )
        else:
            print("[!] Registration failed")
            sys.exit(1)

    # === RECORD ===
    elif args.command == "record":
        host = args.host
        regist_key = None
        morning = None
        ps5 = args.ps5
        psn_account_id = None

        # Load from config if specified
        if args.config:
            config = load_config(args.config)
            host = host or config.get("host")
            ps5 = config.get("ps5", ps5)
            if config.get("regist_key"):
                regist_key = bytes.fromhex(config["regist_key"])
            if config.get("morning"):
                morning = bytes.fromhex(config["morning"])
            if config.get("psn_account_id"):
                psn_account_id = base64.b64decode(config["psn_account_id"])

        # Override with CLI args
        if args.regist_key:
            regist_key = bytes.fromhex(args.regist_key)
        if args.morning:
            morning = bytes.fromhex(args.morning)
        if args.psn_account_id:
            psn_account_id = base64.b64decode(args.psn_account_id)

        # Generate random morning if not provided
        if morning is None:
            morning = os.urandom(16)
            print(f"[+] Generated random morning: {morning.hex()}")

        if not host or not regist_key:
            print("[!] --host and --regist-key are required (or use --config)")
            sys.exit(1)

        if len(regist_key) != SESSION_AUTH_SIZE:
            print(f"[!] regist_key must be {SESSION_AUTH_SIZE} bytes ({SESSION_AUTH_SIZE * 2} hex chars)")
            sys.exit(1)

        # Resolution map
        res_map = {
            "360p": (640, 360, 2000),
            "540p": (960, 540, 6000),
            "720p": (1280, 720, 10000),
            "1080p": (1920, 1080, 15000),
        }
        width, height, default_bitrate = res_map[args.resolution]
        bitrate = args.bitrate if args.bitrate != 15000 else default_bitrate

        codec_map = {"h264": CODEC_H264, "h265": CODEC_H265, "h265-hdr": CODEC_H265_HDR}
        codec = codec_map[args.codec]

        if args.fallback:
            record_with_chiaki_cli(
                host, regist_key.hex(), morning.hex(), args.output,
                duration=args.duration, ps5=ps5, resolution=args.resolution, fps=args.fps
            )
        else:
            recorder = StreamRecorder(
                host=host,
                regist_key=regist_key,
                morning=morning,
                ps5=ps5,
                codec=codec,
                width=width,
                height=height,
                fps=args.fps,
                bitrate=bitrate,
                output=args.output,
                duration=args.duration,
                psn_account_id=psn_account_id,
                lib_path=args.lib_path,
                verbose=args.verbose,
            )
            recorder.record()


if __name__ == "__main__":
    main()
