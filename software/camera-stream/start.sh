#!/bin/bash
# Camera stream launcher
# Uses libcamera V4L2 compatibility layer for Raspberry Pi CSI cameras (e.g. OV5647)
# that require libcamera and won't work with raw V4L2 streaming.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_PRELOAD=/usr/libexec/aarch64-linux-gnu/libcamera/v4l2-compat.so

exec "$SCRIPT_DIR/build/camera-stream" "$@"
