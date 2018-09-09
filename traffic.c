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

#ifdef __FreeBSD__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <string.h>
#include <errno.h>

/* FIXME: use libgawen error reporting */
#include <stdlib.h>
#include <err.h>

#include <gawen/common.h>

/*
 Most of the code below thanks to:
  - /usr/src/usr.bin/systat/ifstat.c
  - "Implementing System Control Nodes (sysctl)" by John Baldwin in FreeBSD Journal (Jan/Feb 2014)
*/

static int iface_num;   /* ID of the interface (0 for all) */
static int iface_count; /* number of ifaces */

/* Get the number of interfaces present on the system. */
static unsigned int getifcount(void)
{
  unsigned int count;
  size_t       count_len = sizeof(count);

  static int name[] = {
    CTL_NET,
    PF_LINK,
    NETLINK_GENERIC,
    IFMIB_SYSTEM,
    IFMIB_IFCOUNT
  };

  if(sysctl(name, sizeof_array(name), &count, &count_len, NULL, 0) < 0)
    err(EXIT_FAILURE, "sysctl");

  return count;
}

/* Fetch the MIB.
   Return 0 on success, -1 if the interface is not enabled. */
static int getifmibdata(int ifnum, struct ifmibdata *data)
{
  size_t data_len = sizeof(*data);

  static int name[] = {
    CTL_NET,
    PF_LINK,
    NETLINK_GENERIC,
    IFMIB_IFDATA,
    0,
    IFDATA_GENERAL
  };

  name[4] = ifnum;

  if(sysctl(name, sizeof_array(name), data, &data_len, NULL, 0) < 0) {
    if(errno == ENOENT)
      return -1;
    err(EXIT_FAILURE, "sysctl");
  }

  return 0;
}

/* Get the ID of an interface by its name or -1 if not found. */
static int getifnum(unsigned int count, const char *iface)
{
  unsigned int i = 0;

  while(i < count) {
    struct ifmibdata data;

    if(getifmibdata(++i, &data) == 0 && !strcmp(data.ifmd_name, iface))
      return i;
  }

  return -1;
}

/* Number of bytes transferred for a single interface (selected in iface_num). */
static uint64_t getiobytes_single(void)
{
  struct ifmibdata data;

  if(getifmibdata(iface_num, &data) == 0)
    return data.ifmd_data.ifi_ibytes + data.ifmd_data.ifi_obytes;

  return 0;
}

/* Number of bytes transferred over all active interfaces. */
static uint64_t getiobytes_all(void)
{
  int i = 0;
  uint64_t iobytes = 0;

  while(i < iface_count) {
    struct ifmibdata data;

    if(getifmibdata(++i, &data) == 0)
      iobytes += data.ifmd_data.ifi_ibytes + data.ifmd_data.ifi_obytes;
  }

  return iobytes;
}

uint64_t (*getiobytes)(void);

void init_ifstat(const char *iface)
{
  iface_count = getifcount();

  if(iface) {
    iface_num  = getifnum(iface_count, iface);
    getiobytes = getiobytes_single;

    if(iface_num < 0)
      errx(EXIT_FAILURE, "interface %s not available", iface);
  } else {
    iface_num  = 0;
    getiobytes = getiobytes_all;
  }
}

#endif
