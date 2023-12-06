#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <thread>

void handle_client(int client_fd) {
    printf("Accepted a connection with ID: %d\n", client_fd);
    
    close(client_fd);
}

int main(int argc, char** argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Failed to create socket. Exiting...\n");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        printf("Failed to bind. Exiting...\n");
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        printf("Failed to listen. Exiting...\n");
        return 1;
    }

    while (true) {
        printf("Waiting for a connection...\n");
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == -1) {
            printf("Failed to accept a connection.\n");
            continue;
        }
        std::thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }

    close(server_fd);
    return 0;
}
