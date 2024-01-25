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

namespace fs = std::filesystem;

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

class FileWatcher {
private:
	std::vector<std::pair<int, fs::path>> watchlist;
	std::optional<fs::path> lastMove;

public:
	int fd;

	FileWatcher(std::string path) {
		this->fd = inotify_init1(IN_NONBLOCK);
		this->add(path);
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
				} else if (event->mask & IN_DELETE) {
					this->remove(*path, false);
					std::cout << "Delete: " << *path << std::endl;
				}
			} else {
				std::cout << " [file]" << std::endl;
				if (event->mask & IN_CLOSE_WRITE) {
					std::cout << "Write: " << *path << std::endl;
				} else if (event->mask & IN_DELETE) {
					std::cout << "Delete: " << *path << std::endl;
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
    write(fd, 0, 1);
    
    // Send file modification time
    auto current_time = fs::last_write_time(filePath).time_since_epoch().count();
    write(fd, &current_time, sizeof(current_time));
    
    // Send file size
    auto file_size = (uint64_t)fs::file_size(filePath);
    write(fd, &file_size, sizeof(uint64_t));
    
    // Send file content
    int fileFd = open(filePath.c_str(), O_RDONLY);
    
    char buffer[1024];
    ssize_t bytesRead;

    while ((bytesRead = read(fileFd, buffer, BUFFER_SIZE)) > 0) {
        write(fd, buffer, bytesRead);
    }

}

void sendDeleteRequest(int fd, const fs::path& filePath) {
    write(fd, "d", 1);   
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd, 0, 1);
}

void sendCreateRequest(int fd, const fs::path& filePath) {
    write(fd, "c", 1);
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd, 0, 1);
}

void sendMoveReqest(const fs::path& filePath, const fs::path& filePath2, int fd) {
    write(fd, "m", 1);
    
    write(fd, filePath.c_str(), sizeof(filePath));
    write(fd, 0, 1);

    write(fd, filePath2.c_str(), sizeof(filePath2));
    write(fd, 0, 1);
}

int main() {
	FileWatcher fw("./sync");
	
	pollfd pfd[1];
	pfd[0].fd = fw.fd;
	pfd[0].events = POLLIN;
	while (poll(pfd, 1, -1)) {
		if (pfd[0].events & POLLIN) {
			fw.handle();
		}
	}

	return 0;
}
