#!/usr/bin/env python3
"""Deterministic clipboard/key process fixture: never accesses a desktop."""
import base64
import json
import os
from pathlib import Path
import sys
import time

root = Path(os.environ['SCRYBE_PASTE_FIXTURE'])

def log(message):
    with (root / 'events').open('a') as stream:
        stream.write(message + '\n')

mode = (root / 'mode').read_text().strip()
if len(sys.argv) > 1 and sys.argv[1] == 'key':
    if sys.argv[2:] == ['47:0', '42:0', '29:0']:
        log('RELEASE')
        sys.exit(0)
    log('KEY ' + ' '.join(sys.argv[2:]))
    if mode == 'key-hang':
        time.sleep(30)
    sys.exit(1 if mode == 'key-fail' else 0)

request = json.loads(sys.stdin.readline())
text = base64.b64decode(request['text']).decode()
log('START ' + text)
if mode == 'prepare-hang':
    time.sleep(30)
if mode == 'prepare-fail':
    print('ERROR fixture preparation failed', flush=True)
    sys.exit(1)
time.sleep(0.05)
log('READY ' + text)
print('READY', flush=True)
if mode == 'lost-immediate':
    print('LOST', flush=True)
    sys.exit(0)
if mode == 'lost-after-key':
    while not (root / 'events').exists() or 'KEY' not in (root / 'events').read_text():
        time.sleep(.01)
    time.sleep(.05)
    log('USER_COPY')
    print('LOST', flush=True)
    sys.exit(0)
for line in sys.stdin:
    log(line.strip() + ' ' + text)
    print('DONE', flush=True)
