/* credis.h -- a C client library for Redis, public API.
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

#ifndef __CREDIS_H
#define __CREDIS_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Functions list below is modelled after the Redis Command Reference (except
 * for the credis_connect() and credis_close() functions), use this reference
 * for further descriptions of each command:
 *
 *    http://code.google.com/p/redis/wiki/CommandReference
 *
 * Comments are only available when it is not obvious how Credis implements
 * the Redis command. In general, functions return 0 on success or a negative
 * value on error. Refer to CREDIS_ERR_* codes. The return code -1 is
 * typically used when for instance a key is not found.
 *
 * IMPORTANT! Memory buffers are allocated, used and managed by credis
 * internally. Subsequent calls to credis functions _will_ destroy the data
 * to which returned values reference to. If for instance the returned value
 * by a call to credis_get() is to be used later in the program, a strdup()
 * is highly recommended. However, each `REDIS' handle has its own state and
 * manages its own memory buffers independently. That means that one of two
 * handles can be destroyed while the other keeps its connection and data.
 *
 * EXAMPLE
 *
 * Connect to a Redis server and set value of key `fruit' to `banana':
 *
 *    REDIS rh = credis_connect("localhost", 6789, 2000);
 *    credis_set(rh, "fruit", "banana");
 *    credis_close(rh);
 *
 * TODO
 *
 *  - Add support for missing Redis commands marked as TODO below
 *  - Currently only support for zero-terminated strings, not for storing
 *    abritary binary data as bulk data. Basically an API issue since it is
 *    partially supported internally.
 *  - Test
 */

/* handle to a Redis server connection */
typedef struct _cr_redis* REDIS;

#define CREDIS_OK 0
#define CREDIS_ERR -90
#define CREDIS_ERR_NOMEM -91
#define CREDIS_ERR_RESOLVE -92
#define CREDIS_ERR_CONNECT -93
#define CREDIS_ERR_SEND -94
#define CREDIS_ERR_RECV -95
#define CREDIS_ERR_TIMEOUT -96
#define CREDIS_ERR_PROTOCOL -97

#define CREDIS_TYPE_NONE 1
#define CREDIS_TYPE_STRING 2
#define CREDIS_TYPE_LIST 3
#define CREDIS_TYPE_SET 4

#define CREDIS_SERVER_MASTER 1
#define CREDIS_SERVER_SLAVE 2

typedef enum _cr_aggregate {
  NONE,
  SUM,
  MIN,
  MAX
} REDIS_AGGREGATE;

#define CREDIS_VERSION_STRING_SIZE 32
#define CREDIS_MULTIPLEXING_API_SIZE 16
#define CREDIS_USED_MEMORY_HUMAN_SIZE 32

typedef struct _cr_info {
  char redis_version[CREDIS_VERSION_STRING_SIZE];
  int arch_bits;
  char multiplexing_api[CREDIS_MULTIPLEXING_API_SIZE];
  long process_id;
  long uptime_in_seconds;
  long uptime_in_days;
  int connected_clients;
  int connected_slaves;
  int blocked_clients;
  unsigned long used_memory;
  char used_memory_human[CREDIS_USED_MEMORY_HUMAN_SIZE];
  long long changes_since_last_save;
  int bgsave_in_progress;
  long last_save_time;
  int bgrewriteaof_in_progress;
  long long total_connections_received;
  long long total_commands_processed;
  long long expired_keys;
  unsigned long hash_max_zipmap_entries;
  unsigned long hash_max_zipmap_value;
  long pubsub_channels;
  unsigned int pubsub_patterns;
  int vm_enabled;
  int role;
} REDIS_INFO;


/*
 * Connection handling
 */

/* `host' is the host to connect to, either as an host name or a IP address,
 * if set to NULL connection is made to "localhost". `port' is the TCP port
 * that Redis is listening to, set to 0 will use default port (6379).
 * `timeout' is the time in milliseconds to use as timeout, when connecting
 * to a Redis server and waiting for reply, it can be changed after a
 * connection has been made using credis_settimeout() */
REDIS credis_connect(const char *host, int port, int timeout);

/* set Redis server reply `timeout' in millisecs */
void credis_settimeout(REDIS rhnd, int timeout);

void credis_close(REDIS rhnd);

void credis_quit(REDIS rhnd);

int credis_auth(REDIS rhnd, const char *password);

int credis_ping(REDIS rhnd);

/* if a function call returns error it is _possible_ that the Redis server
 * replied with an error message. It is returned by this function. */
char* credis_errorreply(REDIS rhnd);

/*
 * Commands used by collectd write_redis
 */

/* returns -1 if member was already a member of the sorted set and only score was updated,
 * 0 is returned if the new element was added */
int credis_zadd(REDIS rhnd, const char *key, double score, const char *member);

/* returns -1 if the given member was already a member of the set */
int credis_sadd(REDIS rhnd, const char *key, const char *member);

/*
 * Remote server control commands
 */

/* Because the information returned by the Redis changes with virtually every
 * major release, credis tries to parse for as many fields as it is aware of,
 * staying backwards (and forwards) compatible with older (and newer) versions
 * of Redis.
 * Information fields not supported by the Redis server connected to, are set
 * to zero. */
int credis_info(REDIS rhnd, REDIS_INFO *info);

int credis_monitor(REDIS rhnd);

/* setting host to NULL and/or port to 0 will turn off replication */
int credis_slaveof(REDIS rhnd, const char *host, int port);

/* TODO
 * CONFIG Configure a Redis server at runtime
 */


#ifdef __cplusplus
}
#endif

#endif /* __CREDIS_H */
