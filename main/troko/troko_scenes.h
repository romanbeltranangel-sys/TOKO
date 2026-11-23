// Troko · escenas animadas (calendario, temporizador, clima) portadas de los simuladores TFT ST7735 128x160.
// Sin dependencias de ESP-IDF: se pueden probar en un PC (ver tools/test_troko_scenes.cc).
#pragma once

#include <cstdint>

#include "troko_gfx.h"

namespace troko {

// ---------------------------------------------------------------- Calendario
struct CalendarParams {
    int year = 2026, month = 11, day = 24;      // fecha objetivo (month 1-12)
    int today_y = 0, today_m = 0, today_d = 0;  // "hoy" (today_y = 0 -> desconocido)
    int marks[32] = {0};
    int mark_count = 0;
    char title[96] = "";
    int duration_ms = 5800;
    bool full_month = false;                    // true = solo el mes completo, sin animacion
};

class CalendarScene {
public:
    void Start(const CalendarParams& p, uint32_t now_ms);
    void Render(Canvas& cv, uint32_t now_ms);
    bool Finished(uint32_t now_ms) const;        // la animacion llego al final
    const CalendarParams& params() const { return p_; }

private:
    CalendarParams p_;
    uint32_t start_ms_ = 0;
    void Draw(Canvas& cv, float p, bool full_month);
};

// ---------------------------------------------------------------- Temporizador
class TimerScene {
public:
    enum class Mode { kIdle, kIntro, kRunning, kPaused, kAlarm };

    void Start(uint32_t total_ms, const char* label, uint32_t now_ms);  // intro + cuenta
    void Reset(uint32_t now_ms);                                        // a "LISTO"
    void TogglePauseResume(uint32_t now_ms);
    void JumpToAlarm(uint32_t now_ms);
    void Tick(uint32_t now_ms);
    void Render(Canvas& cv, uint32_t now_ms);

    Mode mode() const { return mode_; }
    uint32_t remaining_ms() const { return total_ms_ > elapsed_ms_ ? total_ms_ - elapsed_ms_ : 0; }
    uint32_t total_ms() const { return total_ms_; }
    int alarm_cycle() const { return static_cast<int>(alarm_elapsed_ms_ / 4200); }  // cuenta ciclos de alarma
    const char* label() const { return label_; }

private:
    Mode mode_ = Mode::kIdle;
    uint32_t total_ms_ = 10000;
    float intro_elapsed_ms_ = 0, elapsed_ms_f_ = 0;
    uint32_t elapsed_ms_ = 0;
    float alarm_elapsed_f_ = 0;
    uint32_t alarm_elapsed_ms_ = 0;
    float second_pulse_ = 0;
    int last_whole_second_ = -1;
    bool has_last_ts_ = false;
    uint32_t last_ts_ = 0;
    char label_[40] = "Temporizador";

    void RenderIntro(Canvas& cv, float intro_p);
    void RenderTimerStage(Canvas& cv, float enter_alpha, double now_sec);
    void RenderAlarm(Canvas& cv, float alarm_ms);
};

// ---------------------------------------------------------------- Clima
enum class Cond { kPartly, kSunny, kRain, kNight };

struct ForecastDay {
    char day[8] = "";   // "SAB", "DOM"...
    Cond cond = Cond::kPartly;
    int min = 18, max = 33;
};

struct WeatherData {
    char city[40] = "CULIACÁN";
    Cond cond = Cond::kPartly;
    int now = 31, max = 32, min = 20, hum = 34, wind = 11;
    ForecastDay forecast[4];
};

class WeatherScene {
public:
    void Start(const WeatherData& d, uint32_t now_ms);
    void Render(Canvas& cv, uint32_t now_ms);
    bool Finished(uint32_t now_ms) const;
    // Para pruebas: dibuja el progreso p (0..1) del ciclo
    void RenderAt(Canvas& cv, float p, double time_sec);

private:
    WeatherData d_;
    uint32_t start_ms_ = 0;
};

}  // namespace troko
