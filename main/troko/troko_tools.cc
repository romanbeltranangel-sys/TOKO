#include "troko_tools.h"

#include <esp_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mcp_server.h"
#include "troko_ui.h"
#include "troko_weather.h"

#define TAG "TrokoTools"

namespace {
int DaysIn(int y, int m) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = d[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) n = 29;
    return n;
}
}  // namespace

void TrokoRegisterTools(Display* display) {
    TrokoUi::GetInstance().Attach(display);
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.troko.calendar.show",
        "Show an animated calendar on Troko's screen (eyes -> month grid -> zoom into the day -> event card). "
        "Use it when the user asks to see the calendar, a date, a countdown/reminder of a special date or an "
        "event (e.g. 'muéstrame el calendario', 'marca el 24 de noviembre: Meses con mi amor'). "
        "`date` is the target day as YYYY-MM-DD, `title` is the event name, `marked_days` is an optional "
        "comma-separated list of other day numbers of the same month to highlight (e.g. '20,27,30'). "
        "Set `month_only` to true to just show the whole month without animation.",
        PropertyList({Property("date", kPropertyTypeString), Property("title", kPropertyTypeString, std::string("")),
                      Property("marked_days", kPropertyTypeString, std::string("")),
                      Property("month_only", kPropertyTypeBoolean, false)}),
        [](const PropertyList& props) -> ReturnValue {
            std::string date = props["date"].value<std::string>();
            int y = 0, m = 0, d = 0;
            if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 1970 || y > 2200 || m < 1 || m > 12 ||
                d < 1 || d > DaysIn(y, m)) {
                return std::string("Invalid date. Use YYYY-MM-DD.");
            }
            troko::CalendarParams p;
            p.year = y;
            p.month = m;
            p.day = d;
            std::snprintf(p.title, sizeof(p.title), "%s", props["title"].value<std::string>().c_str());
            std::string marks = props["marked_days"].value<std::string>();
            const char* s = marks.c_str();
            while (*s && p.mark_count < 31) {
                char* end = nullptr;
                long v = std::strtol(s, &end, 10);
                if (end == s) {
                    ++s;
                    continue;
                }
                if (v >= 1 && v <= 31) p.marks[p.mark_count++] = static_cast<int>(v);
                s = end;
            }
            p.full_month = props["month_only"].value<bool>();
            TrokoUi::GetInstance().ShowCalendar(p);
            return std::string("Calendar shown on screen.");
        });

    mcp.AddTool(
        "self.troko.timer.start",
        "Start a visual countdown timer on Troko's screen (orbital ring, urgency colors, expressive alarm when it "
        "ends; the alarm also beeps until the user presses the BOOT button). Use it for 'pon un temporizador de "
        "5 minutos', 'cronómetro', 'avísame en 10 segundos'. `seconds` is the total duration; `label` is an "
        "optional short name (e.g. 'Pasta').",
        PropertyList({Property("seconds", kPropertyTypeInteger, 1, 86399),
                      Property("label", kPropertyTypeString, std::string("Temporizador"))}),
        [](const PropertyList& props) -> ReturnValue {
            int seconds = props["seconds"].value<int>();
            TrokoUi::GetInstance().StartTimer(static_cast<uint32_t>(seconds), props["label"].value<std::string>());
            return TrokoUi::GetInstance().TimerControl("status");
        });

    mcp.AddTool(
        "self.troko.timer.control",
        "Control the running timer. `action` is one of: 'pause_resume' (pause or resume), 'reset' (back to ready "
        "state), 'stop' (cancel the timer and silence the alarm), 'show' (bring the timer back on screen), "
        "'status' (get remaining seconds). Returns JSON with the state and remaining seconds.",
        PropertyList({Property("action", kPropertyTypeString)}),
        [](const PropertyList& props) -> ReturnValue {
            return TrokoUi::GetInstance().TimerControl(props["action"].value<std::string>());
        });

    mcp.AddTool(
        "self.troko.weather.show",
        "Fetch the real current weather and the 4-day forecast and show it on Troko's screen (eyes -> current "
        "temperature -> min/max -> forecast card). Returns a summary you should read aloud to the user. Use it "
        "for 'cómo está el clima', 'qué temperatura hace', 'va a llover'. `city` is optional; empty means the "
        "default city (Culiacán).",
        PropertyList({Property("city", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& props) -> ReturnValue {
            troko::WeatherData data;
            std::string error;
            if (!TrokoFetchWeather(props["city"].value<std::string>(), &data, &error)) {
                return std::string("Could not get the weather: ") + error;
            }
            TrokoUi::GetInstance().ShowWeather(data);
            return TrokoWeatherSummary(data);
        });

    mcp.AddTool("self.troko.screen.hide", "Close any calendar/weather/timer panel shown on Troko's screen and go "
                                          "back to the eyes. A running timer keeps counting.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    TrokoUi::GetInstance().Hide();
                    return true;
                });
    ESP_LOGI(TAG, "Herramientas de Troko registradas");
}
