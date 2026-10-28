"""Extracts the complete .wav members of a truncated .tar.gz download.

Usage: python3 -I extract_head.py ARCHIVE OUT_DIR
Only plain file names are written, so a hostile archive cannot escape OUT_DIR.
"""
import os
import sys
import tarfile
import zlib


def main(archive, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    count = 0
    try:
        with tarfile.open(archive, mode="r|gz") as tar:
            for member in tar:
                name = os.path.basename(member.name)
                if not member.isfile() or not name.endswith(".wav"):
                    continue
                data = tar.extractfile(member).read()
                if len(data) != member.size:
                    break  # cut off by the truncated download
                with open(os.path.join(out_dir, name), "wb") as out:
                    out.write(data)
                count += 1
    except (EOFError, tarfile.ReadError, zlib.error):
        pass  # the download ends inside the next member
    print(f"{count} files")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
