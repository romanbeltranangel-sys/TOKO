// Prueba en PC de las escenas de Troko: genera PPM de varios fotogramas.
// Compilar:  g++ -std=c++17 -O2 -Imain/troko tools/test_troko_scenes.cc main/troko/troko_gfx.cc main/troko/troko_scenes.cc -o /tmp/troko_test
// Ejecutar:  /tmp/troko_test /tmp/frames
#include <cstdio>
#include <cstring>
#include <string>
#include "troko_scenes.h"
using namespace troko;

static void Save(const Canvas& cv, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", kW, kH);
    fwrite(cv.Rgb(), 1, kW * kH * 3, f);
    fclose(f);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    Canvas cv;
    char name[256];

    // Calendario
    CalendarScene cal;
    CalendarParams cp;
    cp.year = 2026; cp.month = 11; cp.day = 24;
    cp.today_y = 2026; cp.today_m = 11; cp.today_d = 20;
    int marks[] = {20, 24, 27, 30};
    memcpy(cp.marks, marks, sizeof(marks)); cp.mark_count = 4;
    snprintf(cp.title, sizeof(cp.title), "Meses Con Mi Amor");
    cal.Start(cp, 0);
    const float ps[] = {0.08f, 0.17f, 0.30f, 0.50f, 0.65f, 0.80f, 0.88f, 1.0f};
    for (int i = 0; i < 8; ++i) {
        cal.Render(cv, static_cast<uint32_t>(ps[i] * 5800));
        snprintf(name, sizeof(name), "%s/cal_%d.ppm", dir.c_str(), i);
        Save(cv, name);
    }
    CalendarParams full = cp; full.full_month = true;
    cal.Start(full, 0); cal.Render(cv, 0);
    snprintf(name, sizeof(name), "%s/cal_full.ppm", dir.c_str()); Save(cv, name);

    // Temporizador
    TimerScene tm;
    tm.Start(10000, "Temporizador", 0);
    uint32_t t = 0;
    int shot = 0;
    const uint32_t shots[] = {200, 500, 850, 1500, 4000, 6500, 8500, 10000, 10500};
    for (uint32_t target : shots) {
        while (t < target + 900) { t += 16; tm.Render(cv, t); if (t >= target + 900 - 16) break; }
        snprintf(name, sizeof(name), "%s/timer_%d.ppm", dir.c_str(), shot++);
        Save(cv, name);
    }
    // Alarma: avanzar
    tm.JumpToAlarm(t);
    const uint32_t ash[] = {300, 1200, 2200, 3200, 3800, 4150};
    uint32_t a0 = t; int ai = 0;
    for (uint32_t target : ash) {
        while (t < a0 + target) { t += 16; tm.Render(cv, t); }
        snprintf(name, sizeof(name), "%s/alarm_%d.ppm", dir.c_str(), ai++);
        Save(cv, name);
    }
    // Pausa + hora larga
    TimerScene t2; t2.Start(3725000, "Horno", 0);
    uint32_t u = 0; while (u < 1500) { u += 16; t2.Render(cv, u); }
    snprintf(name, sizeof(name), "%s/timer_long.ppm", dir.c_str()); Save(cv, name);
    t2.TogglePauseResume(u); t2.Render(cv, u + 16);
    snprintf(name, sizeof(name), "%s/timer_pause.ppm", dir.c_str()); Save(cv, name);

    // Clima
    WeatherScene ws;
    WeatherData wd;
    const char* days[4] = {"SÁB", "DOM", "LUN", "MAR"};
    Cond conds[4] = {Cond::kPartly, Cond::kSunny, Cond::kRain, Cond::kNight};
    int mins[4] = {20, 19, 21, 18}, maxs[4] = {33, 34, 30, 32};
    for (int i = 0; i < 4; ++i) { snprintf(wd.forecast[i].day, 8, "%s", days[i]); wd.forecast[i].cond = conds[i]; wd.forecast[i].min = mins[i]; wd.forecast[i].max = maxs[i]; }
    const Cond cur[4] = {Cond::kPartly, Cond::kSunny, Cond::kRain, Cond::kNight};
    for (int c = 0; c < 4; ++c) {
        wd.cond = cur[c];
        ws.Start(wd, 0);
        const float wp[] = {0.10f, 0.20f, 0.40f, 0.60f, 0.92f};
        for (int i = 0; i < 5; ++i) {
            ws.RenderAt(cv, wp[i], 1.3 + wp[i] * 7);
            snprintf(name, sizeof(name), "%s/weather_%d_%d.ppm", dir.c_str(), c, i);
            Save(cv, name);
        }
    }
    printf("ok\n");
    return 0;
}
