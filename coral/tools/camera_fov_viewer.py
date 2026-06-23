#!/usr/bin/python3
# Live camera viewer for checking the camera's field of view (e.g. against the
# casing aperture). Streams frames from the board over USB and shows them in a
# window with a framing grid + crosshair overlay.
#
# Usage:
#   python3 tools/camera_fov_viewer.py                 # native 324x324, rotation 270 (matches deployment)
#   python3 tools/camera_fov_viewer.py --format GRAY   # exactly what the model sees
#   python3 tools/camera_fov_viewer.py --width 500 --height 500 --rotation 0
#
# Requires the board running the `camera_streaming_rpc_usb` firmware (NOT the
# cloud_payload). Build + flash:
#   make -C out camera_streaming_rpc_usb -j$(nproc)
#   python3 coralmicro/scripts/flashtool.py --build_dir out \
#       --elf_path out/coralmicro/examples/camera_streaming_rpc/camera_streaming_rpc_usb
# Reflash cloud_payload when done.
#
# Keys:  q/ESC quit   s save snapshot   g toggle grid   [ ] rotate -/+ 90deg

import argparse
import base64
import time
from datetime import datetime

import cv2
import numpy as np
import requests

# Native sensor size is 324x324 (CameraTask::kWidth/kHeight).
NATIVE = 324


def get_frame(host, width, height, fmt, filt, rotation, awb, timeout=10):
    payload = {
        'jsonrpc': '2.0',
        'id': 0,
        'method': 'get_image_from_camera',
        'params': [{
            'width': width,
            'height': height,
            'format': fmt,
            'filter': filt,
            'rotation': rotation,
            'auto_white_balance': awb,
        }],
    }
    resp = requests.post(f'http://{host}:80/jsonrpc', json=payload,
                         timeout=timeout).json()
    if 'error' in resp:
        raise RuntimeError(resp['error']['message'])
    result = resp['result']
    raw = base64.b64decode(result['base64_data'])
    w, h = result['width'], result['height']

    if fmt == 'RGB':
        img = np.frombuffer(raw, np.uint8).reshape(h, w, 3)
        img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    elif fmt == 'GRAY':
        gray = np.frombuffer(raw, np.uint8).reshape(h, w)
        img = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    else:  # RAW Bayer
        bayer = np.frombuffer(raw, np.uint8).reshape(h, w)
        img = cv2.cvtColor(bayer, cv2.COLOR_BAYER_BG2BGR)
    return img


def draw_overlay(img, rotation, awb, fps):
    h, w = img.shape[:2]
    green, yellow = (0, 255, 0), (0, 255, 255)
    # rule-of-thirds grid
    for i in (1, 2):
        cv2.line(img, (w * i // 3, 0), (w * i // 3, h), green, 1)
        cv2.line(img, (0, h * i // 3), (w, h * i // 3), green, 1)
    # center crosshair
    cx, cy = w // 2, h // 2
    cv2.line(img, (cx - 10, cy), (cx + 10, cy), yellow, 1)
    cv2.line(img, (cx, cy - 10), (cx, cy + 10), yellow, 1)
    # frame border (helps spot vignetting / casing intrusion at the edges)
    cv2.rectangle(img, (0, 0), (w - 1, h - 1), green, 1)
    hud = f'{w}x{h}  rot{rotation}  awb={"on" if awb else "off"}  {fps:4.1f} fps'
    cv2.putText(img, hud, (6, h - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.4,
                (0, 0, 0), 3, cv2.LINE_AA)
    cv2.putText(img, hud, (6, h - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.4,
                yellow, 1, cv2.LINE_AA)
    return img


def main():
    p = argparse.ArgumentParser(description='Live camera FOV viewer (USB).')
    p.add_argument('--host', default='10.10.10.1', help="board USB IP")
    p.add_argument('--width', type=int, default=NATIVE)
    p.add_argument('--height', type=int, default=NATIVE)
    p.add_argument('--format', default='RGB', choices=['RGB', 'GRAY', 'RAW'])
    p.add_argument('--filter', default='BILINEAR',
                   choices=['BILINEAR', 'NEAREST_NEIGHBOR'])
    p.add_argument('--rotation', type=int, default=270, choices=[0, 90, 180, 270],
                   help="default 270 matches the deployed cloud_payload (k270)")
    p.add_argument('--no-awb', dest='awb', action='store_false',
                   help="disable auto white balance")
    p.add_argument('--scale', type=int, default=2, help="display upscale factor")
    p.add_argument('--save-dir', default='.', help="where 's' saves snapshots")
    args = p.parse_args()

    win = 'Coral camera FOV  (q quit  s save  g grid  [ ] rotate)'
    cv2.namedWindow(win, cv2.WINDOW_NORMAL)
    rotation, grid = args.rotation, True
    last, fps = time.time(), 0.0
    print(f'Streaming from {args.host} — Ctrl-C or q to quit.')

    while True:
        try:
            frame = get_frame(args.host, args.width, args.height, args.format,
                              args.filter, rotation, args.awb)
        except (requests.RequestException, RuntimeError) as e:
            # board busy / reflashing / unplugged — keep trying
            blank = np.zeros((args.height, args.width, 3), np.uint8)
            cv2.putText(blank, 'waiting for board...', (10, args.height // 2),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1)
            cv2.imshow(win, blank)
            if cv2.waitKey(500) & 0xFF in (ord('q'), 27):
                break
            continue

        now = time.time()
        fps = 0.9 * fps + 0.1 * (1.0 / max(now - last, 1e-3))
        last = now
        if grid:
            frame = draw_overlay(frame, rotation, args.awb, fps)
        if args.scale != 1:
            frame = cv2.resize(frame, None, fx=args.scale, fy=args.scale,
                               interpolation=cv2.INTER_NEAREST)
        cv2.imshow(win, frame)

        key = cv2.waitKey(1) & 0xFF
        if key in (ord('q'), 27):
            break
        elif key == ord('g'):
            grid = not grid
        elif key == ord('['):
            rotation = (rotation - 90) % 360
        elif key == ord(']'):
            rotation = (rotation + 90) % 360
        elif key == ord('s'):
            path = f"{args.save_dir}/fov_{datetime.now():%H%M%S}.png"
            cv2.imwrite(path, frame)
            print('saved', path)

    cv2.destroyAllWindows()


if __name__ == '__main__':
    main()
