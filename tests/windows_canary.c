#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_unsigned(const char *text, unsigned long minimum, unsigned long maximum,
                          unsigned long *value) {
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int open_address(const char *host, const char *port, int passive,
                        struct addrinfo **addresses) {
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = passive != 0 ? AI_PASSIVE : 0;
    return getaddrinfo(host, port, &hints, addresses);
}

static int run_client(const char *host, const char *port) {
    struct addrinfo *addresses = NULL;
    struct addrinfo *cursor;
    SOCKET connection = INVALID_SOCKET;
    int result = 1;
    const char marker = 'D';

    if (open_address(host, port, 0, &addresses) != 0) {
        return 1;
    }
    for (cursor = addresses; cursor != NULL; cursor = cursor->ai_next) {
        connection = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (connection == INVALID_SOCKET) {
            continue;
        }
        if (connect(connection, cursor->ai_addr, (int)cursor->ai_addrlen) == 0 &&
            send(connection, &marker, 1, 0) == 1) {
            result = 0;
            break;
        }
        (void)closesocket(connection);
        connection = INVALID_SOCKET;
    }
    if (connection != INVALID_SOCKET) {
        (void)closesocket(connection);
    }
    freeaddrinfo(addresses);
    return result;
}

static int run_server(const char *host, const char *port, DWORD duration_ms) {
    struct addrinfo *addresses = NULL;
    SOCKET listener = INVALID_SOCKET;
    ULONGLONG deadline;
    int enabled = 1;
    int result = 1;

    if (open_address(host, port, 1, &addresses) != 0) {
        return 1;
    }
    listener = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
    if (listener == INVALID_SOCKET) {
        goto cleanup;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&enabled,
                     (int)sizeof(enabled));
    if (bind(listener, addresses->ai_addr, (int)addresses->ai_addrlen) != 0 ||
        listen(listener, SOMAXCONN) != 0) {
        goto cleanup;
    }

    deadline = GetTickCount64() + (ULONGLONG)duration_ms;
    result = 0;
    while (GetTickCount64() < deadline) {
        fd_set readable;
        struct timeval timeout;
        int selected;

        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        selected = select(0, &readable, NULL, NULL, &timeout);
        if (selected == SOCKET_ERROR) {
            result = 1;
            break;
        }
        if (selected > 0 && FD_ISSET(listener, &readable)) {
            SOCKET connection = accept(listener, NULL, NULL);
            if (connection != INVALID_SOCKET) {
                char marker;
                (void)recv(connection, &marker, 1, 0);
                (void)closesocket(connection);
            }
        }
    }

cleanup:
    if (listener != INVALID_SOCKET) {
        (void)closesocket(listener);
    }
    if (addresses != NULL) {
        freeaddrinfo(addresses);
    }
    return result;
}

int main(int argc, char **argv) {
    WSADATA winsock;
    unsigned long port;
    unsigned long duration_ms;
    int result;

    if (argc < 4 || parse_unsigned(argv[3], 1UL, 65535UL, &port) != 0) {
        (void)fprintf(stderr,
                      "usage: windows_canary client <address> <port>\n"
                      "       windows_canary server <address> <port> <duration-ms>\n");
        return 2;
    }
    (void)port;
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return 1;
    }
    if (strcmp(argv[1], "client") == 0 && argc == 4) {
        result = run_client(argv[2], argv[3]);
    } else if (strcmp(argv[1], "server") == 0 && argc == 5 &&
               parse_unsigned(argv[4], 1000UL, 300000UL, &duration_ms) == 0) {
        result = run_server(argv[2], argv[3], (DWORD)duration_ms);
    } else {
        result = 2;
    }
    (void)WSACleanup();
    return result;
}
