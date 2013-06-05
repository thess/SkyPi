#ifndef SKYPI_H_INCLUDED
#define SKYPI_H_INCLUDED

#include <math.h>
#include <time.h>
#include <assert.h>

#include <config.h>

// boolean constants
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef NULL
#define NULL (void *)0
#endif

/* Assume not near black hole nor in Tennessee */
#define PI 3.14159265358979323846

//	Frequently used astronomical constants

#define J2000				2451545.0		// Julian day of J2000 epoch
#define JulianCentury		36525.0			// Days in Julian century
#define AstronomicalUnit	149597870.0		// Astronomical unit in kilometres
#define SunSMAX	 (AstronomicalUnit * 1.000001018) // Semi-major axis of Earth's orbit
#define EarthRad			6378.14			// Earth's equatorial radius, km (IAU 1976)
#define LunatBase			2423436.0		/* Base date for E. W. Brown's numbered
											   series of lunations (1923 January 16) */
#define SynMonth			29.53058868		// Synodic month (mean time from new Moon to new Moon)

/*  Handy mathematical functions  */

#ifdef abs
#undef abs
#endif

#define sgn(x) (((x) < 0) ? -1 : ((x) > 0 ? 1 : 0))       /* Extract sign */
#define abs(x) ((x) < 0 ? (-(x)) : (x))                   /* Absolute val */
#define fixangle(a) ((a) - 360.0 * (floor((a) / 360.0)))  /* Fix angle */
#define fixangr(a)  ((a) - (PI*2) * (floor((a) / (PI*2))))  /* Fix angle in radians */
#define dtr(x) ((x) * (PI / 180.0))                       /* Degree->Radian */
#define rtd(x) ((x) * (180.0 / PI))                       /* Radian->Degree */

#define max(a,b)	((a) > (b) ? (a) : (b))
#define min(a,b)	((a) < (b) ? (a) : (b))

struct planet {                 // Planet information entry
    double hlong;               // FK5 Heliocentric longitude
    double hlat;                // FK5 Heliocentric latitude
    double hrv;                 // Heliocentric radius vector
    double dhlong;              // Dynamical Heliocentric longitude
    double dhlat;               // Dynamical Heliocentric latitude
    double ra;                  // Apparent right ascension
    double dec;                 // Apparent declination
    double dist;                // True distance from the Earth
    double mag;                 // Approximate magnitude
    double lha;                 // Local hour angle
    double alt;                 // Altitude above (-below) horizon
    double az;                  // Azimuth from South: West positive, East negative
};
extern struct planet planet_info[7];		// Calculated planetary information

struct pplanet {
    char    *Name;
    short   Color;
    short   Size;   // Point diameter (neg for filled)
};

enum planet_idx {
    SUN = 0,
    MERCURY,
    VENUS,
    MOON,
    MARS,
    JUPITER,
    SATURN
};

extern void highmoon(double jd, double *l, double *b, double *r, int qPC);
extern double obliqeq(double jd);
extern void ecliptoeq(double jd, double Lambda, double Beta, double *Ra, double *Dec);
extern void set_tm_time(struct tm *t, int islocal);
extern double jtime(struct tm *t);
extern void jyear(double td, long *yy, int *mm, int *dd);
extern void jhms(double j, int *h, int *m, int *s);
extern double gmst(double jd);
extern double ucttoj(long year, int mon, int mday, int hour, int min, int sec);
extern void sunpos(double jd, int apparent, double *ra, double *dec, double *rv, double *slong);
extern void planets(double jd);             /* Update planetary positions */
extern void nutation(double jd, double *deltaPsi, double *deltaEpsilon);

void calcPlanets(double jd, double siteLat, double siteLon, int qPC);

#endif // SKYPI_H_INCLUDED
