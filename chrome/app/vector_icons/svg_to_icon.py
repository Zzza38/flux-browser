#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Compiles an SVG into Chromium's .icon vector format.

Chromium has no in-tree SVG converter; upstream points at the external Skiafy
web tool. This handles the subset of SVG used by our own icon sources so the
.svg can stay the source of truth and the .icon can be regenerated.

Supported: <path> with M/L/H/V/C/S/Z commands, absolute or relative. A path
carrying data-icon-color="runtime" is emitted without PATH_COLOR_ARGB so
CreateVectorIcon() can tint it at runtime.

Usage:
  python3 svg_to_icon.py input.svg output.icon [--check]
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET

SVG_NS = '{http://www.w3.org/2000/svg}'

# Command -> coordinates consumed per repetition.
ARITY = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Z': 0}

TOKEN_RE = re.compile(r'([MmLlHhVvCcSsZz])|(-?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)')


def fail(msg):
  sys.stderr.write('svg_to_icon: %s\n' % msg)
  sys.exit(1)


def fmt(value):
  """Formats a coordinate the way .icon files do: 270.f, 270.5f."""
  rounded = round(value, 4)
  if rounded == int(rounded):
    return '%d.f' % int(rounded)
  return '%sf' % ('%.4f' % rounded).rstrip('0')


def parse_color(fill):
  """Parses #RGB/#RRGGBB into an opaque PATH_COLOR_ARGB directive."""
  value = fill.strip().lstrip('#')
  if len(value) == 3:
    value = ''.join(c * 2 for c in value)
  if not re.fullmatch(r'[0-9a-fA-F]{6}', value):
    fail('unsupported fill %r; use #RGB or #RRGGBB' % fill)
  r, g, b = (int(value[i:i + 2], 16) for i in (0, 2, 4))
  return 'PATH_COLOR_ARGB, 0xFF, 0x%02X, 0x%02X, 0x%02X,' % (r, g, b)


def tokenize(d):
  tokens = []
  pos = 0
  for match in TOKEN_RE.finditer(d):
    if d[pos:match.start()].strip(', \t\r\n'):
      fail('unparsable path data near %r' % d[pos:match.start() + 8])
    pos = match.end()
    tokens.append(match.group(1) or float(match.group(2)))
  if d[pos:].strip(', \t\r\n'):
    fail('trailing garbage in path data: %r' % d[pos:])
  return tokens


def convert_path(d):
  """Turns path data into .icon directive lines, resolving relative commands."""
  tokens = tokenize(d)
  lines = []
  index = 0
  command = None
  # Current point, and the subpath start we return to on Z.
  x = y = start_x = start_y = 0.0
  # Second control point of the previous cubic, for S reflection.
  prev_ctrl = None

  while index < len(tokens):
    if isinstance(tokens[index], str):
      command = tokens[index]
      index += 1
    elif command is None:
      fail('path data must begin with a command')
    elif command in 'Mm':
      # Repeated coordinates after a moveto are implicit linetos.
      command = 'L' if command == 'M' else 'l'

    upper = command.upper()
    relative = command.islower()
    arity = ARITY[upper]

    args = tokens[index:index + arity]
    if len(args) < arity or any(isinstance(a, str) for a in args):
      fail('command %s is missing coordinates' % command)
    index += arity

    if upper == 'Z':
      lines.append('CLOSE,')
      x, y = start_x, start_y
      prev_ctrl = None
      continue

    if upper == 'H':
      x = x + args[0] if relative else args[0]
      lines.append('H_LINE_TO, %s,' % fmt(x))
      prev_ctrl = None
      continue

    if upper == 'V':
      y = y + args[0] if relative else args[0]
      lines.append('V_LINE_TO, %s,' % fmt(y))
      prev_ctrl = None
      continue

    if upper in ('M', 'L'):
      x = x + args[0] if relative else args[0]
      y = y + args[1] if relative else args[1]
      lines.append('%s, %s, %s,' % ('MOVE_TO' if upper == 'M' else 'LINE_TO',
                                    fmt(x), fmt(y)))
      if upper == 'M':
        start_x, start_y = x, y
      prev_ctrl = None
      continue

    # C and S both emit a full cubic.
    if upper == 'S':
      # The first control point mirrors the previous cubic's second one.
      if prev_ctrl:
        c1x, c1y = 2 * x - prev_ctrl[0], 2 * y - prev_ctrl[1]
      else:
        c1x, c1y = x, y
      c2x = x + args[0] if relative else args[0]
      c2y = y + args[1] if relative else args[1]
      nx = x + args[2] if relative else args[2]
      ny = y + args[3] if relative else args[3]
    else:
      c1x = x + args[0] if relative else args[0]
      c1y = y + args[1] if relative else args[1]
      c2x = x + args[2] if relative else args[2]
      c2y = y + args[3] if relative else args[3]
      nx = x + args[4] if relative else args[4]
      ny = y + args[5] if relative else args[5]

    lines.append('CUBIC_TO, %s, %s, %s, %s, %s, %s,' %
                 (fmt(c1x), fmt(c1y), fmt(c2x), fmt(c2y), fmt(nx), fmt(ny)))
    prev_ctrl = (c2x, c2y)
    x, y = nx, ny

  return lines


def convert(svg_text, source_name):
  root = ET.fromstring(svg_text)

  view_box = (root.get('viewBox') or '').split()
  if len(view_box) != 4:
    fail('svg needs a viewBox')
  min_x, min_y, width, height = (float(v) for v in view_box)
  if min_x or min_y:
    fail('viewBox must start at 0 0; translate the paths instead')
  if width != height:
    fail('icon canvas must be square, got %gx%g' % (width, height))

  paths = list(root.iter(SVG_NS + 'path'))
  if not paths:
    fail('no <path> elements found')

  out = [
      '// Copyright 2026 The Chromium Authors',
      '// Use of this source code is governed by a BSD-style license that can be',
      '// found in the LICENSE file.',
      '',
      '// Generated by svg_to_icon.py from %s. Do not edit by hand.' % source_name,
      'CANVAS_DIMENSIONS, %d,' % int(width),
  ]

  for i, path in enumerate(paths):
    if i:
      out.append('NEW_PATH,')
    if path.get('data-icon-color') != 'runtime':
      out.append(parse_color(path.get('fill', '#000000')))
    out.append('FILL_RULE_NONZERO,')
    out.extend(convert_path(path.get('d', '')))

  # Every directive is comma-terminated except the last one in the file.
  out[-1] = out[-1].rstrip(',')
  return '\n'.join(out) + '\n'


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('svg')
  parser.add_argument('icon')
  parser.add_argument('--check', action='store_true',
                      help='exit non-zero if the .icon is stale instead of writing')
  args = parser.parse_args()

  with open(args.svg, encoding='utf-8') as f:
    generated = convert(f.read(), args.svg.replace('\\', '/').split('/')[-1])

  if args.check:
    try:
      with open(args.icon, encoding='utf-8') as f:
        current = f.read()
    except FileNotFoundError:
      current = None
    if current != generated:
      fail('%s is stale; rerun without --check' % args.icon)
    print('%s is up to date' % args.icon)
    return

  with open(args.icon, 'w', encoding='utf-8', newline='\n') as f:
    f.write(generated)
  print('wrote %s' % args.icon)


if __name__ == '__main__':
  main()
