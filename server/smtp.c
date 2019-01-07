#include "../common/header.h"
#include "smtp.h"
#include "process.h"

int count_list_elems(struct client_socket_list *root) {
	int result = 0;
	while (root != NULL) {
		result++;
		root = root->next;
	}
	return result;
}


struct client_socket_list* delete_elem(struct client_socket_list* curr, int state_value)
{
  	/* See if we are at end of list. */
  	if (curr == NULL)
    	return NULL;

  	/*
  	 * Check to see if current node is one
   	 * to be deleted.
   	*/
  	if (curr->c_sock.state == state_value) {
  		close(curr->c_sock.fd);
    	struct client_socket_list* temp;

    	/* Save the next pointer in the node. */
    	temp= curr->next;

    	/* Deallocate the node. */
    	free(curr);

    	/*
     	* Return the NEW pointer to where we
     	* were called from.  I.e., the pointer
     	* the previous call will use to "skip
     	* over" the removed node.
     	*/
    	return temp;
  	}

  	/*
   	* Check the rest of the list, fixing the next
   	* pointer in case the next node is the one
   	* removed.
   	*/
  	curr->next = delete_elem(curr->next, state_value);


  	/*
   	* Return the pointer to where we were called
   	* from.  Since we did not remove this node it
   	* will be the same.
   	*/
  	return curr;
}

// -1 - error
// 0 - timeout
// else - amount of clients
int check_clients_by_timeout(struct client_socket_list *clients, int max_fd, struct timeval timeout) {
	fd_set socket_set;
	struct client_socket_list *p;
	int rc;

	FD_ZERO(&socket_set);
	if (clients != NULL) {
		for (p = clients; p != NULL; p = p->next) {
    		FD_SET(p->c_sock.fd, &socket_set);
    		// printf("client_socket%d = %d SOCKET_STATE = %d\n", getpid(), p->c_sock.fd, p->c_sock.state);
    	}

    	return select(max_fd + 1, &socket_set, NULL, NULL, &timeout);
	}
	return -1;
}

int run_process(struct process *pr) {
	struct mesg_buffer message;

	printf("SMTP run process start");

	int received_bytes_count;				// число прочитанных байт
	char buffer_output[SERVER_BUFFER_SIZE];	// выходной буфер для записи ответа

	char smtp_stub[SERVER_BUFFER_SIZE] = "Hi, you've come to smtp server";

	int new_socket;								// файловый дескриптор сокета, соединяющегося с сервером

	while (1) {
		//printf("PROCESS_RUNS\n");
		//sleep(50);
		struct timeval tv; // timeval используется внутри select для выхода из ожидания по таймауту
		tv.tv_sec = 15;
		tv.tv_usec = 0;

		// delete all closed sockets
		if (pr->sock_list != NULL) {
			int before = 0;
			int after = 0;
			do {
				before = count_list_elems(pr->sock_list);
				pr->sock_list = delete_elem(pr->sock_list, SOCKET_STATE_CLOSED);
				after = count_list_elems(pr->sock_list);
			} while ((before - after) > 0);
		}

		// add all the sockets to socket list
		struct client_socket_list *p;
		int rc;
		printf("listeners_list = %p is_null = %d\n",pr->listeners_list, (pr->listeners_list == NULL));
		printf("sock_list = %p is_null = %d\n",pr->sock_list, (pr->sock_list == NULL));

		FD_ZERO(&(pr->socket_set));
		// first add listeners
		if (pr->listeners_list != NULL) {
			for (p = pr->listeners_list; p != NULL; p = p->next) {
    			FD_SET(p->c_sock.fd, &(pr->socket_set));
    			//printf("lisening_socket = %d\n", p->c_sock.fd);
    		}
		}
		// then add client sockets if exist
		if (pr->sock_list != NULL) {
			for (p = pr->sock_list; p != NULL; p = p->next) {
    			FD_SET(p->c_sock.fd, &(pr->socket_set));
    			printf("client_socket%d = %d SOCKET_STATE = %d\n", getpid(), p->c_sock.fd, p->c_sock.state);
    		}
		}

		// now we can use select with timeout
		rc = select(pr->max_fd + 1, &(pr->socket_set), NULL, NULL, &tv);
		if (rc == 0) {
			// no sockets are ready - timeout
			printf("no sockets are ready - timeout\n");
			for (p = pr->sock_list; p != NULL; p = p->next) {
				if (!FD_ISSET(p->c_sock.fd, &(pr->socket_set))) {
					p->c_sock.state = SOCKET_STATE_CLOSED;
				}
			}
		} else {
			// some sockets are ready
			// check listeners
			if (pr->listeners_list != NULL) {
				// проходим по списку сокетов в поисках установленного соединения
				for (p = pr->listeners_list; p != NULL; p = p->next) {
					if (FD_ISSET(p->c_sock.fd, &(pr->socket_set))) {

						// принимаем соединение
    					new_socket = accept(p->c_sock.fd, (struct sockaddr *) &(pr->serv_address),  (socklen_t*) &(pr->addrlen));
    					printf("new_socket = %d pid = %d\n", new_socket, getpid());
    					if (new_socket < 0) 
    					{ 
        					printf("accept() failed"); 
        					continue;
    					} 

    					//FD_CLR(p->c_sock.fd, &(pr->socket_set));
    					int file_flags = fcntl(new_socket, F_GETFL, 0);
    					if (file_flags == -1)
    					{
        					printf("Fail to receive socket flags");
        					continue;
    					}

    					if (fcntl(new_socket, F_SETFL, file_flags | O_NONBLOCK))
    					{	
        					printf("Fail to set flag 'O_NONBLOCK' for socket");
        					continue;
    					}

    					struct client_socket cl_sock;
        				cl_sock.fd = new_socket;
        				cl_sock.buffer = (char *) malloc(SERVER_BUFFER_SIZE);
        				cl_sock.state = SOCKET_STATE_INIT;
        				cl_sock.buffer_offset = 0;
        				cl_sock.message = (struct msg *) malloc(sizeof(struct msg));

        				// добавить новый сокет в список сокетов процесса
        				struct client_socket_list *new_scket = malloc(sizeof(struct client_socket_list));
        				new_scket->c_sock = cl_sock;
        				new_scket->next = pr->sock_list;
        				pr->sock_list = new_scket;

        				if (pr->max_fd < cl_sock.fd) {
            				pr->max_fd = cl_sock.fd;
        				}
					}
				}
			}
			// end check listeners

			// check clients
			if (pr->sock_list != NULL) {
				for (p = pr->sock_list; p != NULL; p = p->next) {
					if (FD_ISSET(p->c_sock.fd, &(pr->socket_set))) {

        				// call smtp_handler for socket
        				int new_sock = (p->c_sock.fd);
        				//new_smtp_handler(&new_sock, getpid());
        				new_smtp_handler_with_states(&(p->c_sock));
        				//FD_SET(p->c_sock.fd, &(pr->socket_set));

					}
				}
			}	
		}
	}


	// TODO: select for sets + smtp_handler (base_version)
	// TODO: states in smtp_handler (mid_version)

}

void new_smtp_handler(int *socket_fd, const int pid) {
	printf("SMTP handler start");

	int client_socket_fd = *socket_fd;		// клентский сокет, полученный после select()
	int received_bytes_count;				// число прочитанных байт
	// int i, j;
	char buffer[SERVER_BUFFER_SIZE];		// буфер, в который считываем
	char buffer_output[SERVER_BUFFER_SIZE];	// выходной буфер для записи ответа

	char smtp_stub[SERVER_BUFFER_SIZE] = "Hi, you've come to smtp server";

	sprintf(buffer_output, "%s\n", smtp_stub);
	printf("%s\n", smtp_stub);
	if (send(client_socket_fd, buffer_output, strlen(buffer_output), 0) < 0) {
		return;
	}
}

void new_smtp_handler_with_states(struct client_socket *c_sock) {
	printf("SMTP handler begin");
	int received_bytes_count;
	char buffer_output[SERVER_BUFFER_SIZE];
	char *eol;
	struct sockaddr_in address;
	address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( SERVER_PORT );

    /*fd_set sockset;
    
    FD_ZERO(&sockset);
    FD_SET(state->sockfd, &sockset);
	*/

	int buffer_left = SERVER_BUFFER_SIZE - c_sock->buffer_offset - 1;
	if (buffer_left == 0) {
		printf("500: Too long\n");
		// TODO: handle too long commands
	}

	received_bytes_count = recv(c_sock->fd, c_sock->buffer + c_sock->buffer_offset, buffer_left, 0);

	// считали 0 байт - клиент перестал отправлять данные
	if (received_bytes_count == 0) {
		printf("remote host closed socket %d\n", c_sock->fd);
		c_sock->state = SOCKET_STATE_CLOSED;
		return;
	}
	if (received_bytes_count == -1) {
		printf("problems with socket %d\n", c_sock->fd);
		c_sock->state = SOCKET_STATE_CLOSED;
		return;
	}

	printf("Server: %d - receive ok\n");

	while (strstr(c_sock->buffer, "\r\n")) {
		eol = strstr(c_sock->buffer, "\r\n");
		eol[0] = '\0'; 

		// обработка ключевых слов
		printf("Client: %d, message: %s\n", c_sock->fd, c_sock->buffer);

		char *message_buffer = (char *) malloc (SERVER_BUFFER_SIZE);
		strcpy(message_buffer, c_sock->buffer);
		// конец строки для сравнения (потом переделать под разделение на данные и команды)
		c_sock->buffer[4] = '\0';

		if (STR_EQUAL(c_sock->buffer, "HELO")) { 
			// начальное приветствие
			//sprintf(buffer_output, HEADER_250_OK);
			handle_HELO(c_sock,message_buffer,buffer_output,&address);
		} else if (STR_EQUAL(c_sock->buffer, "EHLO")) { 
			// улучшенное начальное приветствие
			//sprintf(buffer_output, HEADER_250_OK);
			handle_EHLO(c_sock,message_buffer,buffer_output,&address);
		} else if (STR_EQUAL(c_sock->buffer, "MAIL")) { 
			// получено новое письмо от
			sprintf(buffer_output, HEADER_250_OK);
		} else if (STR_EQUAL(c_sock->buffer, "RCPT")) { 
			// письмо направлено ... 
			sprintf(buffer_output, HEADER_250_OK_RECIPIENT);
		} else if (STR_EQUAL(c_sock->buffer, "DATA")) { 
			// содержимое письма
			sprintf(buffer_output, HEADER_354_CONTINUE);
		} else if (STR_EQUAL(c_sock->buffer, "RSET")) { 
			// сброс соединения
			sprintf(buffer_output, HEADER_250_OK_RESET);
		} else if (STR_EQUAL(c_sock->buffer, "NOOP")) { 
			// ничего не делать
			sprintf(buffer_output, HEADER_250_OK_NOOP);
		} else if (STR_EQUAL(c_sock->buffer, "QUIT")) { 
			// закрыть соединение
			sprintf(buffer_output, HEADER_221_OK);
			printf("Server: %d, message: %s", c_sock->fd, buffer_output);
			send(c_sock->fd, buffer_output, strlen(buffer_output), 0);
			c_sock->state = SOCKET_STATE_CLOSED;
			//close(client_socket_fd);
			//kill(pid, SIGTERM);
		} else { 
			// метод не был определен
			sprintf(buffer_output, HEADER_502_NOT_IMPLEMENTED);
		}
		printf("Server: %d, message: %s", c_sock->fd, buffer_output);
		send(c_sock->fd, buffer_output, strlen(buffer_output), 0);

		// смещаем буфер для обработки следующей команды
		memmove(c_sock->buffer, eol + 2, SERVER_BUFFER_SIZE - (eol + 2 - c_sock->buffer));

	}
}

int handle_HELO(struct client_socket *c_sock, char *msg_buffer, char buffer_output[], struct sockaddr_in *address) {
	char* host = get_domain(msg_buffer);
    int addrlen = sizeof(*address);
    getpeername(c_sock->fd, (struct sockaddr*)address, (socklen_t*)&addrlen);
    char* host_ip = ip_to_hostname(inet_ntoa(address->sin_addr));
    if (strcmp(host, host_ip) == 0) {
        sprintf(buffer_output, "%s %s\r\n", HEADER_250_OK, host);
        printf("Server: %d, HELO: %s", c_sock->fd, buffer_output);
    } else {
        sprintf(buffer_output, "%s %s\r\n", HEADER_252_OK, host);
        printf("Server: %d, HELO: %s", c_sock->fd, buffer_output);
    }
    free(host);
    send(c_sock->fd, buffer_output, strlen(buffer_output), 0);
    c_sock->state = SOCKET_STATE_WAIT;
    return 0;
}

int handle_EHLO(struct client_socket *c_sock, char *msg_buffer, char buffer_output[], struct sockaddr_in *address) {
	char* host = get_domain(msg_buffer);
    int addrlen = sizeof(*address);
    getpeername(c_sock->fd, (struct sockaddr*)address, (socklen_t*)&addrlen);
    
    free(host);
    sprintf(buffer_output, HEADER_250_OK);
    printf("Server: %d, EHLO: %s", c_sock->fd, buffer_output);
    send(c_sock->fd, buffer_output, strlen(buffer_output), 0);
    c_sock->state = SOCKET_STATE_WAIT;
    return 0;
}
/*
int handle_QUIT(struct client_socket *c_sock, char *msg_buffer, char buffer_output[], struct sockaddr_in *address) {

}*/

void smtp_handler(int *socket_fd, const int pid) {
	printf("SMTP handler begin");

	int client_socket_fd = *socket_fd;		// клентский сокет, полученный после select()
	int received_bytes_count;				// число прочитанных байт
	// int i, j;
	char buffer[SERVER_BUFFER_SIZE];		// буфер, в который считываем
	char buffer_output[SERVER_BUFFER_SIZE];	// выходной буфер для записи ответа

	char smtp_stub[SERVER_BUFFER_SIZE] = "Hi, you've come to smtp server";

	sprintf(buffer_output, "%s\n", smtp_stub);
	printf("%s\n", smtp_stub);
	send(client_socket_fd, buffer_output, strlen(buffer_output), 0);

	while (1) {
		fd_set socket_set; // fd_set используется для select - выбор свободного сокета для записи
		struct timeval tv; // timeval используется внутри select для выхода из ожидания по таймауту

		// начальная инициализация socket_set единственным сокетом socket_fd
		FD_ZERO(&socket_set);
		FD_SET(client_socket_fd, &socket_set);
		tv.tv_sec = 15;
		tv.tv_usec = 0;

		// ожидаем select-ом возможности писать в сокет сервером в течение 15с
		int sock_count = select(client_socket_fd + 1, &socket_set, NULL, NULL, &tv);

		printf("%d\n", sock_count);

		// если так и не дождались возможности писать - ошибка
		if (!FD_ISSET(client_socket_fd, &socket_set)) {
			sprintf(buffer_output, "socket is not available for 15s\n");
			printf("socket is not available for 15s\n");
			send(client_socket_fd, buffer_output, strlen(buffer_output), 0);
			break;
		}

		// читаем, что прислал клиент, во входной буфер
		received_bytes_count = recv(client_socket_fd, buffer, SERVER_BUFFER_SIZE, 0);
		// считали 0 байт - клиент перестал отправлять данные
		if (received_bytes_count == 0) {
			printf("remote host closed socket %d\n", client_socket_fd);
			break;
		}
		if (received_bytes_count == -1) {
			printf("problems with socket %d\n", client_socket_fd);
			break;
		}

		char *eol; // признак конца линии обрабатываемой команды
		while (strstr(buffer, "\r\n")) {
			eol = strstr(buffer, "\r\n");
			eol[0] = '\0'; 

			// обработка ключевых слов
			printf("Client: %d, message: %s\n", client_socket_fd, buffer);

			// конец строки для сравнения (потом переделать под разделение на данные и команды)
			buffer[4] = '\0';

			if (STR_EQUAL(buffer, "HELO")) { 
				// начальное приветствие
				sprintf(buffer_output, HEADER_250_OK);
			} else if (STR_EQUAL(buffer, "EHLO")) { 
				// улучшенное начальное приветствие
				sprintf(buffer_output, HEADER_250_OK);
			} else if (STR_EQUAL(buffer, "MAIL")) { 
				// получено новое письмо от
				sprintf(buffer_output, HEADER_250_OK);
			} else if (STR_EQUAL(buffer, "RCPT")) { 
				// письмо направлено ... 
				sprintf(buffer_output, HEADER_250_OK_RECIPIENT);
			} else if (STR_EQUAL(buffer, "DATA")) { 
				// содержимое письма
				sprintf(buffer_output, HEADER_354_CONTINUE);
			} else if (STR_EQUAL(buffer, "RSET")) { 
				// сброс соединения
				sprintf(buffer_output, HEADER_250_OK_RESET);
			} else if (STR_EQUAL(buffer, "NOOP")) { 
				// ничего не делать
				sprintf(buffer_output, HEADER_250_OK_NOOP);
			} else if (STR_EQUAL(buffer, "QUIT")) { 
				// закрыть соединение
				sprintf(buffer_output, HEADER_221_OK);
				printf("Server: %d, message: %s", client_socket_fd, buffer_output);
				send(client_socket_fd, buffer_output, strlen(buffer_output), 0);
				close(client_socket_fd);
				//kill(pid, SIGTERM);
			} else { 
				// метод не был определен
				sprintf(buffer_output, HEADER_502_NOT_IMPLEMENTED);
			}
			printf("Server: %d, message: %s", client_socket_fd, buffer_output);
			send(client_socket_fd, buffer_output, strlen(buffer_output), 0);


			// смещаем буфер для обработки следующей команды
			memmove(buffer, eol + 2, SERVER_BUFFER_SIZE - (eol + 2 - buffer));

		}
	}
	// обработка команды кончилась - закрываем сокет
	close(client_socket_fd);
	//kill(pid, SIGTERM);
}


