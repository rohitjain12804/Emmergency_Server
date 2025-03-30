#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

#define DISCOVERY_PORT 10841

void displayServices() {
    printf("\nAvailable Emergency Services:\n");
    printf("1. Police\n");
    printf("2. Ambulance\n");
    printf("3. Fire\n");
    printf("4. Vehicle Repair\n");
    printf("5. Food Delivery\n");
    printf("6. Blood Bank\n");
    printf("7. Exit\n");
    printf("Please enter the number corresponding to the service you need: ");
}

char* discoverServer() {
    int sock;
    struct sockaddr_in broadcastAddr, serverAddr;
    char buffer[1024];
    socklen_t addr_len = sizeof(serverAddr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation error");
        return NULL;
    }

    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("Error enabling broadcast");
        close(sock);
        return NULL;
    }

    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    char* discoveryMessage = "DISCOVER_EMERGENCY_SERVER";
    printf("Searching for emergency server...\n");

    for (int i = 0; i < 5; i++) {  // Try 5 times
        if (sendto(sock, discoveryMessage, strlen(discoveryMessage), 0, (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
            perror("Error sending discovery message");
            close(sock);
            return NULL;
        }

        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;

        int poll_res = poll(&pfd, 1, 2000);  // 2-second timeout

        if (poll_res > 0) {
            int recv_len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&serverAddr, &addr_len);
            if (recv_len > 0) {
                buffer[recv_len] = '\0';  // Null-terminate the response
                if (strstr(buffer, "EMERGENCY_SERVER:") != NULL) {
                    close(sock);
                    int serverPort;
                    sscanf(buffer, "EMERGENCY_SERVER:%d", &serverPort);
                    char* serverInfo = malloc(50);
                    sprintf(serverInfo, "%s:%d", inet_ntoa(serverAddr.sin_addr), serverPort);
                    printf("Discovered server at %s\n", serverInfo);
                    return serverInfo;
                }
            }
        }
    }

    printf("No server found.\n");
    close(sock);
    return NULL;
}

int connectToServer(const char* serverInfo) {
    char serverIp[50];
    int serverPort;

    sscanf(serverInfo, "%[^:]:%d", serverIp, &serverPort);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return -1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, serverIp, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    return sock;
}

int main() {
    char* serverInfo = discoverServer();
    if (!serverInfo) {
        return 1;
    }

    int sock = connectToServer(serverInfo);
    free(serverInfo);
    if (sock < 0) {
        return 1;
    }

    int choice;
    char buffer[1024];

    while (1) {
        displayServices();
        if (scanf("%d", &choice) != 1) {
            // Handle invalid input (non-integer input)
            printf("Invalid input. Please enter a number between 1 and 7.\n");
            // Clear the input buffer
            while (getchar() != '\n'); // Discard invalid input
            continue;
	}
        if (choice == 7) {
	    printf("Exiting... Sending exit message to server.\n");
	    const char* exitMessage = "exit";
	    send(sock, exitMessage, strlen(exitMessage), 0);  // Send exit message to server
	    printf("Client exited.\n");
	    break;  // Exit
	}
	

        const char* services[] = {
            "Police", "Ambulance", "Fire", "Vehicle Repair", "Food Delivery", "Blood Bank"
        };

        if (choice < 1 || choice > 6) {
            printf("Invalid choice. Please try again.\n");
            continue;
        }

        send(sock, services[choice - 1], strlen(services[choice - 1]), 0);
        int valread = read(sock, buffer, sizeof(buffer));
        if (valread > 0) {
            buffer[valread] = '\0';  // Null-terminate the response
            printf("Response from server: %s\n", buffer);
        }
    }

    close(sock);
    return 0;
}

