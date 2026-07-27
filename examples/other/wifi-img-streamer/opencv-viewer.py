#!/usr/bin/env python3

import argparse
import socket
import struct
import time
from pathlib import Path

import cv2
import numpy as np


CPX_HEADER_SIZE = 4
IMAGE_HEADER_FORMAT = "<BHHBBI"
IMAGE_HEADER_SIZE = struct.calcsize(IMAGE_HEADER_FORMAT)
MAGIC = 0xBC
RAW_ENCODING = 0
JPEG_ENCODING = 1
MAX_IMAGE_SIZE = 2 * 1024 * 1024


parser = argparse.ArgumentParser(
    description="Reliable AI-deck RAW/JPEG stream viewer"
)
parser.add_argument(
    "-n",
    "--host",
    default="192.168.4.1",
    help="AI-deck IP address",
)
parser.add_argument(
    "-p",
    "--port",
    type=int,
    default=5000,
    help="AI-deck TCP port",
)
parser.add_argument(
    "--save",
    action="store_true",
    help="Save received frames",
)
parser.add_argument(
    "--timeout",
    type=float,
    default=3.0,
    help="Socket receive timeout in seconds",
)
args = parser.parse_args()


def connect() -> socket.socket:
    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(args.timeout)

        try:
            print(
                f"Connecting to socket on {args.host}:{args.port}...",
                flush=True,
            )
            sock.connect((args.host, args.port))
            print("Socket connected", flush=True)
            return sock
        except OSError as error:
            print(f"Connection failed: {error}. Retrying...", flush=True)
            sock.close()
            time.sleep(1)


def receive_exact(sock: socket.socket, size: int) -> bytes:
    if size < 0:
        raise ValueError(f"Invalid receive size: {size}")

    data = bytearray()

    while len(data) < size:
        chunk = sock.recv(size - len(data))

        if not chunk:
            raise ConnectionError("AI-deck closed the TCP connection")

        data.extend(chunk)

    return bytes(data)


def receive_cpx_payload(sock: socket.socket) -> bytes:
    header = receive_exact(sock, CPX_HEADER_SIZE)
    length, _, _ = struct.unpack("<HBB", header)

    payload_size = length - 2

    if payload_size < 0 or payload_size > 65535:
        raise ValueError(f"Invalid CPX payload size: {payload_size}")

    return receive_exact(sock, payload_size)


def receive_image(sock: socket.socket):
    header_payload = receive_cpx_payload(sock)

    if len(header_payload) != IMAGE_HEADER_SIZE:
        raise ValueError(
            f"Invalid image header size: "
            f"{len(header_payload)} != {IMAGE_HEADER_SIZE}"
        )

    magic, width, height, depth, image_format, image_size = struct.unpack(
        IMAGE_HEADER_FORMAT,
        header_payload,
    )

    if magic != MAGIC:
        raise ValueError(f"Invalid image magic: 0x{magic:02x}")

    if width <= 0 or height <= 0 or depth <= 0:
        raise ValueError(
            f"Invalid image geometry: {width}x{height}x{depth}"
        )

    if image_size <= 0 or image_size > MAX_IMAGE_SIZE:
        raise ValueError(f"Invalid image size: {image_size}")

    image_data = bytearray()

    while len(image_data) < image_size:
        payload = receive_cpx_payload(sock)
        remaining = image_size - len(image_data)

        if len(payload) > remaining:
            payload = payload[:remaining]

        image_data.extend(payload)

    return width, height, depth, image_format, bytes(image_data)


def decode_image(
    width: int,
    height: int,
    depth: int,
    image_format: int,
    image_data: bytes,
):
    if image_format == RAW_ENCODING:
        expected_size = width * height * depth

        if len(image_data) != expected_size:
            raise ValueError(
                f"Invalid RAW size: {len(image_data)} != {expected_size}"
            )

        raw = np.frombuffer(image_data, dtype=np.uint8).reshape(
            (height, width)
        )
        color = cv2.cvtColor(raw, cv2.COLOR_BayerBG2BGR)
        return raw, color

    if image_format == JPEG_ENCODING:
        encoded = np.frombuffer(image_data, dtype=np.uint8)
        decoded = cv2.imdecode(encoded, cv2.IMREAD_UNCHANGED)

        if decoded is None:
            raise ValueError("OpenCV failed to decode JPEG frame")

        return None, decoded

    raise ValueError(f"Unsupported image format: {image_format}")


def save_frame(
    frame_number: int,
    image_format: int,
    image_data: bytes,
    raw,
    decoded,
) -> None:
    output = Path("stream_out")

    if image_format == JPEG_ENCODING:
        output.mkdir(parents=True, exist_ok=True)
        (output / f"img_{frame_number:06d}.jpg").write_bytes(image_data)
        return

    raw_dir = output / "raw"
    color_dir = output / "debayer"
    raw_dir.mkdir(parents=True, exist_ok=True)
    color_dir.mkdir(parents=True, exist_ok=True)

    cv2.imwrite(str(raw_dir / f"img_{frame_number:06d}.png"), raw)
    cv2.imwrite(
        str(color_dir / f"img_{frame_number:06d}.png"),
        decoded,
    )


frame_count = 0
start_time = time.monotonic()
client_socket = None

try:
    while True:
        if client_socket is None:
            client_socket = connect()

        try:
            width, height, depth, image_format, image_data = receive_image(
                client_socket
            )

            raw, decoded = decode_image(
                width,
                height,
                depth,
                image_format,
                image_data,
            )

            frame_count += 1
            elapsed = time.monotonic() - start_time
            fps = frame_count / elapsed if elapsed > 0 else 0.0

            if frame_count == 1 or frame_count % 10 == 0:
                print(
                    f"frame={frame_count} "
                    f"fps={fps:.2f} "
                    f"size={len(image_data)} "
                    f"format={'JPEG' if image_format else 'RAW'}",
                    flush=True,
                )

            if image_format == RAW_ENCODING:
                cv2.imshow("AI-deck RAW", raw)
                cv2.imshow("AI-deck Color", decoded)
            else:
                cv2.imshow("AI-deck JPEG", decoded)

            if args.save:
                save_frame(
                    frame_count,
                    image_format,
                    image_data,
                    raw,
                    decoded,
                )

            key = cv2.waitKey(1) & 0xFF

            if key in (ord("q"), 27):
                break

        except (
            socket.timeout,
            ConnectionError,
            OSError,
            ValueError,
            struct.error,
        ) as error:
            print(
                f"Stream interrupted: {error}. Reconnecting...",
                flush=True,
            )

            try:
                client_socket.close()
            except OSError:
                pass

            client_socket = None

            # Даём Qt обработать события, чтобы окно не выглядело зависшим.
            cv2.waitKey(1)
            time.sleep(0.5)

finally:
    if client_socket is not None:
        client_socket.close()

    cv2.destroyAllWindows()
