#!/usr/bin/python3
# Pre-flight step: wipes the board's on-flash backup (/backup/*, see
# BackupAppendLog / BackupSaveImage in cloud_regressor.cc) so bench-test
# frames don't eat into the flight's block budget (see the sizing comment
# above kBackupImageEveryN in cloud_regressor.cc) -- a plain reflash does
# NOT clear this, it only rewrites the model file into the existing littlefs.
#
# Usage:
#   python3 tools/clear_backup.py            # asks for confirmation first
#   python3 tools/clear_backup.py --yes      # skip the confirmation prompt
#
# The board must be connected via USB and running the cloud_payload firmware
# (flight or bench build -- clear_backup is always exported).

import argparse
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
    p.add_argument('--yes', action='store_true',
                   help='skip the confirmation prompt')
    args = p.parse_args()

    files = rpc(args.host, 'list_backup_files')
    if not files:
        print('/backup is already empty.')
        return

    print(f'{len(files)} file(s) currently in /backup on {args.host}.')
    if not args.yes:
        reply = input('Delete them all? This cannot be undone. [y/N] ')
        if reply.strip().lower() != 'y':
            print('Aborted.')
            return

    result = rpc(args.host, 'clear_backup')
    print(f'Deleted {result["deleted"]} file(s).')

    remaining = rpc(args.host, 'list_backup_files')
    if remaining:
        print(f'WARNING: {len(remaining)} file(s) still present after clear.',
              file=sys.stderr)
        sys.exit(1)
    print('/backup confirmed empty.')


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
