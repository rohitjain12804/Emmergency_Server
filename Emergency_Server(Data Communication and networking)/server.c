#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

#define PORT 10840
#define DISCOVERY_PORT 10841
#define MAX_CLIENTS 100
#define LOG_FILE "client_logs.csv"

struct sockaddr_in clientAddr;
struct pollfd clients[MAX_CLIENTS];
int num_clients = 0;
volatile sig_atomic_t running = 1;

char* services[] = {
    "Police: 100",
    "Ambulance: 102",
    "Fire: 101",
    "Vehicle Repair: 1800-102-1111",
    "Food Delivery: 1800-210-0000",
    "Blood Bank: 1910"
};

void sigintHandler(int sig_num) {
    running = 0;
    printf("\nShutting down server...\n");
}

// Function to log client information to CSV
void logClientService(const char* clientIP, int clientPort, const char* service) {
    FILE* logFile = fopen(LOG_FILE, "a");
    if (!logFile) {
        perror("Error opening log file");
        return;
    }
    fprintf(logFile, "%s,%d,%s\n", clientIP, clientPort, service);
    fclose(logFile);
}

// Function to initialize the log file
void initLogFile() {
    FILE* logFile = fopen(LOG_FILE, "r");
    if (!logFile) {
        // If file doesn't exist, create it and add headers
        logFile = fopen(LOG_FILE, "w");
        if (logFile) {
            fprintf(logFile, "Client IP,Port Number,Service Taken\n");
            fclose(logFile);
        } else {
            perror("Error creating log file");
        }
    } else {
        fclose(logFile);
    }
}

void* handleDiscovery(void* arg) {
    int sockfd;
    struct sockaddr_in serverAddr, clientAddr;
    char buffer[1024];
    socklen_t addr_size;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(DISCOVERY_PORT);

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    printf("Listening for discovery requests on port %d...\n", DISCOVERY_PORT);

    while (running) {
        addr_size = sizeof(clientAddr);
        int recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &addr_size);
        if (recv_len > 0) {
            char* response = "EMERGENCY_SERVER:10840";  // Corrected port number
            sendto(sockfd, response, strlen(response), 0, (struct sockaddr*)&clientAddr, addr_size);
        }
    }

    close(sockfd);
    return NULL;
}

void removeClient(int idx) {
    printf("Client disconnected: FD %d\n", clients[idx].fd);
    close(clients[idx].fd);
    clients[idx] = clients[num_clients - 1];  // Replace with the last client
    num_clients--;
}

void handleRequest(int fd, struct sockaddr_in clientAddr) {
    char buffer[1024] = {0};
    int valread = read(fd, buffer, sizeof(buffer));
    if (valread <= 0) {
        return;
    }

    buffer[valread] = '\0';
    printf("Client %s:%d requested: %s\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), buffer);

    char* response;
    if (strcmp(buffer, "Police") == 0) response = services[0];
    else if (strcmp(buffer, "Ambulance") == 0) response = services[1];
    else if (strcmp(buffer, "Fire") == 0) response = services[2];
    else if (strcmp(buffer, "Vehicle Repair") == 0) response = services[3];
    else if (strcmp(buffer, "Food Delivery") == 0) response = services[4];
    else if (strcmp(buffer, "Blood Bank") == 0) response = services[5];
    else if (strcmp(buffer, "exit") == 0) {
        printf("Client %s:%d exited.\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        logClientService(inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), "Exited");
        close(fd);
        removeClient(fd);
        return;
    } else {
        response = "Invalid service request";
    }

    send(fd, response, strlen(response), 0);
    logClientService(inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), buffer);
}

int main() {
    signal(SIGINT, sigintHandler);

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // Initialize the log file
    initLogFile();

    pthread_t discovery_thread;
    pthread_create(&discovery_thread, NULL, handleDiscovery, NULL);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Emergency server started on port %d\n", PORT);

    clients[0].fd = server_fd;
    clients[0].events = POLLIN;
    num_clients = 1;

    while (running) {
        int poll_count = poll(clients, num_clients, -1);

        if (poll_count < 0) {
            if (errno == EINTR) continue;
            perror("Poll error");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < num_clients; i++) {
            if (clients[i].revents & POLLIN) {
                if (clients[i].fd == server_fd) {
                    new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
                    if (new_socket < 0) {
                        perror("Accept failed");
                        exit(EXIT_FAILURE);
                    }

                    printf("New client connected: %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                    clients[num_clients].fd = new_socket;
                    clients[num_clients].events = POLLIN;
                    num_clients++;
                } else {
                    handleRequest(clients[i].fd, address);
                }
            }
        }
    }

    close(server_fd);
    pthread_join(discovery_thread, NULL);
    return 0;
}

