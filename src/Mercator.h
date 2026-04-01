#ifndef MERCATOR_H
#define MERCATOR_H
#include <cmath>

static constexpr double PI = 3.14159265358979323846;

static inline double lon2x(double lon, int z){
    double n = std::ldexp(1.0, z); // 2^z
    return (lon + 180.0) / 360.0 * 256.0 * n;
}
static inline double lat2y(double lat, int z){
    double n = std::ldexp(1.0, z);
    double s = std::sin(lat * PI / 180.0);
    double y = 0.5 - 0.25 * std::log((1+s)/(1-s))/PI;
    return y * 256.0 * n;
}
static inline double x2lon(double x, int z){
    double n = std::ldexp(1.0, z);
    return x / (256.0 * n) * 360.0 - 180.0;
}
static inline double y2lat(double y, int z){
    double n = std::ldexp(1.0, z);
    double t = PI * (1.0 - 2.0 * (y/(256.0*n)));
    return 180.0/PI * std::atan(std::sinh(t));
}
#endif
