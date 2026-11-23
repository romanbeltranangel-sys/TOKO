#include "troko_weather.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "board.h"
#include "http.h"

#define TAG "TrokoWeather"

namespace {

struct JsonDeleter {
    void operator()(cJSON* p) const { if (p) cJSON_Delete(p); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool LowerEq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Descarga una URL por GET (en una tarea con pila grande por el TLS).
struct FetchJob {
    std::string url;
    std::string body;
    std::string error;
    bool ok = false;
    SemaphoreHandle_t done = nullptr;
};

void FetchTask(void* arg) {
    FetchJob* job = static_cast<FetchJob*>(arg);
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetHeader("User-Agent", "Troko/1.0 (ESP32-S3)");
    http->SetHeader("Accept", "application/json");
    if (auto opened = http->Open("GET", job->url); !opened) {
        job->error = "No se pudo conectar: " + opened.error().ToString();
    } else {
        auto status = http->GetStatusCode();
        if (!status) {
            job->error = "Sin respuesta del servidor de clima";
        } else if (*status != 200) {
            job->error = "El servidor de clima respondio con codigo " + std::to_string(*status);
        } else {
            job->body = http->ReadAll();
            job->ok = !job->body.empty();
            if (!job->ok) job->error = "Respuesta vacia del servidor de clima";
        }
        http->Close();
    }
    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}

bool HttpGet(const std::string& url, std::string* body, std::string* error) {
    FetchJob job;
    job.url = url;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        *error = "Sin memoria";
        return false;
    }
    if (xTaskCreate(FetchTask, "troko_http", 12288, &job, 5, nullptr) != pdPASS) {
        vSemaphoreDelete(job.done);
        *error = "No se pudo crear la tarea de red";
        return false;
    }
    bool finished = xSemaphoreTake(job.done, pdMS_TO_TICKS(15000)) == pdTRUE;
    if (!finished) {
        // La tarea sigue usando `job`; no la liberamos para evitar un acceso invalido.
        ESP_LOGE(TAG, "Tiempo de espera agotado");
        *error = "Tiempo de espera agotado consultando el clima";
        // Esperar un poco mas a que termine para poder liberar con seguridad
        finished = xSemaphoreTake(job.done, pdMS_TO_TICKS(20000)) == pdTRUE;
        if (!finished) return false;  // (pierde ~100 bytes; evita corrupcion)
    }
    vSemaphoreDelete(job.done);
    if (!job.ok) {
        *error = job.error;
        return false;
    }
    *body = std::move(job.body);
    return true;
}

int DowSun0(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Codigo WMO -> condicion del icono
troko::Cond MapCode(int code, bool is_day) {
    if (code == 0 || code == 1) return is_day ? troko::Cond::kSunny : troko::Cond::kNight;
    if (code == 2 || code == 3 || code == 45 || code == 48) return troko::Cond::kPartly;
    return troko::Cond::kRain;  // llovizna, lluvia, nieve, tormenta
}

const char* CondText(troko::Cond c) {
    switch (c) {
        case troko::Cond::kSunny: return "soleado";
        case troko::Cond::kRain: return "con lluvia";
        case troko::Cond::kNight: return "despejado";
        default: return "parcialmente nublado";
    }
}

double JNum(const cJSON* obj, const char* key, double def) {
    const cJSON* it = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(it) ? it->valuedouble : def;
}

}  // namespace

bool TrokoFetchWeather(const std::string& city_in, troko::WeatherData* out, std::string* error) {
    std::string city = city_in;
    while (!city.empty() && city.back() == ' ') city.pop_back();
    size_t first = city.find_first_not_of(' ');
    city = first == std::string::npos ? "" : city.substr(first);

    double lat = TROKO_DEFAULT_LAT, lon = TROKO_DEFAULT_LON;
    std::string display_city = TROKO_DEFAULT_CITY;

    if (!city.empty() && !LowerEq(city, "culiacán") && !LowerEq(city, "culiacan")) {
        std::string url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=es&format=json&name=" +
                          UrlEncode(city);
        std::string body;
        if (!HttpGet(url, &body, error)) return false;
        JsonPtr root(cJSON_Parse(body.c_str()));
        const cJSON* results = root ? cJSON_GetObjectItem(root.get(), "results") : nullptr;
        const cJSON* r0 = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
        if (!r0) {
            *error = "No encontre la ciudad " + city;
            return false;
        }
        lat = JNum(r0, "latitude", lat);
        lon = JNum(r0, "longitude", lon);
        const cJSON* name = cJSON_GetObjectItem(r0, "name");
        display_city = cJSON_IsString(name) ? name->valuestring : city;
    }

    char url[512];
    std::snprintf(url, sizeof(url),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
                  "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                  "&forecast_days=4&timezone=auto&wind_speed_unit=kmh",
                  lat, lon);
    std::string body;
    if (!HttpGet(url, &body, error)) return false;
    JsonPtr root(cJSON_Parse(body.c_str()));
    if (!root) {
        *error = "Respuesta de clima invalida";
        return false;
    }
    const cJSON* cur = cJSON_GetObjectItem(root.get(), "current");
    const cJSON* daily = cJSON_GetObjectItem(root.get(), "daily");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(daily)) {
        *error = "Respuesta de clima incompleta";
        return false;
    }
    const cJSON* dt = cJSON_GetObjectItem(daily, "time");
    const cJSON* dcode = cJSON_GetObjectItem(daily, "weather_code");
    const cJSON* dmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
    const cJSON* dmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
    if (!cJSON_IsArray(dt) || !cJSON_IsArray(dcode) || !cJSON_IsArray(dmax) || !cJSON_IsArray(dmin) ||
        cJSON_GetArraySize(dt) < 1) {
        *error = "Pronostico incompleto";
        return false;
    }

    troko::WeatherData d;
    std::snprintf(d.city, sizeof(d.city), "%s", display_city.c_str());
    bool is_day = JNum(cur, "is_day", 1) > 0.5;
    d.cond = MapCode(static_cast<int>(JNum(cur, "weather_code", 2)), is_day);
    d.now = static_cast<int>(std::lround(JNum(cur, "temperature_2m", 25)));
    d.hum = static_cast<int>(std::lround(JNum(cur, "relative_humidity_2m", 50)));
    d.wind = static_cast<int>(std::lround(JNum(cur, "wind_speed_10m", 0)));
    const cJSON* max0 = cJSON_GetArrayItem(dmax, 0);
    const cJSON* min0 = cJSON_GetArrayItem(dmin, 0);
    d.max = cJSON_IsNumber(max0) ? static_cast<int>(std::lround(max0->valuedouble)) : d.now;
    d.min = cJSON_IsNumber(min0) ? static_cast<int>(std::lround(min0->valuedouble)) : d.now;

    static const char* const names[7] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
    int n = std::min(4, cJSON_GetArraySize(dt));
    for (int i = 0; i < 4; ++i) {
        int src = std::min(i, n - 1);
        const cJSON* date = cJSON_GetArrayItem(dt, src);
        int y = 0, m = 0, dd = 0;
        if (cJSON_IsString(date) && std::sscanf(date->valuestring, "%d-%d-%d", &y, &m, &dd) == 3 && m >= 1 && m <= 12) {
            std::snprintf(d.forecast[i].day, sizeof(d.forecast[i].day), "%s", names[DowSun0(y, m, dd)]);
        }
        const cJSON* c = cJSON_GetArrayItem(dcode, src);
        d.forecast[i].cond = MapCode(cJSON_IsNumber(c) ? c->valueint : 2, true);
        const cJSON* mx = cJSON_GetArrayItem(dmax, src);
        const cJSON* mn = cJSON_GetArrayItem(dmin, src);
        d.forecast[i].max = cJSON_IsNumber(mx) ? static_cast<int>(std::lround(mx->valuedouble)) : d.max;
        d.forecast[i].min = cJSON_IsNumber(mn) ? static_cast<int>(std::lround(mn->valuedouble)) : d.min;
    }
    *out = d;
    return true;
}

std::string TrokoWeatherSummary(const troko::WeatherData& d) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "Clima en %s: %d grados, %s. Maxima %d y minima %d grados. Humedad %d%% y viento de %d km/h. "
                  "Pronostico: %s %d a %d grados, %s %d a %d, %s %d a %d, %s %d a %d.",
                  d.city, d.now, CondText(d.cond), d.max, d.min, d.hum, d.wind, d.forecast[0].day, d.forecast[0].min,
                  d.forecast[0].max, d.forecast[1].day, d.forecast[1].min, d.forecast[1].max, d.forecast[2].day,
                  d.forecast[2].min, d.forecast[2].max, d.forecast[3].day, d.forecast[3].min, d.forecast[3].max);
    return buf;
}
