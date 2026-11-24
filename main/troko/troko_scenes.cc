#include "troko_scenes.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace troko {

namespace {

// ------------------------------------------------------------ utilidades
inline int RoundI(float v) { return static_cast<int>(std::floor(v + 0.5f)); }

int DaysInMonth(int y, int m1) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = d[(m1 - 1 + 12) % 12];
    if (m1 == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) n = 29;
    return n;
}

// Dia de la semana, domingo = 0 (algoritmo de Sakamoto)
int DowSun0(int y, int m1, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m1 < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m1 - 1] + d) % 7;
}

const char* const kMonths[12] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
const char* const kWeekdays[7] = {"L", "M", "X", "J", "V", "S", "D"};

int Utf8Count(const char* s) {
    int n = 0;
    for (; *s; ++s)
        if ((static_cast<unsigned char>(*s) & 0xC0) != 0x80) ++n;
    return n;
}

// Copia los primeros n caracteres UTF-8
void Utf8Prefix(const char* s, int n, char* out, size_t out_size) {
    size_t i = 0;
    int count = 0;
    while (s[i]) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (count == n) break;
            ++count;
        }
        ++i;
    }
    if (i >= out_size) i = out_size - 1;
    std::memcpy(out, s, i);
    out[i] = 0;
}

void Utf8PopLast(char* s) {
    size_t n = std::strlen(s);
    if (n == 0) return;
    do {
        --n;
    } while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80);
    s[n] = 0;
}

// Mayusculas para texto en espanol (ASCII + vocales acentuadas y ñ)
void ToUpperEs(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 3 < out_size;) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            out[o++] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : static_cast<char>(c);
            ++i;
        } else if (c == 0xC3 && in[i + 1]) {
            unsigned char d = static_cast<unsigned char>(in[i + 1]);
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) d = static_cast<unsigned char>(d - 0x20);  // à-þ -> À-Þ
            out[o++] = static_cast<char>(c);
            out[o++] = static_cast<char>(d);
            i += 2;
        } else {
            out[o++] = static_cast<char>(c);
            ++i;
        }
    }
    out[o] = 0;
}

// Copia segura (siempre termina en 0)
void Copy(char* dst, size_t n, const char* src) {
    size_t l = std::strlen(src);
    if (l >= n) l = n - 1;
    std::memcpy(dst, src, l);
    dst[l] = 0;
}
void Append(char* dst, size_t n, const char* add) {
    size_t l = std::strlen(dst), a = std::strlen(add);
    if (l + a >= n) return;
    std::memcpy(dst + l, add, a + 1);
}

// Ajuste de palabras por lineas (port de wrapText del simulador). Devuelve el numero de lineas.
constexpr int kMaxLines = 4;
constexpr int kLineLen = 96;
__attribute__((noinline))
int WrapText(const char* text, float size, float max_w, int max_lines, char lines[][kLineLen]) {
    if (max_lines > kMaxLines) max_lines = kMaxLines;
    auto fits = [&](const char* s) { return Canvas::TextWidth(s, size) <= max_w; };
    // Quita caracteres del final hasta que "s…" quepa, y agrega los puntos suspensivos
    auto ellipsize = [&](char* s) {
        char tmp[kLineLen];
        for (;;) {
            Copy(tmp, sizeof(tmp), s);
            Append(tmp, sizeof(tmp), "…");
            if (Utf8Count(s) <= 1 || fits(tmp)) break;
            Utf8PopLast(s);
        }
        Copy(s, kLineLen, tmp);
    };
    int total = 0;
    bool overflow = false;
    auto push = [&](const char* s) {
        if (total < max_lines) Copy(lines[total++], kLineLen, s);
        else overflow = true;
    };
    char current[kLineLen] = "";
    char word[kLineLen];
    const char* p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
        if (!*p) break;
        size_t wl = 0;
        while (p[wl] && p[wl] != ' ' && p[wl] != '\t' && p[wl] != '\n') ++wl;
        if (wl >= sizeof(word)) wl = sizeof(word) - 1;
        std::memcpy(word, p, wl);
        word[wl] = 0;
        p += wl;
        char test[kLineLen];
        Copy(test, sizeof(test), current);
        if (current[0]) Append(test, sizeof(test), " ");
        Append(test, sizeof(test), word);
        if (fits(test)) {
            Copy(current, sizeof(current), test);
        } else {
            if (current[0]) push(current);
            if (!fits(word)) {
                Copy(current, sizeof(current), word);
                ellipsize(current);
            } else {
                Copy(current, sizeof(current), word);
            }
        }
    }
    if (current[0]) push(current);
    if (overflow && total > 0) ellipsize(lines[total - 1]);
    return total;
}

constexpr Color kWhite = RGB(0xffffff);
constexpr Color kBlack = RGB(0x000000);
constexpr Color kSky = RGB(0x38bdf8);
constexpr Color kGreen = RGB(0x22c55e);
constexpr Color kAmber = RGB(0xf59e0b);
constexpr Color kRed = RGB(0xef4444);

}  // namespace

// ============================================================================
//                                  CALENDARIO
// ============================================================================
void CalendarScene::Start(const CalendarParams& p, uint32_t now_ms) {
    p_ = p;
    if (p_.duration_ms < 900) p_.duration_ms = 900;
    if (p_.duration_ms > 12000) p_.duration_ms = 12000;
    if (!p_.title[0]) std::snprintf(p_.title, sizeof(p_.title), "Evento");
    start_ms_ = now_ms;
}

bool CalendarScene::Finished(uint32_t now_ms) const {
    return p_.full_month || (now_ms - start_ms_) >= static_cast<uint32_t>(p_.duration_ms);
}

void CalendarScene::Render(Canvas& cv, uint32_t now_ms) {
    float p = Clamp(static_cast<float>(now_ms - start_ms_) / static_cast<float>(p_.duration_ms));
    Draw(cv, p_.full_month ? 1.0f : p, p_.full_month);
}

void CalendarScene::Draw(Canvas& cv, float p, bool full_month) {
    const int year = p_.year, month = p_.month - 1, target_day = p_.day;
    const bool same_month_as_today = p_.today_y == year && p_.today_m == p_.month;

    bool marked[33] = {false};
    marked[target_day] = true;
    for (int i = 0; i < p_.mark_count; ++i)
        if (p_.marks[i] >= 1 && p_.marks[i] <= 31) marked[p_.marks[i]] = true;
    const int today_key = p_.today_y * 10000 + p_.today_m * 100 + p_.today_d;

    cv.Clear(kBlack);

    if (!full_month && p < 0.26f) {
        // ETAPA 1: ojos
        float ep = p / 0.26f;
        float openness = 1.0f;
        if (ep > 0.25f && ep < 0.38f) openness = 1.0f - EaseInOutCubic((ep - 0.25f) / 0.13f);
        else if (ep >= 0.38f && ep < 0.52f) openness = EaseInOutCubic((ep - 0.38f) / 0.14f);
        float look_down = EaseInOutCubic(Clamp((ep - 0.50f) / 0.26f));
        float slide_up = EaseInOutCubic(Clamp((ep - 0.74f) / 0.26f));
        float ry = std::max(2.0f, std::round(21.0f * openness));
        float cy = std::round(80.0f + look_down * 9.0f - slide_up * 112.0f);
        cv.Save();
        cv.SetAlpha(1.0f - slide_up * 0.4f);
        cv.FillEllipse(36, cy, 20, ry, kWhite);
        cv.FillEllipse(92, cy, 20, ry, kWhite);
        cv.Restore();
    }
    if (!full_month && p < 0.22f) return;

    const float cal_appear = full_month ? 1.0f : EaseOutCubic(Clamp((p - 0.22f) / 0.20f));
    const float zoom_t = full_month ? 0.0f : EaseInOutCubic(Clamp((p - 0.42f) / 0.32f));
    const float card_t = full_month ? 0.0f : EaseOutCubic(Clamp((p - 0.74f) / 0.22f));

    const float gl = 4, gt = 36, cw = 17, ch = 19;
    const int first_offset = (DowSun0(year, p_.month, 1) + 6) % 7;
    const int dim = DaysInMonth(year, p_.month);
    const int target_index = first_offset + target_day - 1;
    const float tx = gl + (target_index % 7 + 0.5f) * cw;
    const float ty = gt + (target_index / 7 + 0.5f) * ch;

    char buf[64];

    // ---- encabezado
    cv.Save();
    if (zoom_t < 0.35f) {
        cv.SetAlpha((1.0f - Clamp(zoom_t / 0.35f)) * cal_appear);
        cv.FillRoundRect(4, 3, 120, 17, 3, kWhite);
        std::snprintf(buf, sizeof(buf), "%s %d", kMonths[month], year);
        cv.Text(buf, 9, 12, 9.5f, Align::Left, kBlack);
        std::snprintf(buf, sizeof(buf), "D:%02d", target_day);
        cv.Text(buf, 119, 12, 9.5f, Align::Right, kBlack);
        for (int col = 0; col < 7; ++col)
            cv.Text(kWeekdays[col], std::round(gl + (col + 0.5f) * cw), 27, 8, Align::Center, RGB(0xcbd5e1));
    } else if (card_t <= 0.15f) {
        cv.SetAlpha(Clamp((zoom_t - 0.35f) / 0.25f) * (1.0f - Clamp(card_t / 0.15f)));
        cv.FillRoundRect(4, 4, 120, 16, 3, kBlack);
        cv.StrokeRoundRect(4.5f, 4.5f, 119, 15, 3, 1, kWhite);
        std::snprintf(buf, sizeof(buf), "%02d %s %d", target_day, kMonths[month], year);
        cv.Text(buf, 64, 12.5f, 8.5f, Align::Center, kWhite);
    }
    cv.Restore();

    // ---- cuadricula (con recorte)
    cv.Save();
    cv.ClipRect(3, 35, 122, 121);
    const float view_cx = 64, view_cy = 95.5f;
    const float scale = 1.0f + zoom_t * 2.45f;
    cv.Translate(view_cx, view_cy);
    cv.Scale(scale, scale);
    cv.Translate(-Lerp(view_cx, tx, zoom_t), -Lerp(view_cy, ty, zoom_t));

    for (int d = 1; d <= dim; ++d) {
        float stagger = (static_cast<float>(d - 1) / dim) * 0.35f;
        float cell_alpha = full_month ? 1.0f : Clamp((cal_appear - stagger) / (1.0f - stagger));
        if (cell_alpha <= 0.01f) continue;
        int idx = first_offset + d - 1;
        float x = gl + (idx % 7) * cw, y = gt + (idx / 7) * ch;

        bool is_target = d == target_day;
        bool is_marked = marked[d];
        bool cell_active = is_marked && (p_.today_y == 0 || (year * 10000 + p_.month * 100 + d) >= today_key);
        bool is_active = cell_active && !is_target;
        bool is_past = is_marked && !cell_active && !is_target;
        bool is_today = same_month_as_today && d == p_.today_d && !is_target;

        cv.Save();
        cv.SetAlpha(cell_alpha);
        if (is_target) {
            cv.FillRoundRect(x + 1, y + 1, cw - 2, ch - 2, 2.5f, kWhite);
        } else if (is_active) {
            cv.FillRoundRect(x + 1, y + 1, cw - 2, ch - 2, 2.5f, kGreen);
        } else if (is_past || is_today) {
            cv.FillRoundRect(x + 1.5f, y + 1.5f, cw - 3, ch - 3, 2, RGB(0x0f172a));
            cv.StrokeRoundRect(x + 1.5f, y + 1.5f, cw - 3, ch - 3, 2, 0.9f, is_today ? kSky : RGB(0x64748b));
        } else {
            cv.FillRoundRect(x + 1.5f, y + 1.5f, cw - 3, ch - 3, 2, RGB(0x12151c));
        }
        Color tc = (is_target || is_active) ? kBlack : (is_past ? RGB(0x94a3b8) : RGB(0xe5e7eb));
        std::snprintf(buf, sizeof(buf), "%d", d);
        cv.Text(buf, x + cw / 2, y + ch / 2 + 0.5f, (is_target || is_active) ? 8.0f : 7.5f, Align::Center, tc);
        cv.Restore();
    }
    cv.Restore();

    // ---- tarjeta del evento
    if (!full_month && card_t > 0.01f) {
        cv.Save();
        int card_w = RoundI(Lerp(44, 116, card_t));
        int card_h = RoundI(Lerp(48, 138, card_t));
        int card_x = RoundI((kW - card_w) / 2.0f);
        int card_y = RoundI(Lerp(view_cy - 24, 11, card_t));
        cv.FillRoundRect(static_cast<float>(card_x), static_cast<float>(card_y), static_cast<float>(card_w),
                         static_cast<float>(card_h), 7, RGB(0x0c1018));
        cv.StrokeRoundRect(card_x + 0.5f, card_y + 0.5f, card_w - 1.0f, card_h - 1.0f, 7, 1.2f, kWhite);
        if (card_t > 0.32f) {
            cv.SetAlpha(Clamp((card_t - 0.32f) / 0.48f));
            const float band_h = 18;
            cv.FillRoundRect(card_x + 3.0f, card_y + 3.0f, card_w - 6.0f, band_h, 4, kWhite);
            std::snprintf(buf, sizeof(buf), "%02d %s %d", target_day, kMonths[month], year);
            cv.Text(buf, 64, card_y + 3 + band_h / 2 + 0.5f, 8.5f, Align::Center, kBlack);

            float big_y = card_y + band_h + 24;
            std::snprintf(buf, sizeof(buf), "%02d", target_day);
            cv.Text(buf, 64, big_y, 24, Align::Center, kSky);

            float line_y = big_y + 18;
            cv.FillRect(card_x + 10.0f, line_y, card_w - 20.0f, 1, RGB(0x1e293b));

            char lines[kMaxLines][kLineLen];
            int nl = WrapText(p_.title, 8.5f, card_w - 14.0f, 3, lines);
            const float lh = 12;
            float area_top = line_y + 4, area_bottom = card_y + card_h - 4.0f;
            float block = nl * lh;
            float first_y = area_top + std::max(lh / 2, (area_bottom - area_top - block) / 2 + lh / 2);
            for (int i = 0; i < nl; ++i)
                cv.Text(lines[i], 64, static_cast<float>(RoundI(first_y + i * lh)), 8.5f, Align::Center, RGB(0xf8fafc));
        }
        cv.Restore();
    }
}

// ============================================================================
//                               TEMPORIZADOR
// ============================================================================
namespace {

constexpr float kIntroMs = 900.0f;
constexpr float kAlarmCycleMs = 4200.0f;

void DrawOledEye(Canvas& cv, float cx, float cy, float rx, float ry, float tilt, float happy_arch) {
    float srx = std::max(1.5f, rx), sry = std::max(1.5f, ry);
    cv.Save();
    cv.Translate(cx, cy);
    if (tilt != 0.0f) cv.Rotate(tilt);
    cv.FillEllipse(0, 0, srx, sry, kWhite);
    if (happy_arch > 0.02f) {
        float off_y = Lerp(sry * 1.45f, sry * 0.34f, happy_arch);
        cv.FillEllipse(0, off_y, srx * 1.18f, sry * 0.92f, kBlack);
    }
    cv.Restore();
}

void DrawSparkle(Canvas& cv, float x, float y, float radius, Color c) {
    float px = std::round(x), py = std::round(y);
    float r = std::max(1.0f, std::round(radius));
    cv.FillRect(px - 1, py - 1, 2, 2, c);
    if (r >= 2) {
        cv.FillRect(px - r, py, r * 2, 1, c);
        cv.FillRect(px, py - r, 1, r * 2, c);
    }
}

Color ProgressColor(float ratio) {
    if (ratio > 0.5f) return kSky;
    if (ratio > 0.25f) return kGreen;
    if (ratio > 0.1f) return kAmber;
    return kRed;
}

// Dos segmentos con esquina (corchete de esquina)
void Bracket(Canvas& cv, float x0, float y0, float x1, float y1, float x2, float y2, float lw, Color c) {
    float hw = lw * 0.5f;
    auto sgn = [](float v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); };
    cv.StrokeLine(x0, y0, x1 + sgn(x1 - x0) * hw, y1 + sgn(y1 - y0) * hw, lw, c);
    cv.StrokeLine(x1 - sgn(x2 - x1) * hw, y1 - sgn(y2 - y1) * hw, x2, y2, lw, c);
}

}  // namespace

void TimerScene::Start(uint32_t total_ms, const char* label, uint32_t now_ms) {
    total_ms_ = total_ms < 1000 ? 1000 : total_ms;
    if (label && *label) Utf8Prefix(label, 24, label_, sizeof(label_));
    else std::snprintf(label_, sizeof(label_), "Temporizador");
    intro_elapsed_ms_ = 0;
    elapsed_ms_f_ = 0;
    elapsed_ms_ = 0;
    alarm_elapsed_f_ = 0;
    alarm_elapsed_ms_ = 0;
    second_pulse_ = 0;
    last_whole_second_ = static_cast<int>((total_ms_ + 999) / 1000);
    mode_ = Mode::kIntro;
    last_ts_ = now_ms;
    has_last_ts_ = true;
}

void TimerScene::Reset(uint32_t now_ms) {
    intro_elapsed_ms_ = kIntroMs;
    elapsed_ms_f_ = 0;
    elapsed_ms_ = 0;
    alarm_elapsed_f_ = 0;
    alarm_elapsed_ms_ = 0;
    second_pulse_ = 0;
    last_whole_second_ = static_cast<int>((total_ms_ + 999) / 1000);
    mode_ = Mode::kIdle;
    last_ts_ = now_ms;
    has_last_ts_ = true;
}

void TimerScene::TogglePauseResume(uint32_t now_ms) {
    if (mode_ == Mode::kRunning) {
        mode_ = Mode::kPaused;
    } else if (mode_ == Mode::kPaused || mode_ == Mode::kIdle) {
        mode_ = Mode::kRunning;
        last_ts_ = now_ms;
        has_last_ts_ = true;
    } else if (mode_ == Mode::kAlarm) {
        Start(total_ms_, label_, now_ms);
    }
}

void TimerScene::JumpToAlarm(uint32_t now_ms) {
    intro_elapsed_ms_ = kIntroMs;
    elapsed_ms_f_ = static_cast<float>(total_ms_);
    elapsed_ms_ = total_ms_;
    alarm_elapsed_f_ = 0;
    alarm_elapsed_ms_ = 0;
    mode_ = Mode::kAlarm;
    last_ts_ = now_ms;
    has_last_ts_ = true;
}

void TimerScene::Tick(uint32_t now_ms) {
    if (!has_last_ts_) {
        last_ts_ = now_ms;
        has_last_ts_ = true;
    }
    float dt = now_ms >= last_ts_ ? static_cast<float>(now_ms - last_ts_) : 0.0f;
    if (dt > 100.0f) dt = 100.0f;
    last_ts_ = now_ms;

    if (second_pulse_ > 0) second_pulse_ = std::max(0.0f, second_pulse_ - dt / 240.0f);

    if (mode_ == Mode::kIntro) {
        intro_elapsed_ms_ += dt;
        if (intro_elapsed_ms_ >= kIntroMs) {
            intro_elapsed_ms_ = kIntroMs;
            mode_ = Mode::kRunning;
            last_whole_second_ = static_cast<int>((total_ms_ + 999) / 1000);
        }
    } else if (mode_ == Mode::kRunning) {
        elapsed_ms_f_ += dt;
        float remaining = std::max(0.0f, static_cast<float>(total_ms_) - elapsed_ms_f_);
        int whole = static_cast<int>(std::ceil(remaining / 1000.0f));
        if (whole != last_whole_second_ && remaining > 0) {
            last_whole_second_ = whole;
            second_pulse_ = 1.0f;
        }
        if (elapsed_ms_f_ >= static_cast<float>(total_ms_)) {
            elapsed_ms_f_ = static_cast<float>(total_ms_);
            alarm_elapsed_f_ = 0;
            mode_ = Mode::kAlarm;
        }
        elapsed_ms_ = static_cast<uint32_t>(elapsed_ms_f_);
    } else if (mode_ == Mode::kAlarm) {
        alarm_elapsed_f_ += dt;
        alarm_elapsed_ms_ = static_cast<uint32_t>(alarm_elapsed_f_);
    }
}

void TimerScene::Render(Canvas& cv, uint32_t now_ms) {
    Tick(now_ms);
    cv.Clear(kBlack);
    double now_sec = now_ms / 1000.0;
    if (mode_ == Mode::kIntro) {
        float p = Clamp(intro_elapsed_ms_ / kIntroMs);
        RenderIntro(cv, p);
        if (p > 0.75f) RenderTimerStage(cv, (p - 0.75f) / 0.25f, now_sec);
    } else if (mode_ == Mode::kAlarm) {
        RenderAlarm(cv, alarm_elapsed_f_);
    } else {
        RenderTimerStage(cv, 1.0f, now_sec);
    }
}

void TimerScene::RenderIntro(Canvas& cv, float p) {
    float appear = EaseOutBack(Clamp(p / 0.25f));
    float blink_open = 1.0f;
    if (p > 0.30f && p < 0.45f) blink_open = 1.0f - EaseInOutCubic((p - 0.30f) / 0.15f);
    else if (p >= 0.45f && p < 0.62f) blink_open = EaseInOutCubic((p - 0.45f) / 0.17f);
    float slide_up = EaseInOutCubic(Clamp((p - 0.65f) / 0.35f));
    float rx = std::max(1.5f, 20.0f * appear);
    float ry = std::max(2.0f, 21.0f * appear * blink_open);
    float cy = 80.0f - slide_up * 116.0f;
    cv.Save();
    cv.SetAlpha(1.0f - slide_up * 0.35f);
    DrawOledEye(cv, 36, cy, rx, ry, 0, 0);
    DrawOledEye(cv, 92, cy, rx, ry, 0, 0);
    cv.Restore();
}

void TimerScene::RenderTimerStage(Canvas& cv, float enter_alpha, double now_sec) {
    const float total = static_cast<float>(std::max<uint32_t>(1000, total_ms_));
    const float remaining_ms = std::max(0.0f, total - elapsed_ms_f_);
    const float ratio = Clamp(remaining_ms / total);
    const bool paused = mode_ == Mode::kPaused;
    const bool idle = mode_ == Mode::kIdle;
    const bool urgent = !paused && !idle && remaining_ms > 0 && remaining_ms <= 5000;
    const Color main_color = paused ? RGB(0x94a3b8) : ProgressColor(ratio);

    // texto del tiempo restante
    int total_sec = std::max(0, static_cast<int>(std::ceil((remaining_ms - 1.0f) / 1000.0f)));
    int hh = total_sec / 3600, mm = (total_sec % 3600) / 60, ss = total_sec % 60;
    char time_text[24];
    if (hh > 0) std::snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d", hh, mm, ss);
    else std::snprintf(time_text, sizeof(time_text), "%02d:%02d", mm, ss);

    cv.Save();
    cv.SetAlpha(enter_alpha);

    float shake_x = urgent ? static_cast<float>(std::sin(now_sec * 52.0)) * 1.4f : 0.0f;
    float shake_y = urgent ? static_cast<float>(std::cos(now_sec * 44.0)) * 1.1f : 0.0f;
    float heartbeat = urgent ? 1.0f + 0.08f * std::fabs(static_cast<float>(std::sin(now_sec * kPi * 3.5))) : 1.0f;

    if (urgent) {
        float ca = 0.25f + 0.65f * std::fabs(static_cast<float>(std::sin(now_sec * kPi * 4.0)));
        Color c = WithAlpha(kRed, ca);
        Bracket(cv, 2, 10, 2, 2, 10, 2, 2, c);
        Bracket(cv, 118, 2, 126, 2, 126, 10, 2, c);
        Bracket(cv, 2, 150, 2, 158, 10, 158, 2, c);
        Bracket(cv, 118, 158, 126, 158, 126, 150, 2, c);
    }

    // barra superior
    cv.FillRoundRect(4, 4, 120, 18, 4, RGB(0x111827));
    cv.StrokeRoundRect(4.5f, 4.5f, 119, 17, 4, 1, urgent ? kRed : RGB(0x1e293b));
    char short_label[64];
    if (Utf8Count(label_) > 13) {
        Utf8Prefix(label_, 12, short_label, sizeof(short_label));
        Append(short_label, sizeof(short_label), "…");
    } else {
        Copy(short_label, sizeof(short_label), label_);
    }
    cv.Text(short_label, 8, 13.5f, 8, Align::Left, RGB(0xf8fafc));
    const char* state = paused ? "PAUSA" : idle ? "LISTO" : urgent ? "URG" : "RUN";
    cv.Text(state, 120, 13.5f, 8, Align::Right, paused ? kAmber : (urgent ? kRed : main_color));

    // anillo orbital
    float cx = 64.0f + shake_x, cy = 82.0f + shake_y;
    float R = 43.0f + second_pulse_ * 1.8f;
    float start_a = -kPi / 2, end_a = start_a + kPi * 2 * ratio;
    cv.StrokeCircle(cx, cy, R, 5, RGB(0x161e2e));
    for (int i = 0; i < 12; ++i) {
        float a = (i / 12.0f) * kPi * 2 - kPi / 2;
        float ix = cx + std::cos(a) * (R - 6.5f), iy = cy + std::sin(a) * (R - 6.5f);
        cv.FillRect(std::round(ix) - 1, std::round(iy) - 1, 2, 2, (i % 3 == 0) ? RGB(0x475569) : RGB(0x1e293b));
    }
    if (ratio > 0.002f) {
        cv.StrokeArc(cx, cy, R, start_a, end_a, 5, main_color);
        float tip_x = cx + std::cos(end_a) * R, tip_y = cy + std::sin(end_a) * R;
        cv.FillCircle(tip_x, tip_y, 4.2f + second_pulse_ * 1.6f, urgent ? RGB(0xef4444, 0.35f) : RGB(0xffffff, 0.25f));
        cv.FillCircle(tip_x, tip_y, 2.3f, kWhite);
        for (int pi = 0; pi < 2; ++pi) {
            float oa = static_cast<float>(now_sec * 6.5 + pi * kPi);
            float od = 5.5f + static_cast<float>(std::sin(now_sec * 5.0 + pi)) * 1.2f;
            cv.FillRect(std::round(tip_x + std::cos(oa) * od), std::round(tip_y + std::sin(oa) * od), 1.5f, 1.5f,
                        pi == 0 ? kWhite : main_color);
        }
    }

    // digitos
    cv.Save();
    cv.Translate(cx, cy - 1);
    cv.Scale(heartbeat, heartbeat);
    bool long_fmt = std::strlen(time_text) > 5;
    cv.Text(time_text, 0, 0, long_fmt ? 14.0f : 19.0f, Align::Center,
            paused ? RGB(0xcbd5e1) : (urgent ? RGB(0xfecaca) : kWhite));
    char pct[16];
    int pct_value = static_cast<int>(std::lround(ratio * 100));
    if (pct_value < 0) pct_value = 0;
    if (pct_value > 100) pct_value = 100;
    std::snprintf(pct, sizeof(pct), "%d%%", pct_value);
    cv.Text(pct, 0, 15, 7.5f, Align::Center, main_color);
    cv.Restore();

    // barra lineal
    const float bx = 8, by = 142, bw = 112, bh = 8;
    cv.FillRoundRect(bx, by, bw, bh, 3, RGB(0x111827));
    cv.StrokeRoundRect(bx + 0.5f, by + 0.5f, bw - 1, bh - 1, 3, 1, RGB(0x1e293b));
    int fill_w = RoundI((bw - 4) * ratio);
    if (fill_w > 0) cv.FillRoundRect(bx + 2, by + 2, static_cast<float>(fill_w), bh - 4, 2, main_color);

    cv.Restore();
}

void TimerScene::RenderAlarm(Canvas& cv, float alarm_ms) {
    const float cycle_pos = std::fmod(alarm_ms, kAlarmCycleMs) / kAlarmCycleMs;
    const double sec = alarm_ms / 1000.0;

    // ondas concentricas
    for (int i = 0; i < 3; ++i) {
        float wt = static_cast<float>(std::fmod(sec * 0.85 + i / 3.0, 1.0));
        float radius = 14.0f + EaseOutCubic(wt) * 76.0f;
        float alpha = (1.0f - wt) * 0.42f;
        cv.StrokeCircle(64, 88, radius, 1.6f, (i % 2 == 0) ? RGB(0x38bdf8, alpha) : RGB(0xef4444, alpha));
    }

    // badge
    bool blink = std::sin(sec * kPi * 5.0) > -0.25;
    cv.FillRoundRect(14, 6, 100, 18, 5, blink ? kRed : RGB(0x111827));
    cv.StrokeRoundRect(14.5f, 6.5f, 99, 17, 5, 1, kWhite);
    cv.Text("¡TIEMPO!", 64, 15.5f, 9.5f, Align::Center, kWhite);

    float lcx = 36, rcx = 92, cy = 88;
    float lrx = 20, lry = 21, rrx = 20, rry = 21;
    float ltilt = 0, rtilt = 0, happy = 0;

    if (cycle_pos < 0.22f) {
        float u = cycle_pos / 0.22f;
        float drop = EaseOutBack(Clamp(u / 0.75f));
        cy = Lerp(148, 88, drop);
        float stretch = std::sin(Clamp(u / 0.7f) * kPi) * 0.34f;
        float squash = u > 0.55f ? std::sin(((u - 0.55f) / 0.45f) * kPi) * 0.30f : 0.0f;
        lrx = rrx = 20.0f * (1 - stretch * 0.5f + squash);
        lry = rry = 21.0f * (1 + stretch - squash * 0.65f);
    } else if (cycle_pos < 0.48f) {
        float u = (cycle_pos - 0.22f) / 0.26f;
        float env = std::sin(u * kPi);
        float buzz_x = static_cast<float>(std::sin(sec * 78.0)) * 2.6f * env;
        float look_x = std::sin(u * kPi * 3) * 6.5f;
        float pulse = 1 + 0.14f * env;
        lcx = 36 + buzz_x + look_x;
        rcx = 92 + buzz_x + look_x;
        cy = 88 + static_cast<float>(std::cos(sec * 62.0)) * 1.2f * env;
        lrx = rrx = 20 * pulse;
        lry = rry = 21 * (pulse + 0.05f * env);
        float spark_a = 0.4f + 0.6f * std::fabs(static_cast<float>(std::sin(sec * 24.0)));
        cv.Save();
        cv.SetAlpha(spark_a);
        DrawSparkle(cv, 10, 74, 3, kAmber);
        DrawSparkle(cv, 118, 74, 3, kAmber);
        DrawSparkle(cv, 12, 102, 2, kWhite);
        DrawSparkle(cv, 116, 102, 2, kWhite);
        cv.Restore();
    } else if (cycle_pos < 0.74f) {
        float u = (cycle_pos - 0.48f) / 0.26f;
        float env = u < 0.2f ? EaseOutCubic(u / 0.2f) : (u > 0.8f ? 1 - EaseInOutCubic((u - 0.8f) / 0.2f) : 1.0f);
        happy = env;
        float bounce = std::fabs(std::sin(u * kPi * 3)) * 5 * env;
        cy = 88 - bounce;
        lrx = rrx = 21 + 2 * env;
        lry = rry = 21;
        ltilt = -0.07f * env;
        rtilt = 0.07f * env;
        struct Star { float x, y, p; Color c; };
        const Star stars[5] = {{16, 54, 0.0f, kWhite}, {112, 54, 1.5f, kSky}, {64, 48, 3.0f, kWhite},
                               {18, 120, 4.2f, kSky}, {110, 120, 5.4f, kWhite}};
        for (const Star& st : stars) {
            float tw = 0.4f + 0.6f * static_cast<float>(std::sin(sec * 10.0 + st.p));
            DrawSparkle(cv, st.x, st.y, std::round(tw * 3 * env) + 1, st.c);
        }
    } else {
        float u = (cycle_pos - 0.74f) / 0.26f;
        if (u < 0.36f) {
            float w = std::sin((u / 0.36f) * kPi);
            lry = Lerp(21, 2.2f, w);
            lrx = Lerp(20, 23, w);
            ltilt = -0.16f * w;
            rry = 21 + 2 * w;
        } else if (u < 0.72f) {
            float w = std::sin(((u - 0.36f) / 0.36f) * kPi);
            rry = Lerp(21, 2.2f, w);
            rrx = Lerp(20, 23, w);
            rtilt = 0.16f * w;
            lry = 21 + 2 * w;
        } else {
            float out_t = EaseInOutCubic((u - 0.72f) / 0.28f);
            cy = Lerp(88, 148, out_t);
            lry = rry = Lerp(21, 6, out_t);
            lrx = rrx = Lerp(20, 24, out_t);
        }
    }
    DrawOledEye(cv, lcx, cy, lrx, lry, ltilt, happy);
    DrawOledEye(cv, rcx, cy, rrx, rry, rtilt, happy);

    char foot[72];
    std::snprintf(foot, sizeof(foot), "%s · 00:00", label_);
    cv.Text(foot, 64, 148, 7.5f, Align::Center, RGB(0x94a3b8));
}

// ============================================================================
//                                  CLIMA
// ============================================================================
namespace {

constexpr float kWeatherCycleMs = 7600.0f;

const char* CondLabel(Cond c) {
    switch (c) {
        case Cond::kSunny: return "SOLEADO";
        case Cond::kRain: return "LLUVIA";
        case Cond::kNight: return "DESPEJADO";
        default: return "PARC. NUBLADO";
    }
}

Color TempColor(int t) {
    if (t >= 34) return kRed;
    if (t >= 29) return kAmber;
    if (t >= 22) return kGreen;
    return kSky;
}

void DrawWeatherIcon(Canvas& cv, float cx, float cy, float scale, Cond cond, double t) {
    cv.Save();
    cv.Translate(cx, cy);
    cv.Scale(scale, scale);
    const float ts = static_cast<float>(t);

    if (cond == Cond::kSunny) {
        cv.Save();
        cv.Rotate(static_cast<float>(std::fmod(t * 0.9, 2 * kPi)));
        for (int i = 0; i < 8; ++i) {
            float a = (i / 8.0f) * kPi * 2;
            float pulse = 12.0f + static_cast<float>(std::sin(t * 4.0 + i)) * 1.4f;
            cv.StrokeLine(std::cos(a) * 9, std::sin(a) * 9, std::cos(a) * pulse, std::sin(a) * pulse, 2, kAmber);
        }
        cv.Restore();
        cv.FillCircle(0, 0, 7, RGB(0xfbbf24));
    } else if (cond == Cond::kPartly) {
        cv.Save();
        cv.Translate(-5, -4);
        cv.Rotate(static_cast<float>(std::fmod(t * 0.7, 2 * kPi)));
        for (int i = 0; i < 6; ++i) {
            float a = (i / 6.0f) * kPi * 2;
            cv.StrokeLine(std::cos(a) * 6.5f, std::sin(a) * 6.5f, std::cos(a) * 9.5f, std::sin(a) * 9.5f, 1.6f, kAmber);
        }
        cv.FillCircle(0, 0, 5.2f, RGB(0xfbbf24));
        cv.Restore();
        float drift = static_cast<float>(std::sin(t * 2.2)) * 1.4f;
        const Prim cloud[4] = {Prim::Circle(-2 + drift, 3, 4.8f), Prim::Circle(4 + drift, 2, 5.5f),
                               Prim::Circle(9 + drift, 4, 3.8f), Prim::RRect(-6 + drift, 3, 18, 5.5f, 2.5f)};
        cv.FillUnion(cloud, 4, RGB(0xf8fafc));
    } else if (cond == Cond::kRain) {
        const Prim cloud[4] = {Prim::Circle(-5, -2, 4.8f), Prim::Circle(1, -4, 5.8f), Prim::Circle(7, -1, 4.2f),
                               Prim::RRect(-9, -2, 20, 5.5f, 2.5f)};
        cv.FillUnion(cloud, 4, RGB(0x94a3b8));
        for (int d = 0; d < 3; ++d) {
            float drop_p = static_cast<float>(std::fmod(t * 2.4 + d * 0.33, 1.0));
            float gx = -5 + d * 5 - drop_p * 2;
            float gy = 5 + drop_p * 7;
            cv.SetAlpha(1.0f - drop_p);
            cv.StrokeLine(gx, gy, gx - 1.2f, gy + 3, 1.5f, kSky);
        }
    } else {  // noche
        cv.FillCircle(0, 0, 7.5f, RGB(0xfde047));
        cv.FillCircle(3.5f, -2.5f, 6.5f, kBlack);
        float tw = 0.4f + 0.6f * std::fabs(std::sin(ts * 4.0f));
        cv.FillRect(6, -6, 2, 2, RGB(0xffffff, tw));
        cv.FillRect(-8, 4, 1.5f, 1.5f, RGB(0xffffff, tw));
    }
    cv.Restore();
}

void RenderEyesStage(Canvas& cv, float ep, Cond cond, double t) {
    float appear = EaseOutBack(Clamp(ep / 0.22f));
    float slide_up = EaseInOutCubic(Clamp((ep - 0.76f) / 0.24f));
    float blink = 1.0f;
    if (ep > 0.32f && ep < 0.44f) blink = 1.0f - EaseInOutCubic((ep - 0.32f) / 0.12f);
    else if (ep >= 0.44f && ep < 0.56f) blink = EaseInOutCubic((ep - 0.44f) / 0.12f);

    float look_x = cond == Cond::kRain ? static_cast<float>(std::sin(t * 8.0)) * 3.0f
                                       : std::sin(ep * kPi * 2) * 3.0f;
    float look_y = (cond == Cond::kPartly || cond == Cond::kRain) ? -4.0f : 0.0f;
    float rx = std::max(2.0f, 20.0f * appear);
    float ry = std::max(2.0f, 21.0f * appear * blink);
    float cy = std::round(80.0f + look_y - slide_up * 114.0f);

    cv.Save();
    cv.SetAlpha(1.0f - slide_up * 0.4f);
    cv.FillEllipse(36 + look_x, cy, rx, ry, kWhite);
    cv.FillEllipse(92 + look_x, cy, rx, ry, kWhite);

    if (cond == Cond::kSunny && ep > 0.2f) {
        cv.SetAlpha(Clamp((ep - 0.2f) / 0.18f) * (1.0f - slide_up));
        for (float gx : {14.0f, 70.0f}) {
            cv.FillRoundRect(gx + look_x, cy - 11, 44, 21, 5, RGB(0x090d16));
            cv.StrokeRoundRect(gx + look_x, cy - 11, 44, 21, 5, 1.4f, kSky);
        }
        cv.FillRect(58 + look_x, cy - 5, 12, 3, kSky);
    } else if (cond == Cond::kRain) {
        for (int i = 0; i < 4; ++i) {
            float gy = static_cast<float>(std::fmod(t * 95.0 + i * 38.0, 140.0));
            cv.FillRect(18.0f + i * 30.0f, gy, 2, 6, kSky);
        }
    }
    cv.Restore();
}

void RenderCurrent(Canvas& cv, float enter_t, float exit_t, const WeatherData& d, double t) {
    float alpha = Clamp(enter_t) * (1.0f - Clamp(exit_t));
    if (alpha <= 0.01f) return;
    float slide_y = (1.0f - EaseOutCubic(enter_t)) * 18.0f - EaseInOutCubic(exit_t) * 28.0f;
    Color accent = TempColor(d.now);

    cv.Save();
    cv.SetAlpha(alpha);
    cv.Translate(0, slide_y);

    cv.FillRoundRect(4, 4, 120, 17, 3.5f, kWhite);
    cv.Text(d.city, 8, 13, 8.5f, Align::Left, kBlack);
    cv.Text("HOY", 120, 13, 8.5f, Align::Right, RGB(0x0284c7));

    DrawWeatherIcon(cv, 31, 52, 1.25f, d.cond, t);

    char buf[40];
    std::snprintf(buf, sizeof(buf), "%d°", d.now);
    cv.Text(buf, 117, 50, 31, Align::Right, kWhite);
    cv.Text(CondLabel(d.cond), 64, 77, 8, Align::Center, accent);

    cv.FillRoundRect(4, 88, 58, 36, 5, RGB(0x0c1322));
    cv.StrokeRoundRect(4.5f, 88.5f, 57, 35, 5, 1, kSky);
    cv.Text("▼ MÍNIMA", 33, 97, 7.5f, Align::Center, kSky);
    std::snprintf(buf, sizeof(buf), "%d°C", d.min);
    cv.Text(buf, 33, 113, 14, Align::Center, kWhite);

    cv.FillRoundRect(66, 88, 58, 36, 5, RGB(0x1c1012));
    cv.StrokeRoundRect(66.5f, 88.5f, 57, 35, 5, 1, kAmber);
    cv.Text("▲ MÁXIMA", 95, 97, 7.5f, Align::Center, RGB(0xfbbf24));
    std::snprintf(buf, sizeof(buf), "%d°C", d.max);
    cv.Text(buf, 95, 113, 14, Align::Center, kWhite);

    cv.FillRoundRect(4, 130, 120, 24, 4, RGB(0x111827));
    cv.StrokeRoundRect(4.5f, 130.5f, 119, 23, 4, 1, RGB(0x1e293b));
    std::snprintf(buf, sizeof(buf), "HUM:%d%%", d.hum);
    cv.Text(buf, 9, 142.5f, 7.5f, Align::Left, RGB(0x94a3b8));
    std::snprintf(buf, sizeof(buf), "V:%dkm/h", d.wind);
    cv.Text(buf, 119, 142.5f, 7.5f, Align::Right, RGB(0x94a3b8));

    cv.Restore();
}

void RenderForecast(Canvas& cv, float enter_t, const WeatherData& d, double time_sec) {
    float t = EaseOutCubic(Clamp(enter_t));
    if (t <= 0.01f) return;
    int card_w = RoundI(Lerp(56, 120, t)), card_h = RoundI(Lerp(64, 150, t));
    int card_x = RoundI((kW - card_w) / 2.0f), card_y = RoundI(Lerp(60, 5, t));

    cv.Save();
    cv.FillRoundRect(static_cast<float>(card_x), static_cast<float>(card_y), static_cast<float>(card_w),
                     static_cast<float>(card_h), 6, RGB(0x0c1018));
    cv.StrokeRoundRect(card_x + 0.5f, card_y + 0.5f, card_w - 1.0f, card_h - 1.0f, 6, 1.2f, kWhite);

    if (t > 0.35f) {
        float content_alpha = Clamp((t - 0.35f) / 0.45f);
        cv.SetAlpha(content_alpha);
        cv.FillRoundRect(card_x + 3.0f, card_y + 3.0f, card_w - 6.0f, 17, 3.5f, kWhite);
        char city7[24], head[64];
        Utf8Prefix(d.city, 7, city7, sizeof(city7));
        std::snprintf(head, sizeof(head), "PRONÓSTICO · %s", city7);
        cv.Text(head, 64, card_y + 12.0f, 8, Align::Center, kBlack);

        int gmin_i = d.min, gmax_i = d.max;
        for (int i = 0; i < 4; ++i) {
            gmin_i = std::min(gmin_i, d.forecast[i].min);
            gmax_i = std::max(gmax_i, d.forecast[i].max);
        }
        float gmin = static_cast<float>(gmin_i) - 2, gmax = static_cast<float>(gmax_i) + 2;
        float span = std::max(1.0f, gmax - gmin);
        float start_y = card_y + 26.0f;
        const float row_h = 30;

        for (int idx = 0; idx < 4; ++idx) {
            const ForecastDay& it = d.forecast[idx];
            float row_stagger = Clamp((content_alpha - idx * 0.12f) / 0.5f);
            if (row_stagger <= 0.02f) continue;
            float ry = start_y + idx * row_h;
            cv.Save();
            cv.SetAlpha(content_alpha * row_stagger);
            cv.FillRoundRect(card_x + 4.0f, ry, card_w - 8.0f, row_h - 3, 4, idx % 2 == 0 ? RGB(0x111827) : RGB(0x0b111e));
            cv.Text(it.day, card_x + 8.0f, ry + 13.5f, 8, Align::Left, RGB(0xf8fafc));
            DrawWeatherIcon(cv, card_x + 36.0f, ry + 13.5f, 0.56f, it.cond, time_sec + idx);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d°", it.min);
            cv.Text(buf, card_x + 63.0f, ry + 13.5f, 8, Align::Right, kSky);

            float bar_x = card_x + 66.0f, bar_w = 28, bar_y = ry + 11.5f;
            cv.FillRoundRect(bar_x, bar_y, bar_w, 4, 2, RGB(0x1e293b));
            float left_norm = Clamp((it.min - gmin) / span), right_norm = Clamp((it.max - gmin) / span);
            float fill_start = bar_x + std::round(left_norm * bar_w);
            float fill_w = std::max(4.0f, std::round((right_norm - left_norm) * bar_w * row_stagger));
            cv.FillRoundRectGradient(fill_start, bar_y, fill_w, 4, 2, bar_x, bar_x + bar_w, kSky, kGreen, kAmber);

            std::snprintf(buf, sizeof(buf), "%d°", it.max);
            cv.Text(buf, card_x + card_w - 7.0f, ry + 13.5f, 8, Align::Right, RGB(0xfbbf24));
            cv.Restore();
        }
    }
    cv.Restore();
}

}  // namespace

void WeatherScene::Start(const WeatherData& d, uint32_t now_ms) {
    d_ = d;
    ToUpperEs(d.city, d_.city, sizeof(d_.city));
    char cut[40];
    Utf8Prefix(d_.city, 14, cut, sizeof(cut));
    std::snprintf(d_.city, sizeof(d_.city), "%s", cut);
    start_ms_ = now_ms;
}

bool WeatherScene::Finished(uint32_t now_ms) const {
    return (now_ms - start_ms_) >= static_cast<uint32_t>(kWeatherCycleMs);
}

void WeatherScene::Render(Canvas& cv, uint32_t now_ms) {
    float elapsed = static_cast<float>(now_ms - start_ms_);
    float p = Clamp(elapsed / kWeatherCycleMs);
    if (p >= 1.0f) p = 0.92f;  // queda fija en el pronostico
    RenderAt(cv, p, now_ms / 1000.0);
}

void WeatherScene::RenderAt(Canvas& cv, float p, double time_sec) {
    cv.Clear(kBlack);
    if (p < 0.22f) RenderEyesStage(cv, p / 0.22f, d_.cond, time_sec);
    if (p >= 0.18f && p < 0.64f)
        RenderCurrent(cv, Clamp((p - 0.18f) / 0.14f), Clamp((p - 0.52f) / 0.12f), d_, time_sec);
    if (p >= 0.56f) RenderForecast(cv, Clamp((p - 0.56f) / 0.18f), d_, time_sec);
}

}  // namespace troko
