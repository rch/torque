/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef sun
#include <sys/stream.h>
#include <sys/stropts.h>
#endif /* sun */
#if defined(HAVE_SYS_IOCTL_H)
#include <sys/ioctl.h>
#endif
#if !defined(sgi) && !defined(_AIX) && !defined(linux) && !defined(__CYGWIN__)
#include <sys/tty.h>
#endif  /* ! sgi */

#include "portability.h"
#include "pbs_ifl.h"
#include "../lib/Libifl/lib_ifl.h"
#include "../lib/Libutils/lib_utils.h"
#include "server_limits.h"
#include "net_connect.h"
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_job.h"
#include "port_forwarding.h"
#include "mom_config.h"

static char cc_array[PBS_TERM_CCA];

static struct winsize wsz;

extern int mom_reader_go;

#ifdef HAVE_GETADDRINFO
static int IPv4or6 = AF_UNSPEC;
#endif

extern int conn_qsub(char *, long, char *);
extern int DEBUGMODE;

/*
 * read_net - read data from network till received amount expected
 *
 * returns >0 amount read
 *  -1 on error
 */

static int read_net(

  int   sock,
  char *buf,
  int   amt)

  {
  int got;
  int total = 0;

  while (amt > 0)
    {
    got = read_ac_socket(sock, buf, amt);

    if (got == 0)
      {
      /* end of file */

      break;
      }

    if (got < 0)
      {
      /* FAILURE */
      if (errno == EINTR)
        continue;

      return(-1);
      }

    /* read (some) data */

    amt   -= got;

    buf   += got;

    total += got;
    }  /* END while (amt > 0) */

  return(total);
  }  /* END read_net() */





/*
 * rcvttype - receive the terminal type of the real terminal
 *
 * Sent over network as "TERM=type_string"
 */

char *rcvttype(

  int sock)

  {
  static char buf[PBS_TERM_BUF_SZ];

  /* read terminal type as sent by qsub */

  if ((read_net(sock, buf, PBS_TERM_BUF_SZ) != PBS_TERM_BUF_SZ) ||
      (strncmp(buf, "TERM=", 5) != 0))
    {
    return(NULL);
    }

  /* get the basic control characters from qsub's termial */

  if (read_net(sock, cc_array, PBS_TERM_CCA) != PBS_TERM_CCA)
    {
    return(NULL);
    }

  return(buf);
  }




/*
 * set_termcc - set the basic modes for the slave terminal, and set the
 * control characters to those sent by qsub.
 */

void set_termcc(

  int fd)

  {

  struct termios slvtio;

#ifdef PUSH_STREAM
  ioctl(fd, I_PUSH, "ptem");
  ioctl(fd, I_PUSH, "ldterm");
#endif /* PUSH_STREAM */

  if (tcgetattr(fd, &slvtio) < 0)
    {
    return; /* cannot do it, leave as is */
    }

#ifdef IMAXBEL
  slvtio.c_iflag = (BRKINT | IGNPAR | ICRNL | IXON | IXOFF | IMAXBEL);

#else
  slvtio.c_iflag = (BRKINT | IGNPAR | ICRNL | IXON | IXOFF);

#endif
  slvtio.c_oflag = (OPOST | ONLCR);

#if defined(ECHOKE) && defined(ECHOCTL)
  slvtio.c_lflag = (ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL);

#else
  slvtio.c_lflag = (ISIG | ICANON | ECHO | ECHOE | ECHOK);

#endif
  slvtio.c_cc[VEOL]   = '\0';

  slvtio.c_cc[VEOL2]  = '\0';

  slvtio.c_cc[VSTART] = '\021';  /* ^Q */

  slvtio.c_cc[VSTOP]  = '\023';  /* ^S */

#if defined(VDSUSP)
  slvtio.c_cc[VDSUSP] = '\031';  /* ^Y */

#endif
#if defined(VREPRINT)
  slvtio.c_cc[VREPRINT] = '\022'; /* ^R */

#endif
  slvtio.c_cc[VLNEXT] = '\017';  /* ^V */

  slvtio.c_cc[VINTR]  = cc_array[0];

  slvtio.c_cc[VQUIT]  = cc_array[1];

  slvtio.c_cc[VERASE] = cc_array[2];

  slvtio.c_cc[VKILL]  = cc_array[3];

  slvtio.c_cc[VEOF]   = cc_array[4];

  slvtio.c_cc[VSUSP]  = cc_array[5];

  tcsetattr(fd, TCSANOW, &slvtio);

  return;
  }  /* END set_termcc() */






/*
 * rcvwinsize - receive the window size of the real terminal window
 *
 * Sent over network as "WINSIZE rn cn xn yn"  where .n is numeric string
 */

int rcvwinsize(

  int sock)

  {
  char buf[PBS_TERM_BUF_SZ];

  if (read_net(sock, buf, PBS_TERM_BUF_SZ) != PBS_TERM_BUF_SZ)
    {
    /* FAILURE */

    return(-1);
    }

  if (sscanf(buf, "WINSIZE %hu,%hu,%hu,%hu",
             &wsz.ws_row,
             &wsz.ws_col,
             &wsz.ws_xpixel,
             &wsz.ws_ypixel) != 4)
    {
    /* FAILURE */

    return(-1);
    }

  return(0);
  }  /* END rcvwinsize() */





int
setwinsize(int pty)
  {
  if (ioctl(pty, TIOCSWINSZ, &wsz) < 0)
    {
    perror("ioctl TIOCSWINSZ");
    return (-1);
    }

  return (0);
  }





/*
 * reader process - reads from the remote socket, and writes
 * to the master pty
 */

int mom_reader(

  int s,
  int ptc)

  {
  extern ssize_t read_blocking_socket(int fd, void *buf, ssize_t count);
  char buf[1024];
  int c;

  /* read from the socket, and write to ptc */

  while (mom_reader_go)
    {
    c = read_blocking_socket(s, buf, sizeof(buf));

    if (c > 0)
      {
      int   wc;
      char *p = buf;

      while (c > 0)
        {
        if ((wc = write_ac_socket(ptc, p, c)) < 0)
          {
          if (errno == EINTR)
            {
            /* write interrupted - retry */

            continue;
            }

          /* FAILURE - write failed */

          return(-1);
          }

        c -= wc;

        p += wc;
        }  /* END while (c > 0) */

      continue;
      }

    if (c == 0)
      {
      /* SUCCESS - all data written */

      return (0);
      }

    if (c < 0)
      {
      if (errno == EINTR)
        {
        /* read interrupted - retry */

        continue;
        }

      /* FAILURE - read failed */

      return(-1);
      }
    }    /* END while (1) */

  /* NOTREACHED*/

  return(0);
  }  /* END mom_reader() */






/*
 * Writer process: reads from master pty, and writes
 * data out to the rem socket
 */

int mom_writer(

  int s,
  int ptc)

  {
  char buf[1024];
  int c;

  /* read from ptc, and write to the socket */

  while (1)
    {
    c = read_ac_socket(ptc, buf, sizeof(buf));

    if (c > 0)
      {
      int wc;
      char *p = buf;

      while (c > 0)
        {
        if ((wc = write_ac_socket(s, p, c)) < 0)
          {
          if (errno == EINTR)
            {
            continue;
            }

          /* FAILURE - write failed */

          return(-1);
          }

        c -= wc;

        p += wc;
        }  /* END while (c > 0) */

      continue;
      }  /* END if (c > 0) */

    if (c == 0)
      {
      /* SUCCESS - all data read */

      return(0);
      }

    if (c < 0)
      {
      if (errno == EINTR)
        {
        /* read interupted, retry */

        continue;
        }

      /* FAILURE - read failed */

      return(-1);
      }
    }    /* END while(1) */

  /*NOTREACHED*/

  return(-1);
  }  /* END mom_writer */





/* adapted from openssh */
/*
 * Creates an internet domain socket for listening for X11 connections.
 * Returns a suitable display number (>0) for the DISPLAY variable
 * or -1 if an error occurs.
 */

int x11_create_display(

  int   x11_use_localhost, /* non-zero to use localhost only */
  char *display,           /* O */
  char *phost,             /* hostname where qsub is waiting */
  int   pport,             /* port where qsub is waiting */
  char *homedir,           /* need to set $HOME for xauth */
  char *x11authstr)        /* proto:data:screen */

  {
#ifdef HAVE_GETADDRINFO
  int              display_number;
  int              sock;
  u_short          port;

  struct addrinfo  hints;
  struct addrinfo *ai;
  struct addrinfo *aitop;
  char             strport[NI_MAXSERV];
  int              gaierr;
  int              n;
  int              num_socks = 0;
  unsigned int     x11screen;
  char             x11proto[512];
  char             x11data[512];
  char             cmd[512];
  char             auth_display[512];
  FILE            *f;
  pid_t            childpid;

  struct pfwdsock *socks;
  const char            *homeenv = "HOME";

  *display = '\0';

  if ((socks = (struct pfwdsock *)calloc(sizeof(struct pfwdsock), NUM_SOCKS)) == NULL)
    {
    /* FAILURE - cannot alloc memory */

    fprintf(stderr,"ERROR: could not calloc!\n");

    return(-1);
    }

  if (put_env_var(homeenv, homedir))
    {
    /* FAILURE */
    free(socks);
    fprintf(stderr, "ERROR: could not insert %s into environment\n", homeenv);

    return(-1);
    }

  for (n = 0;n < NUM_SOCKS;n++)
    socks[n].active = 0;

  x11proto[0] = x11data[0] = '\0';

  errno = 0;

  if ((n = sscanf(x11authstr, "%511[^:]:%511[^:]:%u",
                  x11proto,
                  x11data,
                  &x11screen)) != 3)
    {
    fprintf(stderr, "sscanf(%s)=%d failed: %s\n",
      x11authstr,
      n,
      strerror(errno));

    free(socks);

    return(-1);
    }

  for (display_number = X11OFFSET;display_number < MAX_DISPLAYS;display_number++)
    {
    port = 6000 + display_number;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = IPv4or6;
    hints.ai_flags = x11_use_localhost ? 0 : AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(strport, sizeof(strport), "%d",
      port);

    if ((gaierr = getaddrinfo(NULL, strport, &hints, &aitop)) != 0)
      {
      fprintf(stderr, "getaddrinfo: %.100s\n",
        gai_strerror(gaierr));

      free(socks);

      return(-1);
      }

    /* create a socket and bind it to display_number foreach address */

    for (ai = aitop;ai != NULL;ai = ai->ai_next)
      {
      if (ai->ai_family != AF_INET 
#ifdef IPV6_V6ONLY
            && ai->ai_family != AF_INET6
#endif
        )
        continue;

      sock = socket(ai->ai_family, SOCK_STREAM, 0);

      if (sock < 0)
        {
        if ((errno != EINVAL) && (errno != EAFNOSUPPORT))
          {
          fprintf(stderr, "socket: %.100s\n",
            strerror(errno));

          free(socks);

          return (-1);
          }
        else
          {
          DBPRT(("x11_create_display: Socket family %d *NOT* supported\n",
            ai->ai_family));

          continue;
          }
        }

      DBPRT(("x11_create_display: Socket family %d is supported\n",
        ai->ai_family));

#ifdef IPV6_V6ONLY

      if (ai->ai_family == AF_INET6)
        {
        int on = 1;

        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) < 0)
          {
          DBPRT(("setsockopt IPV6_V6ONLY: %.100s\n", strerror(errno)));
          }
        }

#endif
      if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0)
        {
        DBPRT(("bind port %d: %.100s\n", port, strerror(errno)));
        close(sock);

        if (ai->ai_next)
          continue;

        for (n = 0; n < num_socks; n++)
          {
          close(socks[n].sock);
          }

        num_socks = 0;

        break;
        }

      socks[num_socks].sock = sock;
      socks[num_socks].active = 1;
      num_socks++;
#ifndef DONT_TRY_OTHER_AF

      if (num_socks == NUM_SOCKS)
        break;

#else
      if (x11_use_localhost)
        {
        if (num_socks == NUM_SOCKS)
          break;
        }
      else
        {
        break;
        }

#endif
      }

    freeaddrinfo(aitop);

    if (num_socks > 0)
      break;
    }  /* END for (display) */

  if (display_number >= MAX_DISPLAYS)
    {
    fprintf(stderr, "Failed to allocate internet-domain X11 display socket.\n");

    free(socks);

    return(-1);
    }

  /* Start listening for connections on the socket. */

  for (n = 0;n < num_socks;n++)
    {
    DBPRT(("listening on fd %d\n",
      socks[n].sock));

    if (listen(socks[n].sock,TORQUE_LISTENQUEUE) < 0)
      {
      fprintf(stderr,"listen: %.100s\n",
        strerror(errno));

      close(socks[n].sock);

      free(socks);

      return(-1);
      }

    socks[n].listening = 1;
    }  /* END for (n) */

  /* setup local xauth */

  sprintf(display, "localhost:%u.%u",
    display_number,
    x11screen);

  snprintf(auth_display, sizeof(auth_display), "unix:%u.%u",
    display_number,
    x11screen);

  snprintf(cmd, sizeof(cmd), "%s %s -",
    xauth_path,
    DEBUGMODE ? "-v" : "-q");

  f = popen(cmd, "w");

  if (f != NULL)
    {
    fprintf(f, "remove %s\n",
      auth_display);

    fprintf(f, "add %s %s %s\n",
      auth_display,
      x11proto,
      x11data);

    pclose(f);
    }
  else
    {
    fprintf(stderr, "could not run %s\n",
      cmd);

    free(socks);

    return(-1);
    }

  if ((childpid = fork()) > 0)
    {
    free(socks);

    DBPRT(("successful x11 init, returning display %d\n",
      display_number));

    return(display_number);
    }

  if (childpid < 0)
    {
    fprintf(stderr, "failed x11 init fork\n");

    free(socks);

    return(-1);
    }

  DBPRT(("entering port_forwarder\n"));

  port_forwarder(socks, conn_qsub, phost, pport, NULL);
#endif        /* HAVE_GETADDRINFO */

  exit(EXIT_FAILURE);
  }  /* END x11_create_display() */



/* END mom_inter.c */
