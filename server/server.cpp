#include <string>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "threads.h"
#include "cla_parser.h"
#include "syscall_utils.h"

// Global state used by the communication & worker threads
SharedData data;

static bool get_args(int argc, char *argv[], int* port, int* pool_size) {
	ClaParser cla_parser(argc, argv);

	if (!cla_parser.valid_args()) {
		return false;
	}

	std::string port_ = cla_parser.get_argument(std::string("-p"));
	std::string pool_size_ = cla_parser.get_argument(std::string("-s"));
	std::string queue_size_ = cla_parser.get_argument(std::string("-q"));
	std::string block_size_ = cla_parser.get_argument(std::string("-b"));

	if (port_.empty() || pool_size_.empty() || queue_size_.empty() || block_size_.empty()) {
		return false;
	}

	*port = atoi(port_.c_str());
	*pool_size = atoi(pool_size_.c_str());
	data.task_capacity = atoi(queue_size_.c_str());
	data.block_size = atoi(block_size_.c_str());

	return true;
}

int main(int argc, char* argv[]) {
	int port = 0;
	int thread_pool_size = 0;

	// Process command line arguments
	if (!get_args(argc, argv, &port, &thread_pool_size)) {
		std::cerr << "Invalid program arguments\n";
		exit(EXIT_FAILURE);
	}

	pthread_mutex_init(&data.log_mutex, nullptr);
	pthread_mutex_init(&data.queue_mutex, nullptr);
	pthread_cond_init(&data.cond_nonfull, nullptr);
	pthread_cond_init(&data.cond_nonempty, nullptr);

	// Configure sockets to start serving clients
	int sock;
	call_or_exit(sock = socket(AF_INET, SOCK_STREAM, 0), "socket (server)");

	struct sockaddr_in server;

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(port);

	call_or_exit(bind(sock, (struct sockaddr *) &server, sizeof(server)), "bind (server)");
	call_or_exit(listen(sock, 10), "listen (server)"); 

	std::cerr << "Server was successfully initialized...\n"
	          << "Listening for connections to port " << port << "\n\n";

	int status;
	pthread_t thread_id;

	for (int i = 0; i < thread_pool_size; i++) {
		status = pthread_create(&thread_id, nullptr, worker_thread, nullptr);
		pthread_call_or_exit(status, "pthread_create (worker)");

		status = pthread_detach(thread_id);
		pthread_call_or_exit(status, "pthread_detach (worker)");
	}

	int new_sock;
	socklen_t client_size;
	struct sockaddr_in client;
	char client_ip[INET_ADDRSTRLEN];

	while (true) {
		client_size = sizeof(client);
		call_or_exit(
			new_sock = accept(sock, (struct sockaddr *) &client, &client_size),
			"accept (server)"
		);

		inet_ntop(AF_INET, &client.sin_addr, client_ip, INET_ADDRSTRLEN);

		status = pthread_mutex_lock(&data.log_mutex);
		pthread_call_or_exit(status, "pthread_mutex_lock (log_mutex)");

		std::cerr << "[Thread " << pthread_self()
		          << "]: Accepted connection from " << client_ip << "\n";

		status = pthread_mutex_unlock(&data.log_mutex);
		pthread_call_or_exit(status, "pthread_mutex_unlock (log_mutex)");

		int* arg = new int(new_sock);

		// Let a communication thread handle the client (pass the socket fd to it)
		status = pthread_create(&thread_id, nullptr, communication_thread, arg);
		pthread_call_or_exit(status, "pthread_create (communication thread)");

		status = pthread_detach(thread_id);
		pthread_call_or_exit(status, "pthread_detach (communication thread)");
	}

	return 0;
}
