#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <poll.h>
#include <ostream>
#include <string>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <limits.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>

namespace fs = std::filesystem;

char nullchar = '\0';

constexpr int BUFFER_SIZE = 1024 + PATH_MAX;
template<typename T> T try_or_exit(T result) {
	if (result == -1) {
		perror("Error");
		exit(1);
	}
	return result;
}

template<typename T> T try_or_exit(T result, std::string message) {
	if (result == -1) {
		perror(message.c_str());
		exit(1);
	}
	return result;
}

void sendModifyFileRequest(int fd, const fs::path& filePath);
void sendDeleteRequest(int fd, const fs::path& filePath);
void sendCreateRequest(int fd, const fs::path& filePath);
void sendMoveReqest(const fs::path& filePath, const fs::path& filePath2, int fd);

class FileWatcher {
private:
	std::vector<std::pair<int, fs::path>> watchlist;
	std::optional<fs::path> lastMove;

public:
	int fd;
	int server_socket;

	FileWatcher(std::string path, int server_sock) {
		this->fd = inotify_init1(IN_NONBLOCK);
		this->add(path);
		this->server_socket = server_sock;
	}

	~FileWatcher() {
		for (auto const &[wd, _] : this->watchlist) {
			inotify_rm_watch(this->fd, wd);
		}
		close(this->fd);
	}

	void add(fs::path path) {
		std::cout << "[FW] Watching: " << path;
		this->watchlist.push_back({
			try_or_exit(inotify_add_watch(this->fd, path.c_str(), IN_CREATE | IN_CLOSE_WRITE | IN_MOVE | IN_DELETE), "inotify_add_watch"),
			path
		});
		std::cout << " (" << this->watchlist.crbegin()->first << ")" << std::endl;

		for (auto const& entry : fs::directory_iterator{path}) {
			if (entry.is_directory()) {
				this->add(entry.path());
			}
		}
	}

	void remove(fs::path path, bool move) {
		for (auto it = this->watchlist.begin(); it != this->watchlist.end(); it++) {
			if (std::mismatch(it->second.begin(), it->second.end(), path.begin(), path.end()).second == path.end()) { 
				std::cout << "[FW] Removing: " << it->second << " (" << it->first << ")" << std::endl;
				if (move) {
					try_or_exit(inotify_rm_watch(this->fd, it->first), "inotify_rm_watch");
				}
				this->watchlist.erase(it);
				break;
			}
		}
	}

	void handle() {
		char buf[4096] __attribute__((aligned(alignof(inotify_event))));
		inotify_event* event;

		ssize_t len = read(this->fd, &buf, sizeof(buf));
		if (len == -1) {
			if (errno != EAGAIN) {
				try_or_exit(len, "read");
			}

			return;
		}

		for (char* ptr = buf; ptr < buf + len; ptr += sizeof(inotify_event) + event->len) {
			std::optional<fs::path> path;
			event = (inotify_event*)ptr;

			if (event->mask & IN_IGNORED) {
				continue;
			}

			if (event->mask & IN_OPEN)
				printf("[FW] IN_OPEN: ");
			else if (event->mask & IN_CLOSE_NOWRITE)
				printf("[FW] IN_CLOSE_NOWRITE: ");
			else if (event->mask & IN_CLOSE_WRITE)
				printf("[FW] IN_CLOSE_WRITE: ");
			else if (event->mask & IN_CREATE)
				printf("[FW] IN_CREATE: ");
			else if (event->mask & IN_DELETE)
				printf("[FW] IN_DELETE: ");
			else if (event->mask & IN_MOVED_FROM)
				printf("[FW] IN_MOVED_FROM: ");
			else if (event->mask & IN_MOVED_TO)
				printf("[FW] IN_MOVED_TO: ");
			else
				printf("[FW] UNKNOWN (%x): ", event->mask);

			printf( "%i: ", event->wd );

			for (auto const &[wd, ppath] : this->watchlist) {
				if (event->wd == wd) {
					path = ppath / event->name;
					std::cout << *path; //std::format("{}{}", path.string(), std::string(event->name));
					break;
				}
			}

			if (!path) {
				std::cout << std::endl;
				std::cerr << "!!! Unknown watch descriptor: " << event->wd << std::endl;
				continue;
			}

			if (event->mask & IN_ISDIR) {
				std::cout << " [directory]" << std::endl;
				if (event->mask & IN_CREATE) {
					this->add(*path);
					sendCreateRequest(this->server_socket, *path);
				} else if (event->mask & IN_DELETE) {
					this->remove(*path, false);
					std::cout << "Delete: " << *path << std::endl;
					sendDeleteRequest(this->server_socket, *path);
				}
			} else {
				std::cout << " [file]" << std::endl;
				if (event->mask & IN_CLOSE_WRITE) {
					std::cout << "Write: " << *path << std::endl;
					sendModifyFileRequest(this->server_socket, *path);
				} else if (event->mask & IN_DELETE) {
					std::cout << "Delete: " << *path << std::endl;
					sendDeleteRequest(this->server_socket, *path);
				}
			}

			if (event->mask & IN_MOVED_FROM) {
				this->lastMove = path;
			} else if (event->mask & IN_MOVED_TO) {
				if (this->lastMove) {
					std::cout << "Move: " << *this->lastMove << " -> " << *path << std::endl;
					if (event->mask & IN_ISDIR) {
						this->remove(*this->lastMove, true);
						this->add(*path);
					}
				} else {
					std::cerr << "!!! Unknown move: ??? -> " << *path << std::endl;
				}
			}
		}
	}
};

// Function to send a file to the server
void sendFileToServer(const std::string& filePath) {
    // Implement your logic to send the file to the server here
    // You can use networking libraries or APIs to send the file
    // to the server using a specific protocol (e.g., HTTP, FTP, etc.)
    std::cout << "Sending file to server: " << filePath << std::endl;
}

// Function to receive files from the server
void receiveFilesFromServer() {
    // Implement your logic to receive files from the server here
    // You can use networking libraries or APIs to receive files
    // from the server using a specific protocol (e.g., HTTP, FTP, etc.)
    std::cout << "Receiving files from server" << std::endl;
}

void sendModifyFileRequest(int fd, const fs::path& filePath) {
    write(fd, "u", 1);
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd, &nullchar, 1);
    
    // Send file modification time
    auto current_time = fs::last_write_time(filePath).time_since_epoch().count();
    write(fd, &current_time, sizeof(current_time));
    
    // Send file size
    auto file_size = (uint64_t)fs::file_size(filePath);
    write(fd, &file_size, sizeof(uint64_t));
    
    // Send file content
    int fileFd = open(filePath.c_str(), O_RDONLY);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    while ((bytesRead = read(fileFd, buffer, BUFFER_SIZE)) > 0) {
        write(fd, buffer, bytesRead);
    }

}

void sendDeleteRequest(int fd, const fs::path& filePath) {
    write(fd, "d", 1);   
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd,  &nullchar, 1);
}

void sendCreateRequest(int fd, const fs::path& filePath) {
    write(fd, "c", 1);
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd,  &nullchar, 1);
}

void sendMoveReqest(const fs::path& filePath, const fs::path& filePath2, int fd) {
    write(fd, "m", 1);
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd,  &nullchar, 1);

    write(fd, filePath2.c_str(), sizeof(filePath2));
    write(fd,  &nullchar, 1);
}


int main(int argc, char *argv[]) {

	if(argc < 2){
		std::cout << "Usage: ./client <server_ip> <server_port>" << std::endl;
		return 1;
	}

	// Create a directory if it does not exist
	fs::create_directory("./sync");
	
    // Create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    // Specify the server address
    sockaddr_in serverAddress;
	serverAddress.sin_addr.s_addr = inet_addr(argv[1]);
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(atoi(argv[2])); // replace with your port

    if (connect(sock, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Failed to connect to server." << std::endl;
        return 1;
    }
    FileWatcher fw("./sync", sock);

    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        std::cerr << "Failed to create epoll file descriptor." << std::endl;
        return 1;
    }

    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fw.fd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fw.fd, &event) == -1) {
        std::cerr << "Failed to add file descriptor to epoll." << std::endl;
        return 1;
    }

    event.data.fd = sock;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, sock, &event) == -1) {
        std::cerr << "Failed to add socket descriptor to epoll." << std::endl;
        return 1;
    }

    while (true) {
        epoll_event events[10];
        int numEvents = epoll_wait(epollFd, events, 10, -1);
        if (numEvents == -1) {
            std::cerr << "epoll_wait failed." << std::endl;
            return 1;
        }

        for (int i = 0; i < numEvents; i++) {
            if (events[i].data.fd == fw.fd) {
                fw.handle();
            } else if (events[i].data.fd == sock) {
                // handle server response here
            }
        }
    }

    close(sock);

    return 0;
}