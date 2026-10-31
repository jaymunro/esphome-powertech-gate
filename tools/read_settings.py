#!/usr/bin/env python3
"""Decode a saved legacy snapshot for inspection; never connect or write settings."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def decode_snapshot(raw, schema=None):
    if schema is None:
        schema = json.loads((ROOT / 'settings/ps19001.json').read_text())
    raw = raw.removeprefix('C=')
    if len(raw) > 2048 or not all(32 <= ord(c) <= 126 for c in raw):
        raise ValueError('Snapshot must be a bounded single printable line')
    if raw[:17] != 'ACK READ FUNCTION' or raw[17:18] not in (',', ':'):
        raise ValueError('Not a legacy settings response')
    body, separator, suffix = raw[18:].partition(';')
    records = []
    values = {}
    for token in body.split(','):
        if not re.fullmatch(r'[0-9A-Z]:[0-9A-Fa-f]{1,8}', token):
            raise ValueError('Malformed or truncated field')
        label, value = token.split(':')
        if label in values:
            raise ValueError('Duplicate field')
        values[label] = value
        records.append([label, value])
    settings = []
    known = {p['field'] for p in schema['fields']}
    if not known <= values.keys():
        raise ValueError('Missing required settings fields')
    for p in schema['fields']:
        value = values[p['field']]
        decimal = p['format'] == 'decimal_width_2'
        if not re.fullmatch(r'[0-9]{2}' if decimal else r'[0-9A-Fa-f]', value):
            raise ValueError('Unexpected numeric format for field ' + p['field'])
        index = int(value, 10 if decimal else 16) - p['offset']
        if not 0 <= index < len(p['options']):
            raise ValueError('Out of range field ' + p['field'])
        settings.append({'field': p['field'], 'name': p['name'],
                         'value': p['options'][index], 'wire': value})
        if p['field'] == '1' and p['options'][index] in schema['unsupported_motor_types']:
            raise ValueError('Motor type requires a separate current-parameter schema')
    return {'model': schema['model'], 'header': raw[:18], 'records': records,
            'suffix': separator + suffix, 'settings': settings,
            'unknown_fields': {k: v for k, v in records if k not in known}}


def reassemble_snapshot(snapshot):
    """Losslessly reproduce the captured ACK, not a WRITE FUNCTION command."""
    return snapshot['header'] + ','.join(k + ':' + v for k, v in snapshot['records']) + snapshot['suffix']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path, help='File containing one complete ACK line')
    args = parser.parse_args()
    print(json.dumps(decode_snapshot(args.snapshot.read_text().strip()), indent=2))
