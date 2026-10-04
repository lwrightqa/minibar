#!/usr/bin/env python3
"""Gzip a web asset reproducibly (no timestamp, no file name) for embedding. Owner: net builder.
Usage: gzip_asset.py <in> <out>"""
import gzip
import sys

src, dst = sys.argv[1], sys.argv[2]
with open(src, 'rb') as f:
    data = f.read()
with open(dst, 'wb') as f:
    f.write(gzip.compress(data, compresslevel=9, mtime=0))
