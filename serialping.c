/*
Copyright (c) 2008, Max Vilimpoc, http://vilimpoc.org/

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above
      copyright notice, this list of conditions and the following
      disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials
      provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <getopt.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <sys/time.h>
#include <limits.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* Our infinite loop controller. */
static bool keepRunning = true;

/* Signal handler for clean exits. */
void onSigint(int signal)
{
    /* printf("onSigint().\n"); */
    keepRunning = false;
}

void Usage(const char * const progName)
{
    printf("\n");
    printf("Usage: %s [-d /dev/ttySn] [-c 115200_8N1] [-i 0.1] [-p testpattern]\n", progName);
    printf("\n");
    printf("  [*]  --device   or  -d       specifies the serial device\n");
    printf("  [*]  --config   or  -c       specifies the device configuration\n");
    printf("       --interval or  -i       specifies the send interval for data\n");
    printf("       --pattern  or  -p       specifies the data pattern to send\n");
    printf("\n");
    printf("  [*]  indicates parameter is required\n");
    printf("\n");
}

typedef enum
{
    ARG_D               =   0x001,
    ARG_C               =   0x002,
    ARG_I               =   0x004,
    ARG_P               =   0x008,
    ARG_HELP            =   0x010,
    ARG_AUTO_PARAMTEST  =   0x100,
    ARG_AUTO_BAUDTEST   =   0x200,
} ARG_CONSTANTS;

typedef enum
{
    CONST_DEVNAMESTR    =   30,  /* i.e. /dev/ttyUSBnnnnnnnnnnnnnnnnn */
    CONST_CONFIGSTR     =   11,  /* i.e. 115200_8N1                   */
    CONST_PATTERNSTR    =   81,  /* A standard row of text.           */
} STRVAR_CONSTANTS;

/* If requiredArgs != 0 after parameter parsing, then we are missing  */
/* some absolutely necessary parameters.                              */
uint32_t requiredArgs = ARG_D | ARG_C;

int main(int argc, char **argv)
{
    int retVal = 0;

    const char *optionString = "hd:c:i:p:";
    struct option optionTable[] =
    {
        { "help",           no_argument,        NULL, ARG_HELP },
        { "device",         required_argument,  NULL, ARG_D },
        { "config",         required_argument,  NULL, ARG_C },
        { "interval",       optional_argument,  NULL, ARG_I },
        { "pattern",        optional_argument,  NULL, ARG_P },
        { "auto_paramtest", no_argument,        NULL, ARG_AUTO_PARAMTEST },
        { "auto_baudtest",  no_argument,        NULL, ARG_AUTO_BAUDTEST  },
    };

    int optionIndex = 0;
    int getoptRet   = 0;

    char devNameStr[CONST_DEVNAMESTR];
    memset(devNameStr, 0, CONST_DEVNAMESTR);

    char configStr[CONST_CONFIGSTR];
    memset(configStr, 0, CONST_CONFIGSTR);

    uint32_t pingInterval = 100000; /* in microseconds, default 0.1s */

    char patternStr[CONST_PATTERNSTR] = "The quick brown fox jumped over the lazy dog.";
    uint32_t patternLength = strlen(patternStr);

    /* Process incoming options. */
    while(-1 != (getoptRet = getopt_long(argc, argv, optionString, optionTable, &optionIndex)))
    {
        /* printf("getoptRet: %c, %x\n", getoptRet, getoptRet); */
        switch(getoptRet)
        {
            case 'h':
            case ARG_HELP:
                Usage(argv[0]);
                return 0;
                break;
            case 'd':
            case ARG_D:
                if (strlen(optarg) >= CONST_DEVNAMESTR)
                {
                    printf("ERROR: Device name is too long.\n");
                    return -1;
                }
                strncpy(devNameStr, optarg, (CONST_DEVNAMESTR - 1));
                requiredArgs &= ~(ARG_D);
                break;
            case 'c':
            case ARG_C:
                if (strlen(optarg) >= CONST_CONFIGSTR)
                {
                    printf("ERROR: Config string is too long.\n");
                    return -1;
                }
                strncpy(configStr, optarg, (CONST_CONFIGSTR - 1));
                requiredArgs &= ~(ARG_C);
                break;
            case 'i':
            case ARG_I:
                {
                double pingInterval_d64 = atof(optarg);
                double maxInterval = (double) UINT32_MAX / 1000000.0;
                if (pingInterval_d64 > maxInterval)
                {
                    printf("ERROR: Cannot have an interval that large. Why would you want to?\n");
                    return -1;
                }
                else
                {
                    pingInterval = (uint32_t) (pingInterval_d64 * 1000000.0);
                }
                }
                break;
            case 'p':
            case ARG_P:
                if (strlen(optarg) >= CONST_PATTERNSTR)
                {
                    printf("ERROR: Pattern string is too long.\n");
                    return -1;
                }
                strncpy(patternStr, optarg, (CONST_PATTERNSTR - 1));
                patternLength = strlen(patternStr);
                break;
            case ARG_AUTO_PARAMTEST:
                break;
            case ARG_AUTO_BAUDTEST:
                break;
            default:
                Usage(argv[0]);
                return -1;
                break;
        }
    }

    /* If -c or -d was not specified, then throw an error here. */
    if (requiredArgs)
    {
        Usage(argv[0]);
        return -1;
    }

    /* Settings for a raw terminal. */
    struct termios ts;
    memset(&ts, 0, sizeof(ts));

    ts.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    ts.c_oflag &= ~OPOST;
    ts.c_cflag &= ~(PARENB);
    ts.c_cflag |= CLOCAL;           /* Ignore modem control lines. */
    ts.c_cflag &= ~(CREAD);         /* No reading, only writing.   */
    ts.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* Parse config string. */
    char * underscorePtr = strchr(configStr, '_');

    if (NULL == underscorePtr)
    {
        printf("ERROR: Character Size, Parity, and Stop Bit length not specified.\n");
        return -1;
    }

    if (4 != strlen(underscorePtr))
    {
        printf("ERROR: Character Size, Parity, and Stop Bit length improperly specified.\n");
        return -1;
    }

    uint32_t baudRate = strtol(configStr, (char **) NULL, 10);
    switch(baudRate)
    {
        case 50:
            baudRate = B50;
            break;
        case 75:
            baudRate = B75;
            break;
        case 110:
            baudRate = B110;
            break;
        case 134:
            baudRate = B134;
            break;
        case 150:
            baudRate = B150;
            break;
        case 200:
            baudRate = B200;
            break;
        case 300:
            baudRate = B300;
            break;
        case 600:
            baudRate = B600;
            break;
        case 1200:
            baudRate = B1200;
            break;
        case 2400:
            baudRate = B2400;
            break;
        case 4800:
            baudRate = B4800;
            break;
        case 9600:
            baudRate = B9600;
            break;
        case 19200:
            baudRate = B19200;
            break;
        case 38400:
            baudRate = B38400;
            break;
        case 57600:
            baudRate = B57600;
            break;
        case 115200:
            baudRate = B115200;
            break;
/* These two odd ones exist on cygwin. */
#ifdef  B128000
        case 128000:
            baudRate = B128000;
            break;
#endif
        case 230400:
            baudRate = B230400;
            break;
#ifdef  B256000
        case 256000:
            baudRate = B256000;
            break;
#endif
        default:
            printf("ERROR: Invalid Baud Rate specified.\n");
            baudRate = B0;
            return -1;
            break;
    }
#ifdef CBAUD
    ts.c_cflag &= ~(CBAUD);
    ts.c_cflag |= baudRate;
#else
    cfsetispeed( &ts, baudRate );
    cfsetospeed( &ts, baudRate );
#endif
    ts.c_cflag &= ~CSIZE;
    ts.c_cflag |= CS8;
    ts.c_cflag &= ~CSTOPB;
    uint32_t charSize;
    ++underscorePtr;
    switch(*underscorePtr)
    {
        case '5':
            charSize = CS5;
            break;
        case '6':
            charSize = CS6;
            break;
        case '7':
            charSize = CS7;
            break;
        case '8':
            charSize = CS8;
            break;
        default:
            printf("ERROR: Invalid Character Size specified.\n");
            return -1;
            break;
    }
    ts.c_cflag &= ~(CSIZE);
    ts.c_cflag |= charSize;

    ++underscorePtr;
    switch(*underscorePtr)
    {
        case 'N':
            ts.c_cflag &= ~(PARENB);
            break;
        case 'E':
            ts.c_cflag |= PARENB;
            ts.c_cflag &= ~(PARODD);
            break;
        case 'O':
            ts.c_cflag |= PARENB;
            ts.c_cflag |= PARODD;
            break;
        default:
            printf("ERROR: Invalid Parity specified.\n");
            return -1;
            break;
    }

    ++underscorePtr;
    switch(*underscorePtr)
    {
        case '1':
            ts.c_cflag &= ~(CSTOPB);
            break;
        case '2':
            ts.c_cflag |= CSTOPB;
            break;
        default:
            printf("ERROR: Invalid number of Stop Bits specified.\n");
            return -1;
            break;
    }


    if (0 != access(devNameStr, W_OK))
    {
        printf("ERROR: User does not have permission to read, write, or execute commands on: %s\n", 
               devNameStr);
        return -1;
    }

    /* Open the serial port. */
    int spFd = open(devNameStr, O_WRONLY | O_NONBLOCK);
    if (-1 == spFd)
    {
        printf("ERROR: open() call failed.\n");
        goto cleanup_easy;
    }

    /* Set the device options */
    if (0 != tcsetattr(spFd, TCSANOW, &ts))
    {
        printf("ERROR: goto cleanup_open.\n");
        goto cleanup_open;
    }

    /* Register signal handler. */
    signal(SIGINT, onSigint);

    /* Keep track of wall time. */
    struct timeval now;

    /* Start regular pings.   */
    for (;true == keepRunning;)
    {
        gettimeofday(&now, NULL);
        printf("Sent: %d.%06d\n", (int) now.tv_sec, (int) now.tv_usec);
        if (0 > write(spFd, patternStr, patternLength))
        {
            perror("write");
        }
        usleep(pingInterval);
    }

    close(spFd);
    return retVal;

/* C "exception" unwinders. */
cleanup_open:
    close(spFd);
cleanup_easy:
    return -1;
}

