#pragma once
// Bit-identical trigonometry on every platform (header-only, no project.hpp).
//
// std::sin/std::cos/std::asin come from the C library, and macOS libm, glibc
// and MinGW's libm round their last bit differently. That is harmless on
// screen, but a value that is HASHED (a bake cache signature) or WRITTEN to a
// committed file must not depend on which OS ran the editor: the day/night
// cycle's sun direction, hashed into the GI and shadow signatures, made a Mac
// read the Showcase's checked-in GI cache as stale and ship it without probes.
//
// Only +, -, *, /, sqrt and the exact floor/frexp/ldexp, done in double, so
// with -ffp-contract=off (CMakeLists.txt) every IEEE platform gets the same
// bits. Accurate to well under a float ulp; not fast, and not meant for
// per-pixel loops.

#include <cmath>

namespace detmath {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kHalfPi = 1.57079632679489661923;

namespace detail {

// sin(r) and cos(r) for |r| <= pi/4 (Taylor; the first dropped term is
// below 1e-17 there).
inline double sinPoly(double r) {
    const double r2 = r * r;
    double p = -1.0 / 1307674368000.0;   // -1/15!
    p = p * r2 + 1.0 / 6227020800.0;     //  1/13!
    p = p * r2 - 1.0 / 39916800.0;       // -1/11!
    p = p * r2 + 1.0 / 362880.0;         //  1/9!
    p = p * r2 - 1.0 / 5040.0;           // -1/7!
    p = p * r2 + 1.0 / 120.0;            //  1/5!
    p = p * r2 - 1.0 / 6.0;              // -1/3!
    return r + r * r2 * p;
}
inline double cosPoly(double r) {
    const double r2 = r * r;
    double p = 1.0 / 20922789888000.0;   //  1/16!
    p = p * r2 - 1.0 / 87178291200.0;    // -1/14!
    p = p * r2 + 1.0 / 479001600.0;      //  1/12!
    p = p * r2 - 1.0 / 3628800.0;        // -1/10!
    p = p * r2 + 1.0 / 40320.0;          //  1/8!
    p = p * r2 - 1.0 / 720.0;            // -1/6!
    p = p * r2 + 1.0 / 24.0;             //  1/4!
    p = p * r2 - 0.5;                    // -1/2!
    return 1.0 + r2 * p;
}

// Reduces x to r in [-pi/4, pi/4] and the quadrant q (0..3), x = q*pi/2 + r.
// Two-part pi/2 (Cody-Waite) keeps r exact enough for |x| up to ~1e5, far
// past any angle an editor value holds.
inline double reduce(double x, int& q) {
    const double k = std::floor(x / kHalfPi + 0.5);
    const double hi = 1.5707963267341256e+00;  // first 33 bits of pi/2
    const double lo = 6.0771005065061922e-11;  // pi/2 - hi
    const double r = (x - k * hi) - k * lo;
    long long kk = (long long)k % 4;
    if (kk < 0) kk += 4;
    q = (int)kk;
    return r;
}

// atan(x) for x >= 0: argument reduction to [0, 2 - sqrt 3], then the odd
// series (|x|^25/25 < 1e-16 there).
inline double atanPos(double x) {
    bool inv = false;
    if (x > 1.0) { x = 1.0 / x; inv = true; }
    bool shift = false;
    const double kT = 0.26794919243112270;  // tan(pi/12) = 2 - sqrt 3
    const double kS3 = 1.73205080756887729;  // sqrt 3
    if (x > kT) { x = (x * kS3 - 1.0) / (kS3 + x); shift = true; }
    const double x2 = x * x;
    double p = 0.0;
    for (int n = 23; n >= 3; n -= 2) p = p * x2 + ((n / 2) % 2 ? -1.0 : 1.0) / n;
    double a = x + x * x2 * p;
    if (shift) a += kPi / 6.0;
    if (inv) a = kHalfPi - a;
    return a;
}

}  // namespace detail

inline double sin(double x) {
    int q = 0;
    const double r = detail::reduce(x, q);
    switch (q) {
        case 0: return detail::sinPoly(r);
        case 1: return detail::cosPoly(r);
        case 2: return -detail::sinPoly(r);
        default: return -detail::cosPoly(r);
    }
}
inline double cos(double x) {
    int q = 0;
    const double r = detail::reduce(x, q);
    switch (q) {
        case 0: return detail::cosPoly(r);
        case 1: return -detail::sinPoly(r);
        case 2: return -detail::cosPoly(r);
        default: return detail::sinPoly(r);
    }
}
inline double atan2(double y, double x) {
    if (x == 0.0 && y == 0.0) return 0.0;
    const double ay = y < 0.0 ? -y : y, ax = x < 0.0 ? -x : x;
    double a = ax >= ay ? detail::atanPos(ay / ax) : kHalfPi - detail::atanPos(ax / ay);
    if (x < 0.0) a = kPi - a;
    return y < 0.0 ? -a : a;
}
inline double asin(double x) {
    if (x >= 1.0) return kHalfPi;
    if (x <= -1.0) return -kHalfPi;
    return atan2(x, std::sqrt((1.0 - x) * (1.0 + x)));  // sqrt is exactly rounded
}

inline double acos(double x) { return kHalfPi - asin(x); }

inline constexpr double kLn2Hi = 6.93147180369123816490e-01;  // exact in 32 bits
inline constexpr double kLn2Lo = 1.90821492927058770002e-10;  // ln 2 - kLn2Hi

// log(x) for x > 0: x = m * 2^e with m in [sqrt(1/2), sqrt 2), then
// log m = 2 atanh(s), s = (m-1)/(m+1), |s| < 0.172 (s^23/23 < 1e-19).
inline double log(double x) {
    if (!(x > 0.0)) return -1e300;
    int e = 0;
    double m = std::frexp(x, &e);  // exact: m in [0.5, 1)
    if (m < 0.70710678118654752440) { m *= 2.0; --e; }
    const double s = (m - 1.0) / (m + 1.0), s2 = s * s;
    double p = 0.0;
    for (int n = 21; n >= 3; n -= 2) p = p * s2 + 1.0 / n;
    const double lm = 2.0 * (s + s * s2 * p);
    return e * kLn2Hi + (e * kLn2Lo + lm);
}

// exp(x): x = k ln2 + r with |r| <= ln2/2, Taylor for e^r, then the exact
// scaling by 2^k.
inline double exp(double x) {
    if (x > 709.0) return 1e300;
    if (x < -745.0) return 0.0;
    const double k = std::floor(x / (kLn2Hi + kLn2Lo) + 0.5);
    const double r = (x - k * kLn2Hi) - k * kLn2Lo;
    double p = 1.0;
    for (int n = 18; n >= 1; --n) p = 1.0 + p * r / n;
    return std::ldexp(p, (int)k);
}

// pow for x >= 0 (every caller here raises magnitudes).
inline double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x <= 0.0) return 0.0;
    if (y == 1.0) return x;
    if (y == 2.0) return x * x;
    return exp(y * log(x));
}

inline float log(float x) { return (float)log((double)x); }
inline float exp(float x) { return (float)exp((double)x); }
inline float pow(float x, float y) { return (float)pow((double)x, (double)y); }

inline float sin(float x) { return (float)sin((double)x); }
inline float acos(float x) { return (float)acos((double)x); }
inline float cos(float x) { return (float)cos((double)x); }
inline float asin(float x) { return (float)asin((double)x); }
inline float atan2(float y, float x) { return (float)atan2((double)y, (double)x); }

}  // namespace detmath
