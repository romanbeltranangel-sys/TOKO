// Troko · mini renderizador 2D (estilo Canvas2D) para pantallas pequenas.
// Sin dependencias de ESP-IDF ni LVGL: dibuja en un buffer RGB888 y lo convierte a RGB565.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace troko {

constexpr int kW = 128;
constexpr int kH = 160;
constexpr float kPi = 3.14159265358979f;

struct Color {
    uint8_t r, g, b;
    float a;
};
constexpr Color RGB(uint32_t hex, float a = 1.0f) {
    return Color{static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF),
                 static_cast<uint8_t>(hex & 0xFF), a};
}
constexpr Color WithAlpha(Color c, float a) { return Color{c.r, c.g, c.b, a}; }

inline float Clamp(float v, float lo = 0.0f, float hi = 1.0f) {
    if (!(v == v)) return lo;  // NaN
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float Lerp(float a, float b, float t) { t = Clamp(t); return a + (b - a) * t; }
inline float EaseOutCubic(float t) { float u = 1.0f - Clamp(t); return 1.0f - u * u * u; }
inline float EaseInOutCubic(float t) {
    float c = Clamp(t);
    if (c < 0.5f) return 4.0f * c * c * c;
    float u = -2.0f * c + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}
inline float EaseOutBack(float t) {
    float c = Clamp(t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = c - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

enum class Align { Left, Center, Right };

// Forma elemental para FillUnion (circulo o rectangulo redondeado)
struct Prim {
    enum Kind { kCircle, kRoundRect } kind;
    float a, b, c, d, e;  // circulo: cx, cy, r | rrect: x, y, w, h, r
    static Prim Circle(float cx, float cy, float r) { return Prim{kCircle, cx, cy, r, 0, 0}; }
    static Prim RRect(float x, float y, float w, float h, float r) { return Prim{kRoundRect, x, y, w, h, r}; }
};

class Canvas {
public:
    Canvas();

    void Clear(Color c);

    // Estado (equivalente a ctx.save / ctx.restore / translate / rotate / scale)
    void Save();
    void Restore();
    void Translate(float x, float y);
    void Rotate(float rad);
    void Scale(float sx, float sy);
    void SetAlpha(float a) { st_.alpha = Clamp(a); }
    float Alpha() const { return st_.alpha; }
    void ClipRect(int x, int y, int w, int h);  // coordenadas de pantalla

    // Primitivas
    void FillEllipse(float cx, float cy, float rx, float ry, Color c);
    void FillCircle(float cx, float cy, float r, Color c) { FillEllipse(cx, cy, r, r, c); }
    void FillRect(float x, float y, float w, float h, Color c) { FillRoundRect(x, y, w, h, 0, c); }
    void FillRoundRect(float x, float y, float w, float h, float r, Color c);
    void StrokeRoundRect(float x, float y, float w, float h, float r, float lw, Color c);
    void StrokeCircle(float cx, float cy, float r, float lw, Color c);
    void StrokeArc(float cx, float cy, float r, float a0, float a1, float lw, Color c);  // cabos redondos
    void StrokeLine(float x0, float y0, float x1, float y1, float lw, Color c);
    // Union de formas rellenada de una sola vez (sin doble mezcla de transparencia)
    void FillUnion(const Prim* prims, int n, Color c);
    // Relleno con degradado horizontal de 3 paradas entre gx0 y gx1
    void FillRoundRectGradient(float x, float y, float w, float h, float r, float gx0, float gx1,
                               Color c0, Color c1, Color c2);

    // Texto monoespaciado (UTF-8), linea base "middle" como en Canvas2D
    void Text(const char* utf8, float x, float y, float size, Align align, Color c);
    static float TextWidth(const char* utf8, float size);

    const uint8_t* Rgb() const { return buf_; }
    void ToRgb565(uint16_t* out) const;

private:
    struct State {
        float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
        float alpha = 1;
        int clip[4] = {0, 0, kW, kH};  // x0, y0, x1, y1 (exclusivo)
    };
    State st_;
    State stack_[10];
    int depth_ = 0;
    uint8_t buf_[kW * kH * 3];

    void Blend(int x, int y, const Color& c, float coverage);
    template <class SdfFn, class ColFn>
    void Raster(float lx0, float ly0, float lx1, float ly1, SdfFn sdf, ColFn colorAt);
    float ScaleFactor() const;
};

}  // namespace troko
