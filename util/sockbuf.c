#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static unsigned
_get_bufsz(int socket, int bufOptName)
{
  unsigned  curSize;
  socklen_t sizeSize = sizeof(curSize);
  if (getsockopt(socket, SOL_SOCKET, bufOptName, (char*)&curSize, &sizeSize) < 0)
    return 0;
  return curSize;
}

unsigned
u_sock_sndbuf_sz(int socket)
{
  return _get_bufsz(socket, SO_SNDBUF);
}

unsigned
u_sock_rcvbuf_sz(int socket)
{
  return _get_bufsz(socket, SO_RCVBUF);
}

static unsigned
_set_bufsz(int socket, int bufOptName, unsigned requestedSize)
{
  socklen_t sizeSize = sizeof(requestedSize);
  setsockopt(socket, SOL_SOCKET, bufOptName, (char*)&requestedSize, sizeSize);
  return _get_bufsz(socket, bufOptName);
}
unsigned
u_sock_set_sndbuf_sz(int socket, unsigned requestedSize)
{
	return _set_bufsz(socket, SO_SNDBUF, requestedSize);
}

unsigned
u_sock_set_rcvbuf_sz(int socket, unsigned requestedSize)
{
	return _set_bufsz(socket, SO_RCVBUF, requestedSize);
}

static unsigned
_inc_bufsz(int socket, int bufOptName, unsigned requestedSize)
{
  unsigned curSize = _get_bufsz(socket, bufOptName);
  while (requestedSize > curSize)
  {
    socklen_t sizeSize = sizeof(requestedSize);
    if (setsockopt(socket, SOL_SOCKET, bufOptName, (char*)&requestedSize, sizeSize) >= 0)
    {
      return requestedSize;
    }
    requestedSize = (requestedSize+curSize)/2;
  }
  return _get_bufsz(socket, bufOptName);
}

unsigned
u_sock_inc_sndbuf_sz(int socket, unsigned requestedSize)
{
  return _inc_bufsz(socket, SO_SNDBUF, requestedSize);
}

unsigned
u_sock_inc_rcvbuf_sz(int socket, unsigned requestedSize)
{
  return _inc_bufsz(socket, SO_RCVBUF, requestedSize);
}
