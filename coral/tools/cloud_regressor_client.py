#!/usr/bin/python3
# Debug client — displays the last captured frame from the board.
# Remove alongside the GetLastImageRpc endpoint before flight.
#
# Usage:
#   python3 examples/cloud_payload/cloud_regressor_client.py
#   python3 examples/cloud_payload/cloud_regressor_client.py --save ./debug_frames/
#
# The board must be connected via USB and running the cloud_payload firmware.
# Default host 10.10.10.1 is the board's USB network address.

import argparse
import base64
import os
import sys
from datetime import datetime
import requests
from PIL import Image, ImageDraw
from PIL.PngImagePlugin import PngInfo


def rpc(host, method, params=None):
    payload = {'method': method, 'jsonrpc': '2.0', 'id': 0}
    if params is not None:
        payload['params'] = params
    response = requests.post(f'http://{host}:80/jsonrpc',
                             json=payload, timeout=10).json()
    if 'error' in response:
        raise RuntimeError(response['error']['message'])
    return response['result']


def to_image(result):
    width    = result['width']
    height   = result['height']
    fraction = result['fraction']
    pixels   = base64.b64decode(result['pixels'])
    im = Image.frombytes('L', (width, height), pixels).convert('RGB')
    return im, width, height, fraction * 100.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='10.10.10.1',
                        help='Board IP over USB network')
    parser.add_argument('--save', metavar='DIR', default=None,
                        help='Also save each frame as a PNG to this directory')
    parser.add_argument('--burst', action='store_true',
                        help='Fetch all frames from the last button-held burst '
                             '(for motion-blur inspection) instead of the last '
                             'single frame')
    args = parser.parse_args()

    if args.save:
        os.makedirs(args.save, exist_ok=True)

    try:
        if args.burst:
            fetch_burst(args)
        else:
            fetch_single(args)
    except RuntimeError as e:
        print(f'Board error: {e}', file=sys.stderr)
        sys.exit(1)


def save_clean(im, out_dir, cover_pct, kind, index=None, when=None):
    """Save the raw frame with no overlay (clean for training). Timestamp and
    cloud percent go in both the filename and the PNG metadata.

    Filename: <date>_<time>_cloud<pct>_<single|burstNN>.png
    """
    when = when or datetime.now()
    ts = when.strftime('%Y%m%d_%H%M%S')
    suffix = kind if index is None else f'{kind}{index:02d}'
    path = os.path.join(out_dir, f'{ts}_cloud{cover_pct:.1f}_{suffix}.png')

    meta = PngInfo()
    meta.add_text('timestamp', when.isoformat(timespec='seconds'))
    meta.add_text('cloud_percent', f'{cover_pct:.1f}')
    im.save(path, pnginfo=meta)
    return path


def show_annotated(im, label):
    """Display a copy with the label drawn on it — never the saved file."""
    view = im.copy()
    ImageDraw.Draw(view).text((4, 4), label, fill=(255, 255, 0))
    view.show()


def fetch_single(args):
    im, width, height, cover_pct = to_image(rpc(args.host, 'get_last_image'))
    print(f'cloud cover: {cover_pct:.1f}%  ({width}x{height} Y8)')

    if args.save:
        print(f'Saved to {save_clean(im, args.save, cover_pct, "single")}')

    show_annotated(im, f'{cover_pct:.1f}%')


def fetch_burst(args):
    count = rpc(args.host, 'get_burst_count')['count']
    if count == 0:
        print('No burst captured yet. Hold the user button, then retry.')
        return
    print(f'Burst has {count} frames (oldest first).')

    # One timestamp for the whole burst; index keeps frame order within it.
    when = datetime.now()
    for i in range(count):
        im, _w, _h, cover_pct = to_image(
            rpc(args.host, 'get_burst_frame', {'index': i}))
        if args.save:
            path = save_clean(im, args.save, cover_pct, 'burst',
                              index=i, when=when)
            print(f'  frame {i}: {cover_pct:.1f}%  -> {path}')
        else:
            print(f'  frame {i}: {cover_pct:.1f}%')
            show_annotated(im, f'#{i}  {cover_pct:.1f}%')


if __name__ == '__main__':
    try:
        main()
    except requests.exceptions.ConnectionError:
        print('ERROR: Cannot reach board. Check USB connection and IP.',
              file=sys.stderr)
        sys.exit(1)
