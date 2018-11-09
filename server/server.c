#include "../common/header.h"
#include "smtp.h"

int main(int argc, char **argv) {

	int server_fd;								// файловый дескриптор для серверного сокета
	int new_socket;								// файловый дескриптор сокета, соединяющегося с сервером
	// int read_bytes;								// число считанных байт

	struct sockaddr_in address;					// структура для обработки интернет-адресов
	int opt = 1;
	size_t addrlen = sizeof(address);
	//char buffer[SERVER_BUFFER_SIZE];			// принимающий буфер

	//char *stub_message = "Hello from server";   // серверная заглушка-ответ

	// создание файлового дескриптора серверного сокета
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == 0) {
		perror("socket() failed"); 
        exit(SERVER_EXIT_FAILURE);
	}

	// принудительное связывание сокета с портом (8080)
	// установка опций
	// SOL_SOCKET - 
	// SO_REUSEADDR - 
	// SO_REUSEPORT - 
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt() failed"); 
        exit(SERVER_EXIT_FAILURE);
	};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(SERVER_PORT);

	// непосредственное связывание сокета с портом
	if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) 
    { 
        perror("bind() failed"); 
        exit(SERVER_EXIT_FAILURE); 
    } 

    // говорим, что "слушаем" соединения на связанном порту 
    if (listen(server_fd, SERVER_BACKLOG_SIZE) < 0) 
    { 
        perror("listen() failed"); 
        exit(SERVER_EXIT_FAILURE); 
    } 

    // принимаем соединение
    new_socket = accept(server_fd, (struct sockaddr *) &address,  (socklen_t*) &addrlen);
    if (new_socket < 0) 
    { 
        perror("accept() failed"); 
        exit(SERVER_EXIT_FAILURE); 
    } 

    // соединение принято - можно делать обработку smtp
    int *sock_fd = (int *) malloc(sizeof(int));
    *sock_fd = new_socket;

    // создание дочернего процесса, где будет происходить обработка
    pid_t pid;
    switch (pid = fork()) {
    	case -1:
    		perror("accept() failed"); 
        	exit(SERVER_EXIT_FAILURE); 
        case 0:  // процесс - потомок
        	printf("child process forked with pid: %d\n", getpid());
        	printf("parent pid: %d\n", getppid());
        	smtp_handler(sock_fd);
        default: // процесс - родитель
        	continue;
    }
    

	return 0;
}