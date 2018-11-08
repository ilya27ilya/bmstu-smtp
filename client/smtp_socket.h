#ifndef SMTP_SOCKET_H
#define SMTP_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "structs.h"

struct smtpSocket
{
    int fd;
};

struct ListSocket 
{
    int count;
    struct smtpSocket * sockets;
};

void InitSmtpSocket(struct FileDescSet * fdSet);
int Send();
void Revoke();

#define BUFFER 4096
#define SOCK_COUNT 4

#endif //SMTP_SOCKET_H