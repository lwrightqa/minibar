/*
 * weather_proto.h: the weather data for one city, from Open-Meteo (https://api.open-meteo.com/v1/forecast).
 * Pure C: builds the request URL and reads the answer, so it also builds on Linux for the host tests.
 * Owner: lead developer.
 *
 * Coordinates are in millionths of a degree (lat_e6, lon_e6), so there is no floating point on the device.
 * Temperatures are in tenths of a degree Celsius (c10); the screen converts to Fahrenheit when asked.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_HOURS 3          /* the forecast strip: the next three whole hours */
#define WEATHER_URL_MAX 320

/* The icon the screen draws: one per state, the same set as the mock-up. */
typedef enum {
    WX_KIND_SUN,
    WX_KIND_CLOUD,
    WX_KIND_FOG,
    WX_KIND_RAIN,
    WX_KIND_SNOW,
    WX_KIND_STORM,
    WX_KIND_WIND,
} weather_kind_t;

typedef struct {
    int hour;                    /* local hour, 0 to 23, as the city's time zone gives it */
    int32_t c10;                 /* temperature, tenths of a degree Celsius */
    weather_kind_t kind;
} weather_hour_t;

typedef struct {
    int32_t now_c10;             /* current temperature */
    int32_t feels_c10;           /* apparent temperature */
    int32_t high_c10;            /* today's high */
    int32_t low_c10;             /* today's low */
    int32_t wind_kmh;            /* 10 m wind speed */
    weather_kind_t kind;         /* now */
    weather_hour_t hours[WEATHER_HOURS];
    int n_hours;
} weather_t;

/* The request for one city, with the time zone's local times (timezone=auto) and Celsius. Returns the length, or 0 when
 * the buffer is too small. tz is not sent: Open-Meteo works the zone out from the coordinates. */
size_t weather_url(char *out, size_t cap, int32_t lat_e6, int32_t lon_e6);

/* Reads Open-Meteo's answer into *out. Returns 0 on success, -1 when the answer lacks the current reading or the daily
 * high and low (the screen then keeps the last good answer). Values out of range are refused, never clamped. */
int weather_parse(const char *json, size_t len, weather_t *out);

/* The icon for a WMO weather code (Open-Meteo's weather_code), with the 10 m wind speed for "wind". */
weather_kind_t weather_kind(int code, int32_t wind_kmh);

#ifdef __cplusplus
}
#endif
