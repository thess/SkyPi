/*

	Calculate planetary positions using the VSOP87 theory

	Source: Home Planet for Windows
	Author: John Walker
	URL:    http://www.fourmilab.ch/homeplanet/

*/

#include "SkyPi.h"

#include "PlanetTerms.inc"

#define Kappa	(20.49552 / 3600.0)
#define astor(x) ((x) * (PI / (180.0 * 3600.0)))   /* Arc second->Radian */

static int quickPlanetCalc;					// Shortcut planet calculation ?

/*  CALCPLANET  --  Calculate planetary positions and altitude and azimuth from
					viewer's position.  */

void calcPlanets(double jd, double siteLat, double siteLon, int qPC)
{
	int i;
	double igmst, latsin, latcos;

    quickPlanetCalc = qPC;

	planets(jd);
	igmst = gmst(jd);
	latsin = sin(siteLat);
	latcos = cos(siteLat);
	for (i = 0; i <= 6; i++) {
		planet_info[i].lha = fixangr(dtr(igmst * 15) - siteLon - planet_info[i].ra);
		planet_info[i].az = atan2(sin(planet_info[i].lha),
                                  cos(planet_info[i].lha) * latsin - tan(planet_info[i].dec) * latcos);
		planet_info[i].alt = asin(latsin * sin(planet_info[i].dec) +
								  latcos * cos(planet_info[i].dec) * cos(planet_info[i].lha));
	}
}

/*  PLANETPOS  --  Calculate position of a single planet from the
				   terms defining it.  */

static void planetPos(int planet, double jd,
                        double *l, double *b, double *r, double *ldyn, double *bdyn)
{
	int i, j, k, nterms;
	double x, Tn;
	double y[3];
	double ld;
	double tau = (jd - 2451545.0) / 365250.0;
	struct pTerms *pt;
	double *abc;
	int isSun = FALSE;

    if (planet == 0) {
        isSun = TRUE;
        planet = 3;
    }
    pt = planetTerms[planet];
    for (i = 0 ; i < 3 ; i++) {
        y[i] = 0;
        Tn = 1; /* T^0 = 1 */
        for (j = 0 ; j < 6 ; j++) {
            x = 0;
            abc = pt[i * 6 + j].termArray;
            nterms = pt[i * 6 + j].termCount;
            if (quickPlanetCalc) {
                nterms = min(nterms, 6);
            }
            for (k = 0 ; k < nterms; abc += 3, k++) {
                x += abc[0] * cos(abc[1] + abc[2] * tau);
            }
            y[i] += x * Tn;
            Tn *= tau;
        }
    }

	*ldyn = fixangr(y[0]);
	*bdyn = y[1];
	*r = y[2];

	/* Convert from Dynamic to FK5 equator & ecliptic */

	tau *= 10;
	ld = *ldyn - dtr(1.397 * tau + 0.00031 * tau * tau);
	*b = *bdyn + astor(0.03916 * (cos(ld) - sin(ld)));
	*l = *ldyn + astor(-0.09033 + 0.03916 * tan(*bdyn) * (cos(ld) + sin(ld)));
	if (isSun) {
		*l = fixangr(*l + PI);
	}
}

/*	PLANETSPOS	--	Calculate positions for planets */

void planets(double jd)
{
	int i;
	double l, b, r, ld, bd, x, y, z, tau, jc, glon, glat, theta,
		   aberrE, aberrPI, aberrDlambda, aberrDbeta, epsilon,
		   ecos, esin, nPsi, nEps, tra,
		   sunL, sunB, sunR;	   /* Sun position */

    // Keep GCC happy
    sunL = sunB = sunR = 0;

	/* Calculate obliquity of the ecliptic and nutation
	   in obliquity and longitude.	These depend solely upon
	   the epoch and thus can be used for all the calculations
	   below. */

    epsilon = dtr(obliqeq(jd));
    nutation(jd, &nPsi, &nEps);
    epsilon += nEps;				/* Correct obliquity for nutation */
    esin = sin(epsilon);
    ecos = cos(epsilon);

	for (i = 0; i <= 6; i++)
	{
        if (i == 3) {				// Plug moon position in slot 3
            highmoon(jd, &l, &b, &r, quickPlanetCalc);
            ecliptoeq(jd, l, b, &planet_info[3].ra, &planet_info[3].dec);
            planet_info[i].dist = r;	// Note that moon distance is from Earth's centre
        } else {
            planetPos(i, jd, &l, &b, &r, &ld, &bd);
            if (i == 0) {
                sunL = ld;
                sunB = bd;
                sunR = r;
                x = -r * cos(bd) * cos(ld);
                y = -r * cos(bd) * sin(ld);
                z = -r * sin(bd);
                r = 0;
            } else {
                x = r * cos(bd) * cos(ld) - sunR * cos(sunB) * cos(sunL);
                y = r * cos(bd) * sin(ld) - sunR * cos(sunB) * sin(sunL);
                z = r * sin(bd) 		  - sunR * sin(sunB);
            }

            planet_info[i].hrv = r;			/* Heliocentric radius vector */
            planet_info[i].hlong = rtd(l);	/* Heliocentric FK5 longitude */
            planet_info[i].hlat = rtd(b);	/* Heliocentric FK5 latitude */
            planet_info[i].dhlong = rtd(ld);/* Heliocentric dynamical longitude */
            planet_info[i].dhlat = rtd(bd); /* Heliocentric dynamical latitude */
            planet_info[i].dist = sqrt(x * x + y * y + z * z); /* True distance from Earth */

            /* Light travel time over true distance from Earth. */
            tau = 0.0057755183 * planet_info[i].dist;

            /* Recompute apparent position taking into count
               speed of light delay. */

            planetPos(i, jd - tau, &l, &b, &r, &ld, &bd);
            if (i == 0) {
                x = -r * cos(bd) * cos(ld);
                y = -r * cos(bd) * sin(ld);
                z = -r * sin(bd);
                r = 0;
            } else {
                x = r * cos(bd) * cos(ld) - sunR * cos(sunB) * cos(sunL);
                y = r * cos(bd) * sin(ld) - sunR * cos(sunB) * sin(sunL);
                z = r * sin(bd) 		  - sunR * sin(sunB);
            }

            tau = 0.0057755183 * sqrt(x * x + y * y + z * z);

            /* Geocentric longitude and latitude, corrected for
               light travel time. */

            glon = atan2(y, x);
            glat = atan2(z, sqrt(x * x + y * y));

            /* Compute aberration. */

            theta = fixangr(sunL + PI);
            jc = (jd - J2000) / JulianCentury;
            aberrE = 0.016708617 - 0.000042037 * jc - 0.0000001236 * (jc * jc);
            aberrPI = dtr(102.93735 + 0.71953 * jc + 0.00046 * (jc * jc));
            aberrDlambda = ((-Kappa * cos(theta - glon)) +
                            (aberrE * Kappa * cos(aberrPI - glon))) / cos(glat);
            aberrDbeta = (-Kappa) * sin(glat) * (sin(theta - glon) -
                         aberrE * sin(aberrPI - glon));

            /* Correct for aberration. */

            glon += dtr(aberrDlambda);
            glat += dtr(aberrDbeta);

            /* Reduce to the FK5 system. */

            glat += astor(0.03916 * (cos(ld) - sin(ld)));
            glon += astor((-0.09033 + 0.03916 * tan(bd) * (cos(ld) + sin(ld))));

            /* Correct for nutation. */

            glon += nPsi;

            /* Transform into apparent right ascension and declination. */

            tra = atan2(sin(glon) * ecos - tan(glat) * esin, cos(glon));
            planet_info[i].dec = asin(sin(glat) * ecos + cos(glat) * esin * sin(glon));
            planet_info[i].ra = fixangr(tra);
        }
    }
}

