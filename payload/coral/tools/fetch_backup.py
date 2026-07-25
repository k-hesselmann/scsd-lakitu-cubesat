#!/usr/bin/python3
# Pulls everything under /backup (see BackupAppendLog / BackupSaveImage in
# cloud_regressor.cc) off the board over USB, e.g. after a flight.
#
# Usage:
#   python3 tools/fetch_backup.py                  # -> ./backup_frames/
#   python3 tools/fetch_backup.py --out ./flight1/
#
# The board must be connected via USB and running the cloud_payload firmware
# (flight or bench build -- these two RPCs are always exported). Convert the
# pulled .raw images with tools/raw_to_png.py.

import argparse
import base64
import os
import sys

import requests


def rpc(host, method, params=None):
    payload = {'method': method, 'jsonrpc': '2.0', 'id': 0}
    if params is not None:
        payload['params'] = params
    response = requests.post(f'http://{host}:80/jsonrpc',
                             json=payload, timeout=30).json()
    if 'error' in response:
        raise RuntimeError(response['error']['message'])
    return response['result']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='10.10.10.1', help='board IP over USB')
    p.add_argument('--out', default='./backup_frames', help='output directory')
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)

    files = rpc(args.host, 'list_backup_files')
    if not files:
        print('No backup files on the board yet.')
        return
    print(f'{len(files)} file(s) on board.')

    fetched, skipped = 0, 0
    for f in sorted(files, key=lambda f: f['name']):
        name, size = f['name'], f['size']
        local_path = os.path.join(args.out, name)
        if os.path.exists(local_path) and os.path.getsize(local_path) == size:
            skipped += 1
            continue  # already have this one (e.g. a prior partial pull)

        data = base64.b64decode(rpc(args.host, 'get_backup_file',
                                    {'name': name})['data'])
        with open(local_path, 'wb') as fh:
            fh.write(data)
        print(f'  {name}  ({len(data)} bytes)')
        fetched += 1

    print(f'\nDone: {fetched} fetched, {skipped} already present.')


if __name__ == '__main__':
    try:
        main()
    except requests.exceptions.ConnectionError:
        print('ERROR: Cannot reach board. Check USB connection and IP.',
              file=sys.stderr)
        sys.exit(1)
    except RuntimeError as e:
        print(f'Board error: {e}', file=sys.stderr)
        sys.exit(1)
