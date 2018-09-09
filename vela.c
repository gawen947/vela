/* Copyright (c) 2018, David Hauweele <david@hauweele.net>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
   ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>

#include <gawen/common.h>

#include "traffic.h"
#include "sample.h"

static unsigned long   flags;
static unsigned long   sample_size;
static uint64_t        treshold;
static uint64_t        last_poll_iobytes;
static struct timeval  last_poll_time;
static const char     *mail;
static const char     *iface;

/* Pretends that the counter start now.
   It avoids measuring speed when capturing traffic. */
static void reset_counters(void)
{
  gettimeofday(&last_poll_time, NULL);
  last_poll_iobytes = getiobytes();
}

static void alert(uint64_t old_iobytes, uint64_t new_iobytes, uint64_t elapsed_usec)
{
  struct sigaction old_act;
  struct sigaction act_ign  = { .sa_handler = SIG_IGN,  .sa_flags = 0 };

  sigaction(SIGALRM, &act_ign, &old_act);

  /* FIXME: disable (then reenable) the SIGVALRM signal */
  double speed = 1.0 * (new_iobytes - old_iobytes) / elapsed_usec;
  printf("ALERT! speed=%3.3f MBps\n", speed);

  printf("start capture\n");
  sample(iface, "test.pcap", sample_size);

  reset_counters();
  sigaction(SIGALRM, &old_act, NULL);
}

static void sig_poll(int signum)
{
  uint64_t       elapsed_usec;
  uint64_t       current_iobytes;
  struct timeval current_time, elapsed_time;

  UNUSED(signum);

  gettimeofday(&current_time, NULL);
  current_iobytes = getiobytes();

  timersub(&current_time, &last_poll_time, &elapsed_time);
  elapsed_usec = elapsed_time.tv_sec * 1000000 + elapsed_time.tv_usec;

  if((current_iobytes - last_poll_iobytes) * 1000000 > treshold * elapsed_usec)
    alert(last_poll_iobytes, current_iobytes, elapsed_usec);

  last_poll_time    = current_time;
  last_poll_iobytes = current_iobytes;
}

void start_poll(unsigned long _flags, uint64_t _treshold, unsigned long _sample_size, unsigned int poll_ms, const char *_iface, const char *_mail)
{
  struct sigaction act_poll = { .sa_handler = sig_poll, .sa_flags = 0 };
  struct itimerval itimer;

  flags       = _flags;
  sample_size = _sample_size;
  treshold    = _treshold;
  mail        = _mail;
  iface       = _iface;

  init_ifstat(iface);

  itimer.it_value.tv_sec  = itimer.it_interval.tv_sec  = poll_ms / 1000;
  itimer.it_value.tv_usec = itimer.it_interval.tv_usec = (poll_ms % 1000) * 1000;

  sigaction(SIGALRM, &act_poll, NULL);
  setitimer(ITIMER_REAL, &itimer, NULL);

  /* caller should wait */
}
