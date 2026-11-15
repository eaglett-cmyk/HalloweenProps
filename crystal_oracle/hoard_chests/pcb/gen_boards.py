#!/usr/bin/env python3
"""
gen_boards.py - builds the two Hoard Chest PCBs from scratch.

    "C:\\Program Files\\KiCad\\9.0\\bin\\python.exe" gen_boards.py          (both boards)
    "C:\\Program Files\\KiCad\\9.0\\bin\\python.exe" gen_boards.py hub|node

Writes hub/hoard_hub.kicad_pcb and node/hoard_node.kicad_pcb (+ .kicad_pro with the rules).
Everything is placed by coordinates in this file and routed by the small grid router below
(A* maze router, 0.635 mm grid, two layers, 45-degree moves, vias, through-hole pads usable as
layer changes).  GND is carried by filled zones on both layers plus stitching vias.

Design coordinates in this file: millimetres, X right, Y UP, origin = bottom-left board corner
(the brief's convention).  The .kicad_pcb uses KiCad's Y-down page coordinates; Design.k()/d()
convert.  See PCB-NOTES.md for the parts, netlist and the measurements.
"""
import os, sys, math, heapq, functools
import numpy as np
import pcbnew

print = functools.partial(print, flush=True)

FPLIB = r"C:\Program Files\KiCad\9.0\share\kicad\footprints"
HERE = os.path.dirname(os.path.abspath(__file__))
MM = pcbnew.FromMM

G = 0.635                      # routing grid (1/4 of 2.54)
CLR = 0.25                     # copper clearance
EDGE_CLR = 0.30                # copper to board edge
W_SIG = 0.30                   # signal track
W_WIDE = 0.80                  # routed +5V branches
W_STUB5 = 1.20                 # +5V inside each RJ45 (pin4-pin5 and to the trunk)
W_TRUNK = 1.50                 # +5V trunk
VIA_D, VIA_DRILL = 0.8, 0.4
# track-centre-to-foreign-copper thresholds for the router masks (clearance + half width + margin)
T_CLS = [0.42, 0.45, 0.68]     # 0: orthogonal 0.3 track, 1: diagonal 0.3 track, 2: 0.8 track / via
VIA_COST = 7.0

F, B = 0, 1
LAYER = {F: pcbnew.F_Cu, B: pcbnew.B_Cu}


# ----------------------------------------------------------------------------- geometry helpers
def loc2des(rot, lx, ly):
    """footprint-local offset (KiCad local, y down) -> design offset (y up) for rotation rot."""
    a = math.radians(rot)
    c, s = round(math.cos(a)), round(math.sin(a))
    kx = lx * c + ly * s
    ky = -lx * s + ly * c
    return (kx, -ky)


def sdist(shape, X, Y):
    k = shape[0]
    if k == 'C':
        _, x, y, r = shape
        return np.hypot(X - x, Y - y) - r
    if k == 'R':
        _, x, y, hw, hh = shape
        qx = np.abs(X - x) - hw
        qy = np.abs(Y - y) - hh
        return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0)
    if k == 'S':
        _, x1, y1, x2, y2, hw = shape
        dx, dy = x2 - x1, y2 - y1
        L2 = dx * dx + dy * dy
        if L2 < 1e-12:
            t = 0.0 * X
        else:
            t = np.clip(((X - x1) * dx + (Y - y1) * dy) / L2, 0.0, 1.0)
        return np.hypot(X - (x1 + t * dx), Y - (y1 + t * dy)) - hw
    raise ValueError(shape)


def sbbox(shape):
    k = shape[0]
    if k == 'C':
        _, x, y, r = shape
        return (x - r, y - r, x + r, y + r)
    if k == 'R':
        _, x, y, hw, hh = shape
        return (x - hw, y - hh, x + hw, y + hh)
    _, x1, y1, x2, y2, hw = shape
    return (min(x1, x2) - hw, min(y1, y2) - hw, max(x1, x2) + hw, max(y1, y2) + hw)


def sd1(shape, x, y):
    return float(sdist(shape, np.array(x, float), np.array(y, float)))


# ----------------------------------------------------------------------------- router
class Router:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.NX = int(math.floor(W / G + 1e-9)) + 1
        self.NY = int(math.floor(H / G + 1e-9)) + 1
        self.FX, self.FY = 2 * self.NX - 1, 2 * self.NY - 1
        self.own = np.zeros((2, 3, self.FX, self.FY), np.int32)
        self.holes = np.zeros((self.FX, self.FY), bool)   # True = a new 0.4 mm via drill would be too close to a hole
        fx = np.arange(self.FX) * (G / 2)
        fy = np.arange(self.FY) * (G / 2)
        self.XF, self.YF = np.meshgrid(fx, fy, indexing='ij')
        cx = np.arange(self.NX) * G
        cy = np.arange(self.NY) * G
        self.XC, self.YC = np.meshgrid(cx, cy, indexing='ij')

    def block_outline(self, rr):
        W, H = self.W, self.H
        qx = np.abs(self.XF - W / 2) - (W / 2 - rr)
        qy = np.abs(self.YF - H / 2) - (H / 2 - rr)
        sdf = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rr
        inner = -sdf
        for c, thr in enumerate([W_SIG / 2 + EDGE_CLR + 0.05, W_SIG / 2 + EDGE_CLR + 0.05,
                                 W_WIDE / 2 + EDGE_CLR + 0.05]):
            for l in (F, B):
                self.own[l, c][inner < thr] = -1

    def _win(self, bb, m, step, nx, ny):
        i0 = max(0, int(math.floor((bb[0] - m) / step)))
        i1 = min(nx - 1, int(math.ceil((bb[2] + m) / step)))
        j0 = max(0, int(math.floor((bb[1] - m) / step)))
        j1 = min(ny - 1, int(math.ceil((bb[3] + m) / step)))
        return i0, i1, j0, j1

    def add(self, shape, layers, net):
        """mark copper of `net` (net<0: blocks everything)."""
        bb = sbbox(shape)
        i0, i1, j0, j1 = self._win(bb, T_CLS[2] + 0.05, G / 2, self.FX, self.FY)
        if i1 < i0 or j1 < j0:
            return
        X = self.XF[i0:i1 + 1, j0:j1 + 1]
        Y = self.YF[i0:i1 + 1, j0:j1 + 1]
        d = sdist(shape, X, Y)
        for l in layers:
            for c in range(3):
                sel = d < T_CLS[c]
                if not sel.any():
                    continue
                sub = self.own[l, c, i0:i1 + 1, j0:j1 + 1]
                v = sub[sel]
                if net < 0:
                    v = np.full_like(v, -1)
                else:
                    v = np.where((v == 0) | (v == net), net, -1)
                sub[sel] = v

    def add_hole(self, x, y, r):
        rr = r + VIA_DRILL / 2 + 0.25 + 0.08
        i0, i1, j0, j1 = self._win((x, y, x, y), rr, G / 2, self.FX, self.FY)
        X = self.XF[i0:i1 + 1, j0:j1 + 1]
        Y = self.YF[i0:i1 + 1, j0:j1 + 1]
        self.holes[i0:i1 + 1, j0:j1 + 1] |= np.hypot(X - x, Y - y) < rr

    def cells_inside(self, shape, layers, margin=0.02):
        bb = sbbox(shape)
        i0, i1, j0, j1 = self._win(bb, G, G, self.NX, self.NY)
        X = self.XC[i0:i1 + 1, j0:j1 + 1]
        Y = self.YC[i0:i1 + 1, j0:j1 + 1]
        d = sdist(shape, X, Y)
        ii, jj = np.nonzero(d < -margin)
        out = []
        for l in layers:
            out += [(l, int(i0 + a), int(j0 + b)) for a, b in zip(ii, jj)]
        return out

    def nid(self, l, i, j):
        return (l * self.NX + i) * self.NY + j

    def route(self, net, srcs, dsts, wide, tht):
        NX, NY, FY = self.NX, self.NY, self.FY
        free = (self.own == 0) | (self.own == net)
        fb = [[free[l, c].tobytes() for c in range(3)] for l in (F, B)]
        co, cd = (2, 2) if wide else (0, 1)
        fo = (fb[0][co], fb[1][co])
        fd = (fb[0][cd], fb[1][cd])
        nohole = (~self.holes).tobytes()
        fv = (bytes(a & b for a, b in zip(fb[0][2], nohole)), fb[1][2])
        dset = set()
        di0 = dj0 = 10 ** 9
        di1 = dj1 = -1
        for (l, i, j) in dsts:
            dset.add(self.nid(l, i, j))
            di0, di1, dj0, dj1 = min(di0, i), max(di1, i), min(dj0, j), max(dj1, j)

        def h(i, j):
            dx = max(di0 - i, 0, i - di1)
            dy = max(dj0 - j, 0, j - dj1)
            return max(dx, dy) + 0.414 * min(dx, dy)

        heap, g, came = [], {}, {}
        for (l, i, j) in srcs:
            n = self.nid(l, i, j)
            if n in dset:
                return [(l, i, j)]
            if not fo[l][(2 * i) * FY + 2 * j]:
                continue
            g[n] = 0.0
            came[n] = -1
            heapq.heappush(heap, (h(i, j), 0.0, n))
        moves = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
                 (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
        NN = NX * NY
        found = None
        pops = 0
        while heap:
            f, gc, n = heapq.heappop(heap)
            if gc > g.get(n, 1e18) + 1e-9:
                continue
            if n in dset:
                found = n
                break
            pops += 1
            if pops > 400000:
                break
            l, rem = divmod(n, NN)
            i, j = divmod(rem, NY)
            for di, dj, base in moves:
                ni, nj = i + di, j + dj
                if ni < 0 or nj < 0 or ni >= NX or nj >= NY:
                    continue
                arr = fd[l] if (di and dj) else fo[l]
                if not arr[(2 * i + di) * FY + 2 * j + dj] or not arr[(2 * ni) * FY + 2 * nj]:
                    continue
                if di and dj:
                    cost = base * 1.08
                elif (l == F and di) or (l == B and dj):
                    cost = base * 1.35          # F prefers vertical runs, B horizontal
                else:
                    cost = base
                nn = l * NN + ni * NY + nj
                ng = gc + cost
                if ng < g.get(nn, 1e18) - 1e-9:
                    g[nn] = ng
                    came[nn] = n
                    heapq.heappush(heap, (ng + h(ni, nj), ng, nn))
            ol = 1 - l
            if (i, j) in tht:
                c = 0.3
            elif fv[0][(2 * i) * FY + 2 * j] and fv[1][(2 * i) * FY + 2 * j]:
                c = VIA_COST
            else:
                c = None
            if c is not None:
                nn = ol * NN + rem
                ng = gc + c
                if ng < g.get(nn, 1e18) - 1e-9:
                    g[nn] = ng
                    came[nn] = n
                    heapq.heappush(heap, (ng + h(i, j), ng, nn))
        if found is None:
            return None
        path = []
        n = found
        while n != -1:
            l, rem = divmod(n, NN)
            i, j = divmod(rem, NY)
            path.append((l, i, j))
            n = came[n]
        path.reverse()
        return path


def path_geometry(path, tht):
    """-> segments [(layer,x1,y1,x2,y2)], vias [(x,y)]"""
    segs, vias = [], []

    def flush(run):
        if len(run) < 2:
            return
        pts = [run[0]]
        for k in range(1, len(run) - 1):
            a, b, c = run[k - 1], run[k], run[k + 1]
            if (b[1] - a[1], b[2] - a[2]) != (c[1] - b[1], c[2] - b[2]):
                pts.append(b)
        pts.append(run[-1])
        for a, b in zip(pts, pts[1:]):
            segs.append((a[0], a[1] * G, a[2] * G, b[1] * G, b[2] * G))

    run = [path[0]]
    for a, b in zip(path, path[1:]):
        if a[0] != b[0]:
            flush(run)
            if (a[1], a[2]) not in tht:
                vias.append((a[1] * G, a[2] * G))
            run = [b]
        else:
            run.append(b)
    flush(run)
    return segs, vias


# ----------------------------------------------------------------------------- design wrapper
class Design:
    def __init__(self, path, W, H, title):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        for ext in ('.kicad_pcb', '.kicad_pro', '.kicad_prl'):
            p = os.path.splitext(path)[0] + ext
            if os.path.exists(p):
                os.remove(p)
        self.path = path
        self.board = pcbnew.NewBoard(path)
        self.W, self.H = W, H
        self.ox, self.oy = 50.0, 50.0
        self.nets = {}
        self.netidx = {}
        self.fixed = []     # (shape, layers, netname, term) copper not made by the router
        self.noterm = set() # pad positions the router must not start from (they have escape stubs)
        self.gnd_tree_refs = None   # footprints whose GND pads get an explicit GND track tree
        self.texts = []     # silk text boxes already placed (design bbox)
        self.keepouts = []  # (x,y,r)
        self.routes = []
        tb = self.board.GetTitleBlock()
        tb.SetTitle(title)
        tb.SetDate("2026-11-12")
        tb.SetRevision("1")
        self.setup_rules()

    def gnd_tree_pad(self, p):
        if self.gnd_tree_refs is None:
            return False
        return p.GetParentFootprint().GetReference() in self.gnd_tree_refs

    # coordinates
    def k(self, x, y):
        return pcbnew.VECTOR2I(MM(float(self.ox + x)), MM(float(self.oy + self.H - y)))

    def d(self, v):
        return (pcbnew.ToMM(v.x) - self.ox, self.H - (pcbnew.ToMM(v.y) - self.oy))

    def dbox(self, box):
        x0, y0 = self.d(pcbnew.VECTOR2I(box.GetLeft(), box.GetBottom()))
        x1, y1 = self.d(pcbnew.VECTOR2I(box.GetRight(), box.GetTop()))
        return (x0, y0, x1, y1)

    def setup_rules(self):
        ds = self.board.GetDesignSettings()
        ds.SetBoardThickness(MM(1.6))
        ds.m_MinClearance = MM(0.2)
        ds.m_TrackMinWidth = MM(0.25)
        ds.m_ViasMinSize = MM(0.6)
        ds.m_MinThroughDrill = MM(0.3)
        ds.m_CopperEdgeClearance = MM(EDGE_CLR)
        ds.m_HoleClearance = MM(0.25)
        ds.m_HoleToHoleMin = MM(0.25)
        nc = ds.m_NetSettings.GetDefaultNetclass()
        nc.SetClearance(MM(CLR))
        nc.SetTrackWidth(MM(W_SIG))
        nc.SetViaDiameter(MM(VIA_D))
        nc.SetViaDrill(MM(VIA_DRILL))

    def net(self, name):
        if name is None:
            return None
        if name not in self.nets:
            n = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(n)
            self.nets[name] = n
            self.netidx[name] = len(self.netidx) + 1
        return self.nets[name]

    # board outline
    def outline(self, rr):
        W, H = self.W, self.H

        def seg(a, b):
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT)
            s.SetStart(self.k(*a))
            s.SetEnd(self.k(*b))
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(MM(0.1))
            self.board.Add(s)

        def arc(c, a0):
            s = pcbnew.PCB_SHAPE(self.board)
            s.SetShape(pcbnew.SHAPE_T_ARC)
            p = lambda a: (c[0] + rr * math.cos(math.radians(a)), c[1] + rr * math.sin(math.radians(a)))
            s.SetArcGeometry(self.k(*p(a0)), self.k(*p(a0 + 45)), self.k(*p(a0 + 90)))
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(MM(0.1))
            self.board.Add(s)

        seg((rr, 0), (W - rr, 0))
        seg((W, rr), (W, H - rr))
        seg((W - rr, H), (rr, H))
        seg((0, H - rr), (0, rr))
        arc((W - rr, rr), 270)
        arc((W - rr, H - rr), 0)
        arc((rr, H - rr), 90)
        arc((rr, rr), 180)
        self.rr = rr

    # footprints
    def place(self, lib, name, ref, value, x, y, rot, pins=None):
        fp = pcbnew.FootprintLoad(os.path.join(FPLIB, lib + ".pretty"), name)
        if fp is None:
            raise RuntimeError("footprint not found %s:%s" % (lib, name))
        fp.SetReference(ref)
        fp.SetValue(value)
        self.board.Add(fp)
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(self.k(x, y))
        for p in fp.Pads():
            num = p.GetNumber()
            if pins and num in pins and pins[num]:
                p.SetNet(self.net(pins[num]))
        fp._rot = rot
        fp._origin = (x, y)
        return fp

    def custom_fp(self, ref, value, libname, x, y):
        fp = pcbnew.FOOTPRINT(self.board)
        fp.SetFPID(pcbnew.LIB_ID("hoard", libname))
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPosition(self.k(x, y))
        self.board.Add(fp)
        fp._rot = 0
        fp._origin = (x, y)
        return fp

    def fp_pad(self, fp, num, x, y, size, drill, shape='C', net=None, npth=False):
        p = pcbnew.PAD(fp)
        p.SetNumber(num)
        if npth:
            p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
            p.SetLayerSet(p.UnplatedHoleMask())
        else:
            p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
            p.SetLayerSet(p.PTHMask())
        p.SetShape(pcbnew.F_Cu, pcbnew.PAD_SHAPE_RECTANGLE if shape == 'R' else pcbnew.PAD_SHAPE_CIRCLE)
        p.SetSize(pcbnew.F_Cu, pcbnew.VECTOR2I(MM(size), MM(size)))
        p.SetDrillSize(pcbnew.VECTOR2I(MM(drill), MM(drill)))
        fp.Add(p)
        p.SetPosition(self.k(x, y))
        if net:
            p.SetNet(self.net(net))
        return p

    def fp_line(self, fp, layer, a, b, w=0.12):
        s = pcbnew.PCB_SHAPE(fp)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(self.k(*a))
        s.SetEnd(self.k(*b))
        s.SetLayer(layer)
        s.SetWidth(MM(w))
        fp.Add(s)

    def fp_rect(self, fp, layer, x0, y0, x1, y1, w=0.12):
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            self.fp_line(fp, layer, a, b, w)

    def fp_circle(self, fp, layer, x, y, r, w=0.12):
        s = pcbnew.PCB_SHAPE(fp)
        s.SetShape(pcbnew.SHAPE_T_CIRCLE)
        s.SetCenter(self.k(x, y))
        s.SetEnd(self.k(x + r, y))
        s.SetLayer(layer)
        s.SetWidth(MM(w))
        fp.Add(s)

    def mount_hole(self, ref, x, y, keep, silk=True):
        fp = self.custom_fp(ref, "M3 3.4mm NPTH", "MountingHole_3.4mm_NPTH", x, y)
        self.fp_pad(fp, "", x, y, 3.4, 3.4, npth=True)
        self.fp_circle(fp, pcbnew.F_CrtYd, x, y, 3.0, 0.05)
        self.fp_circle(fp, pcbnew.F_Fab, x, y, keep, 0.1)
        if silk:
            self.fp_circle(fp, pcbnew.F_SilkS, x, y, 2.9, 0.15)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        self.keepouts.append((x, y, keep))
        return fp

    def keepout_area(self, x, y, r, pour_only=False):
        z = pcbnew.ZONE(self.board)
        z.SetIsRuleArea(True)
        ls = pcbnew.LSET()
        ls.AddLayer(pcbnew.F_Cu)
        ls.AddLayer(pcbnew.B_Cu)
        z.SetLayerSet(ls)
        z.SetDoNotAllowTracks(not pour_only)
        z.SetDoNotAllowVias(not pour_only)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        z.SetZoneName("hole keep-clear")
        o = z.Outline()
        o.NewOutline()
        for k in range(32):
            a = 2 * math.pi * k / 32
            p = self.k(x + r * math.cos(a), y + r * math.sin(a))
            o.Append(p.x, p.y)
        self.board.Add(z)

    # copper
    def track(self, pts, w, layer, net, fixed=True, term=None):
        """term: None = any grid cell inside the copper may be a route terminal; else list of points
        (only for the LAST segment; earlier segments get no terminal cells)."""
        nseg = len(pts) - 1
        for k, (a, b) in enumerate(zip(pts, pts[1:])):
            t = pcbnew.PCB_TRACK(self.board)
            t.SetStart(self.k(*a))
            t.SetEnd(self.k(*b))
            t.SetWidth(MM(w))
            t.SetLayer(LAYER[layer])
            t.SetNet(self.net(net))
            self.board.Add(t)
            if fixed:
                tm = None if term is None else (term if k == nseg - 1 else [])
                self.fixed.append((('S', a[0], a[1], b[0], b[1], w / 2), [layer], net, tm))

    def via(self, x, y, net):
        v = pcbnew.PCB_VIA(self.board)
        v.SetPosition(self.k(x, y))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetWidth(MM(VIA_D))
        v.SetDrill(MM(VIA_DRILL))
        v.SetNet(self.net(net))
        self.board.Add(v)
        return v

    def gnd_zones(self, clearance=0.3):
        for lay in (pcbnew.F_Cu, pcbnew.B_Cu):
            z = pcbnew.ZONE(self.board)
            z.SetLayer(lay)
            z.SetNet(self.net("GND"))
            z.SetLocalClearance(MM(clearance))
            z.SetMinThickness(MM(0.25))
            z.SetThermalReliefGap(MM(0.4))
            z.SetThermalReliefSpokeWidth(MM(0.5))
            z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
            z.SetZoneName("GND_" + ("F" if lay == pcbnew.F_Cu else "B"))
            o = z.Outline()
            o.NewOutline()
            for (x, y) in ((0, 0), (self.W, 0), (self.W, self.H), (0, self.H)):
                p = self.k(x, y)
                o.Append(p.x, p.y)
            self.board.Add(z)

    # pads -> router shapes
    def pad_shapes(self):
        out = []
        for fp in self.board.GetFootprints():
            for p in fp.Pads():
                x, y = self.d(p.GetPosition())
                attr = p.GetAttribute()
                if attr == pcbnew.PAD_ATTRIB_NPTH:
                    r = pcbnew.ToMM(p.GetDrillSize().x) / 2
                    out.append((('C', x, y, r), [F, B], None, p, False))
                    continue
                sz = p.GetSize(pcbnew.F_Cu)
                sx, sy = pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)
                ang = p.GetOrientationDegrees() % 180
                if abs(ang - 90) < 1:
                    sx, sy = sy, sx
                shp = p.GetShape(pcbnew.F_Cu)
                if shp == pcbnew.PAD_SHAPE_CIRCLE:
                    s = ('C', x, y, sx / 2)
                elif shp == pcbnew.PAD_SHAPE_OVAL:
                    if sx >= sy:
                        s = ('S', x - (sx - sy) / 2, y, x + (sx - sy) / 2, y, sy / 2)
                    else:
                        s = ('S', x, y - (sy - sx) / 2, x, y + (sy - sx) / 2, sx / 2)
                else:
                    s = ('R', x, y, sx / 2, sy / 2)
                tht = attr == pcbnew.PAD_ATTRIB_PTH
                lays = [F, B] if tht else ([F] if p.IsOnLayer(pcbnew.F_Cu) else [B])
                nm = p.GetNetname() or None
                out.append((s, lays, nm, p, tht))
        return out

    # ------------------------------------------------------------------ routing
    def route_nets(self, plan, wide_nets=(), max_iter=30, pre_gnd=()):
        """plan: ordered list of net names to route (GND excluded)."""
        R = Router(self.W, self.H)
        R.block_outline(self.rr)
        for (x, y, r) in self.keepouts:
            R.add(('C', x, y, r), [F, B], -1)
        pads = self.pad_shapes()
        items = {}
        tht = {}
        for (s, lays, nm, p, is_tht) in pads:
            ni = self.netidx.get(nm, -1) if nm else -1
            R.add(s, lays, ni)
            if p.GetDrillSize().x > 0:
                R.add_hole(s[1], s[2], pcbnew.ToMM(p.GetDrillSize().x) / 2)
            if nm == "GND" and not self.gnd_tree_pad(p):
                continue
            if nm:
                key = (round(s[1], 3), round(s[2], 3))
                items.setdefault(nm, []).append((s, lays, [] if key in self.noterm else None))
                if is_tht:
                    for (l, i, j) in R.cells_inside(s, [F]):
                        tht.setdefault(nm, set()).add((i, j))
        for (s, lays, nm, term) in self.fixed:
            R.add(s, lays, self.netidx[nm])
            if nm != "GND":
                items.setdefault(nm, []).append((s, lays, term))
        # GND escapes found necessary by an earlier build (pads that ended up boxed in by tracks):
        # a GND via is reserved at a spot that was solid pour, and the pad is wired to it first.
        gi = self.netidx.get("GND")
        gtht = set()
        for (s, lays, nm, p, is_tht) in pads:
            if nm == "GND" and is_tht:
                gtht.update((i, j) for (_, i, j) in R.cells_inside(s, [F]))
        for (px, py, tx, ty) in pre_gnd:
            pad = min((x for x in pads if x[2] == "GND"), key=lambda x: math.hypot(x[0][1] - px, x[0][2] - py))
            self.via(tx, ty, "GND")
            R.add(('C', tx, ty, VIA_D / 2), [F, B], gi)
            R.add_hole(tx, ty, VIA_DRILL / 2)
            srcs = R.cells_inside(pad[0], pad[1])
            ti, tj = int(round(tx / G)), int(round(ty / G))
            path = R.route(gi, srcs, [(F, ti, tj), (B, ti, tj)], False, gtht)
            if path is None:
                print("  !! GND escape failed for pad at (%.2f, %.2f)" % (px, py))
                continue
            segs, vias = path_geometry(path, gtht)
            for (l, x1, y1, x2, y2) in segs:
                self.track([(x1, y1), (x2, y2)], W_SIG, l, "GND", fixed=False)
                R.add(('S', x1, y1, x2, y2, W_SIG / 2), [l], gi)
            for (x, y) in vias:
                self.via(x, y, "GND")
                R.add(('C', x, y, VIA_D / 2), [F, B], gi)
                R.add_hole(x, y, VIA_DRILL / 2)
            print("  GND escape: pad (%.2f, %.2f) -> via (%.2f, %.2f)" % (px, py, tx, ty))
        base = R.own.copy()
        base_holes = R.holes.copy()

        # group items of each net that are already joined by fixed copper
        def touching(a, b):
            sa, la = a[0], a[1]
            sb, lb = b[0], b[1]
            if not set(la) & set(lb):
                return False
            for s1, s2 in ((sa, sb), (sb, sa)):
                if s1[0] == 'S':
                    for (x, y) in ((s1[1], s1[2]), (s1[3], s1[4])):
                        s2c = s2 if s2[0] != 'S' else s2[:5] + (s2[5],)
                        if sd1(s2c, x, y) <= 0.01:
                            return True
            return False

        groups = {}
        for nm, its in items.items():
            par = list(range(len(its)))

            def fnd(a):
                while par[a] != a:
                    par[a] = par[par[a]]
                    a = par[a]
                return a
            for a in range(len(its)):
                for b in range(a + 1, len(its)):
                    if touching(its[a], its[b]):
                        par[fnd(a)] = fnd(b)
            gs = {}
            for a in range(len(its)):
                gs.setdefault(fnd(a), []).append(its[a])
            cellgroups = []
            for g in gs.values():
                cells = set()
                for (s, lays, term) in g:
                    if term is None:
                        cells.update(R.cells_inside(s, lays))
                    else:
                        cells.update((l, int(round(x / G)), int(round(y / G))) for (x, y) in term for l in lays)
                if not cells:
                    raise RuntimeError("no grid cell inside an item of net %s: %r" % (nm, g[0][0]))
                cellgroups.append(cells)
            groups[nm] = cellgroups

        order = list(plan)
        best = None
        import random
        rng = random.Random(1)
        for it in range(max_iter):
            R.own[:] = base
            R.holes[:] = base_holes
            routes = []
            failed = []
            for nm in order:
                ok = self._route_one(R, nm, groups.get(nm, []), nm in wide_nets, set(tht.get(nm, set())), routes)
                if not ok:
                    failed.append(nm)
            print("  pass %d: %d nets, %d failed %s" % (it + 1, len(order), len(failed), failed[:12]))
            if best is None or len(failed) < len(best[1]):
                best = (routes, failed, R.own.copy(), R.holes.copy())
            if not failed:
                break
            rest = [n for n in order if n not in failed]
            if it % 3 == 2:
                head = rest[:2]           # keep the power/GND nets first, shuffle the others
                tail = rest[2:]
                rng.shuffle(tail)
                rest = head + tail
            order = failed + rest
        routes, failed, own, holes = best
        R.own[:] = own
        R.holes[:] = holes
        for (nm, segs, vias, w) in routes:
            for (l, x1, y1, x2, y2) in segs:
                self.track([(x1, y1), (x2, y2)], w, l, nm, fixed=False)
            for (x, y) in vias:
                self.via(x, y, nm)
        self.R = R
        self.routes = routes
        return failed

    def _route_one(self, R, nm, cellgroups, wide, tht, routes):
        if len(cellgroups) < 2:
            return True
        ni = self.netidx[nm]
        w = W_WIDE if wide else W_SIG
        gs = sorted(cellgroups, key=len, reverse=True)
        tree = set(gs[0])
        rest = [set(g) for g in gs[1:]]
        allok = True
        while rest:
            tarr = np.array([(i, j) for (_, i, j) in tree], float)

            def gdist(g):
                c = np.array([(i, j) for (_, i, j) in g], float).mean(axis=0)
                return float(np.min(np.abs(tarr - c).sum(axis=1)))
            rest.sort(key=gdist)
            g = rest.pop(0)
            path = R.route(ni, list(g), list(tree), wide, tht)
            if path is None:
                allok = False
                continue
            segs, vias = path_geometry(path, tht)
            for (l, x1, y1, x2, y2) in segs:
                R.add(('S', x1, y1, x2, y2, w / 2), [l], ni)
            for (x, y) in vias:
                R.add(('C', x, y, VIA_D / 2), [F, B], ni)
                R.add_hole(x, y, VIA_DRILL / 2)
                tht.add((int(round(x / G)), int(round(y / G))))
            routes.append((nm, segs, vias, w))
            tree.update(g)
            tree.update(path)
            for (l, i, j) in path:
                tree.add((1 - l, i, j)) if (i, j) in tht else None
            for (x, y) in vias:
                i, j = int(round(x / G)), int(round(y / G))
                tree.add((F, i, j))
                tree.add((B, i, j))
        return allok

    def stitch_gnd(self, step_cells=6, exclude=()):
        R = self.R
        gi = self.netidx["GND"]
        free = (R.own[:, 2] == 0) | (R.own[:, 2] == gi)
        vias = []
        for i in range(2, R.NX - 2, step_cells):
            for j in range(2, R.NY - 2, step_cells):
                fi, fj = 2 * i, 2 * j
                # require a clear neighbourhood so the via sits inside real pour on both layers
                win = free[:, max(0, fi - 3):fi + 4, max(0, fj - 3):fj + 4]
                if not win.all():
                    continue
                x, y = i * G, j * G
                if any(x0 <= x <= x1 and y0 <= y <= y1 for (x0, y0, x1, y1) in exclude):
                    continue
                if R.holes[fi, fj]:
                    continue
                vias.append(self.via(x, y, "GND"))
                R.add(('C', x, y, VIA_D / 2), [F, B], gi)
                R.add_hole(x, y, VIA_DRILL / 2)
        return vias

    def fill(self):
        filler = pcbnew.ZONE_FILLER(self.board)
        filler.Fill(self.board.Zones())

    def prune_stitches(self, vias):
        zones = [z for z in self.board.Zones() if not z.GetIsRuleArea()]
        removed = 0
        for v in vias:
            ok = True
            for z in zones:
                lay = z.GetLayer()
                polys = z.GetFilledPolysList(lay)
                if not polys.Contains(v.GetPosition()):
                    ok = False
            if not ok:
                self.board.Remove(v)
                removed += 1
        return removed

    # ------------------------------------------------------------------ silkscreen
    def pad_boxes(self):
        out = []
        for fp in self.board.GetFootprints():
            for p in fp.Pads():
                out.append(self.dbox(p.GetBoundingBox()))
        return out

    def courtyards(self):
        res = {}
        for fp in self.board.GetFootprints():
            xs, ys = [], []
            for g in fp.GraphicalItems():
                if g.GetLayer() == pcbnew.F_CrtYd:
                    b = self.dbox(g.GetBoundingBox())
                    xs += [b[0], b[2]]
                    ys += [b[1], b[3]]
            if xs:
                res[fp.GetReference()] = (min(xs), min(ys), max(xs), max(ys))
        return res

    def _fits(self, bb, own_ref=None, margin=0.25, allow_own_court=True):
        x0, y0, x1, y1 = bb
        e = 0.6
        dbg = os.environ.get("SILKDBG") and own_ref == os.environ.get("SILKDBG")
        if dbg:
            print("   try", [round(v, 2) for v in bb])
        if x0 < e or y0 < e or x1 > self.W - e or y1 > self.H - e:
            if dbg: print("    edge")
            return False

        def ov(a, b, m):
            return not (a[2] + m <= b[0] or b[2] + m <= a[0] or a[3] + m <= b[1] or b[3] + m <= a[1])
        for pb in self._padboxes:
            if ov(bb, pb, margin):
                if dbg: print("    pad", pb)
                return False
        for (tb, tl) in self.texts:
            if tl == self._cur_layer and ov(bb, tb, 0.3):
                if dbg: print("    text", tb)
                return False
        if self._cur_layer == pcbnew.F_SilkS:
            for sb in self._silkg:
                if ov(bb, sb, 0.15):
                    if dbg: print("    silk", sb)
                    return False
        for ref, cb in (self._courts.items() if self._cur_layer == pcbnew.F_SilkS else []):
            if ref == own_ref and allow_own_court:
                continue
            if ref.startswith("H") and ref[1:].isdigit():
                continue
            if ov(bb, cb, 0.05):
                if dbg: print("    court", ref, cb)
                return False
        for (x, y, r) in self.keepouts:
            cx = min(max(x, x0), x1)
            cy = min(max(y, y0), y1)
            if math.hypot(cx - x, cy - y) < 3.0:
                return False
        return True

    def silk_prepare(self):
        self._silkg = []
        for fp in self.board.GetFootprints():
            for g in fp.GraphicalItems():
                if g.GetLayer() == pcbnew.F_SilkS and g.GetClass() == "PCB_SHAPE":
                    self._silkg.append(self.dbox(g.GetBoundingBox()))
        self._padboxes = self.pad_boxes()
        self._courts = self.courtyards()

    def _style(self, t, size, thick, layer, angle):
        t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(thick))
        t.SetTextAngleDegrees(angle)
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
        t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
        if layer == pcbnew.B_SilkS:
            t.SetMirrored(True)

    def try_place(self, t, cands, own_ref=None, required=True, allow_own_court=True):
        self._cur_layer = t.GetLayer()
        if self._cur_layer == pcbnew.B_SilkS:
            allow_own_court = True
        for (x, y) in cands:
            t.SetPosition(self.k(x, y))
            bb = self.dbox(t.GetBoundingBox())
            if self._fits(bb, own_ref, allow_own_court=allow_own_court):
                self.texts.append((bb, self._cur_layer))
                return (x, y)
        if required:
            print("  !! no clean spot for text %r (%s)" % (t.GetText(), own_ref))
        x, y = cands[0]
        t.SetPosition(self.k(x, y))
        self.texts.append((self.dbox(t.GetBoundingBox()), self._cur_layer))
        return None

    def label(self, text, cands, size=1.0, thick=None, layer=pcbnew.F_SilkS, angle=0, required=True):
        t = pcbnew.PCB_TEXT(self.board)
        t.SetText(text)
        self._style(t, size, thick or max(0.15, size * 0.15), layer, angle)
        self.board.Add(t)
        return self.try_place(t, cands, required=required)

    def fp_texts(self, fp, size=1.0, vsize=0.8, value_on_silk=True, extra=None, skip=False):
        ref = fp.GetReference()
        cb = self._courts.get(ref)
        r, v = fp.Reference(), fp.Value()
        self._style(r, size, 0.15, pcbnew.F_SilkS, 0)
        self._style(v, vsize, 0.15 if vsize >= 0.8 else 0.12, pcbnew.F_SilkS if value_on_silk else pcbnew.F_Fab, 0)
        if skip:
            return
        x0, y0, x1, y1 = cb
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        cands = (extra or [])
        for off in (1.0, 1.25, 1.6):
            cands += [(cx, y1 + off), (cx, y0 - off), (x0 + 2, y1 + off), (x1 - 2, y1 + off),
                      (x0 + 2, y0 - off), (x1 - 2, y0 - off)]
        cands += [(x0 - 2.4, cy), (x1 + 2.4, cy), (cx, cy), (cx, cy + 1.6), (cx, cy - 1.6)]
        self.try_place(r, cands, ref)
        if value_on_silk:
            vc = []
            rx, ry = self.d(r.GetPosition())
            vc += [(rx, ry - 1.5), (rx, ry + 1.5), (rx, ry - 1.8), (rx, ry + 1.8)]
            vc += cands
            vc += [(cx + dx, cy + dy) for dy in (0, 1.2, -1.2, 2.4, -2.4) for dx in (0, 3, -3)]
            self.try_place(v, vc, ref)

    def clip_fp_silk(self):
        """trim footprint silk lines that run past the board edge (edge-mounted jacks)."""
        W, H, m = self.W, self.H, 0.35
        for fp in self.board.GetFootprints():
            for g in list(fp.GraphicalItems()):
                if g.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                    continue
                if g.GetClass() != "PCB_SHAPE" or g.GetShape() != pcbnew.SHAPE_T_SEGMENT:
                    continue
                a = self.d(g.GetStart())
                b = self.d(g.GetEnd())
                inside = lambda p: m <= p[0] <= W - m and m <= p[1] <= H - m
                if inside(a) and inside(b):
                    continue
                if not inside(a) and not inside(b):
                    fp.Remove(g)
                    continue
                if not inside(a):
                    a, b = b, a
                # a inside, b outside: clip b to the box
                for _ in range(40):
                    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
                    if inside(mid):
                        a2 = mid
                        a = a2
                    else:
                        b = mid
                inner = self.d(g.GetStart()) if inside(self.d(g.GetStart())) else self.d(g.GetEnd())
                g.SetStart(self.k(*inner))
                g.SetEnd(self.k(*a))

    def save(self):
        pcbnew.SaveBoard(self.path, self.board)
        print("  saved", self.path)


# ----------------------------------------------------------------------------- shared part bits
RJ_LIB, RJ_FP = "Connector_RJ", "RJ45_Amphenol_54602-x08_Horizontal"
RJ_VALUE = "54602-908LF"
RJ_FRONT = (4.445, 13.97)       # local: lateral centre, front face of the body
RJ_ROT = {'top': 180, 'bottom': 0, 'right': 90}


def place_rj45(D, ref, edge, c, nets, overhang=3.0):
    rot = RJ_ROT[edge]
    if edge == 'top':
        tgt = (c, D.H + overhang)
    elif edge == 'bottom':
        tgt = (c, -overhang)
    else:
        tgt = (D.W + overhang, c)
    off = loc2des(rot, *RJ_FRONT)
    ox, oy = tgt[0] - off[0], tgt[1] - off[1]
    fp = D.place(RJ_LIB, RJ_FP, ref, RJ_VALUE, ox, oy, rot, nets)
    # sanity check of my rotation maths against KiCad
    for p in fp.Pads():
        if p.GetNumber() == "3":
            px, py = D.d(p.GetPosition())
            ex = (ox + loc2des(rot, 2.54, 0)[0], oy + loc2des(rot, 2.54, 0)[1])
            assert abs(px - ex[0]) < 1e-3 and abs(py - ex[1]) < 1e-3, (ref, px, py, ex)
    L = lambda lx, ly: (ox + loc2des(rot, lx, ly)[0], oy + loc2des(rot, lx, ly)[1])
    fp._L = L
    for p in fp.Pads():
        if p.GetNetname() == "GND":
            p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
    return fp


def rj45_stubs(D, fp, nets, trunk_ly=None):
    """escape stubs for signal pins 1,3,7 (inward, F.Cu) and the +5V pin4-pin5 link (B.Cu)."""
    L = fp._L
    for pin, lx in ((1, 0.0), (3, 2.54), (7, 7.62)):
        a = L(lx, 0.0)
        b = L(lx, -4.44)
        g = (round(b[0] / G) * G, round(b[1] / G) * G)
        pts = [a, b] if math.hypot(g[0] - b[0], g[1] - b[1]) < 1e-6 else [a, b, g]
        D.track(pts, W_SIG, F, nets[str(pin)], term=[g])
        D.noterm.add((round(a[0], 3), round(a[1], 3)))
    D.track([L(3.81, -2.54), L(5.08, 0.0)], W_STUB5, B, "+5V")
    if trunk_ly is not None:
        D.track([L(5.08, 0.0), L(5.08, trunk_ly)], W_STUB5, B, "+5V")


def rj45_nets(n):
    return {"1": "LED_%d" % n, "2": "GND", "3": "SERVO_%d" % n, "4": "+5V", "5": "+5V",
            "6": "GND", "7": "SW_%d" % n, "8": "GND"}


# ----------------------------------------------------------------------------- HUB
ESP_LEFT = ["EN", "VP", "VN", "IO34", "IO35", "IO32", "IO33", "IO25", "IO26", "IO27", "IO14", "IO12", "IO13", "GND", "VIN"]
ESP_RIGHT = ["IO23", "IO22", "TX0", "RX0", "IO21", "IO19", "IO18", "IO5", "IO17", "IO16", "IO4", "IO2", "IO15", "GND", "3V3"]
ESP_NET = {"VIN": "+5V", "GND": "GND", "3V3": "+3V3", "IO13": "GPIO13", "IO34": "GPIO34", "IO25": "GPIO25",
           "IO26": "SERVO_EN", "IO4": "GPIO4", "IO16": "GPIO16", "IO17": "GPIO17", "IO18": "GPIO18",
           "IO19": "GPIO19", "IO32": "SCAN0", "IO33": "SCAN1", "IO27": "SCAN2", "IO14": "SCAN3",
           "IO21": "SDA", "IO22": "SCL"}


def mux_pins(com, sel, en, chan_prefix, spare):
    """CD74HCT4067 pin map. sel = nets for S0..S3."""
    p = {"1": com, "10": sel[0], "11": sel[1], "14": sel[2], "13": sel[3], "12": "GND", "15": en, "24": "+5V"}
    # I0..I7 = pins 9..2, I8..I15 = pins 23..16 ; channel n -> port n+1
    for ch in range(16):
        pin = 9 - ch if ch < 8 else 31 - ch
        p[str(pin)] = "%s_%d" % (chan_prefix, ch + 1)
    return p


def build_hub(pre_gnd=()):
    print("== HUB")
    W, H = 124.0, 112.0
    D = Design(os.path.join(HERE, "hub", "hoard_hub.kicad_pcb"), W, H, "Hoard Hub")
    D.outline(3.0)
    for k, (x, y) in enumerate([(4, 4), (120, 4), (4, 108), (120, 108)]):
        D.mount_hole("H%d" % (k + 1), x, y, 6.0)
        D.keepout_area(x, y, 6.0)
    for n in ["GND", "+5V", "+3V3"]:
        D.net(n)

    # ---- RJ45 ports
    TRUNK_LY = 2.75
    jacks = {}
    for k in range(5):
        n = k + 1
        jacks[n] = place_rj45(D, "J%d" % n, 'top', 16 + 19 * k, rj45_nets(n))
        n = k + 6
        jacks[n] = place_rj45(D, "J%d" % n, 'bottom', 16 + 19 * k, rj45_nets(n))
        n = k + 11
        jacks[n] = place_rj45(D, "J%d" % n, 'right', 18 + 19 * k, rj45_nets(n))
    for n, fp in jacks.items():
        rj45_stubs(D, fp, rj45_nets(n), TRUNK_LY)

    # +5V trunk (B.Cu, under the jack bodies between the front pin row and the locating pegs)
    yt = jacks[1]._L(0, TRUNK_LY)[1]          # 103.78
    xr = jacks[11]._L(0, TRUNK_LY)[0]         # 115.78
    yb = jacks[6]._L(0, TRUNK_LY)[1]          # 8.22
    D.track([(9.525, yt), (111.5, yt), (xr, 99.5), (xr, 12.0), (111.5, yb), jacks[6]._L(5.08, TRUNK_LY)], W_TRUNK, B, "+5V")

    # ---- power inputs
    # barrel jack: mouth facing -X, front face flush with the left edge, pad 1 (centre pin) at (13.7, 22)
    D.place("Connector_BarrelJack", "BarrelJack_CUI_PJ-102AH_Horizontal", "J_PWR", "PJ-102AH 5.5x2.1",
            13.7, 22.0, 270, {"1": "+5V", "2": "GND", "3": "GND"})
    # screw terminal, wire entry facing -X
    D.place("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
            "J_PWR2", "MKDS 1,5/2-5.08", 5.715, 87.63, 90, {"1": "+5V", "2": "GND"})
    D.place("Capacitor_THT", "CP_Radial_D10.0mm_P5.00mm", "C1", "1000uF 10V", 19.05, 31.75, 0, {"1": "+5V", "2": "GND"})
    # feeds: barrel -> C1 and -> port 6's +5V pins (which sit on the bottom trunk); terminal -> top trunk
    j6 = jacks[6]._L
    D.track([(13.7, 22.0), j6(3.81, -2.54)], W_TRUNK, B, "+5V")
    D.track([(13.7, 22.0), (19.05, 31.75)], W_TRUNK, B, "+5V")
    D.track([(5.715, 87.63), (9.525, 91.44), (9.525, yt)], W_TRUNK, B, "+5V")

    # ---- ESP32 DevKit socket (custom footprint, dual-width right row)
    xVIN, yL = 5.08, 68.58
    u1 = D.custom_fp("U1", "ESP32 DevKit V1 30p", "ESP32_DevKitV1_30pin_dual_width", xVIN, yL)
    for k in range(15):
        x = 40.64 - k * 2.54
        lab = ESP_LEFT[k]
        D.fp_pad(u1, str(k + 1), x, yL, 1.7, 1.0, 'R' if k == 0 else 'C', ESP_NET.get(lab, "U1_" + lab if lab != "GND" else "GND"))
        lab = ESP_RIGHT[k]
        net = ESP_NET.get(lab, "U1_" + lab)
        for yy in (yL - 25.4, yL - 22.86):
            D.fp_pad(u1, str(k + 16), x, yy, 1.7, 1.0, 'C', net)
        D.track([(x, yL - 25.4), (x, yL - 22.86)], 0.5, B, net)
    bx0, bx1, by0, by1 = 1.6, 53.4, (yL - 12.7) - 14.1, (yL - 12.7) + 14.1
    D.fp_rect(u1, pcbnew.F_Fab, bx0, by0, bx1, by1, 0.1)
    D.fp_rect(u1, pcbnew.F_CrtYd, bx0 - 0.25, by0 - 0.25, bx1 + 0.25, by1 + 0.25, 0.05)
    D.fp_rect(u1, pcbnew.F_SilkS, bx0, by0 - 0.05, bx1, by1 + 0.05, 0.12)

    # ---- multiplexers (SOIC-24W, top side) + their pull-down arrays and 100 nF
    so = ("Package_SO", "SOIC-24W_7.5x15.4mm_P1.27mm")
    D.place(*so, "U2", "CD74HCT4067M96", 82.55, 78.74, 270,
            mux_pins("SERVO_5V", ["GPIO16", "GPIO17", "GPIO18", "GPIO19"], "SERVO_EN", "SERVO", 16))
    D.place(*so, "U3", "CD74HCT4067M96", 82.55, 50.8, 270,
            mux_pins("PIXEL_COM", ["SCAN0", "SCAN1", "SCAN2", "SCAN3"], "GND", "LED", 16))
    D.place(*so, "U4", "CD74HCT4067M96", 27.94, 82.55, 270,
            mux_pins("SW_COM", ["SCAN0", "SCAN1", "SCAN2", "SCAN3"], "GND", "SW", 16))

    def rn(ref, x, y, rot, prefix, first):
        pins = {"1": "GND"}
        for k in range(8):
            pins[str(k + 2)] = "%s_%d" % (prefix, first + k)
        D.place("Resistor_THT", "R_Array_SIP9", ref, "10k x8 bussed", x, y, rot, pins)
    rn("RN1", 72.39, 88.9, 0, "SERVO", 1)
    rn("RN2", 95.25, 68.58, 180, "SERVO", 9)
    rn("RN3", 72.39, 60.96, 0, "LED", 1)
    rn("RN4", 95.25, 40.64, 180, "LED", 9)
    cap = ("Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm")
    D.place(*cap, "C3", "100nF", 93.98, 74.93, 90, {"1": "+5V", "2": "GND"})
    D.place(*cap, "C4", "100nF", 93.98, 46.99, 90, {"1": "+5V", "2": "GND"})
    D.place(*cap, "C5", "100nF", 38.1, 78.74, 90, {"1": "+5V", "2": "GND"})

    # ---- 74AHCT125 buffer
    D.place("Package_DIP", "DIP-14_W7.62mm_Socket_LongPads", "U5", "SN74AHCT125N", 76.2, 31.75, 270,
            {"1": "GND", "2": "GPIO25", "3": "SERVO_5V", "4": "GND", "5": "GPIO4", "6": "PIXEL_RAW", "7": "GND",
             "9": "GND", "10": "GND", "12": "GND", "13": "GND", "14": "+5V"})
    D.place(*cap, "C6", "100nF", 80.01, 24.13, 90, {"1": "+5V", "2": "GND"})
    ax = ("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal")
    D.place(*ax, "R4", "100R", 59.69, 35.56, 0, {"1": "PIXEL_RAW", "2": "PIXEL_COM"})
    D.place(*ax, "R1", "10k", 30.48, 22.86, 90, {"1": "SERVO_EN", "2": "+3V3"})
    D.place(*ax, "R2", "4k7", 33.655, 22.86, 90, {"1": "SW_COM", "2": "+3V3"})
    D.place(*ax, "R3", "1k", 36.83, 22.86, 90, {"1": "SW_COM", "2": "GPIO34"})

    # ---- headers, button, VIN cap
    D.place("Connector_PinHeader_2.54mm", "PinHeader_1x05_P2.54mm_Vertical", "J16", "SPARE PORT 16",
            86.36, 21.59, 90, {"1": "+5V", "2": "GND", "3": "LED_16", "4": "SERVO_16", "5": "SW_16"})
    D.place("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical", "J_I2C", "I2C",
            41.91, 26.67, 90, {"1": "+3V3", "2": "GND", "3": "SDA", "4": "SCL"})
    D.place("Button_Switch_THT", "SW_PUSH_6mm", "SW1", "TEST", 10.16, 77.47, 0, {"1": "GPIO13", "2": "GND"})
    D.place("Capacitor_THT", "CP_Radial_D6.3mm_P2.50mm", "C2", "100uF 10V", 3.81, 74.93, 0, {"1": "+5V", "2": "GND"})

    # ---- route
    D.gnd_tree_refs = {"U1", "U2", "U3", "U4", "U5", "RN1", "RN2", "RN3", "RN4", "C1", "C2", "C3", "C4", "C5",
                       "C6", "J16", "J_I2C", "SW1"}
    jack_nets = []
    for n in list(range(1, 16)):
        jack_nets += ["SERVO_%d" % n, "LED_%d" % n, "SW_%d" % n]
    plan = (["+5V", "GND", "GPIO16", "GPIO17", "GPIO18", "GPIO19", "SCAN0", "SCAN1", "SCAN2", "SCAN3",
             "SERVO_EN", "GPIO25", "GPIO4", "SERVO_5V", "PIXEL_RAW", "PIXEL_COM", "SW_COM", "GPIO34",
             "+3V3", "SDA", "SCL", "GPIO13", "SERVO_16", "LED_16", "SW_16"] + jack_nets)
    failed = D.route_nets(plan, wide_nets=("+5V",), pre_gnd=pre_gnd)
    finish(D, failed, hub_silk)
    return D, failed


def hub_silk(D):
    D.clip_fp_silk()
    D.silk_prepare()
    S = pcbnew.F_SilkS
    # port numbers + refs/values behind each jack
    for n in range(1, 16):
        fp = [f for f in D.board.GetFootprints() if f.GetReference() == "J%d" % n][0]
        L = fp._L if hasattr(fp, "_L") else None
        if n <= 5:
            cx, y = 16 + 19 * (n - 1), 95.0
            D.label(str(n), [(cx, y), (cx, y - 0.5)], size=2.0, thick=0.3)
            D.fp_texts(fp, extra=[(cx - 5.2, y), (cx + 5.2, y), (cx - 5.2, y - 1.8)])
        elif n <= 10:
            cx, y = 16 + 19 * (n - 6), 17.1
            D.label(str(n), [(cx, y), (cx + 1.5, y), (cx, y + 0.5), (cx + 1.5, y + 0.5)], size=2.0, thick=0.3)
            D.fp_texts(fp, extra=[(cx - 5.2, y), (cx + 5.2, y), (cx - 5.2, y + 1.8)])
        else:
            cy, x = 18 + 19 * (n - 11), 107.1
            D.label(str(n), [(x, cy), (x - 0.4, cy)], size=2.0, thick=0.3, angle=90)
            r, v = fp.Reference(), fp.Value()
            D._style(r, 1.0, 0.15, S, 90)
            D._style(v, 0.8, 0.15, S, 90)
            D.try_place(r, [(x, cy - 5.0), (x - 1.8, cy - 5.0), (x - 1.8, cy - 3.5)], fp.GetReference())
            D.try_place(v, [(x - 1.8, cy + 2.5), (x - 1.8, cy + 3.5), (x - 1.8, cy - 0.5), (x - 3.2, cy + 2.5), (x, cy + 5.6)], fp.GetReference())
    grid = lambda xs, ys: [(x, y) for y in ys for x in xs]
    D.label("HOARD HUB v1  2026-11", [(55.0, 91.4), (55.0, 91.0), (56.0, 91.4), (54.0, 91.4)], size=1.5, thick=0.25)
    D.label("SILVER 1-5", [(54.0, 88.6), (54.0, 88.0), (56.0, 88.6), (52.0, 88.6), (35.0, 92.6)], size=1.2)
    D.label("GOLD 6-10", [(54.0, 19.6), (35.0, 19.6), (73.0, 19.6), (54, 21), (62, 19.6), (45, 19.6)], size=1.2)
    D.label("GEM 11-15", [(101.6, 56.0), (101.6, 47.0), (101.6, 66), (101.0, 56), (101.6, 37), (101.6, 85), (101.0, 47), (101.0, 66), (101.6, 28)], size=1.2, angle=90)
    D.label("NOT ETHERNET", grid([62, 64, 60, 66, 58], [64.8, 64.4, 65.2]) + grid(range(56, 76, 2), [37.5, 38]) +
            grid(range(14, 52, 2), [39.5, 39.0]), size=2.0, thick=0.3)
    D.label("never plug into a network port", grid([62, 64, 60, 66, 58, 56], [62.6, 62.9, 63.1]) +
            grid(range(56, 76, 2), [35.7]) + grid(range(14, 52, 2), [37.6]), size=1.0, required=False)
    D.label("5V DC ONLY", grid([7.5, 7.0, 8.0, 6.5], [31.0, 31.4, 32.0, 32.5]), size=1.0)
    D.label("centre +", grid([7.5, 7.0, 8.0, 6.5], [29.0, 29.5, 30]), size=1.0)
    D.label("SERVO MUX", [(82.55, 78.74), (82.55, 79.5)], size=1.0, required=False)
    D.label("PIXEL MUX", [(82.55, 50.8), (82.55, 51.5)], size=1.0, required=False)
    D.label("SWITCH MUX", [(27.94, 82.55), (27.94, 83.3)], size=1.0, required=False)
    D.label("5V", [(2.0, 89.0), (11.5, 88.9), (12.0, 88.9)], size=0.9, required=False)
    D.label("GND", [(2.0, 94.0), (12.0, 94.0), (12.5, 94.0)], size=0.9, required=False)
    D.label("USB", [(1.6, 52.5), (1.8, 56), (1.6, 60)], size=0.9, angle=90, required=False)
    for fp in D.board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith("H") or (ref.startswith("J") and ref[1:].isdigit() and int(ref[1:]) <= 15):
            continue
        extra = {"J_PWR2": [(15.5, 92.4), (15.5, 92.8)], "J16": [(100.0, 21.59), (91.44, 26.6), (91.44, 27.2)],
                 "C2": [(5.06, 80.2), (5.06, 79.7)], "J_I2C": [(54.4, 26.67), (54.4, 28.0)]}.get(ref)
        D.fp_texts(fp, extra=extra)
    # pin names on the spare port and I2C header, U1 orientation hints
    for k, s in enumerate(["5V", "G", "LED", "SRV", "SW"]):
        D.label(s, [(86.36 + 2.54 * k, 23.95), (86.36 + 2.54 * k, 19.25)], size=0.8, thick=0.13, required=False)
    for k, s in enumerate(["3V3", "G", "SDA", "SCL"]):
        D.label(s, [(41.91 + 2.54 * k, 29.0), (41.91 + 2.54 * k, 24.4)], size=0.8, thick=0.13, required=False)
    D.label("VIN", [(5.08, 66.3)], size=0.8, thick=0.13, required=False)
    D.label("3V3", [(5.08, 41.0), (5.08, 47.9)], size=0.8, thick=0.13, required=False)
    D.label("EN", [(40.64, 66.3)], size=0.8, thick=0.13, required=False)
    D.label("antenna >", [(47.0, 56.0)], size=1.0, required=False)


# ----------------------------------------------------------------------------- NODE
def build_node(pre_gnd=()):
    print("== NODE")
    W, H = 42.0, 20.0
    D = Design(os.path.join(HERE, "node", "hoard_node.kicad_pcb"), W, H, "Hoard Node")
    D.outline(1.0)
    D.mount_hole("H1", 30.0, 5.0, 3.5, silk=False)
    D.keepout_area(30.0, 5.0, 3.5)
    for n in ["GND", "+5V"]:
        D.net(n)
    nets = {"1": "LED", "2": "GND", "3": "SERVO", "4": "+5V", "5": "+5V", "6": "GND", "7": "SW", "8": "GND"}
    j1 = place_rj45(D, "J1", 'bottom', 9.5, nets)
    rj45_stubs(D, j1, nets, None)
    D.place("Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical", "J2", "SERVO",
            21.59, 16.51, 90, {"1": "GND", "2": "+5V", "3": "SERVO"})
    # PT 1,5/5-3,5-H: wire entry faces +X; footprint pin numbers run top->bottom, so the nets are
    # assigned to give the order +5V, LED, GND, SW, GND from the back (y=0) edge.
    D.place("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_PT-1,5-5-3.5-H_1x05_P3.50mm_Horizontal",
            "J3", "PT 1,5/5-3,5-H", 38.9, 17.0, 270,
            {"5": "+5V", "4": "LED", "3": "GND", "2": "SW", "1": "GND"})
    D.place("Capacitor_THT", "CP_Radial_D8.0mm_P3.50mm", "C1", "470uF 10V", 20.32, 5.715, 0, {"1": "+5V", "2": "GND"})
    failed = D.route_nets(["+5V", "SERVO", "LED", "SW"], wide_nets=("+5V",), pre_gnd=pre_gnd)
    finish(D, failed, node_silk, stitch_step=4)
    return D, failed


def node_silk(D):
    D.clip_fp_silk()
    D.silk_prepare()
    D.label("HOARD NODE v1", [(9.5, 18.6), (9.5, 18.3), (9.0, 18.6)], size=1.1, thick=0.18)
    D.label("NOT ETHERNET", [(9.5, 16.7), (9.5, 16.5), (25.0, 2.0), (25, 1.5)], size=1.0)
    for y, s in ((3.0, "5V"), (6.5, "D"), (10.0, "G"), (13.5, "SW"), (17.0, "G")):
        D.label(s, [(33.2, y), (33.0, y), (33.4, y)], size=0.9, required=False)
    D.label("5V D G SW G", [(35.8, 10.0)], size=0.9, layer=pcbnew.B_SilkS, angle=90, required=False)
    D.label("NOT ETHERNET", [(25.0, 18.6)], size=0.9, layer=pcbnew.B_SilkS, required=False)
    for k, s in enumerate(["G", "5V", "S"]):
        D.label(s, [(21.59 + 2.54 * k, 18.9), (21.59 + 2.54 * k, 14.3)], size=0.8, thick=0.13, required=False)
    for fp in D.board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith("H"):
            continue
        if ref == "J3":
            D.fp_texts(fp, size=0.9, vsize=0.8, value_on_silk=False, skip=True)
            r = fp.Reference()
            D._style(r, 0.9, 0.15, pcbnew.F_SilkS, 90)
            D.try_place(r, [(35.9, 8.25), (35.9, 11.75), (35.9, 4.75)], ref)
            continue
        D.fp_texts(fp, size=0.9, vsize=0.8, value_on_silk=False)


# ----------------------------------------------------------------------------- finish
def finish(D, failed, silk_fn, stitch_step=6):
    if failed:
        print("  !! UNROUTED NETS:", failed)
    silk_fn(D)
    for fp in D.board.GetFootprints():
        if fp.GetReference().startswith("H"):
            continue
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                x, y = D.d(p.GetPosition())
                D.keepout_area(x, y, pcbnew.ToMM(p.GetDrillSize().x) / 2 + 0.5, pour_only=True)
    D.gnd_zones()
    vias = D.stitch_gnd(stitch_step)
    D.fill()
    rm = D.prune_stitches(vias)
    if rm:
        D.fill()
    print("  stitching vias: %d placed, %d pruned" % (len(vias), rm))
    gnd_repair(D, vias)
    D.save()
    fix_starved_thermals(D)


# ----------------------------------------------------------------------------- GND connectivity repair
def fill_rasters(D, R):
    """rasterise the filled GND pour of each layer onto the router's fine grid (scanline, even-odd)."""
    rast = np.zeros((2, R.FX, R.FY), bool)
    ys = np.arange(R.FY) * (G / 2)
    for z in D.board.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != "GND":
            continue
        l = F if z.GetLayer() == pcbnew.F_Cu else B
        ps = z.GetFilledPolysList(z.GetLayer())
        edges = []
        for oi in range(ps.OutlineCount()):
            rings = [ps.Outline(oi)] + [ps.Hole(oi, h) for h in range(ps.HoleCount(oi))]
            for ring in rings:
                n = ring.PointCount()
                if n < 3:
                    continue
                pts = np.array([D.d(ring.CPoint(k)) for k in range(n)])
                edges.append(np.hstack([pts, np.roll(pts, -1, axis=0)]))
        if not edges:
            continue
        E = np.vstack(edges)
        x1, y1, x2, y2 = E.T
        for j, y in enumerate(ys):
            m = ((y1 <= y) & (y2 > y)) | ((y2 <= y) & (y1 > y))
            if not m.any():
                continue
            xs = np.sort(x1[m] + (y - y1[m]) * (x2[m] - x1[m]) / (y2[m] - y1[m]))
            for a, b in zip(xs[0::2], xs[1::2]):
                i0 = max(0, int(math.ceil(a / (G / 2) - 1e-9)))
                i1 = min(R.FX - 1, int(math.floor(b / (G / 2) + 1e-9)))
                if i1 >= i0:
                    rast[l, i0:i1 + 1, j] = True
    return rast


def label4(mask):
    from collections import deque
    lab = np.zeros(mask.shape, np.int32)
    FX, FY = mask.shape
    flat = mask.ravel()
    lf = lab.ravel()
    n = 0
    for start in np.flatnonzero(flat):
        if lf[start]:
            continue
        n += 1
        lf[start] = n
        dq = deque([start])
        while dq:
            c = dq.popleft()
            i, j = divmod(c, FY)
            for nb, ok in ((c - FY, i > 0), (c + FY, i < FX - 1), (c - 1, j > 0), (c + 1, j < FY - 1)):
                if ok and flat[nb] and not lf[nb]:
                    lf[nb] = n
                    dq.append(nb)
    return lab, n


def gnd_items(D):
    """GND conductors that tie to the pour: (shape, layers, kind, obj)."""
    out = []
    for (s, lays, nm, p, tht) in D.pad_shapes():
        if nm == "GND":
            out.append((s, lays, 'pad', p))
    for t in D.board.GetTracks():
        if t.GetNetname() != "GND":
            continue
        if t.GetClass() == "PCB_VIA":
            x, y = D.d(t.GetPosition())
            out.append((('C', x, y, VIA_D / 2), [F, B], 'via', t))
        else:
            a, b = D.d(t.GetStart()), D.d(t.GetEnd())
            l = F if t.GetLayer() == pcbnew.F_Cu else B
            out.append((('S', a[0], a[1], b[0], b[1], pcbnew.ToMM(t.GetWidth()) / 2), [l], 'track', t))
    return out


def gnd_analyse(D, R):
    rast = fill_rasters(D, R)
    labs = [label4(rast[l]) for l in (F, B)]
    items = gnd_items(D)
    par = {}

    def fnd(a):
        par.setdefault(a, a)
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a

    def uni(a, b):
        par[fnd(a)] = fnd(b)
    for k, (s, lays, kind, obj) in enumerate(items):
        node = ('i', k)
        fnd(node)
        bb = sbbox(s)
        i0, i1, j0, j1 = R._win(bb, 0.6, G / 2, R.FX, R.FY)
        X = R.XF[i0:i1 + 1, j0:j1 + 1]
        Y = R.YF[i0:i1 + 1, j0:j1 + 1]
        d = sdist(s, X, Y)
        ring = (d > -0.3) & (d < 0.38)
        for l in lays:
            ls = labs[l][0][i0:i1 + 1, j0:j1 + 1]
            for c in np.unique(ls[ring & (ls > 0)]):
                uni(node, ('c', l, int(c)))
    # tracks touching pads/vias of GND are joined through the zone analysis only when they touch the
    # pour; join item-item contacts explicitly as well
    for a in range(len(items)):
        for b in range(a + 1, len(items)):
            sa, la = items[a][0], items[a][1]
            sb, lb = items[b][0], items[b][1]
            if not set(la) & set(lb):
                continue
            if items[a][2] != 'track' and items[b][2] != 'track':
                continue
            ta, tb = (sa, sb) if items[a][2] == 'track' else (sb, sa)
            for (x, y) in ((ta[1], ta[2]), (ta[3], ta[4])):
                if sd1(tb, x, y) <= 0.01:
                    uni(('i', a), ('i', b))
    clusters = {}
    for k, it in enumerate(items):
        clusters.setdefault(fnd(('i', k)), []).append(k)
    return rast, labs, items, clusters, fnd


def gnd_repair(D, stitch_vias, max_iter=10):
    R = D.R
    D.failed_gnd = []
    gi = D.netidx["GND"]
    tht = set()
    for (s, lays, nm, p, is_tht) in D.pad_shapes():
        if nm == "GND" and is_tht:
            tht.update((i, j) for (_, i, j) in R.cells_inside(s, [F]))
    for tr in D.board.GetTracks():
        if tr.GetClass() == "PCB_VIA" and tr.GetNetname() == "GND":
            x, y = D.d(tr.GetPosition())
            if abs(x / G - round(x / G)) < 1e-3 and abs(y / G - round(y / G)) < 1e-3:
                tht.add((int(round(x / G)), int(round(y / G))))
    for it in range(max_iter):
        rast, labs, items, clusters, fnd = gnd_analyse(D, R)
        padcl = {r: ks for r, ks in clusters.items() if any(items[k][2] == 'pad' for k in ks)}
        main = max(padcl, key=lambda r: sum(1 for k in padcl[r] if items[k][2] == 'pad'))
        others = [r for r in padcl if r != main]
        print("  GND check %d: %d pad clusters" % (it + 1, len(padcl)))
        if not others:
            break
        # component ids belonging to each cluster
        compcl = {}
        for l in (F, B):
            for c in range(1, labs[l][1] + 1):
                compcl[(l, c)] = fnd(('c', l, c))
        mainF = np.zeros(labs[F][1] + 1, bool)
        mainB = np.zeros(labs[B][1] + 1, bool)
        for (l, c), r in compcl.items():
            if r == main:
                (mainF if l == F else mainB)[c] = True
        inner = [None, None]
        for l in (F, B):
            m = rast[l].copy()
            er = m.copy()
            for di in range(-2, 3):
                for dj in range(-2, 3):
                    er[2:-2, 2:-2] &= m[2 + di:m.shape[0] - 2 + di, 2 + dj:m.shape[1] - 2 + dj]
            er[:2, :] = er[-2:, :] = False
            er[:, :2] = er[:, -2:] = False
            inner[l] = er
        viaok = ((R.own[F, 2] == 0) | (R.own[F, 2] == gi)) & ((R.own[B, 2] == 0) | (R.own[B, 2] == gi)) & ~R.holes
        fixed_any = False
        for r in others:
            ks = padcl[r]
            myF = np.zeros(labs[F][1] + 1, bool)
            myB = np.zeros(labs[B][1] + 1, bool)
            for (l, c), rr in compcl.items():
                if rr == r:
                    (myF if l == F else myB)[c] = True
            LF, LB = labs[F][0], labs[B][0]
            cand = inner[F] & inner[B] & viaok & ((myF[LF] & mainB[LB]) | (myB[LB] & mainF[LF]))
            pts = np.argwhere(cand)
            px = np.mean([items[k][0][1] for k in ks])
            py = np.mean([items[k][0][2] for k in ks])
            if len(pts):
                dd = np.hypot(pts[:, 0] * G / 2 - px, pts[:, 1] * G / 2 - py)
                fi, fj = pts[int(np.argmin(dd))]
                x, y = fi * G / 2, fj * G / 2
                D.via(x, y, "GND")
                R.add(('C', x, y, VIA_D / 2), [F, B], gi)
                R.add_hole(x, y, VIA_DRILL / 2)
                fixed_any = True
                continue
            # route a GND track from this cluster's pads to the main pour
            srcs = []
            for k in ks:
                s, lays, kind, obj = items[k]
                if kind == 'pad':
                    srcs += R.cells_inside(s, lays)
            dsts = []
            for l, mainc in ((F, mainF), (B, mainB)):
                sel = inner[l] & mainc[labs[l][0]]
                sel = sel[::2, ::2]
                ii, jj = np.nonzero(sel)
                dsts += [(l, int(a), int(b)) for a, b in zip(ii, jj)]
            path = R.route(gi, srcs, dsts, False, tht) if srcs and dsts else None
            if path is None:
                print("  !! could not join GND cluster near (%.1f, %.1f)" % (px, py))
                both = inner[F] & inner[B] & viaok & mainF[labs[F][0]] & mainB[labs[B][0]]
                pp = np.argwhere(both[::2, ::2])
                pk = [items[k][0] for k in ks if items[k][2] == 'pad'][0]
                if len(pp):
                    dd = np.hypot(pp[:, 0] * G - pk[1], pp[:, 1] * G - pk[2])
                    a = pp[int(np.argmin(dd))]
                    D.failed_gnd.append((pk[1], pk[2], float(a[0] * G), float(a[1] * G)))
                continue
            segs, vias = path_geometry(path, tht)
            for (l, x1, y1, x2, y2) in segs:
                D.track([(x1, y1), (x2, y2)], W_SIG, l, "GND", fixed=False)
                R.add(('S', x1, y1, x2, y2, W_SIG / 2), [l], gi)
            for (x, y) in vias:
                D.via(x, y, "GND")
                R.add(('C', x, y, VIA_D / 2), [F, B], gi)
                R.add_hole(x, y, VIA_DRILL / 2)
                tht.add((int(round(x / G)), int(round(y / G))))
            print("  GND track added near (%.1f, %.1f)" % (px, py))
            fixed_any = True
        D.fill()
        if not fixed_any or D.failed_gnd:
            break
    # drop stitching vias that ended up outside the main GND body (dead islands)
    rast, labs, items, clusters, fnd = gnd_analyse(D, R)
    padcl = {r: ks for r, ks in clusters.items() if any(items[k][2] == 'pad' for k in ks)}
    main = max(padcl, key=lambda r: sum(1 for k in padcl[r] if items[k][2] == 'pad'))
    dead = 0
    for r, ks in clusters.items():
        if r == main:
            continue
        for k in ks:
            if items[k][2] == 'via':
                D.board.Remove(items[k][3])
                dead += 1
    if dead:
        D.fill()
    print("  GND repair: removed %d dead stitching vias" % dead)


def fix_starved_thermals(D, rounds=10):
    import subprocess, json, tempfile
    cli = r"C:\Program Files\KiCad\9.0\bin\kicad-cli.exe"
    for _ in range(rounds):
        js = os.path.join(tempfile.gettempdir(), "hoard_drc_tmp.json")
        subprocess.run([cli, "pcb", "drc", "--severity-error", "--format", "json", "-o", js, D.path],
                       capture_output=True)
        data = json.load(open(js, encoding="utf-8"))
        targets = []
        for v in data.get("violations", []):
            if v["type"] != "starved_thermal":
                continue
            for it in v["items"]:
                desc = it["description"]
                if "pad" in desc and " of " in desc:
                    num = desc.split("pad ")[1].split(" ")[0]
                    ref = desc.split(" of ")[-1].strip()
                    targets.append((ref, num))
        if not targets:
            return
        for fp in D.board.GetFootprints():
            for p in fp.Pads():
                if (fp.GetReference(), p.GetNumber()) in targets:
                    p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        print("  solid zone connection for starved pads:", sorted(set(targets)))
        D.fill()
        D.save()


if __name__ == "__main__":
    which = sys.argv[1:] or ["hub", "node"]
    for nm, fn in (("node", build_node), ("hub", build_hub)):
        if nm not in which:
            continue
        pre = []
        for attempt in range(4):
            D, failed = fn(pre)
            if not D.failed_gnd:
                break
            print("  -> rebuilding %s with %d reserved GND escapes" % (nm, len(D.failed_gnd)))
            pre = pre + D.failed_gnd
