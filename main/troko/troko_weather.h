// Troko · consulta de clima en linea (Open-Meteo, gratis y sin clave de API).
#pragma once

#include <string>

#include "troko_scenes.h"

// Ciudad por defecto si no se indica ninguna (editable aqui).
#define TROKO_DEFAULT_CITY "Culiacán"
#define TROKO_DEFAULT_LAT 24.8091
#define TROKO_DEFAULT_LON -107.3940

// Descarga clima actual + pronostico de 4 dias. Bloquea hasta ~10 s. Devuelve false y rellena `error` si falla.
bool TrokoFetchWeather(const std::string& city, troko::WeatherData* out, std::string* error);

// Resumen hablado en espanol para que la IA lo lea en voz alta
std::string TrokoWeatherSummary(const troko::WeatherData& d);
