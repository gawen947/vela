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

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <fcntl.h>
#include <err.h>

#include <pcap.h>
#include <gawen/common.h>
#include <gawen/iobuf.h>

#define SNAPLEN 0xffff

static pcap_t    *pcap;
static iofile_t   out;
static long long  bytes_left;

/* Informations about the PCAP file format came from:
   http://wiki.wireshark.org/Development/LibpcapFileFormat */

#define PCAP_MAGIC 0xa1b2c3d4
#define PCAP_MAJOR 2
#define PCAP_MINOR 4

#define WRITE(size)                                               \
  static void write ## size (uint ## size ## _t value) {          \
    ssize_t n = iobuf_write(out, &value, sizeof(value));          \
    if(n != sizeof(value))                                        \
      err(EXIT_FAILURE, "cannot write to pcap file");             \
  }

WRITE(32)
WRITE(16)

static void register_packet(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
{
  ssize_t n;

  UNUSED(user);

  write32(h->ts.tv_sec);
  write32(h->ts.tv_usec);
  write32(h->len);
  write32(h->len);

  assert(h->len == h->caplen);

  n = iobuf_write(out, bytes, h->len);
  if(n != h->len)
    err(EXIT_FAILURE, "cannot write");

  bytes_left -= h->len;
  if(bytes_left < 0)
    pcap_breakloop(pcap);
}

static void sample_single(const char *iface, const char *file, unsigned long size)
{
  char errbuf[PCAP_ERRBUF_SIZE];
  int n;

  /* FIXME:
       - file->path should be a directory
       - have a sample duration
       - chose to capture in promiscuous or not

       - use libgawen error reporting
   */
  bytes_left = size;

  pcap = pcap_open_live(iface, SNAPLEN, 1, 0, errbuf);
  if(!pcap)
    errx(EXIT_FAILURE, "cannot open device %s: %s", iface, errbuf);

  out = iobuf_open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(!out)
    err(EXIT_FAILURE, "cannot open %s", file);

  write32(PCAP_MAGIC);            /* magic number */
  write16(PCAP_MAJOR);            /* PCAP version */
  write16(PCAP_MINOR);

  /* FIXME:
      - use correct timezone
      - check correct max packet length,
        even if 0xffff should be large enough.
  */
  write32(0);                     /* timezone in seconds (GMT) */
  write32(0);                     /* accuracy of timestamps */
  write32(0xffff);                /* max length of packets */
  write32(pcap_datalink(pcap));

  n = pcap_dispatch(pcap, 0, register_packet, NULL);
  switch(n) {
  case -1:
    err(EXIT_FAILURE, "capture error on device %s: %s", iface, errbuf);
  case -2:
    n = 0;
    break;
  }

  printf("captured %d packets, size %lu\n", n, (unsigned long)(size - bytes_left));

  iobuf_close(out);
  pcap_close(pcap);
}

void sample(const char *iface, const char *file, unsigned long size)
{
  if(!iface) {
    warnx("capture on multiple interfaces not implemented yet");
    return;
  }

  sample_single(iface, file, size);
}
