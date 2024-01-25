#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <map>
#include <cstdlib>
#include <fstream>
#include <filesystem> 
#include <linux/limits.h>


constexpr int MAX_EVENTS = 10;
constexpr int BUFFER_SIZE = 1024 + PATH_MAX;

namespace fs = std::filesystem;

void sendResponse(const char* response, int clientSocket){
    ssize_t bytesSent = send(clientSocket, response, strlen(response), 0);
    if(bytesSent == -1){
        std::cerr << "Failed to send response to client." << std::endl;
        close(clientSocket);
        return;
    }
}

void createFileOnServer(std::string filepath, char *fileContent){
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to create file on server." << std::endl;
        return;
    }
    file << fileContent;
    file.close();
}


void updateFileOnServer(std::string filepath, char *fileContent){
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to update file on server." << std::endl;
        return;
    }
    file << fileContent;
    file.close();
}

void deleteFileOnServer(std::string filepath){
    fs::remove_all(filepath);
}

void moveFileOnServer(std::string oldFilePath, char *newFilePath){
    fs::rename(oldFilePath, newFilePath);
}


void manageOperationSendFromClient(int operation, std::string filepath, char *fileContent, char *newFilePath=NULL){
    switch(operation){
        case 1:
            createFileOnServer(filepath, fileContent);
            break;
        case 2:
            updateFileOnServer(filepath, fileContent);
            break;
        case 3:
            deleteFileOnServer(filepath);
            break;
        case 4:
            moveFileOnServer(filepath, newFilePath);
            break;
        default:
            std::cerr << "Unknown operation." << std::endl;
            break;
    }
}

int compareModifiedTimes(u_int64_t clientTime, u_int64_t serverTime){
    if(clientTime > serverTime){
        return 1;
    } else if(clientTime < serverTime){
        return -1;
    } else {
        return 0;
    }
}

//0 -> no conflict 1 -> conflict
int resolveConflict(u_int64_t clientTime, u_int64_t serverTime){
    if(compareModifiedTimes(clientTime, serverTime)){
        return 0;
    }
    
    return 1;
}

void sendFileBack(int client_sock, std::string filepath, u_int64_t contentSize, char *fileContent){
    ssize_t bytes_sent = (client_sock, fileContent, sizeof(contentSize), 0);
    if(bytes_sent == -1){
        std::cerr << "Failed to send file to client." << std::endl;
        close(client_sock);
        return;
    }
}


void receivePacketsFromClient(int client_sock){
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

    int i=1;
    std::string filepath = "";
    char *newFilePath[BUFFER_SIZE];
    char operation = buffer[0];
    u_int64_t contentSize;
    char tab[contentSize]; 
    char *fileContent;
    u_int64_t lastModTime;

    while(buffer[i] != '\0'){
        filepath += buffer[i];
        i++;
    }

    if (operation=='m'){
        memcpy(newFilePath, buffer, sizeof(buffer)-i);
    }

    if(operation=='u'){
        lastModTime = buffer[i++]<<56 | buffer[i++]<<48 | buffer[i++]<<40 | buffer[i++]<<32 | buffer[i++]<<24 | buffer[i++]<<16 | buffer[i++]<<8 | buffer[i++];
        contentSize = buffer[i++]<<56 | buffer[i++]<<48 | buffer[i++]<<40 | buffer[i++]<<32 | buffer[i++]<<24 | buffer[i++]<<16 | buffer[i++]<<8 | buffer[i++];

        memcpy(tab, buffer, sizeof(buffer)-i);
        recv(client_sock, tab + (sizeof(buffer)-1), contentSize-sizeof(buffer)-i, 0);

        if(resolveConflict(lastModTime, fs::last_write_time(filepath).time_since_epoch().count())){
            sendResponse("Conflict detected.\n", client_sock);
            sendFileBack(client_sock, filepath, contentSize, tab);
            return;
        }
    }
   
    sendResponse("Files successfully received.\n", client_sock);
    manageOperationSendFromClient(operation, filepath, tab);
    sendResponse("File successfully recreated on server.\n", client_sock);
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

                receivePacketsFromClient(clientSocket);
            }
        }
    }

    close(serverSocket);
    close(epollFd);

    return 0;
}
