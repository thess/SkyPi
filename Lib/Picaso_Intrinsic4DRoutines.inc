// Serial comms for Linux

void WriteBytes(unsigned char *psOutput, int nCount)
{
    int iOut;

    // Quick return if no device
    if (fdComm < 0)
    {
        Error4D = Err4D_OK;
        return;
    }

    iOut = write(fdComm, psOutput, nCount);
    if (iOut < 0)
    {
        printf("write error %d %s\n", errno, strerror(errno));
    }

    if (iOut != nCount)
        printf("Write incomplete!\n");

    return;
}

void WriteChars(unsigned char *psOutput)
{
    // Include NULL in output
    WriteBytes(psOutput, strlen((char *)psOutput) + 1);
    return;
}

void WriteWords(WORD * Source, int Size)
{
 	WORD wk ;
	int i ;
	for (i = 0; i < Size; i++)
	{
		wk = *Source++ ;
		wk = (wk >> 8) + (wk << 8) ;
		WriteBytes((unsigned char *)&wk, 2);
	}
}

// Return system time in ms
DWORD GetTickCount(void)
{
    struct timespec ttime;

    clock_gettime(CLOCK_MONOTONIC, &ttime);

    return (ttime.tv_sec * 1000) + (ttime.tv_nsec / 1000000);
}

// read string from the serial port
// return code:
//   >= 0 = number of characters read
//   -1 = read failed
int ReadSerPort(unsigned char *psData, int iMax)
{
    int iIn, iLeft, iIdx;
    DWORD sttime;

    // Quick return if no device
    if (fdComm < 0)
    {
        Error4D = Err4D_OK;
        *psData = '\0';
        return iMax;
    }

    iIdx = 0;
    iLeft = iMax;
    sttime = GetTickCount();
    while (iLeft > 0)
    {
        iIn = read(fdComm, &psData[iIdx], iLeft);
        if (iIn < 0)
        {
            if (errno == EAGAIN)
            {
                // Would block -- check timeout
                if ((GetTickCount() - sttime) >= TimeLimit4D)
                {
                    //printf("timeout - %d read\n", iIdx);
                    return -(iIdx + 10000);
                }
                usleep(100);  // Wait .1ms
                continue;
            }
            printf("Read error %d %s\n", errno, strerror(errno));
            return -1;
        }
        // Anything?
        if (iIn > 0)
        {
            // Calc remaining
            iLeft -= iIn;
            iIdx += iIn;
        }
        // Keep reading
    }

    return iMax;
}

void getbytes(unsigned char *data, int size)
{
 	int readc;

	readc = ReadSerPort(data, size) ;

	if ((readc != size)
	    && (Callback4D != NULL) )
	{
		Error4D = Err4D_Timeout ;
		Callback4D(Error4D, Error4D_Inv) ;
 	}
}

void GetAck(void)
{
	int readc;
	unsigned char readx ;
	Error4D = Err4D_OK ;

    // Quick return if no device
    if (fdComm < 0)
    {
        Error4D = Err4D_OK;
        return;
    }

   	readc = ReadSerPort(&readx, 1) ;

	if (readc != 1)
    {
		Error4D = Err4D_Timeout ;
        if (Callback4D != NULL)
            Callback4D(Error4D, Error4D_Inv) ;
    }
	else if (readx != 6)
	{
	   	Error4D     = Err4D_NAK ;
	   	Error4D_Inv = readx ;
		if (Callback4D != NULL)
	 		Callback4D(Error4D, Error4D_Inv) ;
	}

    return;
}


WORD GetWord(void)
{
 	int readc;
 	unsigned char readx[2] ;

 	if (Error4D != Err4D_OK)
 		return 0 ;

    readc = ReadSerPort(&readx[0], 2) ;

	if (readc != 2)
	{
		Error4D  = Err4D_Timeout ;
		if (Callback4D != NULL)
	 		return Callback4D(Error4D, Error4D_Inv) ;
		return -Error4D ;
	}
	else
		return readx[0] << 8 | readx[1] ;
}

void getString(unsigned char *outStr, int strLen)
{
 	int readc;

 	if (Error4D != Err4D_OK)
	{
		outStr[0] = '\0' ;
 		return ;
	}

	readc = ReadSerPort(outStr, strLen) ;

	if (readc != strLen)
	{
		Error4D  = Err4D_Timeout ;
		if (Callback4D != NULL)
	 		Callback4D(Error4D, Error4D_Inv) ;
	}

    // Append EOS
	outStr[readc] = '\0' ;

	return;
}

WORD GetAckResp(void)
{
	GetAck() ;
	return GetWord() ;
}

WORD WaitForAck(void)
{
    int saveTimeout = TimeLimit4D;
    void *saveCB = Callback4D;

    // check once per minute
    Callback4D = NULL;
    TimeLimit4D = 60 * 1000;
    do
    {
        GetAck();
    } while (Error4D != Err4D_OK);

    // Restore callback/timeout saves
    TimeLimit4D = saveTimeout;
    Callback4D = saveCB;

    return GetWord();
}
WORD GetAckRes2Words(WORD * word1, WORD * word2)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	*word1 = GetWord() ;
	*word2 = GetWord() ;
	return Result ;
}

void GetAck2Words(WORD * word1, WORD * word2)
{
	GetAck() ;
	*word1 = GetWord() ;
	*word2 = GetWord() ;
}

WORD GetAckResSector(t4DSector Sector)
{
	int Result;
	GetAck() ;
	Result = GetWord() ;
	getbytes(Sector, 512) ;
	return Result ;
}

WORD GetAckResStr(unsigned char * OutStr)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	getString(OutStr, Result) ;
	return Result ;
}

WORD GetAckResData(t4DByteArray OutData, WORD size)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	getbytes(OutData, size) ;
	return Result ;
}

void SetBaudrate(int Newrate)
{
    struct termios serial_opts;
    WORD    nBaud;

    if (fdComm < 0)
        return;

    // This is the only map from 4D Systems to Linux we support (for now)
    switch (Newrate)
    {
    case     BAUD_110:       nBaud =    B110 ; break ;
    case     BAUD_300:       nBaud =    B300 ; break ;
    case     BAUD_600:       nBaud =    B600 ; break ;
    case    BAUD_1200:       nBaud =   B1200 ; break ;
    case    BAUD_2400:       nBaud =   B2400 ; break ;
    case    BAUD_9600:       nBaud =   B9600 ; break ;
    case   BAUD_19200:       nBaud =  B19200 ; break ;
    case   BAUD_38400:       nBaud =  B38400 ; break ;
    case   BAUD_57600:       nBaud =  B57600 ; break ;
    case  BAUD_115200:       nBaud = B115200 ; break ;

    default:
      nBaud = B9600 ;
    }

    // Current config
    tcgetattr(fdComm, &serial_opts);

    cfmakeraw(&serial_opts);
    cfsetospeed(&serial_opts, nBaud);
    cfsetispeed(&serial_opts, nBaud);

    // set new config
    tcsetattr(fdComm, TCSANOW, &serial_opts);

    return;
}

void SetThisBaudrate(int Newrate)
{
    if (fdComm < 0)
        return;

    tcdrain(fdComm);

    SetBaudrate(Newrate) ;

    usleep(1000) ;          // Display sleeps for 100

    return;
}
