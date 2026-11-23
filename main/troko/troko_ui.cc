#include "troko_ui.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>
#include <new>

#include "application.h"
#include "assets/lang_config.h"

#define TAG "TrokoUi"

namespace {
inline uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
constexpr uint32_t kAlarmMaxMs = 120000;      // la alarma suena como maximo 2 minutos
constexpr uint32_t kCalendarHoldMs = 12000;   // tarjeta del evento visible tras la animacion
constexpr uint32_t kWeatherHoldMs = 15000;    // pronostico visible tras la animacion
}  // namespace

TrokoUi& TrokoUi::GetInstance() {
    static TrokoUi instance;
    return instance;
}

bool TrokoUi::EnsureCreated() {
    if (img_) return true;
    fb_ = static_cast<uint16_t*>(heap_caps_malloc(troko::kW * troko::kH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    void* mem = heap_caps_malloc(sizeof(troko::Canvas), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb_ || !mem) {
        ESP_LOGE(TAG, "Sin memoria para el overlay");
        if (fb_) heap_caps_free(fb_);
        if (mem) heap_caps_free(mem);
        fb_ = nullptr;
        return false;
    }
    gfx_ = new (mem) troko::Canvas();
    gfx_->ToRgb565(fb_);

    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc_.header.w = troko::kW;
    dsc_.header.h = troko::kH;
    dsc_.header.stride = troko::kW * 2;
    dsc_.data_size = troko::kW * troko::kH * 2;
    dsc_.data = reinterpret_cast<const uint8_t*>(fb_);

    img_ = lv_image_create(lv_layer_top());
    lv_image_set_src(img_, &dsc_);
    lv_obj_set_pos(img_, 0, 0);
    lv_obj_add_flag(img_, LV_OBJ_FLAG_HIDDEN);

    timer_ = lv_timer_create(TimerCb, 250, this);
    lv_timer_pause(timer_);
    return true;
}

void TrokoUi::TimerCb(lv_timer_t* t) { static_cast<TrokoUi*>(lv_timer_get_user_data(t))->OnTick(); }

void TrokoUi::ShowOverlay(Kind kind, uint32_t hold_ms) {
    kind_ = kind;
    hold_until_ms_ = hold_ms ? NowMs() + hold_ms : 0;
    visible_ = true;
    lv_obj_remove_flag(img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(img_);
    lv_timer_set_period(timer_, 40);
    lv_timer_resume(timer_);
}

void TrokoUi::HideLocked() {
    if (!img_) return;
    visible_ = false;
    kind_ = Kind::kNone;
    hold_until_ms_ = 0;
    lv_obj_add_flag(img_, LV_OBJ_FLAG_HIDDEN);
    if (timer_live_) {
        lv_timer_set_period(timer_, 250);
    } else {
        lv_timer_pause(timer_);
    }
}

void TrokoUi::OnTick() {
    uint32_t now = NowMs();

    // Temporizador en segundo plano: avanzar si no se esta dibujando
    if (timer_live_ && !(visible_ && kind_ == Kind::kTimer)) tmr_.Tick(now);

    if (timer_live_ && tmr_.mode() == troko::TimerScene::Mode::kAlarm) {
        if (!alarm_ringing_) {
            alarm_ringing_ = true;
            alarm_start_ms_ = now;
            last_alarm_cycle_ = -1;
            ShowOverlay(Kind::kTimer, 0);
        }
        int cycle = tmr_.alarm_cycle();
        if (cycle != last_alarm_cycle_) {
            last_alarm_cycle_ = cycle;
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_EXCLAMATION);
        }
        if (now - alarm_start_ms_ > kAlarmMaxMs) {
            timer_live_ = false;
            alarm_ringing_ = false;
            HideLocked();
            return;
        }
    }

    if (!visible_) return;
    if (hold_until_ms_ && now >= hold_until_ms_) {
        HideLocked();
        return;
    }

    switch (kind_) {
        case Kind::kCalendar: cal_.Render(*gfx_, now); break;
        case Kind::kTimer: tmr_.Render(*gfx_, now); break;
        case Kind::kWeather: wx_.Render(*gfx_, now); break;
        default: return;
    }
    gfx_->ToRgb565(fb_);
    lv_obj_invalidate(img_);
}

void TrokoUi::ShowCalendar(const troko::CalendarParams& params) {
    if (!display_) return;
    DisplayLockGuard lock(display_);
    if (!EnsureCreated()) return;
    troko::CalendarParams p = params;
    time_t t = time(nullptr);
    struct tm tmv;
    if (p.today_y == 0 && localtime_r(&t, &tmv) && tmv.tm_year >= 124) {  // hora valida (>= 2024)
        p.today_y = tmv.tm_year + 1900;
        p.today_m = tmv.tm_mon + 1;
        p.today_d = tmv.tm_mday;
    }
    uint32_t now = NowMs();
    cal_.Start(p, now);
    uint32_t run_ms = p.full_month ? 0 : static_cast<uint32_t>(p.duration_ms);
    ShowOverlay(Kind::kCalendar, run_ms + kCalendarHoldMs);
}

void TrokoUi::ShowWeather(const troko::WeatherData& data) {
    if (!display_) return;
    DisplayLockGuard lock(display_);
    if (!EnsureCreated()) return;
    wx_.Start(data, NowMs());
    ShowOverlay(Kind::kWeather, 7600 + kWeatherHoldMs);
}

void TrokoUi::StartTimer(uint32_t seconds, const std::string& label) {
    if (!display_) return;
    DisplayLockGuard lock(display_);
    if (!EnsureCreated()) return;
    uint32_t now = NowMs();
    tmr_.Start(seconds * 1000, label.c_str(), now);
    timer_live_ = true;
    alarm_ringing_ = false;
    ShowOverlay(Kind::kTimer, 0);
}

uint32_t TrokoUi::TimerRemainingSeconds() const { return (tmr_.remaining_ms() + 999) / 1000; }

std::string TrokoUi::TimerStatusJson() {
    if (!timer_live_) return "{\"state\":\"none\"}";
    const char* state = "running";
    switch (tmr_.mode()) {
        case troko::TimerScene::Mode::kPaused: state = "paused"; break;
        case troko::TimerScene::Mode::kAlarm: state = "ringing"; break;
        case troko::TimerScene::Mode::kIdle: state = "ready"; break;
        default: break;
    }
    return std::string("{\"state\":\"") + state + "\",\"remaining_seconds\":" + std::to_string(TimerRemainingSeconds()) +
           ",\"label\":\"" + tmr_.label() + "\"}";
}

std::string TrokoUi::TimerControl(const std::string& action) {
    if (!display_) return "{\"state\":\"none\"}";
    DisplayLockGuard lock(display_);
    if (!EnsureCreated()) return "{\"state\":\"none\"}";
    uint32_t now = NowMs();
    if (!timer_live_) return "{\"state\":\"none\"}";
    if (action == "pause_resume") {
        alarm_ringing_ = false;
        tmr_.TogglePauseResume(now);
        ShowOverlay(Kind::kTimer, 0);
    } else if (action == "reset") {
        alarm_ringing_ = false;
        tmr_.Reset(now);
        ShowOverlay(Kind::kTimer, 0);
    } else if (action == "stop") {
        timer_live_ = false;
        alarm_ringing_ = false;
        HideLocked();
        return "{\"state\":\"none\"}";
    } else if (action == "show") {
        ShowOverlay(Kind::kTimer, 0);
    }
    return TimerStatusJson();
}

void TrokoUi::Hide() {
    if (!display_ || !img_) return;
    DisplayLockGuard lock(display_);
    HideLocked();
}

bool TrokoUi::HandleButtonClick() {
    if (!display_ || !img_) return false;
    DisplayLockGuard lock(display_);
    if (alarm_ringing_) {  // silenciar alarma
        timer_live_ = false;
        alarm_ringing_ = false;
        HideLocked();
        return true;
    }
    if (visible_) {  // cerrar overlay (el temporizador sigue corriendo en segundo plano)
        HideLocked();
        return true;
    }
    return false;
}
