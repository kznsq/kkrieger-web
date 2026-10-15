"""Reader for werkkzeug3 documents (.k files) and the operator class table.

The .k layout follows WerkkzeugApp::Read / WerkDoc::Read in
fr_public/werkkzeug3/werkkzeug.cpp. Everything is little-endian 32-bit words.
"""
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WZ3 = ROOT / "fr_public" / "werkkzeug3"

KC = {"KC_NULL": 0, "KC_BITMAP": 1, "KC_MINMESH": 2, "KC_SCENE": 3, "KC_KKRIEGER": 4,
      "KC_MATERIAL": 5, "KC_MESH": 6, "KC_IPP": 8, "KC_EFFECT": 9, "KC_SMESH": 10,
      "KC_DEMO": 11, "KC_SCENE2": 12, "KC_SPLINE": 13, "KC_IMGLIST": 14, "KC_ANY": 255}
KC_NAME = {v: k[3:].lower() for k, v in KC.items()}

MAGIC_BEGIN = 0x3c3c3c3c
MAGIC_END = 0x3e3e3e3e
CID_WERKKZEUG = 0x01000100


def opc_data(c): return c & 0xff
def opc_input(c): return (c >> 8) & 0xf
def opc_link(c): return (c >> 12) & 0xf
def opc_string(c): return (c >> 16) & 0x7
def opc_spline(c): return (c >> 20) & 0x7


@dataclass
class WerkClass:
    name: str
    id: int
    output: int
    min_in: int
    max_in: int
    convention: int
    packing: str
    edit: str
    init: str
    exec: str
    inputs: list
    links: list


def _split_top(s):
    """Split a C initializer body at top-level commas."""
    out, depth, cur, q = [], 0, "", None
    for ch in s:
        if q:
            cur += ch
            if ch == q and not cur.endswith("\\" + q):
                q = None
            continue
        if ch in "\"'":
            q = ch
            cur += ch
        elif ch == "{":
            depth += 1
            cur += ch
        elif ch == "}":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _kc_list(s):
    s = s.strip().strip("{}").strip()
    if not s or s == "0":
        return []
    return [KC.get(x.strip(), x.strip()) for x in s.split(",") if x.strip()]


def load_classes():
    src = (WZ3 / "werkops.cpp").read_text(encoding="latin-1")
    start = src.index("WerkClass WerkClasses[] =")
    body = src[src.index("{", start) + 1:]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    classes = {}
    depth, cur = 0, None
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = ""
                continue
        elif ch == "}":
            depth -= 1
            if depth == 0:
                parts = _split_top(cur)
                if parts and parts[0] != "0":
                    p = parts + ["0"] * (14 - len(parts))
                    name = p[0].strip('"')
                    cls = WerkClass(
                        name=name, id=int(p[1], 0), output=KC.get(p[2], p[2]),
                        min_in=int(eval(p[3].replace("KK_MAXINPUT", "256"))),
                        max_in=int(eval(p[4].replace("KK_MAXINPUT", "256"))),
                        convention=int(p[5], 0),
                        packing="".join(re.findall(r'"([^"]*)"', p[8])),
                        edit=p[9], init=p[10], exec=p[11],
                        inputs=_kc_list(p[12]), links=_kc_list(p[13]))
                    classes[cls.id] = cls
                cur = None
                continue
            if depth < 0:
                break
        if cur is not None:
            cur += ch
    return classes


@dataclass
class Op:
    index: int
    cls_id: int
    page: int
    x: int
    y: int
    w: int
    conv: int
    scratch: bool
    bypass: int
    hide: int
    name: str
    links: list
    params: list            # raw 32-bit words
    strings: list
    splines: list
    blob: bytes = b""
    anim_ops: list = field(default_factory=list)


@dataclass
class Page:
    name: str
    owner: str
    tree_level: int
    has_scratch: bool
    uses_scratch: bool


class Reader:
    def __init__(self, data: bytes, pos=0):
        self.d = data
        self.p = pos

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def s32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def words(self, n):
        v = list(struct.unpack_from("<%dI" % n, self.d, self.p))
        self.p += 4 * n
        return v

    def string(self):
        n = self.u32()
        s = self.d[self.p:self.p + n]
        self.p += (n + 3) // 4 * 4
        return s.split(b"\0")[0].decode("latin-1")


@dataclass
class Doc:
    version: int
    pages: list
    ops: list
    events: list
    splines: list
    demo: dict


def read_doc(path):
    data = Path(path).read_bytes()
    r = Reader(data)
    if r.u32() == struct.unpack("<I", b"nova")[0]:
        assert r.u32() == 1
    else:
        r.p = 0
    assert r.u32() == MAGIC_BEGIN and r.u32() == CID_WERKKZEUG
    version = r.u32()
    size = r.u32()
    assert 1 <= version <= 14
    ec, pc, oc, ac = r.u32(), r.u32(), r.u32(), r.u32()
    r.p += 16

    pages = []
    for _ in range(pc):
        if version >= 3:
            r.u32(); r.u32(); r.u32(); r.u32(); r.u32()
            has_scratch = r.u32()
            uses_scratch = r.u32()
            tree = r.u32() & 0xffffff
            name, owner, _next = r.string(), r.string(), r.string()
        else:
            r.u32(); r.u32(); r.u32(); r.u32()
            name, owner, tree, has_scratch, uses_scratch = r.string(), "", 0, 0, 0
        pages.append(Page(name, owner, tree, bool(has_scratch), bool(uses_scratch)))

    ops = []
    for i in range(oc):
        w = r.words(8)
        cls_id, _sel, x, y, wd, page, conv, iol = w
        bypass = stringcount = oacount = hide = 0
        if version >= 5:
            bypass, stringcount, oacount, hide = r.u32(), r.u32(), r.u32(), r.u32()
        blobsize = r.u32() if version >= 12 else 0
        name = r.string()
        links = [r.string() for _ in range(opc_link(conv))]
        params = r.words(opc_data(conv))
        strings = []
        if version >= 6:
            strings = [r.string() for _ in range(stringcount)]
        elif conv & 0x000f0000:
            strings = [r.d[r.p:r.p + 32].split(b"\0")[0].decode("latin-1")]
            r.p += 32
        splineconv = opc_spline(conv) if version >= 12 else (conv & 0x00f00000) >> 20
        splines = [r.string() for _ in range(splineconv)]
        anim_ops = []
        if version >= 7:
            for _ in range(oacount):
                px, py, w_, byte, para, uk, autoop, _pad = r.words(8)
                extra = None
                if uk == 1:
                    extra = [r.f32() for _ in range(4)]
                elif uk == 3:
                    extra = r.f32()
                elif uk == 2:
                    extra = r.u32()
                elif uk == 4:
                    extra = r.string()
                elif uk in (6, 7):
                    extra = (r.f32(), r.f32())
                anim_ops.append((px, py, w_, byte, para, uk, autoop, extra))
        op = Op(i, cls_id, page, x, y, wd, conv, bool(iol), bypass, hide, name,
                links, params, strings, splines, anim_ops=anim_ops)
        op._blobsize = blobsize
        ops.append(op)

    events = []
    for _ in range(ec):
        ev = {"op": r.s32()}
        ev["start"], ev["end"], ev["select"] = r.s32(), r.s32(), r.s32()
        ev["velocity"], ev["modulation"] = r.f32(), r.f32()
        ev["line"], ev["sel"] = r.s32(), r.s32()
        if version >= 2:
            ev["mark"], ev["color"], ev["disabled"] = r.s32(), r.u32(), r.s32()
            r.u32()
        ev["name"] = r.string()
        if version >= 8:
            ev["srt"] = [r.f32() for _ in range(9)]
        if version >= 10:
            ev["spline"] = r.string()
        if version >= 13:
            ev["interval"] = (r.f32(), r.f32())
        if version >= 14:
            ev["flags"] = r.u32()
        events.append(ev)

    if version < 7:
        for _ in range(ac):
            r.p += 24
            n = r.u32()
            r.p += 20 + 12 * n

    splines = []
    if version >= 7:
        for pi in range(pc):
            for _ in range(r.u32()):
                kind, count, inter, _ = r.words(4)
                sname = r.string()
                chans = []
                for _c in range(count):
                    keys = r.u32()
                    r.u32()
                    chans.append([(r.f32(), r.f32()) for _k in range(keys)])
                splines.append({"page": pi, "name": sname, "kind": kind,
                                "interpolation": inter, "channels": chans})

    if version >= 12:
        assert r.u32() == 0xdeadbeef
        for op in ops:
            if op._blobsize:
                op.blob = r.d[r.p:r.p + op._blobsize]
                r.p += (op._blobsize + 3) // 4 * 4
        assert r.u32() == 0xdeadbeef

    demo = {}
    if version >= 9:
        w = r.words(11)
        r.p += 20
        demo = {"res": (w[2], w[3]), "name": r.string(), "song": r.string()}
        if version >= 11:
            demo["sample"] = r.string()
    assert r.u32() == MAGIC_END, "document end marker missing"
    return Doc(version, pages, ops, events, splines, demo)


if __name__ == "__main__":
    from collections import Counter
    classes = load_classes()
    doc = read_doc(sys.argv[1])
    print("version", doc.version, "pages", len(doc.pages), "ops", len(doc.ops),
          "events", len(doc.events), "splines", len(doc.splines), doc.demo)
    cnt = Counter(op.cls_id for op in doc.ops)
    byout = Counter()
    for cid, n in sorted(cnt.items()):
        c = classes.get(cid)
        byout[KC_NAME.get(c.output, "?") if c else "?"] += n
        print("  0x%02x %-16s %-8s %5d" % (cid, c.name if c else "???",
              KC_NAME.get(c.output, c.output) if c else "", n))
    print(byout)


# ---------------------------------------------------------------------------
# Graph connection (port of WerkPage::Connect1/Connect2 and WerkDoc::Connect)

class Graph:
    def __init__(self, doc, classes):
        self.doc = doc
        self.classes = classes
        self.stores = {}
        self.active = []                     # ops on the active list of each page
        for op in doc.ops:
            op.inputs, op.link_ops, op.error = [], [], False
            op.cls = classes.get(op.cls_id)
        for pi, page in enumerate(doc.pages):
            scratch = page.has_scratch and page.uses_scratch
            ops = [o for o in doc.ops if o.page == pi and o.scratch == scratch]
            self._connect1(ops)
            self.active.extend(ops)
        for op in self.active:
            op.link_ops = [self.stores.get(n) if n else None for n in op.links]

    def _connect1(self, ops):
        ops.sort(key=lambda o: o.y)          # stable, like the bubble sort
        for i, po in enumerate(ops):
            if po.hide:
                continue
            ins = []
            for j in range(i - 1, -1, -1):
                pp = ops[j]
                if pp.y < po.y - 1:
                    break
                if pp.hide:
                    continue
                if pp.y == po.y - 1 and pp.x + pp.w > po.x and pp.x < po.x + po.w:
                    while pp is not None and pp.bypass:
                        pp = pp.inputs[0] if (not pp.error and pp.inputs) else None
                    if pp is not None:
                        ins.append(pp)
            ins.sort(key=lambda o: o.x)
            po.inputs = ins
            c = po.cls
            if c is None:
                po.error = True
            elif not (c.convention & 0x01000000) and not (c.min_in <= len(ins) <= c.max_in):
                po.error = True
            if po.name:
                if po.name in self.stores:
                    po.error = True
                    self.stores[po.name].error = True
                else:
                    self.stores[po.name] = po

    def result_class(self, op, depth=0):
        """Class of object an op yields, looking through load/store/nop."""
        if op is None or depth > 64:
            return None
        c = op.cls
        if c is None:
            return None
        if c.output != 255:
            return c.output
        if c.id == 0x01:
            return self.result_class(op.link_ops[0] if op.link_ops else None, depth + 1)
        if op.inputs:
            return self.result_class(op.inputs[0], depth + 1)
        return None


def summary(path):
    from collections import Counter
    classes = load_classes()
    doc = read_doc(path)
    g = Graph(doc, classes)
    named = Counter()
    for name, op in g.stores.items():
        named[KC_NAME.get(g.result_class(op), "?")] += 1
    print("active ops", len(g.active), "stores", len(g.stores), dict(named))
    return doc, g
