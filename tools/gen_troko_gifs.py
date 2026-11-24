# Genera las animaciones de Troko (128x128, blanco sobre negro). Uso: python3 tools/gen_troko_gifs.py assets/troko-gif
import math, sys, os
from PIL import Image, ImageDraw
S = 4; W = 128; DUR = 50  # ms por fotograma

def ss(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)          # smoothstep
def pulse(t, a, b, c, d):  # sube a->b, mantiene b->c, baja c->d (0..1)
    return ss((t - a) / max(1e-6, b - a)) * (1 - ss((t - c) / max(1e-6, d - c)))
def lerp(a, b, k): return a + (b - a) * k

class Cv:
    def __init__(self):
        self.im = Image.new('L', (W * S, W * S), 0); self.d = ImageDraw.Draw(self.im)
    def ell(self, cx, cy, rx, ry, v=255):
        self.d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=v)
    def poly(self, pts, v=255): self.d.polygon([(x * S, y * S) for x, y in pts], fill=v)
    def line(self, pts, w, v=255):
        self.d.line([(x * S, y * S) for x, y in pts], fill=v, width=int(w * S), joint='curve')
        for x, y in (pts[0], pts[-1]): self.ell(x, y, w / 2, w / 2, v)
    def arc(self, cx, cy, rx, ry, a0, a1, w, v=255):
        pts = [(cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))
               for a in [a0 + (a1 - a0) * i / 24 for i in range(25)]]
        self.line(pts, w, v)
    def eye(self, cx, cy, rx, ry, lid=0.0, cover=0.0):
        # ojo ovalado; lid = inclinacion del parpado superior (px de diferencia, + = baja hacia el exterior),
        # cover = fraccion del ojo tapada desde arriba
        self.ell(cx, cy, rx, ry)
        if cover > 0 or lid != 0:
            top = cy - ry + 2 * ry * cover
            self.poly([(cx - rx - 2, top - 40), (cx + rx + 2, top - 40), (cx + rx + 2, top + lid), (cx - rx - 2, top - lid)], 0)
    def heart(self, cx, cy, s, v=255):
        pts = []
        for i in range(60):
            t = i / 60 * 2 * math.pi
            x = 16 * math.sin(t) ** 3
            y = -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))
            pts.append((cx + x * s / 16, cy + y * s / 16))
        self.poly(pts, v)
    def out(self): return self.im.resize((W, W), Image.LANCZOS)

def save(frames, path):
    pal = []
    for i in range(256): pal += [i, i, i]
    out = []
    for f in frames:
        p = f.convert('P'); p.putpalette(pal); out.append(p)
    out[0].save(path, save_all=True, append_images=out[1:], duration=DUR, loop=0, optimize=False, disposal=1)

def blink(t, at, dur=0.28):  # 1 abierto .. 0 cerrado
    k = (t - at) / dur
    if k < 0 or k > 1: return 1.0
    return 1 - math.sin(k * math.pi) ** 1.2 * 0.97

def make(name, cycle, fn, outdir):
    n = int(cycle / (DUR / 1000)); fr = []
    for i in range(n):
        c = Cv(); fn(c, i / n * cycle, cycle); fr.append(c.out())
    save(fr, os.path.join(outdir, name + '.gif'))

# ---------------- NEUTRAL (ciclo 9 s, parpadeos irregulares + mirada) ----------------
def neutral(c, t, T):
    b = min(blink(t, 1.3), blink(t, 5.1), blink(t, 5.55, 0.22), blink(t, 7.6))
    gx = 5 * pulse(t, 2.0, 2.5, 3.1, 3.5) - 5 * pulse(t, 6.0, 6.4, 7.0, 7.4)
    gy = -2 * pulse(t, 3.9, 4.2, 4.6, 4.9)
    br = 0.6 * math.sin(t / T * 2 * math.pi * 2)            # respiracion
    ry = max(2.5, (19 + br) * b)
    for ex in (40, 88):
        c.eye(ex + gx, 64 + gy, 12, ry)
# ---------------- ANGRY (ciclo 7 s) ----------------
def angry(c, t, T):
    k = pulse(t, 0.2, 0.9, 5.6, 6.5)                    # tension
    sh = (1.0 * math.sin(t * 38) * k) if 1.0 < t < 5.4 else 0
    mark = pulse(t, 1.0, 1.4, 4.8, 5.4)
    b = blink(t, 3.4, 0.2)
    for ex, side in ((38, 1), (90, -1)):
        c.eye(ex + sh, 66 + 2 * k, 15, 17 * (0.5 + 0.5 * b if b < 1 else 1) , lid=11 * k * side * -1 if False else 0, cover=0)
        # parpado inclinado hacia el centro (cejas enojadas)
        top = 66 - 17 + 4 * k
        inner = ex + 17 * side; outer = ex - 17 * side
        c.poly([(outer, top - 30), (inner, top - 30), (inner, top + 15 * k + 2), (outer, top - 3 * k - 4)], 0)
    s = 1 + 0.18 * math.sin(t * 9)
    if mark > 0.02:                                     # marca de enojo
        m = 7 * s * mark
        for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            c.arc(108 + dx * 6, 20 + dy * 6, m, m, 0, 90 if dx * dy > 0 else 0, 0.1, 0) if False else None
        for a in (0, 90, 180, 270):
            c.arc(108 + (7 if a in (0, 270) else -7) * mark * 0.9, 20 + (7 if a in (0, 90) else -7) * mark * 0.9, 5 * s * mark, 5 * s * mark, a + 180, a + 270, 2.4)
# ---------------- COOL (ciclo 8 s) ----------------
def cool(c, t, T):
    bob = 1.6 * math.sin(t / T * 2 * math.pi * 2)
    y0 = 46 + bob
    for ex in (38, 90):
        c.poly([(ex - 25, y0), (ex + 25, y0), (ex + 21, y0 + 32), (ex - 21, y0 + 32)])
        c.ell(ex - 21, y0 + 30, 4, 3); c.ell(ex + 21, y0 + 30, 4, 3)
    c.poly([(62, y0 + 3), (66, y0 + 3), (66, y0 + 9), (62, y0 + 9)])
    c.line([(13, y0 + 3), (4, y0 - 3)], 3); c.line([(115, y0 + 3), (124, y0 - 3)], 3)
    k = (t % 4.0) / 4.0                                    # destello cada 4 s con pausa
    if k < 0.45:
        x = lerp(-10, 140, ss(k / 0.45))
        for off in (0, 11):
            c.poly([(x + off, y0 - 2), (x + off + 6, y0 - 2), (x + off - 12, y0 + 36), (x + off - 18, y0 + 36)], 0)
    # boca sonriente discreta
    c.arc(64, 96 + bob, 10, 6, 20, 160, 3)
# ---------------- KISSY (ciclo 6 s) ----------------
def kissy(c, t, T):
    sq = 1.0 * math.sin(t / T * 2 * math.pi * 3)
    for ex in (34, 94):
        c.arc(ex, 46 + sq, 14, 11, 200, 340, 5)           # ojos felices
    for k in range(3):                                      # corazones que suben y se desvanecen
        p = ((t / 2.2) + k / 3.0) % 1.0
        sz = lerp(5, 17, ss(p * 1.4))
        x = 64 + 9 * math.sin(p * 6 + k * 2.1)
        y = lerp(112, 66, p)
        if p > 0.7: sz *= 1 - (p - 0.7) / 0.3
        if p < 0.06 or sz < 1: continue
        c.heart(x, y, sz)
    pu = 1 + 0.10 * math.sin(t * 5)
    c.ell(64, 98, 6 * pu, 4.2 * pu)                          # labios

# ---------------- SAD (ciclo 10 s) ----------------
def sad(c, t, T):
    droop = 0.5 + 0.1 * math.sin(t / T * 2 * math.pi * 2)
    look = -3 * pulse(t, 1, 1.6, 4.0, 4.8) + 3 * pulse(t, 6.0, 6.7, 8.0, 8.8)
    b = blink(t, 8.9, 0.4)
    for ex, side in ((38, 1), (90, -1)):
        c.eye(ex + look, 70, 13, 15 * b + 2, 0)
        top = 70 - 15 * b
        inner = ex + 16 * side; outer = ex - 16 * side
        c.poly([(outer, top - 30), (inner, top - 30), (inner, top - 2), (outer, top + 14 * droop + 4)], 0)
    tear_phase = ((t - 1.0) % 5.0) / 5.0
    if tear_phase < 0.8:
        p = tear_phase / 0.8
        grow = ss(p / 0.35) if p < 0.35 else 1
        y = 88 + (ss((p - 0.35) / 0.65) * 30 if p > 0.35 else 0)
        c.ell(30 + look, y, 3 * grow, 4.5 * grow)
# ---------------- EMBARRASSED (ciclo 7 s) ----------------
def embarrassed(c, t, T):
    sh = 0.9 * math.sin(t * 14) * pulse(t, 0.3, 0.6, 5.5, 6.3)
    for ex in (36, 92):
        c.arc(ex + sh, 62, 15, 14, 190, 350, 5)           # ojos ^ (timidos)
    pr = 0.5 + 0.5 * math.sin(t / T * 2 * math.pi * 3)
    for bx in (22, 106):                                   # rubor: lineas diagonales
        for j in range(3):
            c.line([(bx - 6 + j * 6, 88), (bx - 2 + j * 6, 80)], 2.2, int(150 + 105 * pr))
    sw = pulse(t, 1.0, 1.4, 4.8, 5.6)                      # gota de sudor
    if sw > 0.02:
        c.ell(104, 26 + 10 * ss((t - 1.0) / 4.0), 3.6 * sw, 5.4 * sw)
        c.poly([(104, 17 + 10 * ss((t - 1.0) / 4.0)), (100.8, 24 + 10 * ss((t - 1.0) / 4.0)), (107.2, 24 + 10 * ss((t - 1.0) / 4.0))])
# ---------------- CRYING (ciclo 2.8 s: lagrimas continuas, sollozos) ----------------
def crying(c, t, T):
    sob = 2.2 * max(0, math.sin(t / T * 2 * math.pi * 2)) ** 3
    for ex in (38, 90):
        c.arc(ex, 56 + sob, 14, 10, 20, 160, 5)           # ojos cerrados tristes (arco hacia abajo)
    for i, ex in enumerate((38, 90)):
        for k in range(3):
            p = ((t / T) * 1 + k / 3.0 + i * 0.17) % 1.0
            y = lerp(70 + sob, 124, p * p * 0.6 + p * 0.4)
            r = 3.6 * (1 - 0.45 * p)
            if y < 124: c.ell(ex + (2 if i == 0 else -2) * p * 3, y, r * 0.8, r * 1.3)
# ---------------- THINKING (ciclo 8 s) ----------------
def thinking(c, t, T):
    gx = 6 * pulse(t, 0.3, 1.0, 3.2, 3.9) - 6 * pulse(t, 4.4, 5.1, 6.6, 7.3)
    gy = -6 * pulse(t, 0.3, 1.0, 3.2, 3.9)
    b = blink(t, 3.6, 0.22)
    c.eye(40 + gx, 66 + gy, 11, 17 * b + 1.5)
    c.eye(88 + gx, 66 + gy, 11, 17 * b + 1.5)
    # ceja izquierda levantada (una linea)
    rb = pulse(t, 0.3, 0.9, 3.4, 4.0)
    c.line([(26, 36 - 6 * rb), (54, 33 - 8 * rb)], 4)
    # puntos de pensamiento que aparecen en secuencia
    for k in range(3):
        a = pulse(t % 4.0, 0.5 + k * 0.5, 0.9 + k * 0.5, 3.0, 3.6)
        if a > 0.02: c.ell(86 + k * 14, 20 - k * 3, 3.4 * a + 0.5, 3.4 * a + 0.5)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    for name, cyc, fn in (('neutral', 9, neutral), ('angry', 7, angry), ('cool', 8, cool), ('kissy', 6, kissy),
                          ('sad', 10, sad), ('embarrassed', 7, embarrassed), ('crying', 2.8, crying),
                          ('thinking', 8, thinking)):
        make(name, cyc, fn, out)
