/* credis.c -- a C client library for Redis
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

#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "credis.hh"

#ifdef WIN32
void close(int fd) {
  closesocket(fd);
}
#endif

#define CR_ERROR '-'
#define CR_INLINE '+'
#define CR_BULK '$'
#define CR_MULTIBULK '*'
#define CR_INT ':'

#define CR_BUFFER_SIZE 4096
#define CR_BUFFER_WATERMARK ((CR_BUFFER_SIZE)/10+1)
#define CR_MULTIBULK_SIZE 256

#define _STRINGIF(arg) #arg
#define STRINGIFY(arg) _STRINGIF(arg)

#define CR_VERSION_STRING_SIZE_STR STRINGIFY(CREDIS_VERSION_STRING_SIZE)
#define CR_MULTIPLEXING_API_SIZE_STR STRINGIFY(CREDIS_MULTIPLEXING_API_SIZE)
#define CR_USED_MEMORY_HUMAN_SIZE_STR STRINGIFY(CREDIS_USED_MEMORY_HUMAN_SIZE)

#ifdef PRINTDEBUG
/* add -DPRINTDEBUG to CPPFLAGS in Makefile for debug outputs */
#define DEBUG(...)                                 \
  do {                                             \
    printf("%s() @ %d: ", __FUNCTION__, __LINE__); \
    printf(__VA_ARGS__);                           \
    printf("\n");                                  \
  } while (0)
#else
#define DEBUG(...)
#endif

/* format warnings are GNU C specific */
#if !__GNUC__
#define __attribute__(x)
#endif

typedef struct _cr_buffer {
  char *data;
  int idx;
  int len;
  int size;
} cr_buffer;

typedef struct _cr_multibulk {
  char **bulks;
  int *idxs;
  int size;
  int len;
} cr_multibulk;

typedef struct _cr_reply {
  int integer;
  char *line;
  char *bulk;
  cr_multibulk multibulk;
} cr_reply;

typedef struct _cr_redis {
  struct {
    int major;
    int minor;
    int patch;
  } version;
  int fd;
  char *ip;
  int port;
  int timeout;
  cr_buffer buf;
  cr_reply reply;
  int error;
} cr_redis;


/* Returns pointer to the '\r' of the first occurence of "\r\n", or NULL
 * if not found */
static char * cr_findnl(char *buf, int len) {
  while (--len >= 0) {
    if (*(buf++) == '\r')
      if (*buf == '\n')
        return --buf;
  }
  return NULL;
}

/* Allocate at least `size' bytes more buffer memory, keeping content of
 * previously allocated memory untouched.
 * Returns:
 *   0  on success
 *  -1  on error, i.e. more memory not available */
static int cr_moremem(cr_buffer *buf, int size)
{
  char *ptr;
  int total, n;

  n = size / CR_BUFFER_SIZE + 1;
  total = buf->size + n * CR_BUFFER_SIZE;

  DEBUG("allocate %d x CR_BUFFER_SIZE, total %d bytes", n, total);

  ptr = reinterpret_cast<char*>(realloc(buf->data, total));
  if (ptr == NULL)
    return -1;

  buf->data = ptr;
  buf->size = total;
  return 0;
}

/* Allocate at least `size' more multibulk storage, keeping content of
 * previously allocated memory untouched.
 * Returns:
 *   0  on success
 *  -1  on error, i.e. more memory not available */
static int cr_morebulk(cr_multibulk *mb, int size)
{
  char **cptr;
  int *iptr;
  int total, n;

  n = (size / CR_MULTIBULK_SIZE + 1) * CR_MULTIBULK_SIZE;
  total = mb->size + n;

  DEBUG("allocate %d x CR_MULTIBULK_SIZE, total %d (%lu bytes)",
        n, total, total * ((sizeof(char *)+sizeof(int))));
  cptr = reinterpret_cast<char**>(realloc(mb->bulks, total * sizeof(char *)));
  iptr = reinterpret_cast<int*>(realloc(mb->idxs, total * sizeof(int)));

  if (cptr == NULL || iptr == NULL)
    return CREDIS_ERR_NOMEM;

  mb->bulks = cptr;
  mb->idxs = iptr;
  mb->size = total;
  return 0;
}

/* Helper function for select that waits for `timeout' milliseconds
 * for `fd' to become readable (`readable' == 1) or writable.
 * Returns:
 *  >0  `fd' became readable or writable
 *   0  timeout
 *  -1  on error */
int cr_select(int fd, int timeout, int readable)
{
  struct timeval tv;
  fd_set fds;

  tv.tv_sec = timeout/1000;
  tv.tv_usec = (timeout%1000)*1000;

  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  if (readable == 1)
    return select(fd+1, &fds, NULL, NULL, &tv);

  return select(fd+1, NULL, &fds, NULL, &tv);
}
#define cr_selectreadable(fd, timeout) cr_select(fd, timeout, 1)
#define cr_selectwritable(fd, timeout) cr_select(fd, timeout, 0)

/* Receives at most `size' bytes from socket `fd' to `buf'. Times out after
 * `msecs' milliseconds if no data has yet arrived.
 * Returns:
 *  >0  number of read bytes on success
 *   0  server closed connection
 *  -1  on error
 *  -2  on timeout */
static int cr_receivedata(int fd, unsigned int msecs, char *buf, int size)
{
  int rc = cr_selectreadable(fd, msecs);

  if (rc > 0)
    return recv(fd, buf, size, 0);
  else if (rc == 0)
    return -2;
  else
    return -1;
}

/* Sends `size' bytes from `buf' to socket `fd' and times out after `msecs'
 * milliseconds if not all data has been sent.
 * Returns:
 *  >0  number of bytes sent; if less than `size' it means that timeout occurred
 *  -1  on error */
static int cr_senddata(int fd, unsigned int msecs, char *buf, int size)
{
  fd_set fds;
  struct timeval tv;
  int rc, sent=0;

  /* NOTE: On Linux, select() modifies timeout to reflect the amount
   * of time not slept, on other systems it is likely not the same */
  tv.tv_sec = msecs/1000;
  tv.tv_usec = (msecs%1000)*1000;

  while (sent < size) {
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    rc = select(fd+1, NULL, &fds, NULL, &tv);

    if (rc > 0) {
      rc = send(fd, buf+sent, size-sent, 0);
      if (rc < 0)
        return -1;
      sent += rc;
    }
    else if (rc == 0) /* timeout */
      break;
    else
      return -1;
  }

  return sent;
}

/* Buffered read line, returns pointer to zero-terminated string
 * and length of that string. `start' specifies from which byte
 * to start looking for "\r\n".
 * Returns:
 *  >0  length of string to which pointer `line' refers. `idx' is
 *      an optional pointer for returning start index of line with
 *      respect to buffer.
 *   0  connection to Redis server was closed
 *  -1  on error, i.e. a string is not available */
static int cr_readln(REDIS rhnd, int start, char **line, int *idx)
{
  cr_buffer *buf = &(rhnd->buf);
  char *nl;
  int rc, len, avail, more;

  /* do we need more data before we expect to find "\r\n"? */
  if ((more = buf->idx + start + 2 - buf->len) < 0)
    more = 0;

  while (more > 0 ||
         (nl = cr_findnl(buf->data + buf->idx + start, buf->len - (buf->idx + start))) == NULL) {
    avail = buf->size - buf->len;
    if (avail < CR_BUFFER_WATERMARK || avail < more) {
      DEBUG("available buffer memory is low, get more memory");
      if (cr_moremem(buf, more>0?more:1))
        return CREDIS_ERR_NOMEM;

      avail = buf->size - buf->len;
    }

    rc = cr_receivedata(rhnd->fd, rhnd->timeout, buf->data + buf->len, avail);
    if (rc > 0) {
      DEBUG("received %d bytes: %s", rc, buf->data + buf->len);
      buf->len += rc;
    }
    else if (rc == 0)
      return 0; /* EOF reached, connection terminated */
    else
      return -1; /* error */

    /* do we need more data before we expect to find "\r\n"? */
    if ((more = buf->idx + start + 2 - buf->len) < 0)
      more = 0;
  }

  *nl = '\0'; /* zero terminate */

  *line = buf->data + buf->idx;
  if (idx)
    *idx = buf->idx;
  len = nl - *line;
  buf->idx = (nl - buf->data) + 2; /* skip "\r\n" */

  DEBUG("size=%d, len=%d, idx=%d, start=%d, line=%s",
        buf->size, buf->len, buf->idx, start, *line);

  return len;
}

static int cr_receivemultibulk(REDIS rhnd, char *line)
{
  int bnum, blen, i, rc=0, idx;

  bnum = atoi(line);

  if (bnum == -1) {
    rhnd->reply.multibulk.len = 0; /* no data or key didn't exist */
    return 0;
  }
  else if (bnum > rhnd->reply.multibulk.size) {
    DEBUG("available multibulk storage is low, get more memory");
    if (cr_morebulk(&(rhnd->reply.multibulk), bnum - rhnd->reply.multibulk.size))
      return CREDIS_ERR_NOMEM;
  }

  for (i = 0; bnum > 0 && (rc = cr_readln(rhnd, 0, &line, NULL)) > 0; i++, bnum--) {
    if (*(line++) != CR_BULK)
      return CREDIS_ERR_PROTOCOL;

    blen = atoi(line);
    if (blen == -1)
      rhnd->reply.multibulk.idxs[i] = -1;
    else {
      if ((rc = cr_readln(rhnd, blen, &line, &idx)) != blen)
        return CREDIS_ERR_PROTOCOL;

      rhnd->reply.multibulk.idxs[i] = idx;
    }
  }

  if (bnum != 0) {
    DEBUG("bnum != 0, bnum=%d, rc=%d", bnum, rc);
    return CREDIS_ERR_PROTOCOL;
  }

  rhnd->reply.multibulk.len = i;
  for (i = 0; i < rhnd->reply.multibulk.len; i++) {
    if (rhnd->reply.multibulk.idxs[i] > 0)
      rhnd->reply.multibulk.bulks[i] = rhnd->buf.data + rhnd->reply.multibulk.idxs[i];
    else
      rhnd->reply.multibulk.bulks[i] = NULL;
  }

  return 0;
}

static int cr_receivebulk(REDIS rhnd, char *line)
{
  int blen;

  blen = atoi(line);
  if (blen == -1) {
    rhnd->reply.bulk = NULL; /* key didn't exist */
    return 0;
  }
  if (cr_readln(rhnd, blen, &line, NULL) >= 0) {
    rhnd->reply.bulk = line;
    return 0;
  }

  return CREDIS_ERR_PROTOCOL;
}

static int cr_receiveinline(REDIS rhnd, char *line)
{
  rhnd->reply.line = line;
  return 0;
}

static int cr_receiveint(REDIS rhnd, char *line)
{
  rhnd->reply.integer = atoi(line);
  return 0;
}

static int cr_receiveerror(REDIS rhnd, char *line)
{
  rhnd->reply.line = line;
  return CREDIS_ERR_PROTOCOL;
}

static int cr_receivereply(REDIS rhnd, char recvtype)
{
  char *line, prefix=0;

  /* reset common send/receive buffer */
  rhnd->buf.len = 0;
  rhnd->buf.idx = 0;

  if (cr_readln(rhnd, 0, &line, NULL) > 0) {
    prefix = *(line++);

    if (prefix != recvtype && prefix != CR_ERROR)
      return CREDIS_ERR_PROTOCOL;

    switch(prefix) {
    case CR_ERROR:
      return cr_receiveerror(rhnd, line);
    case CR_INLINE:
      return cr_receiveinline(rhnd, line);
    case CR_INT:
      return cr_receiveint(rhnd, line);
    case CR_BULK:
      return cr_receivebulk(rhnd, line);
    case CR_MULTIBULK:
      return cr_receivemultibulk(rhnd, line);
    }
  }

  return CREDIS_ERR_RECV;
}

static void cr_delete(REDIS rhnd)
{
  if (rhnd->reply.multibulk.bulks != NULL)
    free(rhnd->reply.multibulk.bulks);
  if (rhnd->reply.multibulk.idxs != NULL)
    free(rhnd->reply.multibulk.idxs);
  if (rhnd->buf.data != NULL)
    free(rhnd->buf.data);
  if (rhnd->ip != NULL)
    free(rhnd->ip);
  if (rhnd != NULL)
    free(rhnd);
}

REDIS cr_new(void)
{
  REDIS rhnd;

  if ((rhnd = reinterpret_cast<_cr_redis*>(calloc(sizeof(cr_redis), 1))) == NULL ||
      (rhnd->ip = reinterpret_cast<char*>(malloc(32))) == NULL ||
      (rhnd->buf.data = reinterpret_cast<char*>(malloc(CR_BUFFER_SIZE))) == NULL ||
      (rhnd->reply.multibulk.bulks = reinterpret_cast<char**>(malloc(sizeof(char *)*CR_MULTIBULK_SIZE))) == NULL ||
      (rhnd->reply.multibulk.idxs = reinterpret_cast<int*>(malloc(sizeof(int)*CR_MULTIBULK_SIZE))) == NULL) {
    cr_delete(rhnd);
    return NULL;
  }

  rhnd->buf.size = CR_BUFFER_SIZE;
  rhnd->reply.multibulk.size = CR_MULTIBULK_SIZE;

  return rhnd;
}

/* Send message that has been prepared in message buffer prior to the call
 * to this function. Wait and receive reply. */
static int cr_sendandreceive(REDIS rhnd, char recvtype)
{
  int rc;

  DEBUG("Sending message: len=%d, data=%s", rhnd->buf.len, rhnd->buf.data);

  rc = cr_senddata(rhnd->fd, rhnd->timeout, rhnd->buf.data, rhnd->buf.len);

  if (rc != rhnd->buf.len) {
    if (rc < 0)
      return CREDIS_ERR_SEND;
    return CREDIS_ERR_TIMEOUT;
  }

  return cr_receivereply(rhnd, recvtype);
}

/* Prepare message buffer for sending using a printf()-style formatting. */
__attribute__ ((format(printf,3,4)))
static int cr_sendfandreceive(REDIS rhnd, char recvtype, const char *format, ...)
{
  int rc;
  va_list ap;
  cr_buffer *buf = &(rhnd->buf);

  va_start(ap, format);
  rc = vsnprintf(buf->data, buf->size, format, ap);
  va_end(ap);

  if (rc < 0)
    return -1;

  if (rc >= buf->size) {
    DEBUG("truncated, get more memory and try again");
    if (cr_moremem(buf, rc - buf->size + 1))
      return CREDIS_ERR_NOMEM;

    va_start(ap, format);
    rc = vsnprintf(buf->data, buf->size, format, ap);
    va_end(ap);
  }

  buf->len = rc;

  return cr_sendandreceive(rhnd, recvtype);
}

char * credis_errorreply(REDIS rhnd)
{
  return rhnd->reply.line;
}

void credis_close(REDIS rhnd)
{
  if (rhnd) {
    if (rhnd->fd > 0)
      close(rhnd->fd);
#ifdef WIN32
    WSACleanup();
#endif
    cr_delete(rhnd);
  }
}

REDIS credis_connect(const char *host, int port, int timeout)
{
  int fd, rc, flags, yes = 1, use_he = 0;
  struct sockaddr_in sa;
  struct hostent *he;
  REDIS rhnd;

#ifdef WIN32
  unsigned long addr;
  WSADATA data;

  if (WSAStartup(MAKEWORD(2,2), &data) != 0) {
    DEBUG("Failed to init Windows Sockets DLL\n");
    return NULL;
  }
#endif

  if ((rhnd = cr_new()) == NULL)
    return NULL;

  if (host == NULL)
    host = "127.0.0.1";
  if (port == 0)
    port = 6379;

#ifdef WIN32
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&yes, sizeof(yes)) == -1 ||
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes)) == -1)
    goto error;
#else
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&yes, sizeof(yes)) == -1 ||
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&yes, sizeof(yes)) == -1)
    goto error;
#endif

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

#ifdef WIN32
  /* TODO use getaddrinfo() instead! */
  addr = inet_addr(host);
  if (addr == INADDR_NONE) {
    he = gethostbyname(host);
    use_he = 1;
  }
  else {
    he = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);
    use_he = 1;
  }
#else
  if (inet_aton(host, &sa.sin_addr) == 0) {
    he = gethostbyname(host);
    use_he = 1;
  }
#endif

  if (use_he) {
    if (he == NULL)
      goto error;
    memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
  }

  /* connect with user specified timeout */

  flags = fcntl(fd, F_GETFL);
  if ((rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) < 0) {
    DEBUG("Setting socket non-blocking failed with: %d\n", rc);
  }

  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    if (errno != EINPROGRESS)
      goto error;

    if (cr_selectwritable(fd, timeout) > 0) {
      int err;
      unsigned int len = sizeof(err);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1 || err)
        goto error;
    }
    else /* timeout or select error */
      goto error;
  }
  /* else connect completed immediately */

  strcpy(rhnd->ip, inet_ntoa(sa.sin_addr));
  rhnd->port = port;
  rhnd->fd = fd;
  rhnd->timeout = timeout;

  /* We can receive 2 version formats: x.yz and x.y.z, where x.yz was only used prior
   * first 1.1.0 release(?), e.g. stable releases 1.02 and 1.2.6 */
  if (cr_sendfandreceive(rhnd, CR_BULK, "INFO\r\n") == 0) {
    int items = 0;
    char* token;
    char* string;
    string = strdup(rhnd->reply.bulk);

    while ((token = strsep(&string, "\r\n")) != NULL && items < 2)
    {
      items = sscanf(token,
                       "redis_version:%d.%d.%d\r\n",
                       &(rhnd->version.major),
                       &(rhnd->version.minor),
                       &(rhnd->version.patch));
    }

    if (items < 2)
      goto error;
    if (items == 2) {
      rhnd->version.patch = rhnd->version.minor;
      rhnd->version.minor = 0;
    }
    DEBUG("Connected to Redis version: %d.%d.%d\n",
          rhnd->version.major, rhnd->version.minor, rhnd->version.patch);
  }

  return rhnd;

error:
  DEBUG("Reached error status....\n");
  if (fd > 0)
    close(fd);
  cr_delete(rhnd);

  return NULL;
}

void credis_settimeout(REDIS rhnd, int timeout)
{
  rhnd->timeout = timeout;
}

int credis_ping(REDIS rhnd)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "PING\r\n");
}

int credis_auth(REDIS rhnd, const char *password)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "AUTH %s\r\n", password);
}

/* Parse Redis `info' string for a particular `field', storing its value to
 * `storage' according to `format'.
 */
void cr_parseinfo(const char *info, const char *field, const char *format, void *storage)
{
  //char *str
  auto str = strstr(info, field);
  if (str) {
    str += strlen(field) + 1; /* also skip the ':' */
    sscanf(str, format, storage);
  }
}

int credis_info(REDIS rhnd, REDIS_INFO *info)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "INFO\r\n");

  if (rc == 0) {
    char role;
    memset(info, 0, sizeof(REDIS_INFO));
    cr_parseinfo(rhnd->reply.bulk, "redis_version", "%" CR_VERSION_STRING_SIZE_STR "s\r\n", &(info->redis_version));
    cr_parseinfo(rhnd->reply.bulk, "arch_bits", "%d", &(info->arch_bits));
    cr_parseinfo(rhnd->reply.bulk, "multiplexing_api", "%" CR_MULTIPLEXING_API_SIZE_STR "s\r\n", &(info->multiplexing_api));
    cr_parseinfo(rhnd->reply.bulk, "process_id", "%ld", &(info->process_id));
    cr_parseinfo(rhnd->reply.bulk, "uptime_in_seconds", "%ld", &(info->uptime_in_seconds));
    cr_parseinfo(rhnd->reply.bulk, "uptime_in_days", "%ld", &(info->uptime_in_days));
    cr_parseinfo(rhnd->reply.bulk, "connected_clients", "%d", &(info->connected_clients));
    cr_parseinfo(rhnd->reply.bulk, "connected_slaves", "%d", &(info->connected_slaves));
    cr_parseinfo(rhnd->reply.bulk, "blocked_clients", "%d", &(info->blocked_clients));
    cr_parseinfo(rhnd->reply.bulk, "used_memory", "%zu", &(info->used_memory));
    cr_parseinfo(rhnd->reply.bulk, "used_memory_human", "%" CR_USED_MEMORY_HUMAN_SIZE_STR "s", &(info->used_memory_human));
    cr_parseinfo(rhnd->reply.bulk, "changes_since_last_save", "%lld", &(info->changes_since_last_save));
    cr_parseinfo(rhnd->reply.bulk, "bgsave_in_progress", "%d", &(info->bgsave_in_progress));
    cr_parseinfo(rhnd->reply.bulk, "last_save_time", "%ld", &(info->last_save_time));
    cr_parseinfo(rhnd->reply.bulk, "bgrewriteaof_in_progress", "%d", &(info->bgrewriteaof_in_progress));
    cr_parseinfo(rhnd->reply.bulk, "total_connections_received", "%lld", &(info->total_connections_received));
    cr_parseinfo(rhnd->reply.bulk, "total_commands_processed", "%lld", &(info->total_commands_processed));
    cr_parseinfo(rhnd->reply.bulk, "expired_keys", "%lld", &(info->expired_keys));
    cr_parseinfo(rhnd->reply.bulk, "hash_max_zipmap_entries", "%zu", &(info->hash_max_zipmap_entries));
    cr_parseinfo(rhnd->reply.bulk, "hash_max_zipmap_value", "%zu", &(info->hash_max_zipmap_value));
    cr_parseinfo(rhnd->reply.bulk, "pubsub_channels", "%ld", &(info->pubsub_channels));
    cr_parseinfo(rhnd->reply.bulk, "pubsub_patterns", "%u", &(info->pubsub_patterns));
    cr_parseinfo(rhnd->reply.bulk, "vm_enabled", "%d", &(info->vm_enabled));
    cr_parseinfo(rhnd->reply.bulk, "role", "%c", &role);

    info->role = ((role=='m')?CREDIS_SERVER_MASTER:CREDIS_SERVER_SLAVE);
  }

  return rc;
}

int credis_monitor(REDIS rhnd)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "MONITOR\r\n");
}

int credis_slaveof(REDIS rhnd, const char *host, int port)
{
  if (host == NULL || port == 0)
    return cr_sendfandreceive(rhnd, CR_INLINE, "SLAVEOF no one\r\n");
  else
    return cr_sendfandreceive(rhnd, CR_INLINE, "SLAVEOF %s %d\r\n", host, port);
}

static int cr_setaddrem(REDIS rhnd, const char *cmd, const char *key, const char *member)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "%s %s %zu\r\n%s\r\n",
                              cmd, key, strlen(member), member);

  if (rc == 0 && rhnd->reply.integer == 0)
    rc = -1;

  return rc;
}

int credis_zadd(REDIS rhnd, const char *key, double score, const char *member)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "ZADD %s %f %zu\r\n%s\r\n",
                              key, score, strlen(member), member);

  if (rc == 0 && rhnd->reply.integer == 0)
    rc = -1;

  return rc;
}

int credis_sadd(REDIS rhnd, const char *key, const char *member)
{
  return cr_setaddrem(rhnd, "SADD", key, member);
}
