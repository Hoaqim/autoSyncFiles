#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <map>
#include <fstream>

std::map<std::string, std::string> files;

constexpr int MAX_EVENTS = 10;
constexpr int BUFFER_SIZE = 1024;

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    // Set socket options to reuse address and enable non-blocking mode
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        std::cerr << "Failed to set socket options." << std::endl;
        close(serverSocket);
        return 1;
    }
    fcntl(serverSocket, F_SETFL, O_NONBLOCK);

    // Bind the socket to a specific address and port
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(8080); // Change the port number if needed

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == -1) {
        std::cerr << "Failed to bind socket." << std::endl;
        close(serverSocket);
        return 1;
    }

    // Listen for incoming connections
    if (listen(serverSocket, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen for connections." << std::endl;
        close(serverSocket);
        return 1;
    }

    // Create epoll instance
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        std::cerr << "Failed to create epoll instance." << std::endl;
        close(serverSocket);
        return 1;
    }

    // Add server socket to epoll
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
                // Accept new connection
                sockaddr_in clientAddress{};
                socklen_t clientAddressLength = sizeof(clientAddress);
                int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress),
                                          &clientAddressLength);
                if (clientSocket == -1) {
                    std::cerr << "Failed to accept connection." << std::endl;
                    continue;
                }
                fcntl(clientSocket, F_SETFL, O_NONBLOCK);

                // Add client socket to epoll
                event.events = EPOLLIN | EPOLLET;
                event.data.fd = clientSocket;
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
                    std::cerr << "Failed to add client socket to epoll." << std::endl;
                    close(clientSocket);
                    continue;
                }

                std::cout << "New client connected." << std::endl;
            } else {
                // Handle client data
                int clientSocket = events[i].data.fd;
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));

                ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (bytesRead == -1) {
                    std::cerr << "Failed to receive data from client." << std::endl;
                    close(clientSocket);
                    continue;
                } else if (bytesRead == 0) {
                    // Client disconnected
                    std::cout << "Client disconnected." << std::endl;
                    close(clientSocket);
                    continue;
                }

                std::cout << "Received data from client: " << buffer << std::endl;

                const char* response = "Data received.";
                ssize_t bytesSent = send(clientSocket, response, strlen(response), 0);
                if (bytesSent == -1) {
                    std::cerr << "Failed to send response to client." << std::endl;
                    close(clientSocket);
                    continue;
                }
            }
        }
    }

    // Cleanup
    close(serverSocket);
    close(epollFd);

    return 0;
}
