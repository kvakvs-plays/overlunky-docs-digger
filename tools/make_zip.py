"""Writes a ZIP whose bytes depend only on the files' names and contents (fixed dates and attributes), so building the
site twice gives the same downloads. `cmake -E tar --format=zip` cannot: its local headers keep access times.

python tools/make_zip.py <zip> <file>...   (entries are named by the files' base names, in the given order)
"""
import os
import sys
import zipfile


def main():
    """Writes argv[1] from the files argv[2:]."""
    target, files = sys.argv[1], sys.argv[2:]
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for file in files:
            entry = zipfile.ZipInfo(os.path.basename(file), date_time=(2000, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            with open(file, 'rb') as f:
                archive.writestr(entry, f.read())
    return 0


if __name__ == '__main__':
    sys.exit(main())
