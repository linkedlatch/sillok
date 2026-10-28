"""Extracts members of a remote zip with HTTP range requests.

Only the central directory and the requested members are downloaded, not the
whole archive. Usage:
  python3 -I zip_members.py URL OUT_DIR MEMBER...
Members are written under their base name, so the archive cannot write
outside OUT_DIR. Prints the bytes downloaded.
"""
import io
import os
import sys
import urllib.request
import zipfile


class RemoteFile(io.RawIOBase):
    """Seekable read-only view of a URL that fetches byte ranges on demand."""

    def __init__(self, url):
        self.url = url
        request = urllib.request.Request(url, method="HEAD")
        with urllib.request.urlopen(request) as response:
            self.size = int(response.headers["Content-Length"])
        self.position = 0
        self.downloaded = 0

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self.position, io.SEEK_END: self.size}[whence]
        self.position = base + offset
        return self.position

    def read(self, size=-1):
        end = self.size if size < 0 else min(self.size, self.position + size)
        if end <= self.position:
            return b""
        request = urllib.request.Request(
            self.url, headers={"Range": f"bytes={self.position}-{end - 1}"})
        with urllib.request.urlopen(request) as response:
            data = response.read()
        self.downloaded += len(data)
        self.position += len(data)
        return data


def main(url, out_dir, members):
    os.makedirs(out_dir, exist_ok=True)
    remote = RemoteFile(url)
    with zipfile.ZipFile(remote) as archive:
        for member in members:
            with open(os.path.join(out_dir, os.path.basename(member)), "wb") as out:
                out.write(archive.read(member))
    print(f"downloaded {remote.downloaded / 1e6:.2f} MB of {remote.size / 1e6:.1f} MB")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
