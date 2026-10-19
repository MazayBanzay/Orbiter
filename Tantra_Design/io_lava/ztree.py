"""Reader for Orbiter quadtree archives (Textures/<body>/Archive/<layer>.tree), Orbiter Developer Manual 5.1.4."""
import struct, zlib

class ZTree:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(48)
        magic, size, flags, self.dataOfs, self.dataLen, self.n, r1, r2, r3, r4a, r4b = struct.unpack('<4sIIIqIIIIII', h)
        assert magic[:2] == b'TX', magic
        self.f.seek(size)
        toc = self.f.read(self.n * 32)   # TreeNode is 8-byte aligned: 28 + 4 pad
        self.nodes = [struct.unpack_from('<qI4I', toc, i * 32) for i in range(self.n)]
        self.roots = {1: r1, 2: r2, 3: r3}
        self.root4 = (r4a, r4b)

    def index(self, lvl, ilat, ilng):
        """TOC index of tile [lvl, ilat, ilng] (lvl >= 4), or None."""
        if lvl < 4:
            i = self.roots.get(lvl); return None if i == 0xFFFFFFFF else i
        path = []
        while lvl > 4:
            path.append((ilat & 1) * 2 + (ilng & 1)); ilat >>= 1; ilng >>= 1; lvl -= 1
        i = self.root4[ilng]
        for q in reversed(path):
            if i == 0xFFFFFFFF: return None
            i = self.nodes[i][2 + q]
        return None if i == 0xFFFFFFFF else i

    def read(self, lvl, ilat, ilng):
        i = self.index(lvl, ilat, ilng)
        if i is None or self.nodes[i][1] == 0: return None
        pos = self.nodes[i][0]
        end = self.nodes[i + 1][0] if i + 1 < self.n else self.dataLen
        self.f.seek(self.dataOfs + pos)
        return zlib.decompress(self.f.read(end - pos))

    def deepest(self, lvl, ilat, ilng):
        """Deepest ancestor (incl. self) with data: (lvl, ilat, ilng, bytes)."""
        while lvl >= 4:
            d = self.read(lvl, ilat, ilng)
            if d: return lvl, ilat, ilng, d
            lvl -= 1; ilat >>= 1; ilng >>= 1
        return None
