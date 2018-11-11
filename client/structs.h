#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include "./../common/header.h"

//======================//
//      FD TYPES        //
//======================//
#define SOCKET_FD 0
#define FILE_FD 1

//======================//
//       CONTEXT        //
//======================//

#define START_WORK 0
//  HELO
#define HELO 000
#define RECEIVE_HELO_MESSAGE 001
#define SEND_HELO_MESSAGE 002
#define RECEIVE_HELO_CONNECT 003

struct FileDescSet
{
    fd_set set;
    struct FileDescList *list;
    int count;
};

struct FileDescList
{
    struct FileDesc fd;
    struct FileDescList *next;
};

struct FileDesc
{
    int id;
    int type;
    int context;
    int goal;
};

#endif //  STRUCTS_H