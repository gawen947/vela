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

#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <err.h>

#include <gawen/safe-call.h>
#include <gawen/daemon.h>
#include <gawen/common.h>
#include <gawen/xatoi.h>
#include <gawen/help.h>
#include <gawen/log.h>

#include "version.h"
#include "vela.h"

static void sig_quit(int signum)
{
  UNUSED(signum);

  syslog(LOG_DEBUG, "exiting...");
  exit(EXIT_SUCCESS);
}

static void setup_siglist(int signals[], struct sigaction *act, int size)
{
  int i;

  sigfillset(&act->sa_mask);
  for(i = 0 ; i < size ; i++)
    sigaction(signals[i], act, NULL);
}

static void setup_signals(void)
{
  struct sigaction act_quit = { .sa_handler = sig_quit, .sa_flags = 0 };
  struct sigaction act_ign  = { .sa_handler = SIG_IGN, .sa_flags = 0 };

  int signals_quit[] = {
    SIGINT,
    SIGTERM
  };

  int signals_ign[] = {
    SIGCHLD
  };

  setup_siglist(signals_quit, &act_quit, sizeof_array(signals_quit));
  setup_siglist(signals_ign, &act_ign, sizeof_array(signals_ign));
}

static void print_help(const char *name)
{
  struct opt_help messages[] = {
    { 'h', "help",         "Show this help message" },
    { 'V', "version",      "Show version information" },
#ifdef COMMIT
    { 0,   "commit",       "Display commit information" },
#endif /* COMMIT */
    { 'd', "daemon",       "Detach from controlling terminal" },
    { 'p', "pid",          "Write PID to file" },
    { 'l', "log-level",    "Syslog level from 1 to 8 (default: 7)" },
    { 'm', "mail",         "Alert mail destination (default: none)" },
    { 'P', "poll",         "Byte count poll period (default: 5000ms)" },
    { 's', "sample",       "Record a sample of the detected peak (default: 1MiB)" },
    { 'S', "sample-path",  "Path to the directory containing captured samples" },
    { 'g', "grace",        "Amount of time until another peak can be considered an alert (default: 5000ms)" },
    { 'T', "treshold",     "Byte-rate alert threshold (default: 1GB/s)" },
    { 'i', "interface",    "Inteface to watch (default: all)" },
    { 0, NULL, NULL }
  };

  help(name, "[OPTIONS]", messages);
}

int main(int argc, char *argv[])
{
  const char    *prog_name;
  const char    *mail        = NULL;
  const char    *pid_file    = NULL;
  const char    *iface       = NULL;
  unsigned long  flags       = 0;
  unsigned long  sample_size = 1048576; /* 1MiB */
  unsigned int   poll_ms     = 5000;
  unsigned int   loglevel    = LOG_NOTICE;
  uint64_t       tresh_bps   = 1000000000; /* 1GB/s */
  int            exit_status = EXIT_FAILURE;
  int            err;

  enum opt {
    OPT_COMMIT = 0x100
  };

  struct option opts[] = {
    { "help", no_argument, NULL, 'h' },
    { "version", no_argument, NULL, 'V' },
#ifdef COMMIT
    { "commit", no_argument, NULL, OPT_COMMIT },
#endif /* COMMIT */
    { "daemon", no_argument, NULL, 'd' },
    { "pid", required_argument, NULL, 'p' },
    { "log-level", required_argument, NULL, 'l' },
    { "mail", required_argument, NULL, 'm' },
    { "poll", required_argument, NULL, 'P' },
    { "sample", required_argument, NULL, 'S' },
    { "treshold", required_argument, NULL, 'T' },
    { "interface", required_argument, NULL, 'i' },
    { NULL, 0, NULL, 0 }
  };

#ifdef __Linux__
  setproctitle_init(argc, argv, environ); /* libbsd needs that */
#endif
  prog_name = basename(argv[0]);

  while(1) {
    int c = getopt_long(argc, argv, "hVdp:l:m:P:T:S:i:", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
    case 'd':
      flags |= F_DAEMON;
      break;
    case 'p':
      pid_file = optarg;
      break;
    case 'l':
      loglevel = xatou(optarg, &err);
      if(err)
        errx(EXIT_FAILURE, "invalid log level");
      switch(loglevel) {
      case 1:
        loglevel = LOG_EMERG;
        break;
      case 2:
        loglevel = LOG_ALERT;
        break;
      case 3:
        loglevel = LOG_CRIT;
        break;
      case 4:
        loglevel = LOG_ERR;
        break;
      case 5:
        loglevel = LOG_WARNING;
        break;
      case 6:
        loglevel = LOG_NOTICE;
        break;
      case 7:
        loglevel = LOG_INFO;
        break;
      case 8:
        loglevel = LOG_DEBUG;
        break;
      default:
        errx(EXIT_FAILURE, "invalid log level");
      }
      break;
    case 'm':
      mail = optarg;
      break;
    case 'P':
      /* FIXME: accept scaled input */
      poll_ms = xatou(optarg, &err);
      if(err || poll_ms == 0)
        errx(EXIT_FAILURE, "invalid poll period");
      break;
    case 'T':
      /* FIXME: accept scaled input */
      tresh_bps = xatou64(optarg, &err);
      if(err || tresh_bps == 0)
        errx(EXIT_FAILURE, "invalid treshold");
      break;
    case 'S':
      sample_size = xatou(optarg, &err);
      if(err)
        errx(EXIT_FAILURE, "invalid treshold");
      break;
    case 'i':
      /* FIXME: check that interface exists */
      iface = optarg;
      break;
    case 'V':
      version();
      exit_status = EXIT_SUCCESS;
      goto EXIT;
#ifdef COMMIT
    case OPT_COMMIT:
      commit();
      exit_status = EXIT_SUCCESS;
      goto EXIT;
#endif /* COMMIT */
    case 'h':
      exit_status = EXIT_SUCCESS;
    default:
      print_help(prog_name);
      goto EXIT;
    }
  }

  if(optind != argc) {
    print_help(prog_name);
    goto EXIT;
  }

  /* syslog and start notification */
  sysstd_openlog(prog_name, LOG_PID, LOG_DAEMON | LOG_LOCAL0, loglevel);
  sysstd_log(LOG_NOTICE, PACKAGE_VERSION " starting...");
  safecall_err_act = safecall_act_sysstd;

  /* daemon mode */
  if(flags & F_DAEMON) {
    if(daemon(0, 0) < 0)
      sysstd_abort("cannot switch to daemon mode");
    sysstd_log(LOG_INFO, "switched to daemon mode");
  }

  if(pid_file)
    write_pid(pid_file);

  setup_signals();

  /* start polling the interface */
  start_poll(flags, tresh_bps, sample_size, poll_ms, iface, mail);

  /* everything is handled with signals */
  while(1)
    pause();

EXIT:
  exit(exit_status);
}
