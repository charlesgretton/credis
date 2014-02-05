/* credis-test.c -- a sample test application using credis (C client library
 * for Redis)
 *
 * Copyright (c) 2009-2010, Jonas Romfelt <jonas at romfelt dot se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Credis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "credis.h"


long timer(int reset)
{
  static long start=0;
  struct timeval tv;

  gettimeofday(&tv, NULL);

  /* return timediff */
  if (!reset) {
    long stop = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;
    return (stop - start);
  }

  /* reset timer */
  start = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;

  return 0;
}

unsigned long getrandom(unsigned long max)
{
  return (1 + (unsigned long) ( ((double)max) * (rand() / (RAND_MAX + 1.0))));
}

void randomize()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

#define DUMMY_DATA "some dummy data string"
#define LONG_DATA 50000

int main(int argc, char **argv) {
  REDIS redis;
  REDIS_INFO info;
  int rc;

  redis = credis_connect(NULL, 9999, 10000);
  if (redis == NULL) {
    printf("Error connecting to Redis server. Please start server to run tests.\n");
    exit(1);
  }

  printf("Testing a number of credis functions. To perform a simplistic set-command\n"\
         "benchmark, run: `%s <num>' where <num> is the number\n"\
         "of set-commands to send.\n\n", argv[0]);

  printf("\n\n************* misc info ************************************ \n");

  rc = credis_ping(redis);
  printf("ping returned: %d\n", rc);

  rc = credis_info(redis, &info);
  printf("info returned %d\n", rc);
  printf("> redis_version: %s\n", info.redis_version);
  printf("> arch_bits: %d\n", info.arch_bits);
  printf("> multiplexing_api: %s\n", info.multiplexing_api);
  printf("> process_id: %ld\n", info.process_id);
  printf("> uptime_in_seconds: %ld\n", info.uptime_in_seconds);
  printf("> uptime_in_days: %ld\n", info.uptime_in_days);
  printf("> connected_clients: %d\n", info.connected_clients);
  printf("> connected_slaves: %d\n", info.connected_slaves);
  printf("> blocked_clients: %d\n", info.blocked_clients);
  printf("> used_memory: %zu\n", info.used_memory);
  printf("> used_memory_human: %s\n", info.used_memory_human);
  printf("> changes_since_last_save: %lld\n", info.changes_since_last_save);
  printf("> bgsave_in_progress: %d\n", info.bgsave_in_progress);
  printf("> last_save_time: %ld\n", info.last_save_time);
  printf("> bgrewriteaof_in_progress: %d\n", info.bgrewriteaof_in_progress);
  printf("> total_connections_received: %lld\n", info.total_connections_received);
  printf("> total_commands_processed: %lld\n", info.total_commands_processed);
  printf("> expired_keys: %lld\n", info.expired_keys);
  printf("> hash_max_zipmap_entries: %zu\n", info.hash_max_zipmap_entries);
  printf("> hash_max_zipmap_value: %zu\n", info.hash_max_zipmap_value);
  printf("> pubsub_channels: %ld\n", info.pubsub_channels);
  printf("> pubsub_patterns: %u\n", info.pubsub_patterns);
  printf("> vm_enabled: %d\n", info.vm_enabled);
  printf("> role: %d\n", info.role);

  credis_close(redis);

  return 0;
}
