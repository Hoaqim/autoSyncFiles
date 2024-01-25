#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <map>


constexpr int MAX_EVENTS = 10;
constexpr int BUFFER_SIZE = 1024;


void sendResponse(const char* response, int clientSocket){
    ssize_t bytesSent = send(clientSocket, response, strlen(response), 0);
}

void receiveFilesFromClient(int client_sock){
    std::cout << "Receiving files from client" << std::endl;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytesRead = recv(client_sock, buffer, sizeof(buffer), 0);
    if (bytesRead == -1) {
        std::cerr << "Failed to receive data from client." << std::endl;
        close(client_sock);
        return;
    } else if (bytesRead == 0) {
        std::cout << "Client disconnected." << std::endl;
        close(client_sock);
        return;
    }
    //TODO compare files and recreate on srv
    sendResponse("Files successfully received.", client_sock);
}


int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
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
    serverAddress.sin_port = htons(8080);

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
                int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress),
                                          &clientAddressLength);
                if (clientSocket == -1) {
                    std::cerr << "Failed to accept connection." << std::endl;
                    continue;
                }
                fcntl(clientSocket, F_SETFL, O_NONBLOCK);

                event.events = EPOLLIN | EPOLLET;
                event.data.fd = clientSocket;
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
                    std::cerr << "Failed to add client socket to epoll." << std::endl;
                    close(clientSocket);
                    continue;
                }

                std::cout << "New client connected." << std::endl;
            } else {
                int clientSocket = events[i].data.fd;
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));

                receiveFilesFromClient(clientSocket);
            }
        }
    }

    close(serverSocket);
    close(epollFd);

    return 0;
}
