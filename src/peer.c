#define inline
#include <winsock2.h>
#include <ws2tcpip.h>
#undef inline

#include "core.h"
#include "message.h"

#define SERVER_ADDRESS "127.0.0.1"
#define SERVER_PORT "8080"
#define SERVER_STUN_PORT "8081"

int main(void) {
  WSADATA wsa_data;
  SOCKET peer_socket, stun_socket;
  struct addrinfo *ctrl_addr, *stun_addr, hints;

  static u8 tmp_arena_mem[1024];
  Arena arena;
  arena_init(&arena, tmp_arena_mem, sizeof(tmp_arena_mem));

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    printf("failed to startup windows wsa\n");
    return 1;
  }

  {
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(SERVER_ADDRESS, SERVER_PORT, &hints, &ctrl_addr) != 0) {
      printf("failed to get addess info for server: %s:%s\n", SERVER_ADDRESS,
             SERVER_PORT);
      return 1;
    }

    peer_socket = socket(ctrl_addr->ai_family, ctrl_addr->ai_socktype,
                         ctrl_addr->ai_protocol);

    if (connect(peer_socket, ctrl_addr->ai_addr, (int)ctrl_addr->ai_addrlen) ==
        SOCKET_ERROR) {
      printf("failed to fail to connect to server server: %s:%s\n",
             SERVER_ADDRESS, SERVER_PORT);
      return 1;
    }

    freeaddrinfo(ctrl_addr);
  }

  {
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(SERVER_ADDRESS, SERVER_STUN_PORT, &hints, &stun_addr) !=
        0) {
      printf("failed to get addess info for stun server: %s:%s\n",
             SERVER_ADDRESS, SERVER_STUN_PORT);
      return 1;
    }

    stun_socket = socket(stun_addr->ai_family, stun_addr->ai_socktype,
                         stun_addr->ai_protocol);
    unused(stun_socket);
  }

  for (;;) {
    u64 size;
    u8 buffer[1024];
    Message msg;
    u8 byte[1];

    msg.header.type = MessageType_PEER_CONNECTED;
    msg.peer_connected.address = 4321;
    msg.peer_connected.port = 6969;

    message_serialize(&msg, 0, &size);
    assert(sizeof(buffer) >= size);
    message_serialize(&msg, buffer, &size);

    if (send(peer_socket, (char *)buffer, (s32)size, 0) == SOCKET_ERROR) {
      printf("failed to send messange: %.*s\n", (s32)size, buffer);
      return 1;
    }

    *byte = 0;
    if (sendto(stun_socket, (char *)byte, sizeof(byte), 0, stun_addr->ai_addr,
               (int)stun_addr->ai_addrlen) == SOCKET_ERROR) {
      printf("failed to send stun request\n");
      return 1;
    }

    size = recvfrom(stun_socket, (char *)buffer, sizeof(buffer), 0, 0, 0);
    message_deserialize(&arena, buffer, size, &msg);
    message_dump(&msg);

    Sleep(1000 * 3);
  }

  return 0;
}
