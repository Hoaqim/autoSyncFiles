#include <string>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "threads.h"
#include "arg_parser.h"
#include "sys_utill.h"
#include "reader.h"
#include "sys_utill.h"



// Global state used by the communication & worker threads
SharedData data;

static std::string read_dirname(Reader& reader) {
	// Read the directory that the client wants to copy. The first 4 bytes
	// are the payload's size in bytes (least significant byte comes first)

	int nbytes = 0;
	for (int byte, i = 0; i < 4; i++) {
		byte = reader.next();
		nbytes |= byte << (i * 8);
	}

	// Create the target directory path as per the client's request
	std::string dirname(STARTDIR);
	for (int i = 0; i < nbytes; i++) {
		dirname += (char) reader.next();
	}

	// If the client selected the default directory, omit the "." in the path
	if (dirname.back() == '.') {
		dirname = std::string(STARTDIR);
	}

	// Add a trailing slash if it's not there (needed for process_directory)
	if (dirname.back() != '/') {
		dirname += '/';
	}

	return dirname;
}

static void process_directory(std::string& dirname, std::vector<std::string>& filenames) {
	DIR* dp = opendir(dirname.c_str());

	if (dp == nullptr) {
		int status = pthread_mutex_lock(&data.log_mutex);
		pthread_call_or_exit(status, "pthread_mutex_lock (log_mutex)");

		std::cerr << "[Thread " << pthread_self()
		          << "]: Failed to open directory: " << dirname << "\n\n";

		status = pthread_mutex_unlock(&data.log_mutex);
		pthread_call_or_exit(status, "pthread_mutex_unlock (log_mutex)");

		return;
	}

	for (struct dirent* direntp; (direntp = readdir(dp)) != nullptr; ) {
		std::string entry_name = direntp->d_name;

		// Avoid current and parent directory entries so as to not create cycles
		if (entry_name != "." && entry_name != "..") {
			entry_name = dirname + entry_name;

			struct stat st_buf;
			call_or_exit(stat(entry_name.c_str(), &st_buf), "stat (communication thread)");

			if (S_ISDIR(st_buf.st_mode)) {
				entry_name += "/";
				process_directory(entry_name, filenames);
			} else {
				filenames.push_back(entry_name);
			}
		}
	}

	call_or_exit(closedir(dp), "closedir (communication thread)");
}

static bool get_args(int argc, char *argv[], int* port) {
	ArgParser arg_parser(argc, argv);

	if (!arg_parser.valid_args()) {
		return false;
	}

	std::string port_ = arg_parser.get_argument(std::string("-p"));

	if (port_.empty()) {
		return false;
	}

	*port = atoi(port_.c_str());

	return true;
}

int main(int argc, char* argv[]) {
	int port = 0;

	// Process command line arguments
	if (!get_args(argc, argv, &port)) {
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

	int new_sock;
	socklen_t client_size;
	struct sockaddr_in client;
	char client_ip[INET_ADDRSTRLEN];

	int epoll_fd = epoll_create1(0);
	epoll_event client_event;
	client_event.events = EPOLLIN;
	client_event.data.fd = sock;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &client_event);

	while (true) {
		int ew = epoll_wait(epoll_fd, &client_event, 1, -1);
		
		// Accept a new client
		if (client_event.events == EPOLLIN && client_event.data.fd == sock) {
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
			
			// Add client's fd to set of descriptors moniotored by epoll
			client_event.events = EPOLLIN;
			client_event.data.fd = *arg;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *(arg), &client_event);

			// Let a communication thread handle the client (pass the socket fd to it)
			//status = pthread_create(&thread_id, nullptr, communication_thread, arg);
			//pthread_call_or_exit(status, "pthread_create (communication thread)");

			//status = pthread_detach(thread_id);
			//pthread_call_or_exit(status, "pthread_detach (communication thread)");
		
		// Handle client's request
		} else {
			int fd = client_event.data.fd;
			Reader reader(fd);

			// read directory that client wants to acquire
			std::string dirname = read_dirname(reader);

			int status = pthread_mutex_lock(&data.log_mutex);
			pthread_call_or_exit(status, "pthread_mutex_lock (log_mutex)");

			std::cerr << "[Thread " << pthread_self()
					<< "]: About to scan directory " << dirname << "\n";

			status = pthread_mutex_unlock(&data.log_mutex);
			pthread_call_or_exit(status, "pthread_mutex_unlock (log_mutex)");

			// Scan the target directory and add all file names in 'filenames'
			std::vector<std::string> filenames; // All filenames under the directory
			process_directory(dirname, filenames);

			// Let the client know how many files he's about to receive
			std::string msg = "";
			for (int i = 0, n_files = filenames.size(); i < 4; i++) {
				msg += (char) (n_files >> (i * 8)) & 0xFF;
			}

			call_or_exit(write_(fd, msg.c_str(), msg.size()), "write_ (communication thread)");
			
			// Create a lock for the client's socket
			data.fd_to_mutex[fd] = new pthread_mutex_t();
			pthread_mutex_init(data.fd_to_mutex[fd], nullptr);

			// Send each file in directory
			for (std::string filename : filenames) {
				int file_fd;
				file_fd = open(filename.c_str(), O_RDONLY);
				Reader reader(file_fd);
				int status = pthread_mutex_lock(&data.log_mutex);
				pthread_call_or_exit(status, "pthread_mutex_lock (log_mutex");

				std::cerr << "[Thread " << pthread_self()
						<< "]: About to read file " << filename << "\n";

				status = pthread_mutex_unlock(&data.log_mutex);
				pthread_call_or_exit(status, "pthread_mutex_unlock (log_mutex");

				struct stat st_buf;
				call_or_exit(stat(filename.c_str(), &st_buf), "stat (worker thread)");

				std::string msg;
				int filename_size = filename.size();

				// Create message: <file name size> <file name> <file size> (4 + n bytes + 4 bytes)
				for (int i = 0; i < 4; i++) {
					msg += (char) (filename_size >> (i * 8)) & 0xFF;
				}

				msg += filename;

				for (int i = 0, size = st_buf.st_size; i < 4; i++) {
					msg += (char) (size >> (i * 8)) & 0xFF;
				}

				call_or_exit(write_(fd, msg.c_str(), msg.size()), "write_ (worker thread)");

				// Send file data as messages of the form: <payload size> <payload> (in blocks)
				for (int ch, nread; true; ) {
				msg = "";
				nread = 0;

				while (nread < data.block_size) {
					ch = reader.next();
					if (reader.eof()) {
						break;
					}

					nread++;
					msg += (char) ch;
				}

				if (nread == 0) {
					break;
				}

				std::string msg_size = "";
				for (int i = 0; i < 4; i++) {
					msg_size += (char) (nread >> (i * 8)) & 0xFF;
				}

				msg = msg_size + msg;
				call_or_exit(write_(fd, msg.c_str(), msg.size()), "write_ (worker thread)");
				}

				status = pthread_mutex_lock(&data.log_mutex);
				pthread_call_or_exit(status, "pthread_mutex_lock (log_mutex");

				std::cerr << "[Thread " << pthread_self()
						<< "]: Transferred file " << filename << " successfully\n";

				status = pthread_mutex_unlock(&data.log_mutex);
				pthread_call_or_exit(status, "pthread_mutex_unlock (log_mutex");

				call_or_exit(close(file_fd), "close file (worker)");
			}
		}
	}

	return 0;
}
