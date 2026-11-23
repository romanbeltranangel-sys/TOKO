// Troko · capa de pantalla: muestra calendario, temporizador y clima como overlay sobre la UI normal.
#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>

#include "display.h"
#include "troko_gfx.h"
#include "troko_scenes.h"

class TrokoUi {
public:
    static TrokoUi& GetInstance();
    void Attach(Display* display) { display_ = display; }

    void ShowCalendar(const troko::CalendarParams& params);
    void ShowWeather(const troko::WeatherData& data);
    void StartTimer(uint32_t seconds, const std::string& label);
    // action: pause_resume | reset | stop | show | status. Devuelve texto JSON de estado.
    std::string TimerControl(const std::string& action);
    void Hide();

    // Boton BOOT: devuelve true si el clic se uso (silenciar alarma / cerrar overlay).
    bool HandleButtonClick();

private:
    enum class Kind { kNone, kCalendar, kTimer, kWeather };

    TrokoUi() = default;
    bool EnsureCreated();          // con el display bloqueado
    void ShowOverlay(Kind kind, uint32_t hold_ms);
    void HideLocked();
    void OnTick();
    uint32_t TimerRemainingSeconds() const;
    std::string TimerStatusJson();
    static void TimerCb(lv_timer_t* t);

    Display* display_ = nullptr;
    lv_obj_t* img_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_image_dsc_t dsc_{};
    uint16_t* fb_ = nullptr;
    troko::Canvas* gfx_ = nullptr;

    troko::CalendarScene cal_;
    troko::TimerScene tmr_;
    troko::WeatherScene wx_;

    Kind kind_ = Kind::kNone;
    bool visible_ = false;
    bool timer_live_ = false;
    bool alarm_ringing_ = false;
    int last_alarm_cycle_ = -1;
    uint32_t alarm_start_ms_ = 0;
    uint32_t hold_until_ms_ = 0;
};
