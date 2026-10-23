#!/usr/bin/env python3
"""Count owned C/header code, excluding blank/comment-only lines.

Function spans are a lexical estimate, not a C parser; conditional-compilation
branches are counted together. Paths may be files or component directories.
"""
import argparse
import json
from pathlib import Path
import re

DEFAULT_ROOTS = ('rrclient', 'rrserver', 'librustyaxe', 'librustyirc', 'librrprotocol',
                 'libfwdspmgr', 'fwdsp', 'rrbridge', 'callsign-lookup',
                 'bindings', 'targets', 'inc')
SKIP = {'.git', 'ext', 'build', 'node_modules', 'migration'}
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
DEFINITION = re.compile(r'(?m)^(?:static\s+)?[A-Za-z_][^;{}\n]*?\b(\w+)\s*\(')


def blank(text):
    """Mask a token while preserving offsets and physical line boundaries."""
    return re.sub(r'[^\n]', ' ', text)


def code_only(source):
    """Remove comments, preserving quoted strings and character constants."""
    return TOKENS.sub(lambda m: blank(m[0]) if m[0].startswith('/') else m[0], source)


def line_count(source):
    """Count physical lines containing code, including braces/directives."""
    return sum(bool(line.strip()) for line in source.splitlines())


def matching(source, start, opening, closing):
    """Find the matching delimiter in a source with strings/comments masked."""
    depth = 0
    for pos in range(start, len(source)):
        depth += (source[pos] == opening) - (source[pos] == closing)
        if depth == 0:
            return pos
    return None


def inspect_source(source):
    """Return code line count and estimated function sizes for one C source."""
    code = code_only(source)
    masked = TOKENS.sub(lambda m: blank(m[0]), source)
    functions = []
    covered = 0
    for match in DEFINITION.finditer(masked):
        if match.start() < covered:
            continue
        end_args = matching(masked, match.end() - 1, '(', ')')
        if end_args is None:
            continue
        body = end_args + 1
        while body < len(masked) and masked[body].isspace():
            body += 1
        if body == len(masked) or masked[body] != '{':
            continue
        end = matching(masked, body, '{', '}')
        if end is None:
            continue
        covered = end + 1
        functions.append({'name': match[1], 'line': source.count('\n', 0, match.start()) + 1,
                          'code_lines': line_count(code[match.start():end + 1])})
    return {'code_lines': line_count(code), 'functions': functions}


def source_files(paths):
    """Enumerate C sources once, without following directory symlinks/vendors."""
    found = set()
    for name in paths:
        path = Path(name)
        for item in ([path] if path.is_file() else path.rglob('*')):
            if item.suffix in {'.c', '.h'} and not SKIP.intersection(item.parts):
                found.add(item)
    return sorted(found)


def main():
    """Print a reproducible size inventory, optionally checking a chosen cap."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', default=DEFAULT_ROOTS)
    parser.add_argument('--min-lines', type=int, default=800)
    parser.add_argument('--function-lines', type=int, default=300)
    parser.add_argument('--max-lines', type=int, help='Fail if a selected file exceeds this cap')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    rows = []
    for path in source_files(args.paths):
        row = inspect_source(path.read_text())
        row['path'] = str(path)
        rows.append(row)
    rows.sort(key=lambda row: (-row['code_lines'], row['path']))
    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            if row['code_lines'] >= args.min_lines:
                print(f"{row['code_lines']:5}  {row['path']}")
            for func in row['functions']:
                if func['code_lines'] > args.function_lines:
                    print(f"       {func['code_lines']:4}  {row['path']}:{func['line']} {func['name']}()")
    if args.max_lines and any(row['code_lines'] > args.max_lines for row in rows):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
