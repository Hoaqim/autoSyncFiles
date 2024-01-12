#include <string>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "reader.h"
#include "arg_parser.h"
#include "sys_utill.h"
#include <sys/inotify.h>
#include <poll.h>
#include "sys_utill.h"
#include <fcntl.h>
#include <filesystem>
#include <dirent.h>

namespace fs = std::filesystem;

std::string findFilePath(const std::string& fileName) {
    for (const auto& entry : fs::recursive_directory_iterator("./")) {
        if (entry.is_regular_file() && entry.path().filename() == fileName) {
            return entry.path().string();
        }
    }
    return ""; // File not found
}

void copy_directory(Reader& reader, std::string& target_directory);
void send_files_to_server(int server_socket, std::string filename, bool to_delete = false, bool to_create=false){
		
		int fd =open(findFilePath(filename).c_str(), O_RDONLY);

		Reader reader(fd);
		std::string msg_size = "";
		std::string msg = "";
		if(to_delete) {
			msg = filename + "deleted";
			msg_size = sizeof(filename) + sizeof("deleted");
			write_(server_socket, msg.c_str(), msg.size());
			return;
		}

		if(to_create){
			msg = filename + "created";
			msg_size = sizeof(filename) + sizeof("created");
			write_(server_socket, msg.c_str(), msg.size());
			return;
		}

		for (int ch, nread; true; ) {
		
		nread = 0;

		while (nread < 128) {
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

		for (int i = 0; i < 4; i++) {
			msg_size += (char) (nread >> (i * 8)) & 0xFF;
		}
		
		msg = msg_size + msg;

		write_(server_socket, msg.c_str(), msg.size());
		}
}

void send_dir_to_server(int server_socket, std::string dir_name, bool to_delete=false){
	std::string msg = "", msg_size = "";
	msg_size = dir_name.size();
	
	if(to_delete){
		msg += "deleted";
		msg_size += sizeof("deleted");
	};
	
	msg = msg_size + dir_name;

	write_(server_socket, msg.c_str(), msg.size());
}

void handle_events(int fd, int server_socket){
		   char buf[4096]
			   __attribute__ ((aligned(__alignof__(struct inotify_event))));
		   const struct inotify_event *event;
		   ssize_t len;

		   /* Loop while events can be read from inotify file descriptor. */

		   for (;;) {

			   /* Read some events. */

			   len = read(fd, buf, sizeof(buf));
			   if (len == -1 && errno != EAGAIN) {
				   perror("read");
				   exit(EXIT_FAILURE);
			   }
 				/* If the nonblocking read() found no events to read, then
				  it returns -1 with errno set to EAGAIN. In that case,
				  we exit the loop. */

			   if (len <= 0) break;

			   /* Loop over all events in the buffer */
			   for (char *ptr = buf; ptr < buf + len;
					   ptr += sizeof(struct inotify_event) + event->len) {

				   event = (const struct inotify_event *) ptr;
				   
				   
				   if ( event->mask & IN_MODIFY) {
						printf("FILE MODIFIED: %s \n", event->name);
						send_files_to_server(server_socket, event->name);
				   }
			        
					if (event->mask & IN_CREATE){
						if(event->mask & IN_ISDIR){
							printf("DIR CREATED: %s\n", event->name);
							send_dir_to_server(server_socket, event->name);
						}else{
							printf("FILE CREATED: %s\n", event->name);
							send_files_to_server(server_socket, event->name, false, true);
						}
					}

					if (event->mask & IN_DELETE){
						if (event->mask & IN_ISDIR){
							printf("rm -rf dir: %s\n", event->name);
							send_dir_to_server(server_socket, event->name, true);
						} else {
							printf("rm file: %s", event->name);
							send_files_to_server(server_socket, event->name, true);
						}
					}

					if (event->mask & IN_MOVE){
						if (event->mask & IN_ISDIR){
							//TODO
							printf("MOVE DIR: %s \n", event->name);
						}
					}

			   }
		   }
	   }




static bool get_args(int argc, char *argv[], std::string* server_ip, int* port) {
	ArgParser arg_parser(argc, argv);

	if (!arg_parser.valid_args()) {
		return false;
	}

	*server_ip = arg_parser.get_argument(std::string("-i"));
	std::string port_ = arg_parser.get_argument(std::string("-p"));

	if (port_.empty() || server_ip->empty()) {
		return false;
	}

	*port = atoi(port_.c_str());

	return true;
}

void add_watch_recursive(int fd, const std::string& path) {
    int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE | IN_CLOSE_WRITE);
    if (wd == -1) {
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }

    // Recursively add watches for subdirectories
    DIR* dir = opendir(path.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                std::string subPath = path + "/" + entry->d_name;
                add_watch_recursive(fd, subPath);
            }
        }
        closedir(dir);
    }
}

int main(int argc, char* argv[]) {
	std::string server_ip;
	int port;
	int fd;

	std::string client_dir = "start_folder";

	fd = inotify_init1(0);

	if (fd == -1) {
			   perror("inotify_init1");
			   exit(EXIT_FAILURE);
		   }
	
	// Process command line arguments
	if (!get_args(argc, argv, &server_ip, &port)) {
		std::cerr << "Invalid program arguments\n";
		exit(EXIT_FAILURE);
	}

	add_watch_recursive(fd, client_dir.c_str());

	std::cerr << "\n"
			  << "Client's parameters are:\n\n"
			  << "serverIP: " << server_ip << "\n"
			  << "port: " << port << "\n"
			  << "directory: " << client_dir << "\n\n";

	// Configure sockets to request data from the server
	int sock;
	call_or_exit(sock = socket(AF_INET, SOCK_STREAM, 0), "socket (client)");

	struct hostent* hostent;
	struct sockaddr_in server;

	if ((hostent = gethostbyname(server_ip.c_str())) == nullptr) {
		herror("gethostbyname");
		exit(EXIT_FAILURE);
	}

	server.sin_family = AF_INET;
	memcpy(&server.sin_addr, hostent->h_addr, hostent->h_length);
	server.sin_port = htons(port);

	std::cerr << "Connecting to " << server_ip << " on port " << port << "...\n";

	call_or_exit(
		connect(sock, (struct sockaddr *) &server, sizeof(server)),
		"connect (client)"
	);

	std::cerr << "Connected succesfully\n\n";

	int dirname_size;
	std::string msg;

	while(1){
		handle_events(fd, sock);
	}

	printf("Listening for events stopped.\n");
    /* Close inotify file descriptor */
    close(fd);

	// Transmit request (size + payload) for 'directory' over to the server
	dirname_size = client_dir.size(); //size of files to send
	for (int i = 0; i < 4; i++) {
		msg += (char) (dirname_size >> (i * 8)) & 0xFF;
	}

	msg += client_dir;
	call_or_exit(write_(sock, msg.c_str(), msg.size()), "write_ (client)");

	// Read and replicate locally the requested directory from the server
	Reader reader(sock);
	copy_directory(reader, client_dir);

	// Let the server know that the transaction has been completed
	msg = " ";
	call_or_exit(write_(sock, msg.c_str(), msg.size()), "write_ (client)");

	std::cerr << "\n"
			  << "Catalogue has been synchronized\n\n";

	return 0;
}
