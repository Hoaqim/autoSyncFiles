#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <iterator>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include "lib.h"

class Server : public Socket {
public:
	Server(int fd) : Socket(fd) {}

	void handle(std::vector<char> buf) override;
};

class Client : public SyncDir {
	fs::path conflict;
public:
	Client(fs::path base, fs::path conflict) : SyncDir(base), conflict(conflict) {}

	void updateFileConflictHook(std::string filepath, ssize_t mtime, ssize_t len, Socket* source) override {
		std::ostringstream oss;
		oss << "eConflict detected on " << filepath << "\n\n";
		source->sendData(oss.str());
		
		fs::path realFilepath = this-> conflict / std::format("{}-{:#018x}", filepath, mtime); 
		if (fs::exists(realFilepath)) {
			std::cerr << "Failed to save conflict \"" << filepath << "\"" << std::endl;
		}
		
		std::ofstream file(realFilepath, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!file.is_open()) {
			std::cerr << "Failed to save conflict \"" << filepath << "\"" << std::endl;
		}
		
		std::vector<char> buf(len);
		ssize_t i = 0;
		while (i < len) {
			ssize_t rlen = source->readData(buf.data() + i, len);
			if (rlen > 0) {
				file.write(buf.data() + i, rlen);
				i += rlen;
			}
		}
		file.close();
	}
};

Client client("./sync", "./conflict");


void Server::handle(std::vector<char> buf) {
	std::cerr << "xd handler" << std::endl;
	std::string rest(buf.data() + 1, buf.size() - 1);
	switch (buf[0]) {
	case 'e':
		std::cerr << rest << std::endl;
		break;
	case 'u': {
		std::istringstream iss(rest);
		std::string filepath;
		ssize_t len, mtime;
		std::getline(iss, filepath);
		iss >> mtime >> len;
		client.updateFile(filepath, mtime, len, this);
	} break;
	case 'd': {
		if (rest.size() > 0) {
			client.deleteFile(rest, this);
		}
	} break;
	case 'm': {
		std::istringstream iss(rest);
		std::string oldFilepath, newFilepath;
		std::getline(iss, oldFilepath);
		std::getline(iss, newFilepath);
		if (oldFilepath.size() > 0 && newFilepath.size() > 0) {
			client.moveFile(oldFilepath, newFilepath, this);
		}
	} break;
	default:
		std::cerr << "Unknown operation: " << buf[0] << std::endl;
		break;
	}
}

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
	std::optional<std::string> lastMove;

public:
	int fd;
	Server* server;
	std::string base;

	FileWatcher(std::string path, Server* server) : server(server), base(path) {
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
		char buf[BUFFER_SIZE] __attribute__((aligned(alignof(inotify_event))));
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
			std::string strpath;
			event = (inotify_event*)ptr;

			if (event->mask & IN_IGNORED) {
				continue;
			}

			for (auto const &[wd, ppath] : this->watchlist) {
				if (event->wd == wd) {
					path = ppath / event->name;
					strpath = path->string().substr(this->base.length() + 1);
					break;
				}
			}

			if (!path) {
				std::cerr << "!!! Unknown watch descriptor: " << event->wd << std::endl;
				continue;
			}

			std::ostringstream oss;
			ssize_t mtime = fs::last_write_time(*path).time_since_epoch().count();
			if (event->mask & IN_CLOSE_WRITE) {
				std::cout << "[FW] IN_CLOSE_WRITE: " << event->wd
					<< " [file]" << std::endl;
				
				oss << 'u' << strpath << '\n' << mtime << " " << fs::file_size(*path) << "\n\n";
				this->server->sendData(oss.str());
				
				int fileFd = open((*path).c_str(), O_RDONLY);
				if (fileFd == -1) {
					std::cerr << "File doesn't exist" << std::endl;
				}
				char buffer[BUFFER_SIZE];
				ssize_t bytesRead;
				while ((bytesRead = read(fileFd, buffer, BUFFER_SIZE)) > 0) {
					if (!this->server->sendData(buffer, bytesRead)) {
						std::cerr << "Failed to send file to client." << std::endl;
					}
				}

				continue;
			}
			else if ((event->mask ^ (IN_CREATE | IN_ISDIR)) == 0) {
				std::cout << "[FW] IN_CREATE: " << event->wd
					<< " [directory]" << std::endl;
				this->add(*path);
			} else if (event->mask & IN_DELETE) {
				std::cout << "[FW] IN_DELETE: " << event->wd;
				if (event->mask & IN_ISDIR) {
					std::cout << " [directory]" << std::endl;
					this->remove(*path, false);
					std::cout << "Delete: " << path->string() << std::endl;
				} else {
					std::cout << " [file]" << std::endl;
				}

				oss << 'd' << strpath << "\n\n";
			}	else if (event->mask & IN_MOVED_FROM) {
				std::cout << "[FW] IN_MOVED_FROM: " << event->wd << std::endl;
				this->lastMove = strpath;
			}
			else if (event->mask & IN_MOVED_TO) {
				std::cout << "[FW] IN_MOVED_TO: " << event->wd << std::endl;
				if (this->lastMove) {
					if (event->mask & IN_ISDIR) {
						this->remove(*this->lastMove, true);
						this->add(*path);
					}

					oss << 'm' << *this->lastMove << "\n" << strpath << "\n\n";
				} else {
					std::cerr << "!!! Unknown move: ??? -> " << *path << std::endl;
				}
			}
			else {
				std::cout << std::format("[FW] UNKNOWN ({:#04x}): ", event->mask);
			}
			this->server->sendData(oss.str());
		}
	}
};

int main(int argc, char *argv[]) {
	if (argc < 3) {
		std::cerr << "Usage: ./client <server_ip> <server_port>" << std::endl;
		return 1;
	}
	
	if (!fs::exists("./sync")) {
		fs::create_directory("./sync");
	}

	if (!fs::exists("./conflict")) {
		fs::create_directory("./conflict");
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		std::cerr << "Failed to create socket." << std::endl;
		return 1;
	}

	sockaddr_in serverAddress = {
		.sin_family = AF_INET,
		.sin_port = htons(atoi(argv[2])),
		.sin_addr = { .s_addr = inet_addr(argv[1]) },
	};

	if (connect(
		sock,
		reinterpret_cast<sockaddr*>(&serverAddress),
		sizeof(serverAddress)) == -1
	) {
		std::cerr << "Failed to connect to server." << std::endl;
		return 1;
	}

	fcntl(sock, F_SETFL, O_NONBLOCK);

	int epollFd = epoll_create1(0);
	if (epollFd == -1) {
		std::cerr << "Failed to create epoll istance." << std::endl;
		return 1;
	}

	Server* serverptr = new Server(sock);
	
	epoll_event event{};
	event.events = EPOLLIN | EPOLLET;
	event.data.ptr = serverptr;

	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, sock, &event) == -1) {
		std::cerr << "Failed to add socket to epoll." << std::endl;
		return 1;
	}
	
	// TODO: Presync

	FileWatcher fw("sync", serverptr);

	event.data.fd = fw.fd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fw.fd, &event) == -1) {
		std::cerr << "Failed to add file watcher to epoll." << std::endl;
		return 1;
	}

	std::vector<epoll_event> events(MAX_EVENTS);

	while (true) {
		int numEvents = epoll_wait(epollFd, events.data(), MAX_EVENTS, -1);
		if (numEvents == -1) {
			std::cerr << "Failed to wait for events." << std::endl;
			close(sock);
			close(epollFd);
			return 1;
		}

		for (int i = 0; i < numEvents; ++i) {
			if (events[i].data.fd == fw.fd) {
				fw.handle();
			} else {
				((Server*)events[i].data.ptr)->readData();
			}
		}
	};

	close(sock);
	close(epollFd);

	return 0;
}
