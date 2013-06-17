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
#include <sys/stat.h>

#include "SkyPi.h"

// defines for 4dgl constants
#include "Include/Picaso_const4D.h"
#include "Include/Picaso_Serial_4DLibrary.h"

// Scale projection to display (480w x 272h)
#define YPixRad  (272 / dtr(90))
//#define XPixRad  (480 / dtr(120))
#define XPixRad YPixRad

#define SERIALDEFAULT   "/dev/ttyAMA0"
static int comspeed;
static char comport[20];

#define maxrates 20
static int  baudrates[maxrates] = {   110,    300,    600,   1200,   2400,   4800,   9600,
                                     14400,  19200,  31250,  38400,  56000,  57600, 115200,
                                     128000, 256000, 300000, 375000, 500000, 600000} ;

// Unix time structs
static struct tm tmGMT;
static struct tm tmLocal;
static time_t ttime;
static int useSystemTime;
static char rtcval[24];

// Location of starmap DB
#define HYGDEFAULT "/usr/local/lib/SkyPi/starmap.csv"
static char starMap[200];
static int bCLines;

// Julian date/time
static double  JD;

// LatLong of Hudson, MA (in radians)
static double Latitude = dtr(42.38050);
static double Longitude = dtr(-71.53285);
// LatLong of Majuro, MH
//static double Latitude = dtr(7.1429);
//static double Longitude = dtr(171.0396);

// Sleep and cuckoo control
static int napCount;
static int sleeping;
static int bChimes;

// Put display to sleep after napMins minutes if already sleeping
static int napMins  = 30;
// Time to sleep and wake
static int sleepHR  = 23;
static int sleepMin = 30;
static int wakeHR   = 06;
static int wakeMin  = 30;

static int bDaemonize;

//-------------------------------------------------------------------------------

void Usage(void)
{
    printf("RaspPi Realtime Sky Map V%d.%d\n\n", VERSION_MAJOR, VERSION_MINOR);
    printf("SkyPi [options] [device]\n\n");
    printf(" device    Comms port to which display is attached (default: %s)\n", SERIALDEFAULT);
    printf(" options:\n");
    printf("   -f file     Path name of starmap DB (default: %s)\n", HYGDEFAULT);
    printf("   -l lat,long Observer decimal latitude & logitude\n");
    printf("   -q          Disable cuckoo chimes\n");
    printf("   -s speed    Serial device baudrate (default: 9600)\n");
    printf("   -t          Use system time instead of LCD clock\n");
    printf("   -w hh:mm    Display wake time (default: 06:30)\n");
    printf("   -z hh:mm    Display sleep time (default: 23:30)\n");
    printf("   -B          Run in background (daemonize)\n");

    return;
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
    // get altitude and azimuth in pixels
    if (CentralAngle != 0)
    {
        Y = CentralAngle * YPixRad * sin(alt) / sin(CentralAngle);
        X = CentralAngle * XPixRad * cos(alt) * sin(az) / sin(CentralAngle);
    } else {
        X = Y = 0.0;
    }

    *iX = 240 + (int)floor(X);      // center on screen
    *iY = 271 - (int)floor(Y);      // inverty Y coordinate

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

    lha = fixangr(dtr(igmst * 15.0) + Longitude - ra);
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
// dpyTime    Display time at bottom of screen

void dpyTime(void)
{
    char tmpBuf[16];

    // Get current time
    ttime = time(NULL);
    localtime_r(&ttime, &tmLocal);

    // Format it
    sprintf(tmpBuf, "%02d:%02d %s", tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_zone);
    // Font attrs
    txt_FontID(FONT2);
    txt_FGcolour(LIGHTBLUE);
    txt_BGcolour(BLACK);
    txt_Opacity(OPAQUE);

    // Display text in lower left corner
    gfx_MoveTo(8, 272 - charheight('9') - 2);
    putStr(tmpBuf);

    return;
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

    // Open DB file
    fd = fopen(fname, "r");
    if (fd == NULL)
    {
        printf("Cannot open starmap DB file: %s\nError (%d) - %s\n", fname, errno, strerror(errno));
        exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
        }

        n = csv_parse(tmpBuf, &starInfo[0], 6);
        if (n != 5)
        {
            printf("Too many fields: %d, %s\n", n, starInfo[0]);
            exit(EXIT_FAILURE);
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
    {"moon", LIGHTGREY, 5},
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

    for (kPlanet = SATURN; kPlanet >= SUN; kPlanet--)
    {
#ifdef DEBUG_PRINT
        printf("%-10s: RA = %.02f, DEC = %.02f", pp_data[kPlanet].Name, rtd(planet_info[kPlanet].ra), rtd(planet_info[kPlanet].dec));
        printf(", Az = %.02f, Alt = %.02f\n", rtd(planet_info[kPlanet].az), rtd(planet_info[kPlanet].alt));
#endif
        XYFromAzAlt(planet_info[kPlanet].az, planet_info[kPlanet].alt, &iX, &iY);
        if ((iX >= 0 && iX <= 479) &&(iY >= 0 && iY <= 271))
        {
            nSize = pp_data[kPlanet].Size;
            if ((kPlanet == SUN) || (kPlanet == MOON))
            {
                // Just draw object without label
                gfx_CircleFilled(iX, iY, abs(nSize), pp_data[kPlanet].Color);
            } else {
                gfx_Circle(iX, iY, nSize, pp_data[kPlanet].Color);
                if (kPlanet == SATURN)
                    gfx_Ellipse(iX, iY, 6, 2, YELLOW);    //Saturn rings
                // Select font & style
                txt_FontID(FONT1);
                txt_BGcolour(BLACK);
                txt_FGcolour(WHITE);
		txt_Opacity(TRANSPARENT);
                // Add planet label
                gfx_MoveTo(iX + nSize + 1, iY + nSize + 1);
                putStr(pp_data[kPlanet].Name);
            }
        }
    }

    gfx_Clipping(OFF);

    return;
}

//-------------------------------------------------------------------------------

void drawAzAltGrid(void)
{
    int x, y;
    double step = 2.0;
    double az, alt;
    double eps, eqra, eqdec, eqlat;
    double esin, ecos;

    // Draw some Alt-Az lines
    gfx_ClipWindow(0, 0, 479, 271);
    gfx_Clipping(ON);

    gfx_Set(OBJECT_COLOUR, DARKOLIVEGREEN);

    // 0 AZ reference (Facing south)
    //gfx_Vline(239, 0, 271, DARKOLIVEGREEN);

    // 1. Draw arc at -120..120 AZ from 0 - 90 ALT
    for (az = -120; az <= 120; az += 30.0)
    {
        alt = 0;
        XYFromAzAlt(dtr(az), dtr(0), &x, &y);
        gfx_MoveTo(x, y);
        for (alt = alt + step; alt <= 90.0; alt += step)
        {
            XYFromAzAlt(dtr(az), dtr(alt), &x, &y);
            gfx_LineTo(x, y);
        }
    }

    // 2. Draw arc at 15..75 ALT from -150 to +150 AZ
    for (alt = 15.0; alt <= 90.0; alt += 15.0)
    {
        az = -150.0;
        XYFromAzAlt(dtr(az), dtr(alt), &x, &y);
        gfx_MoveTo(x, y);
        for (az = az + step; az <= 150.0; az += step)
        {
            XYFromAzAlt(dtr(az), dtr(alt), &x, &y);
            gfx_LineTo(x, y);
        }
    }

    // 3. Draw ecliptic
    gfx_Set(OBJECT_COLOUR, SALMON);

    // Get current obliquity of ecliptic
    eps = dtr(obliqeq(JD));
    esin = sin(eps);
    ecos = cos(eps);
    // ecliptic intersects equator at 0 longitude
    AzAlt(0.0, 0.0, &az, &alt);
    XYFromAzAlt(az, alt, &x, &y);
    gfx_MoveTo(x, y);

    for (eqlat = 0; eqlat <= dtr(360.0); eqlat += dtr(step))
    {
        eqra = fixangr(atan2(ecos * sin(eqlat), cos(eqlat)));
        eqdec = asin(esin * sin(eqlat));

        AzAlt(eqra, eqdec, &az, &alt);
        XYFromAzAlt(az, alt, &x, &y);
        gfx_LineTo(x, y);
    }

    gfx_Clipping(OFF);

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

void parse_options(int argc, char **argv)
{
    char *cptr;
    int opt, idx;

    optind = 0;
    while ((opt = getopt(argc, argv, "?Bcf:hl:qs:tw:z:")) != -1)
    {
        switch (opt) {
        // Silence the bird
        case 'q':
            bChimes = FALSE;
            break;

        // Draw constellation lines
        case 'c':
            bCLines = TRUE;
            break;

        // Location of starmap file
        case 'f':
            strcpy(starMap, optarg);
            break;

        // Don't get date/time from LCD clock
        case 't':
            useSystemTime = TRUE;
            break;

        // Serial port speed
        case 's':
            comspeed = atoi(optarg);
            for (idx = 0; idx < maxrates; idx++)
            {
                if (baudrates[idx] == comspeed)
                    break;
            }
            if (idx == maxrates)
            {
                printf("Invalid baud rate: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            // 4D speed index
            comspeed = idx;
            break;

        // Observer location
        case 'l':
            Latitude = dtr(strtod(optarg, &cptr));
            if ((cptr != optarg) && (*cptr == ','))
            {
                optarg = ++cptr;
                Longitude = dtr(strtod(optarg, &cptr));
                if (cptr != optarg)
                    break;
            }
            // Lat/Long error
            printf("Invalid latitude/longitude pair: %s\n", optarg);
            exit(EXIT_FAILURE);
            break;

        // Sleep / Wake times
        case 'w':
            if (strptime(optarg, "%H:%M", &tmLocal) == NULL)
            {
                printf("Bad wake time value: '%s\n", optarg);
                exit(EXIT_FAILURE);
            }
            wakeHR = tmLocal.tm_hour;
            wakeMin = tmLocal.tm_min;
            break;

        case 'z':
            if (strptime(optarg, "%H:%M", &tmLocal) == NULL)
            {
                printf("Bad sleep time value: '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            sleepHR = tmLocal.tm_hour;
            sleepMin = tmLocal.tm_min;
            break;

        case 'B':
            bDaemonize = TRUE;
            break;

        // Give help and quit
        case 'h':
        case '?':
            Usage();
            exit(EXIT_SUCCESS);

        // Unrecognized option - give help and fail
        default:
            Usage();
            exit(EXIT_FAILURE);
        }
    }

    return;
}

int main(int argc, char **argv)
{
	int rc;
	int currentMin = 0;
	int bTouched;
	WORD LCDSave = 0;
	WORD sHdl;
	struct stat mstat;

	TimeLimit4D = 2000;
	Callback4D = errCallback;
	// abort on detected 4D Serial error
	Error_Abort4D = TRUE ;

    // Default options
    bChimes = TRUE;
    bCLines = FALSE;
    bDaemonize = FALSE;
    useSystemTime = FALSE;
    strcpy(comport, SERIALDEFAULT);
    strcpy(starMap, HYGDEFAULT);
    comspeed = BAUD_9600;

    parse_options(argc, argv);

    // Check for too many args
    if (argc > (optind + 1))
    {
        printf("Too many args supplied\n");
        Usage();
        exit(EXIT_FAILURE);
    }

    // Check for optional device name
    if (argc > optind)
        strcpy(comport, argv[optind]);

    // Check if DB exists
    rc = stat(starMap, &mstat);
    if (rc < 0)
    {
        printf("Cannot locate starmap DB: %s\nError (%d) - %s\n", starMap, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Run in background?
    if (bDaemonize)
    {
        printf("Detaching...\n");
        rc = daemon(0, 0);
        // Continue even if detach error
        if (rc < 0)
            printf("Unable to run in background - %s\n", strerror(errno));
    }

    // Want to see any FP errors
    feenableexcept(FE_INVALID   |
                   FE_DIVBYZERO |
                   FE_OVERFLOW  |
                   FE_UNDERFLOW);
    feclearexcept(FE_ALL_EXCEPT);

restart:
    // Open display serial port
    rc = OpenComm(comport, comspeed);
    if (rc != 0)
    {
        printf("Error %d Opening: %s - %s\n", errno, comport, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Screen on!
    gfx_Contrast(15);

    // Default is use DS1307 on LCD
    if (!useSystemTime)
    {
        // Get/set system clock from RTC on uLCD

        // Run file / wait for ACK
        TimeLimit4D = 5000;
        rc = file_Run("rtcset.4xe", 0, NULL);
        if (rc != 0)
        {
            printf("Error returned from rtcset = %d\n", rc);
            exit(EXIT_FAILURE);
        }

        // Exit from RTC setup - read clock
        // Create global string value to be filled in
        sHdl = writeString(0, "yy-mm-dd hh:mm:ss");
        rc = file_Run("clockrd.4fn", 1, &sHdl);
        if (rc != 0)
        {
            printf("Error returned from clockrd = %d\n", rc);
            exit(EXIT_FAILURE);
        }

        // Fetch returned string
        readString(sHdl, rtcval);

        // Parse GMT date-time
        memset(&tmGMT, 0, sizeof(tmGMT));
        if (strptime(rtcval, "%y-%m-%d %H:%M:%S", &tmGMT) == NULL)
        {
            printf("LCD returned bad date/time value: '%s'", rtcval);
            exit(EXIT_FAILURE);
        }

        // Convert tm to t_time, UTC -> local
        ttime = timegm(&tmGMT);
        // Set system time (if root)
        rc = stime(&ttime);
        if (rc != 0)
        {
            printf("Time not set - not root?\n");
        }
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
        // Get date/time, setup coords
        ttime = time(NULL);
        gmtime_r(&ttime, &tmGMT);

        // Save current display time
        currentMin = tmGMT.tm_min;

        // Get Julian date inf
        JD = jtime(&tmGMT);

        // Only if display enabled
        if (LCDSave == 0)
        {
            // Start by clearing display
            gfx_Cls();

            // Screen grid
            drawAzAltGrid();

            // Calculate Sun, Moon, etc. (QuickPlanetCalc := true)
            calcPlanets(JD, Latitude, Longitude, TRUE);

            // Plot the star database (no constellation lines)
            plotStarField(starMap, bCLines);

            // Now plot the planets
            plotPlanets();

            // Show current time
            dpyTime();
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
            napCount = napMins;
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
                    if ((tmLocal.tm_hour == sleepHR) && (tmLocal.tm_min == sleepMin))
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
                if (sleeping && ((tmLocal.tm_hour == wakeHR) && (tmLocal.tm_min == wakeMin)))
                {
                    gfx_Contrast(LCDSave);
                    LCDSave = 0;
                    sleeping = FALSE;
                }
            }
        }

        // Maybe time to announce hours
        if (bChimes)
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
