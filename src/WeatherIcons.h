#ifndef WEATHERICONS_H
#define WEATHERICONS_H

#include <Arduino.h>

// 7_SEG Weather Icons in 2 flavors, original and half sized.

extern const unsigned char WeatherIcon_Celsius [] PROGMEM;
extern const unsigned char WeatherIcon_Celsius_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Fahrenheit [] PROGMEM;
extern const unsigned char WeatherIcon_Fahrenheit_Small [] PROGMEM;
extern const unsigned char WeatherIcon_CloudSun [] PROGMEM;
extern const unsigned char WeatherIcon_CloudSun_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Cloudy [] PROGMEM;
extern const unsigned char WeatherIcon_Cloudy_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Rain [] PROGMEM;
extern const unsigned char WeatherIcon_Rain_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Snow [] PROGMEM;
extern const unsigned char WeatherIcon_Snow_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Sunny [] PROGMEM;
extern const unsigned char WeatherIcon_Sunny_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Atmosphere[] PROGMEM;
extern const unsigned char WeatherIcon_Atmosphere_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Drizzle[] PROGMEM;
extern const unsigned char WeatherIcon_Drizzle_Small [] PROGMEM;
extern const unsigned char WeatherIcon_Thunderstorm[] PROGMEM;
extern const unsigned char WeatherIcon_Thunderstorm_Small [] PROGMEM;

const unsigned char* getWeatherIcon(uint16_t Condition, bool Small = false);
const unsigned char* getTemperatureScaleIcon(bool Metric, bool Small = false);

#endif // WEATHERICONS_H
