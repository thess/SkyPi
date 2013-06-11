/* SkyPi.c
 *
 * Copyright (C) 2013        Ted Hess (Kitschensync)
 *
 * SkyPi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SkyPi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SkyPi; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fenv.h>
#include <ctype.h>
#include <termios.h>

#include "SkyPi.h"

// defines for 4dgl constants
#include "Include/Picaso_const4D.h"
#include "Include/Picaso_Serial_4DLibrary.h"

//#define PIXPERRAD   171.8873385
//#define YPixRad  PIXPERRAD
//#define XPixRad  PIXPERRAD

#define YPixRad  (272 / dtr(80))
#define XPixRad  (480 / dtr(120))

// Put display to sleep after NAPMINS minutes if already sleeping
#define NAPMINS     30
#define SLEEPHR     23
#define SLEEPMIN    30
#define WAKEHR      06
#define WAKEMIN     30

#define maxrates 20
static int  baudrates[maxrates] = {   110,    300,    600,   1200,   2400,   4800,   9600,
                                     14400,  19200,  31250,  38400,  56000,  57600, 115200,
                                     128000, 256000, 300000, 375000, 500000, 600000} ;

#define HYGPATH "/usr/local/lib/SkyPi"
#define HYGFILE "hyg11.csv"

// Unix time structs
static char rtcval[24];
static struct tm tmGMT;
static struct tm tmLocal;
static time_t ttime;

// Julian date/time
static double  JD;

// LatLong of Hudson, MA (in radians)
static double Latitude = dtr(42.38050);
static double Longitude = dtr(71.53285);

// Sleep and cuckoo control
static int napCount;
static int sleeping;

//-------------------------------------------------------------------------------

void Usage(char *programName)
{
	fprintf(stderr,"RaspPi Realtime Sky Map V%d.%d\n\n", VERSION_MAJOR, VERSION_MINOR);
	fprintf(stderr,"%s ComPort [speed]\n\n",programName);
	fprintf(stderr," ComPort    Comms port to which display is attached (default ttyAMA0)\n") ;
	fprintf(stderr," speed      Speed at which to begin (default 9600)\n\n") ;
}

int errCallback(int ErrCode, unsigned char Errbyte)
{
	printf("Serial 4D Library reports error %s", Error4DText[ErrCode]) ;
	if (ErrCode == Err4D_NAK)
		printf(" returned data = 0x%02X\n", Errbyte) ;
	else
		printf("\n") ;

    //Abort on error?
    if (Error_Abort4D)
        exit(ErrCode);

	return ErrCode;
}

//-------------------------------------------------------------------------------
// X/Y projection calc


void XYFromAzAlt(double az, double alt, int *iX, int *iY)
{
    double X, Y;
    double CentralAngle;

    // use stereographic projection from equator
    CentralAngle = acos(cos(alt) * cos(az));
    // get altitude in pixels
    Y = CentralAngle * YPixRad * sin(alt) / sin(CentralAngle);

	//get azimuth in pixels
    X = CentralAngle * XPixRad * cos(alt) * sin(az) / sin(CentralAngle);

    *iX = 239 + (int)round(X);      // center on screen
    *iY = 271 - (int)round(Y);      // inverty Y coordinate

    return;
}

//-------------------------------------------------------------------------------
// AzAlt    Convert RA & DEC to Azimuth/Altitude (local)

void AzAlt(double ra, double dec, double *az, double *alt)
{
    double igmst, latsin, latcos, lha;

    igmst = gmst(JD);

	latsin = sin(Latitude);
	latcos = cos(Latitude);

    lha = fixangr(dtr(igmst * 15) - Longitude - ra);
    *az = atan2(sin(lha), cos(lha) * latsin - tan(dec) * latcos);
    *alt = asin(latsin * sin(dec) + latcos * cos(dec) * cos(lha));

    return;
}

//-------------------------------------------------------------------------------
// csv_parse

int csv_parse(char *sLine, char *sElems[], int nElem)
{
    char   *p;
    size_t n;

    p = sLine;
    n = 0;
    while (TRUE)
    {
        // Nothing of use
        if (*p == '\0')
            return n;

        // Save the string
        sElems[n++] = p;
        // Find the next field
        while ((*p != ',') && (*p != '\n'))
            p++;

        // Nothing else of use or too many fields
        if ((*p == '\0') || (n >= nElem))
            return n;

        // Split the field
        *p++ = '\0';
    }

	return  n;
}

//-------------------------------------------------------------------------------
// plotStarField    Read and parse database (optional constellation lines)

void plotStarField(const char *fname, int bConstellaltions)
{
    FILE    *fd;
    size_t  n;
    char    tmpBuf[120];
    char    *starInfo[6];
    char    *pBuffer = &tmpBuf[0];
    size_t  nSize = sizeof(tmpBuf);

    double az, alt;
    static int iX, iY, iMAG;
    WORD    color;

    // Build path to star database file
    strcpy(tmpBuf, HYGPATH);
    if (tmpBuf[strlen(tmpBuf) - 1] != '/')
        strcat(tmpBuf, "/");
    strcat(tmpBuf, fname);

    // Open DB file
    fd = fopen(tmpBuf, "r");
    if (fd == NULL)
    {
        printf("Cannot open database file: %s\n", tmpBuf);
        exit(2);
    }

    gfx_ClipWindow(0, 0, 479, 271);
    gfx_Clipping(ON);

    while(!feof(fd))
    {
        n = getline(&pBuffer, &nSize, fd);
        if (n == -1)
            break;

        if (n >= sizeof(tmpBuf))
        {
            printf("Line too long: '%s'\n", tmpBuf);
            exit(2);
        }

        n = csv_parse(tmpBuf, &starInfo[0], 6);
        if (n != 5)
        {
            printf("Too many fields: %d, %s\n", n, starInfo[0]);
            exit(2);
        }
        // Convert RA, DEC to screen coords

        AzAlt(atof(starInfo[1]), atof(starInfo[2]), &az, &alt);
        XYFromAzAlt(az, alt, &iX, &iY);

#ifdef DEBUG_PRINT
        printf("%-10s: RA = %.02f, DEC = %.02f, MAG = %d",
                starInfo[0], rtd(atof(starInfo[1])), rtd(atof(starInfo[2])), atoi(starInfo[3]));
        printf(", Az = %.02f, Alt = %.02f\n", az, alt);
#endif
        // Want constellation lines?
        if (bConstellaltions)
        {
            if (*(starInfo[4]) == 'S')
            {
                gfx_MoveTo(iX, iY);              //start a constellation line
            }

            if ((*(starInfo[4]) == 'N') || (*(starInfo[4]) == 'E'))
            {
                gfx_Set(OBJECT_COLOUR, 0x0204);
                gfx_LineTo(iX, iY);            //continue a constellation line
            }
        }

        iMAG = (int)round(4.0 - atof(starInfo[3]));

        // Skip over line drawing entries
        if (iMAG < 6)
        {
            if((iX >= 0 && iX <= 479) && (iY >= 0 && iY <= 271))
            {
                // Map spectrum type
                switch (*(starInfo[4]))
                {
                case 'A':   color = LIGHTBLUE; break;   //blue-white
                case 'B':   color = BLUE; break;   //blue
                case 'F':   color = WHITE; break;  //white
                case 'G':   color = YELLOW; break;  //yellow
                case 'K':   color = ORANGE; break;  //orange
                case 'M':   color = RED; break;     //red
                case 'O':   color = BLUE; break;    //bright blue
                case 'W':   color = WHITE; break;   //white
                default:    color = WHITE; break;  //white
                }

                // Draw star according to MAG

                switch(iMAG)
                {
                case 5:
                    gfx_CircleFilled(iX, iY, 2, color);
                    break;

                case 4:
                case 3:
                    gfx_CircleFilled(iX, iY, 1, color);
                    break;

                //case 2:
                //    gfx_CircleFilled(iX, iY, 1, color);
                //    break;

                default:
                    //visible print a dot
                    gfx_PutPixel(iX, iY, color);
                    break;
                }
            }
        }
    }

    gfx_Clipping(OFF);

    fclose(fd);

    return;
}

//-------------------------------------------------------------------------------
// planet graphic data plotting

static struct pplanet pp_data[7] = {
    {"sun", YELLOW, 5},
    {"mercury", LIGHTGREY, 3},
    {"venus", LIGHTGREY, 3},
    {"moon", LIGHTGREY, 4},
    {"mars", RED, 3},
    {"jupiter", YELLOW, 3},
    {"saturn", YELLOW, 3}
};

void plotPlanets(void)
{
    int iX, iY;
    int kPlanet, nSize;

    gfx_ClipWindow(0, 0, 479, 271);
    gfx_Clipping(ON);

    for (kPlanet = SUN; kPlanet <= SATURN; kPlanet++)
    {
#ifdef DEBUG_PRINT
        printf("%-10s: RA = %.02f, DEC = %.02f", pp_data[kPlanet].Name, rtd(planet_info[kPlanet].ra), rtd(planet_info[kPlanet].dec));
        printf(", Az = %.02f, Alt = %.02f\n", rtd(planet_info[kPlanet].az), rtd(planet_info[kPlanet].alt));
#endif
        XYFromAzAlt(planet_info[kPlanet].az, planet_info[kPlanet].alt, &iX, &iY);
        if ((iX >= 0 && iX <= 479) &&(iY >= 0 && iY <= 271))
        {
            nSize = pp_data[kPlanet].Size;
            if (kPlanet == SUN)
            {
                // Just draw sun without label
                gfx_CircleFilled(iX, iY, abs(nSize), pp_data[kPlanet].Color);
            } else {
                gfx_Circle(iX, iY, nSize, pp_data[kPlanet].Color);
                if (kPlanet == SATURN)
                    gfx_Ellipse(iX, iY, 6, 2, YELLOW);    //Saturn rings
                // Add label
                gfx_MoveTo(iX + nSize + 1, iY + nSize + 1);
                putStr(pp_data[kPlanet].Name);
            }
        }
    }

    gfx_Clipping(OFF);

    return;
}

//-------------------------------------------------------------------------------

void drawAzAltGrid(int gType)
{
    int x, y;
    double m, step = 1.0;

    // Draw some Alt-Az lines
    switch (gType)
    {
    case 0:
        // Use circles (not really functional)
        gfx_ClipWindow(0, 0, 479, 115);
        gfx_Clipping(ON);
        gfx_Circle(239, -171, 260, DARKOLIVEGREEN);     //+60 ALT
        gfx_Clipping(OFF);

        gfx_ClipWindow(0, 0, 479, 250);
        gfx_Clipping(ON);
        gfx_Circle(239, -465, 644, DARKOLIVEGREEN);     //+30 ALT
        gfx_Clipping(OFF);

        gfx_ClipWindow(239, 0, 479, 271);
        gfx_Clipping(ON);
        gfx_Circle(-120, 271, 490, DARKOLIVEGREEN);     //+30 AZ
        gfx_Clipping(OFF);

        gfx_ClipWindow(239, 0, 479, 271);
        gfx_Clipping(ON);
        gfx_Circle(19, 271, 478, DARKOLIVEGREEN);       //+60 AZ
        gfx_Clipping(OFF);

        gfx_ClipWindow(0, 0, 239, 271);
        gfx_Clipping(ON);
        gfx_Circle(599, 271, 490, DARKOLIVEGREEN);      //-30 AZ
        gfx_Clipping(OFF);

        gfx_ClipWindow(0, 0, 239, 271);
        gfx_Clipping(ON);
        gfx_Circle(460, 271, 478, DARKOLIVEGREEN);      //-60 AZ
        gfx_Clipping(OFF);
        break;

    case 1:
        // Some screen layout testing

        gfx_ClipWindow(0, 0, 479, 271);
        gfx_Clipping(ON);

        gfx_Set(OBJECT_COLOUR, DARKOLIVEGREEN);

        // 1. Draw arc at +/-90 AZ from 0 - 90 ALT
        XYFromAzAlt(dtr(90.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(90.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        XYFromAzAlt(dtr(-90.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(-90.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        // 2. Draw arc at +/-60 AZ from 0 - 90 ALT
        XYFromAzAlt(dtr(60.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(60.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        XYFromAzAlt(dtr(-60.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(-60.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        // 3. Draw arc at +/-30 AZ from 0 - 90 ALT
        XYFromAzAlt(dtr(30.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(30.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        XYFromAzAlt(dtr(-30.0), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = 1.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(-30.0), dtr(m), &x, &y);
            gfx_LineTo(x, y);
        }

        // 4. Draw arc at 60 ALT from -100 to +100 AZ
        XYFromAzAlt(dtr(-100.0), dtr(60.0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = -99.0; m < 100.0; m += step)
        {
            XYFromAzAlt(dtr(m), dtr(60.0), &x, &y);
            gfx_LineTo(x, y);
        }

        // 5. Draw arc at 30 ALT from -90 to +90 AZ
        XYFromAzAlt(dtr(-90.0), dtr(30.0), &x, &y);
        gfx_MoveTo(x, y);
        for (m = -89.0; m < 90.0; m += step)
        {
            XYFromAzAlt(dtr(m), dtr(30.0), &x, &y);
            gfx_LineTo(x, y);
        }

        gfx_Clipping(OFF);
        break;
    }

    // 0 AZ reference
    gfx_Vline(239, 0, 271, DARKOLIVEGREEN);

    return;
}

//-------------------------------------------------------------------------------
// checkCuckoo -- make sounds if it is appropriate

void checkCuckoo(void)
{
    int nCuckoo = 0;

    // Check for cuckoo timer expired
    if (tmLocal.tm_min == 0)
    {
        // Chime the hour (12hr clock)
        if (tmLocal.tm_hour == 0)
            nCuckoo = 12;
        else if (tmLocal.tm_hour > 12)
            nCuckoo = tmLocal.tm_hour - 12;
        else
            nCuckoo = tmLocal.tm_hour;
    } else {
        // Chime once on the half-hour
        if (tmLocal.tm_min == 30)
            nCuckoo = 1;
    }

    // Play an annoying sound
    if (nCuckoo > 0)
    {
        file_Mount();
        snd_Volume(127);

        while (nCuckoo--)
        {
            file_PlayWAV("cuckoo.wav");
            while (snd_Playing() != 0)
                usleep(100 * 1000);
            usleep(600 * 1000);
        }
        file_Unmount();
    }

    return;
}

//-------------------------------------------------------------------------------

int main(int argc, char **argv)
{
	int comspeed;
	int idx, rc;
	char comport[20];
	int currentMin = 0;
	int bTouched;

	WORD sHdl;
	WORD LCDSave = 0;

	Callback4D = errCallback;  // NULL ;
	Error_Abort4D = TRUE ;  // abort on detected 4D Serial error

	if (argc == 1) {
		/* If no arguments we call the Usage routine and exit */
		Usage(argv[0]);
		return 1;
	}

    // Must have device name
    strcpy(comport, argv[1]);

	if (argc <= 2)
		comspeed = BAUD_9600;
	else
	{
        comspeed = atoi(argv[2]);
		for (idx = 0; idx < maxrates; idx++)
		{
			if (baudrates[idx] == comspeed)
				break;
		}
		if (idx == maxrates)
		{
			printf("Invalid baud rate %s", argv[2]);
			return 1;
		}
		// 4D speed index
		comspeed = idx;
	}

    // Want to see any FP errors
    feenableexcept(FE_INVALID   |
                   FE_DIVBYZERO |
                   FE_OVERFLOW  |
                   FE_UNDERFLOW);
    feclearexcept(FE_ALL_EXCEPT);

restart:
    TimeLimit4D = 2000;
    // Open display serial port
    rc = OpenComm(comport, comspeed);
    if (rc != 0)
    {
        printf("Error %d Opening %s", rc, comport);
        return 2;
    }

    // Screen on!
    gfx_Contrast(15);

    // Get/set system clock from RTC on uLCD

    // Run file / wait for ACK
    TimeLimit4D = 5000;
    rc = file_Run("rtcset.4xe", 0, NULL);
    if (rc != 0)
    {
        printf("Error returned from rtcset = %d", rc);
        return 2;
    }

    // Exit from RTC setup - read clock
    // Create global string value to be filled in
    sHdl = writeString(0, "yy-mm-dd hh:mm:ss");
    rc = file_Run("clockrd.4fn", 1, &sHdl);
    if (rc != 0)
    {
        printf("Error returned from clockrd = %d", rc);
        return 2;
    }

    // Fetch returned string
    readString(sHdl, rtcval);

    // Parse GMT date-time
    memset(&tmGMT, 0, sizeof(tmGMT));
    if (strptime(rtcval, "%y-%m-%d %H:%M:%S", &tmGMT) == NULL)
    {
        printf("Bad date/time value: '%s'", rtcval);
        return 2;
    }

    // Convert tm to t_time, UTC -> local
    ttime = timegm(&tmGMT);
    // Set system time (if root)
    rc = stime(&ttime);
    if (rc != 0)
    {
        printf("Time not set - not root?\n");
    }

    // Reset to normal 2sec timeout
    TimeLimit4D = 2000;

    gfx_ScreenMode(LANDSCAPE) ;
    touch_Set(TOUCH_DISABLE);
    sleep(1);   // wait for things to settle

    // Time (in minutes) before we snooze
    napCount = 0;
    sleeping = FALSE;

    // This is the main display loop
    while (TRUE)
    {
        // Only if display enabled
        if (LCDSave == 0)
        {
            // Start by clearing display
            gfx_Cls();

            txt_FontID(FONT1);
            txt_FGcolour(WHITE);
            txt_BGcolour(BLACK);

            // Screen grid
            drawAzAltGrid(1);

            // Get date/time, setup coords
            ttime = time(NULL);
            gmtime_r(&ttime, &tmGMT);

            // Save current display time
            currentMin = tmGMT.tm_min;

            // Get Julian date inf
            JD = jtime(&tmGMT);

            // Calculate Sun, Moon, etc. (QuickPlanetCalc := true)
            calcPlanets(JD, Latitude, Longitude, TRUE);

            // Plot the star database (no constellation lines)
            plotStarField(HYGFILE, FALSE);

            // Now plot the planets
            plotPlanets();
        }

        // Enable full-screen touch
        touch_Set(TOUCH_ENABLE);
        touch_Set(TOUCH_REGIONDEFAULT);

        // Make sure touch state is idle
        if (touch_Get(TOUCH_STATUS) == TOUCH_PRESSED)
        {
            touch_Get(TOUCH_GETX);
            touch_Get(TOUCH_GETY);
            while (touch_Get(TOUCH_STATUS) != TOUCH_RELEASED)
                usleep(50 * 1000);
        }

        // Wait for next minute
        bTouched = FALSE;
        do {
            ttime = time(NULL);
            localtime_r(&ttime, &tmLocal);
            // Exit and update display elasped minute
            if ((tmLocal.tm_min != currentMin) && (tmLocal.tm_sec == 0))
                break;

            // Service touch while waiting
            if (touch_Get(TOUCH_STATUS) == TOUCH_RELEASED)
            {
                // Reset touch
                touch_Set(TOUCH_REGIONDEFAULT);
                bTouched = TRUE;
                break;
            }
            // Sleep 100ms
            usleep(100 * 1000);
        } while (TRUE);

        touch_Set(TOUCH_DISABLE);

        // Service touch event
        if (bTouched)
        {
            // Exit minute loop and reset if display on
            if (LCDSave == 0)
                break;

            // Reset nap count and turn on display
            napCount = NAPMINS;
            gfx_Contrast(LCDSave);
            LCDSave = 0;
        } else {
            // No touch - check if display on
            if (LCDSave == 0)
            {
                // No already asleep?
                if (!sleeping)
                {
                    // Time to sleep?
                    if ((tmLocal.tm_hour == SLEEPHR) && (tmLocal.tm_min == SLEEPMIN))
                    {
                        // Time to turn-off display and wait for touch
                        LCDSave = gfx_Contrast(OFF);
                        sleeping = TRUE;
                    }
                } else {
                     // Go back to sleep if we woke up
                    if (--napCount <= 0)
                    {
                        LCDSave = gfx_Contrast(OFF);
                    }
                }
            } else {
                // Wakeup display time?
                if (sleeping && ((tmLocal.tm_hour == WAKEHR) && (tmLocal.tm_min == WAKEMIN)))
                {
                    gfx_Contrast(LCDSave);
                    LCDSave = 0;
                    sleeping = FALSE;
                }
            }
        }

        // Maybe time to announce hours
        checkCuckoo();

        // Loop back and re-draw current time
    }

    // Active touch - reset & go into clock set mode

    gfx_Cls();
    if (LCDSave > 0)
    {
        // Wake up display if necessary
        gfx_Contrast(LCDSave);
        LCDSave= 0;
    }

    // Reset LCD
    CloseComm();
    // Restart in 10...
    sleep(10);

    goto restart;

    return 0;
}
