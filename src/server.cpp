#include <iostream>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include "lib.h"

class Client : public Socket {
public:
	Client(int fd) : Socket(fd) {
	}

	void handle(std::vector<char> buf) override;
};

class Server : public SyncDir {
public:
	std::vector<Client*> clientSockets;

	Server(fs::path base) : SyncDir(base) {}

	inline void broadcastExcept(std::string str, Client* except) {
		this->broadcastExcept(str.data(), str.size(), except);
	}

	void broadcastExcept(const char* data, ssize_t len, Client* except) {
		for (const auto client : this->clientSockets) {
			if (client != except && !(client->sendData(data, len))) {
				std::cerr << "Failed to send file to client." << std::endl;
				this->clientSockets.erase(std::remove(this->clientSockets.begin(), this->clientSockets.end(), client), this->clientSockets.end());
				delete client;
			}
		}
	}

	void updateFileConflictHook(std::string filepath, [[maybe_unused]] ssize_t _1, [[maybe_unused]] ssize_t _2, Socket* source) override {
		source->sendData("eConflict detected on " + filepath + "\n\n");

		int fileFd = open((this->base / filepath).c_str(), O_RDONLY);
		if (fileFd == -1) {
			std::cerr << "File doesn't exist" << std::endl;
			return;
		}

		char buffer[BUFFER_SIZE];
		ssize_t bytesRead;
		while ((bytesRead = read(fileFd, buffer, BUFFER_SIZE)) > 0) {
			if (source->sendData(buffer, bytesRead)) {
				std::cerr << "Failed to send file to client." << std::endl;
				this->clientSockets.erase(std::remove(this->clientSockets.begin(), this->clientSockets.end(), source), this->clientSockets.end());
				delete source;
			}
		}
	};

	void updateFilePostHook(std::string filepath, ssize_t mtime, ssize_t len, Socket* source, char* buf) override {
		std::ostringstream oss;
		oss << "u" << filepath << '\n' << mtime << ' ' << len << "\n\n";
		this->broadcastExcept(oss.str(), (Client*) source);
		this->broadcastExcept(buf, len, (Client*) source);
	}

	void moveFilePostHook(std::string oldFilepath, std::string newFilepath, Socket* source) override {
		std::ostringstream oss;
		oss << "m" << oldFilepath << "\n" << newFilepath << "\n\n";
		this->broadcastExcept(oss.str(), (Client *) source);
	}

	void deleteFilePostHook(std::string filepath, Socket* source, uintmax_t count) override {
		if (count) {
			std::ostringstream oss;
			oss << "d" << filepath << "\n\n";
			this->broadcastExcept(oss.str(), (Client*) source);
		}
	}
};

Server server("./srvsync");

void Client::handle(std::vector<char> buf) {
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
		server.updateFile(filepath, mtime, len, this);
	} break;
	case 'd': {
		if (rest.size() > 0) {
			server.deleteFile(rest, this);
		}
	} break;
	case 'm': {
		std::istringstream iss(rest);
		std::string oldFilepath, newFilepath;
		std::getline(iss, oldFilepath);
		std::getline(iss, newFilepath);
		if (oldFilepath.size() > 0 && newFilepath.size() > 0) {
			server.moveFile(oldFilepath, newFilepath, this);
		}
	} break;
	default:
		std::cerr << "Unknown operation: " << buf[0] << std::endl;
		break;
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cerr << "Correct usage ./server <port>\n";
		return 1;
	}

	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		std::cerr << "Failed to create socket." << std::endl;
		return 1;
	}

	if (!fs::exists("./srvsync")) {
		fs::create_directory("./srvsync");
	}	  

	int opt = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
		std::cerr << "Failed to set socket options." << std::endl;
		close(serverSocket);
		return 1;
	}
	fcntl(serverSocket, F_SETFL, O_NONBLOCK);

	sockaddr_in serverAddress{};
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(atoi(argv[1]));

	if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == -1) {
		std::cerr << "Failed to bind socket." << std::endl;
		close(serverSocket);
		return 1;
	}

	if (listen(serverSocket, SOMAXCONN) == -1) {
		std::cerr << "Failed to listen for connections." << std::endl;
		close(serverSocket);
		return 1;
	}

	int epollFd = epoll_create1(0);
	if (epollFd == -1) {
		std::cerr << "Failed to create epoll instance." << std::endl;
		close(serverSocket);
		return 1;
	}

	epoll_event event{};
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = serverSocket;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSocket, &event) == -1) {
		std::cerr << "Failed to add server socket to epoll." << std::endl;
		close(serverSocket);
		close(epollFd);
		return 1;
	}

	std::vector<epoll_event> events(MAX_EVENTS);

	while (true) {
		int numEvents = epoll_wait(epollFd, events.data(), MAX_EVENTS, -1);
		if (numEvents == -1) {
			std::cerr << "Failed to wait for events." << std::endl;
			close(serverSocket);
			close(epollFd);
			return 1;
		}

		for (int i = 0; i < numEvents; ++i) {
			if (events[i].data.fd == serverSocket) {
				sockaddr_in clientAddress{};
				socklen_t clientAddressLength = sizeof(clientAddress);
				int clientSocket = accept(
					serverSocket,
					reinterpret_cast<sockaddr*>(&clientAddress),
					&clientAddressLength
				);
				if (clientSocket == -1) {
					std::cerr << "Failed to accept connection." << std::endl;
					continue;
				}
				fcntl(clientSocket, F_SETFL, O_NONBLOCK);

				event.events = EPOLLIN | EPOLLET;
				event.data.ptr = new Client(clientSocket);
				if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
					std::cerr << "Failed to add client socket to epoll." << std::endl;
					close(clientSocket);
					continue;
				}
				server.clientSockets.push_back((Client*)event.data.ptr);
				std::cout << "New client connected." << std::endl;
			} else {
				((Client*) events[i].data.ptr)->readData();
			}
		}
	}

	close(serverSocket);
	close(epollFd);

	return 0;
}
