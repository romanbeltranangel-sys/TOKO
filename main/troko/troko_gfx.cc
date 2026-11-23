#include "troko_gfx.h"

#include <cstring>

namespace troko {

#include "troko_font_data.inc"

namespace {

inline float Hypot(float x, float y) { return std::sqrt(x * x + y * y); }

// Distancia con signo a una elipse (aproximacion de I. Quilez); valida para dibujar.
inline float SdEllipse(float px, float py, float rx, float ry) {
    px = std::fabs(px);
    py = std::fabs(py);
    if (std::fabs(rx - ry) < 1e-4f) return Hypot(px, py) - rx;
    float k0 = Hypot(px / rx, py / ry);
    float k1 = Hypot(px / (rx * rx), py / (ry * ry));
    if (k1 < 1e-6f) return -std::min(rx, ry);
    return k0 * (k0 - 1.0f) / k1;
}

inline float SdRoundRect(float px, float py, float x, float y, float w, float h, float r) {
    float hx = w * 0.5f, hy = h * 0.5f;
    r = std::max(0.0f, std::min(r, std::min(hx, hy)));
    float qx = std::fabs(px - (x + hx)) - hx + r;
    float qy = std::fabs(py - (y + hy)) - hy + r;
    return Hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Decodifica un caracter UTF-8; avanza el puntero.
uint32_t NextCodepoint(const char*& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    uint32_t cp = 0;
    if (p[0] < 0x80) {
        cp = p[0];
        s += 1;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        s += 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        s += 3;
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
               (p[3] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        s += 4;
    } else {
        cp = '?';
        s += 1;
    }
    return cp;
}

const GlyphInfo* FindGlyph(uint32_t cp) {
    int lo = 0, hi = kGlyphCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (kGlyphs[mid].cp == cp) return &kGlyphs[mid];
        if (kGlyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

constexpr int kMaxText = 64;

int Decode(const char* s, uint32_t* out, int max) {
    int n = 0;
    while (*s && n < max) out[n++] = NextCodepoint(s);
    return n;
}

inline float SampleGlyph(const GlyphInfo& g, float tx, float ty) {
    // tx, ty en pixeles del bitmap (centros de texel en +0.5)
    if (g.w == 0 || g.h == 0) return 0.0f;
    float fx = tx - 0.5f, fy = ty - 0.5f;
    int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    float ax = fx - x0, ay = fy - y0;
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= g.w || y >= g.h) return 0.0f;
        return kGlyphData[g.off + y * g.w + x] / 255.0f;
    };
    float v00 = at(x0, y0), v10 = at(x0 + 1, y0), v01 = at(x0, y0 + 1), v11 = at(x0 + 1, y0 + 1);
    return (v00 * (1 - ax) + v10 * ax) * (1 - ay) + (v01 * (1 - ax) + v11 * ax) * ay;
}

}  // namespace

Canvas::Canvas() { Clear(RGB(0x000000)); }

void Canvas::Clear(Color c) {
    for (int i = 0; i < kW * kH; ++i) {
        buf_[i * 3] = c.r;
        buf_[i * 3 + 1] = c.g;
        buf_[i * 3 + 2] = c.b;
    }
    st_ = State();
    depth_ = 0;
}

void Canvas::Save() {
    if (depth_ < 10) stack_[depth_++] = st_;
}
void Canvas::Restore() {
    if (depth_ > 0) st_ = stack_[--depth_];
}
void Canvas::Translate(float x, float y) {
    st_.e += st_.a * x + st_.c * y;
    st_.f += st_.b * x + st_.d * y;
}
void Canvas::Rotate(float rad) {
    float cs = std::cos(rad), sn = std::sin(rad);
    float a = st_.a * cs + st_.c * sn, b = st_.b * cs + st_.d * sn;
    float c = -st_.a * sn + st_.c * cs, d = -st_.b * sn + st_.d * cs;
    st_.a = a; st_.b = b; st_.c = c; st_.d = d;
}
void Canvas::Scale(float sx, float sy) {
    st_.a *= sx; st_.b *= sx;
    st_.c *= sy; st_.d *= sy;
}
void Canvas::ClipRect(int x, int y, int w, int h) {
    st_.clip[0] = std::max(st_.clip[0], x);
    st_.clip[1] = std::max(st_.clip[1], y);
    st_.clip[2] = std::min(st_.clip[2], x + w);
    st_.clip[3] = std::min(st_.clip[3], y + h);
}

float Canvas::ScaleFactor() const {
    float det = std::fabs(st_.a * st_.d - st_.b * st_.c);
    return std::sqrt(det);
}

void Canvas::Blend(int x, int y, const Color& c, float coverage) {
    float a = coverage * c.a * st_.alpha;
    if (a <= 0.003f) return;
    if (a > 1.0f) a = 1.0f;
    uint8_t* p = &buf_[(y * kW + x) * 3];
    p[0] = static_cast<uint8_t>(p[0] + (c.r - p[0]) * a + 0.5f);
    p[1] = static_cast<uint8_t>(p[1] + (c.g - p[1]) * a + 0.5f);
    p[2] = static_cast<uint8_t>(p[2] + (c.b - p[2]) * a + 0.5f);
}

template <class SdfFn, class ColFn>
void Canvas::Raster(float lx0, float ly0, float lx1, float ly1, SdfFn sdf, ColFn colorAt) {
    float s = ScaleFactor();
    float det = st_.a * st_.d - st_.b * st_.c;
    if (s < 1e-5f || std::fabs(det) < 1e-9f) return;
    // caja en pantalla
    float xs[4] = {lx0, lx1, lx0, lx1}, ys[4] = {ly0, ly0, ly1, ly1};
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < 4; ++i) {
        float dx = st_.a * xs[i] + st_.c * ys[i] + st_.e;
        float dy = st_.b * xs[i] + st_.d * ys[i] + st_.f;
        minx = std::min(minx, dx); maxx = std::max(maxx, dx);
        miny = std::min(miny, dy); maxy = std::max(maxy, dy);
    }
    int px0 = std::max(st_.clip[0], static_cast<int>(std::floor(minx)) - 1);
    int py0 = std::max(st_.clip[1], static_cast<int>(std::floor(miny)) - 1);
    int px1 = std::min(st_.clip[2], static_cast<int>(std::ceil(maxx)) + 1);
    int py1 = std::min(st_.clip[3], static_cast<int>(std::ceil(maxy)) + 1);
    float ia = st_.d / det, ib = -st_.b / det, ic = -st_.c / det, id = st_.a / det;
    for (int py = py0; py < py1; ++py) {
        for (int px = px0; px < px1; ++px) {
            float dx = px + 0.5f - st_.e, dy = py + 0.5f - st_.f;
            float lx = ia * dx + ic * dy;
            float ly = ib * dx + id * dy;
            float d = sdf(lx, ly) * s;
            float cov = 0.5f - d;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            Blend(px, py, colorAt(lx, ly), cov);
        }
    }
}

void Canvas::FillEllipse(float cx, float cy, float rx, float ry, Color c) {
    rx = std::max(rx, 0.05f);
    ry = std::max(ry, 0.05f);
    Raster(cx - rx, cy - ry, cx + rx, cy + ry,
           [=](float x, float y) { return SdEllipse(x - cx, y - cy, rx, ry); },
           [=](float, float) { return c; });
}

void Canvas::FillRoundRect(float x, float y, float w, float h, float r, Color c) {
    w = std::max(0.5f, w);
    h = std::max(0.5f, h);
    Raster(x, y, x + w, y + h, [=](float px, float py) { return SdRoundRect(px, py, x, y, w, h, r); },
           [=](float, float) { return c; });
}

void Canvas::FillUnion(const Prim* prims, int n, Color c) {
    if (n <= 0) return;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < n; ++i) {
        const Prim& p = prims[i];
        if (p.kind == Prim::kCircle) {
            minx = std::min(minx, p.a - p.c); maxx = std::max(maxx, p.a + p.c);
            miny = std::min(miny, p.b - p.c); maxy = std::max(maxy, p.b + p.c);
        } else {
            minx = std::min(minx, p.a); maxx = std::max(maxx, p.a + p.c);
            miny = std::min(miny, p.b); maxy = std::max(maxy, p.b + p.d);
        }
    }
    Raster(minx, miny, maxx, maxy,
           [=](float px, float py) {
               float d = 1e9f;
               for (int i = 0; i < n; ++i) {
                   const Prim& p = prims[i];
                   float di = (p.kind == Prim::kCircle) ? Hypot(px - p.a, py - p.b) - p.c
                                                        : SdRoundRect(px, py, p.a, p.b, p.c, p.d, p.e);
                   d = std::min(d, di);
               }
               return d;
           },
           [=](float, float) { return c; });
}

void Canvas::StrokeRoundRect(float x, float y, float w, float h, float r, float lw, Color c) {
    w = std::max(0.5f, w);
    h = std::max(0.5f, h);
    float hw = lw * 0.5f;
    Raster(x - hw, y - hw, x + w + hw, y + h + hw,
           [=](float px, float py) { return std::fabs(SdRoundRect(px, py, x, y, w, h, r)) - hw; },
           [=](float, float) { return c; });
}

void Canvas::StrokeCircle(float cx, float cy, float r, float lw, Color c) {
    float hw = lw * 0.5f;
    Raster(cx - r - hw, cy - r - hw, cx + r + hw, cy + r + hw,
           [=](float px, float py) { return std::fabs(Hypot(px - cx, py - cy) - r) - hw; },
           [=](float, float) { return c; });
}

void Canvas::StrokeArc(float cx, float cy, float r, float a0, float a1, float lw, Color c) {
    float hw = lw * 0.5f;
    float sweep = a1 - a0;
    if (sweep >= 2.0f * kPi - 1e-4f) {
        StrokeCircle(cx, cy, r, lw, c);
        return;
    }
    if (sweep <= 0.0f) return;
    float e0x = cx + r * std::cos(a0), e0y = cy + r * std::sin(a0);
    float e1x = cx + r * std::cos(a1), e1y = cy + r * std::sin(a1);
    Raster(cx - r - hw, cy - r - hw, cx + r + hw, cy + r + hw,
           [=](float px, float py) {
               float dx = px - cx, dy = py - cy;
               float ang = std::atan2(dy, dx) - a0;
               ang = std::fmod(ang, 2.0f * kPi);
               if (ang < 0) ang += 2.0f * kPi;
               float cap = std::min(Hypot(px - e0x, py - e0y), Hypot(px - e1x, py - e1y)) - hw;
               if (ang <= sweep) return std::min(std::fabs(Hypot(dx, dy) - r) - hw, cap);
               return cap;
           },
           [=](float, float) { return c; });
}

void Canvas::StrokeLine(float x0, float y0, float x1, float y1, float lw, Color c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = Hypot(dx, dy);
    if (len < 1e-4f) return;
    float ux = dx / len, uy = dy / len;
    float hw = lw * 0.5f;
    Raster(std::min(x0, x1) - hw, std::min(y0, y1) - hw, std::max(x0, x1) + hw, std::max(y0, y1) + hw,
           [=](float px, float py) {
               float rx = px - x0, ry = py - y0;
               float along = rx * ux + ry * uy;
               float perp = -rx * uy + ry * ux;
               float ddx = std::fabs(along - len * 0.5f) - len * 0.5f;
               float ddy = std::fabs(perp) - hw;
               return Hypot(std::max(ddx, 0.0f), std::max(ddy, 0.0f)) + std::min(std::max(ddx, ddy), 0.0f);
           },
           [=](float, float) { return c; });
}

void Canvas::FillRoundRectGradient(float x, float y, float w, float h, float r, float gx0, float gx1,
                                   Color c0, Color c1, Color c2) {
    w = std::max(0.5f, w);
    h = std::max(0.5f, h);
    float span = std::max(1e-3f, gx1 - gx0);
    Raster(x, y, x + w, y + h, [=](float px, float py) { return SdRoundRect(px, py, x, y, w, h, r); },
           [=](float lx, float) {
               float t = Clamp((lx - gx0) / span);
               auto mix = [](Color a, Color b, float k) {
                   return Color{static_cast<uint8_t>(a.r + (b.r - a.r) * k), static_cast<uint8_t>(a.g + (b.g - a.g) * k),
                                static_cast<uint8_t>(a.b + (b.b - a.b) * k), 1.0f};
               };
               return t < 0.5f ? mix(c0, c1, t * 2.0f) : mix(c1, c2, (t - 0.5f) * 2.0f);
           });
}

float Canvas::TextWidth(const char* utf8, float size) {
    uint32_t cps[kMaxText];
    int n = Decode(utf8, cps, kMaxText);
    return n * kFontAdvance * size;
}

void Canvas::Text(const char* utf8, float x, float y, float size, Align align, Color c) {
    uint32_t cps[kMaxText];
    int n = Decode(utf8, cps, kMaxText);
    if (n == 0) return;
    float adv = kFontAdvance * size;
    float total = n * adv;
    float x0 = x;
    if (align == Align::Center) x0 = x - total * 0.5f;
    else if (align == Align::Right) x0 = x - total;
    float baseline = y + (kFontAscent - kFontDescent) * 0.5f * size;
    float k = size / static_cast<float>(kFontRef);

    float s = ScaleFactor();
    float det = st_.a * st_.d - st_.b * st_.c;
    if (s < 1e-5f || std::fabs(det) < 1e-9f) return;
    float ia = st_.d / det, ib = -st_.b / det, ic = -st_.c / det, id = st_.a / det;
    int nsub = (k * s < 0.5f) ? 3 : 2;
    float inv_n = 1.0f / (nsub * nsub);

    for (int i = 0; i < n; ++i) {
        const GlyphInfo* g = FindGlyph(cps[i]);
        if (!g) g = FindGlyph('?');
        if (!g || g->w == 0) continue;
        float pen = x0 + i * adv;
        float gx0 = pen + g->left * k, gy0 = baseline + g->top * k;
        float gx1 = gx0 + g->w * k, gy1 = gy0 + g->h * k;
        float xs[4] = {gx0, gx1, gx0, gx1}, ys[4] = {gy0, gy0, gy1, gy1};
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        for (int q = 0; q < 4; ++q) {
            float dx = st_.a * xs[q] + st_.c * ys[q] + st_.e;
            float dy = st_.b * xs[q] + st_.d * ys[q] + st_.f;
            minx = std::min(minx, dx); maxx = std::max(maxx, dx);
            miny = std::min(miny, dy); maxy = std::max(maxy, dy);
        }
        int px0 = std::max(st_.clip[0], static_cast<int>(std::floor(minx)) - 1);
        int py0 = std::max(st_.clip[1], static_cast<int>(std::floor(miny)) - 1);
        int px1 = std::min(st_.clip[2], static_cast<int>(std::ceil(maxx)) + 1);
        int py1 = std::min(st_.clip[3], static_cast<int>(std::ceil(maxy)) + 1);
        for (int py = py0; py < py1; ++py) {
            for (int px = px0; px < px1; ++px) {
                float acc = 0.0f;
                for (int sy = 0; sy < nsub; ++sy) {
                    for (int sx = 0; sx < nsub; ++sx) {
                        float dx = px + (sx + 0.5f) / nsub - st_.e, dy = py + (sy + 0.5f) / nsub - st_.f;
                        float lx = ia * dx + ic * dy;
                        float ly = ib * dx + id * dy;
                        acc += SampleGlyph(*g, (lx - gx0) / k, (ly - gy0) / k);
                    }
                }
                float cov = acc * inv_n;
                if (cov > 0.0f) Blend(px, py, c, cov);
            }
        }
    }
}

void Canvas::ToRgb565(uint16_t* out) const {
    for (int i = 0; i < kW * kH; ++i) {
        uint8_t r = buf_[i * 3], g = buf_[i * 3 + 1], b = buf_[i * 3 + 2];
        out[i] = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

}  // namespace troko
