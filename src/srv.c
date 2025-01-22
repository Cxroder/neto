#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h> // For int32_t
#include <time.h>

#define MAXLINE 1024
#define MAX_PATH_SIZE 256
#define MAX_CLIENTS 10
#define MAX_BUFFER_SIZE 100
#define err_sys perror
#define err_quit(msg)                                                                              \
    do {                                                                                           \
        fprintf(stderr, "%s\n", msg);                                                              \
        exit(EXIT_FAILURE);                                                                        \
    } while (0)
#define SA struct sockaddr
#define TIMEOUT_SECONDS 10 // Adjust timeout duration as needed

struct Packet {
    bool is_nack;
    int32_t sequence_number;
    char payload[MAXLINE];
    char file_path[MAX_PATH_SIZE];
    int mtu;
};

struct ACK_Packet {
    bool is_nack;
    int32_t sequence_number;
};

struct ClientInfo {
    char ip_address[INET_ADDRSTRLEN];  // Store IP address as a string
    int port;
    FILE *file_ptr;
    int expected_sequence_number; // next packet to be written
    int last_acked; // last ack attempt's number
    int mtu; // Maximum Transmission Unit size
    struct {
        int sequence_number;
        char data[MAXLINE];
        int mtu;
    } buffer[MAX_BUFFER_SIZE]; // Buffer for out-of-order packets
    int buffer_count; // Number of packets in buffer
    time_t last_response_time;  // Track the last response time
};

struct ClientInfo clients[MAX_CLIENTS];
int num_clients = 0;

// Function to get RFC 3339 timestamp
char *get_current_rfc3339_time() {
    static char rfc3339_time[30]; // Static buffer to store formatted time
    time_t raw_time;
    struct tm *time_info;

    // Get current time
    time(&raw_time);
    time_info = gmtime(&raw_time);

    // Format time as RFC 3339
    strftime(rfc3339_time, sizeof(rfc3339_time), "%Y-%m-%dT%H:%M:%SZ", time_info);

    return rfc3339_time;
}

// Function to send an ACK or NACK to the client
void send_ack_or_nack(int sockfd, struct sockaddr *cliaddr, socklen_t clilen, int32_t sequence_number, bool is_nack) {
    struct ACK_Packet ack_packet;
    ack_packet.sequence_number = sequence_number;
    ack_packet.is_nack = is_nack;
    /*memset(ack_packet.payload, 0, sizeof(ack_packet.payload)); // Clear payload buffer
    memset(ack_packet.file_path, 0, sizeof(ack_packet.file_path)); // Clear file_path buffer
    ack_packet.mtu = 0; // Initialize mtu to 0 or assign an appropriate value*/

    // Serialize the ack_packet structure into a byte stream
    char buffer[sizeof(struct ACK_Packet)];
    memcpy(buffer, &ack_packet, sizeof(struct ACK_Packet));

    // Send the byte stream to the client
    ssize_t sent = sendto(sockfd, buffer, sizeof(struct ACK_Packet), 0, cliaddr, clilen);
    if (sent != sizeof(struct ACK_Packet)) {
        printf("Size of buffer: %zu\n", sizeof(buffer));
        printf("Size of Packet: %zu\n", sizeof(struct ACK_Packet));
        printf("size of sent: %zu, %zd\n", sizeof(sent), sent);
        perror("Error sending ACK/NACK");
        // Handle error
    } else {
        /*printf("Size of Packet: %zu\n", sizeof(struct ACK_Packet));
        printf("size of sent: %zu, %zd\n", sizeof(sent), sent);*/
        return;
    }
}

void handle_path(int sockfd, SA *pcliaddr, socklen_t clilen, char *ip_address, int port, char *path_name, int mtu, struct ClientInfo *clients, int *num_clients) {
    if (*num_clients < MAX_CLIENTS) {
        // Open the file specified by the client's path name
        FILE *file = fopen(path_name, "wb");
        //printf("openin\n");
        if (!file) {
            printf("Error opening file for client %s:%d\n", ip_address, port);
            return;
        }

        // Save client information, file pointer, and MTU
        strcpy(clients[*num_clients].ip_address, ip_address);
        clients[*num_clients].port = port;
        clients[*num_clients].file_ptr = file;
        clients[*num_clients].mtu = mtu;
        clients[*num_clients].expected_sequence_number = 1;
        clients[*num_clients].last_acked = 0;
        clients[*num_clients].last_response_time = -1;
        (*num_clients)++;
        //printf("clin inits\n");

        // Send ACK for the path packet
        send_ack_or_nack(sockfd, pcliaddr, clilen, 0, false); // Assuming sockfd, cliaddr, and clilen are available
        char *current_time = get_current_rfc3339_time();
        printf("%s, ACK, 0\n", current_time);
    } /*else {
        printf("Maximum number of clients reached\n");
    }*/
}

bool handle_data(int sockfd, SA *pcliaddr, socklen_t clilen, struct ClientInfo *client, int sequence_number, char *data, int droppc, int mtu) {
    // Check if the received packet's sequence number matches the expected sequence number
    //printf("the packet is %d. comparing to %d (mtu is %d) -> ", sequence_number, client->expected_sequence_number, mtu);
    if (sequence_number == client->expected_sequence_number) {
        // Process the packet and write to the file
        //printf("Received in-order packet from client %s:%d\n", client->ip_address, client->port);
        
        //printf("WRITING data to file -> ");
        size_t write_size;
        if (mtu >= 0) {
            write_size = mtu;
            //printf("using new mtu");
        } else {
            write_size = client->mtu;
            //printf("using mtu %d ", client->mtu);
        }
        size_t bytes_written = fwrite(data, 1, write_size, client->file_ptr);
        if (bytes_written != write_size) {
            perror("Error writing to file");
            fclose(client->file_ptr);
            // Handle error
            return false;
        } /*else {
            //printf("written %zu. ", bytes_written);
        }*/

        // Increment the expected sequence number
        client->expected_sequence_number++;
        //printf("raised expectd to %d ~>", client->expected_sequence_number);

        // Process any consecutive packets from the buffer
        while (client->buffer_count > 0 && client->buffer[0].sequence_number == client->expected_sequence_number) {
            // Process the buffered packet and write to the file
            //printf("\nWRIting from buffer\n");

            if (client->buffer[0].mtu >= 0) {
                write_size = client->buffer[0].mtu;
                //printf("using new mtu ");
            } else {
                write_size = client->mtu;
                //printf("using mtu %d ", client->buffer[0].mtu);
            }
            size_t buffer_bytes_written = fwrite(client->buffer[0].data, 1, write_size, client->file_ptr);
            if (buffer_bytes_written != write_size) {
                perror("Error writing to file");
                fclose(client->file_ptr);
                // Handle error
                return false;
            }

            // Increment the expected sequence number
            client->expected_sequence_number++;

            // Remove the processed packet from the buffer
            for (int i = 0; i < client->buffer_count - 1; i++) {
                client->buffer[i] = client->buffer[i + 1];
            }
            client->buffer_count--;
        }
        //printf("checked buffer. Now compare to mtu %d -> ", client->mtu);

        // Simulate dropping ACK based on droppc
        int drop = rand() % 100; // Generate random number between 0 and 99
        char *current_time = get_current_rfc3339_time();
        if (drop >= droppc) {
            // Check if an ACK needs to be sent
            if ( (client->expected_sequence_number > client->last_acked) || (mtu > 0) ) {
                send_ack_or_nack(sockfd, pcliaddr, clilen, sequence_number, false); // Sending ACK
                printf("%s, ACK, %d\n", current_time, abs(sequence_number));
                if (mtu >= 0) {
                    client->last_acked = -sequence_number;
                    //printf("\nCLOSING FILE\n");
                    fclose(clients->file_ptr);
                    return true; // Signal to start timer
                } else {
                    //printf("Not indicatedcd last\n");
                    client->last_acked = client->expected_sequence_number - 1; // Update last_acked
                }
            } else {
                //printf("data not lower than mtu %lu !< %d ", bytes_written, client->mtu);
                return false;
            }
        } else {
            //printf("Dropped ACK for sequence number %d\n", sequence_number);
            printf("%s, DROP ACK, %d\n", current_time, sequence_number);
            return false;
        }

    } else if (sequence_number > client->expected_sequence_number) {
        return false;
        // Packet is out-of-order, store it in the buffer
        //printf("STORING OOD %d, expected %d\n", sequence_number, client->expected_sequence_number);
        int index = 0;
        while (index < client->buffer_count && client->buffer[index].sequence_number < sequence_number) {
            index++;
        }
        if (index < MAX_BUFFER_SIZE) {
            // Shift existing entries to make space for the new packet
            for (int i = client->buffer_count - 1; i >= index; i--) {
                client->buffer[i + 1] = client->buffer[i];
            }
            // Insert the new packet into the buffer
            client->buffer[index].sequence_number = sequence_number;
            client->buffer[index].mtu = mtu;
            strcpy(client->buffer[index].data, data);
            client->buffer_count++;
        } else {
            //printf("Buffer overflow: Discarding out-of-order packet\n");
            return false;
        }
    } else {
        //printf("dupl found %d, expected %d\n", sequence_number, client->expected_sequence_number);
        // Duplicate or older out-of-order packet, ignore it
        //printf("Received duplicate or older out-of-order packet from client %s:%d (sequence number: %d)\n", client->ip_address, client->port, sequence_number);
        return false;
    }
    return false;
}

// Function to process incoming packets
bool process_packet(int sockfd, SA *pcliaddr, socklen_t len, struct ClientInfo *clients, int *num_clients, int droppc) {
    ssize_t n;
    struct Packet packet;

    // Receive the byte stream from the client
    n = recvfrom(sockfd, &packet, sizeof(struct Packet), 0, pcliaddr, &len);
    // Assuming pcliaddr is of type struct sockaddr* (or sockaddr_in* if IPv4)
    struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)pcliaddr;
    // Extract IP address and port
    char *ip_address = inet_ntoa(ipv4_addr->sin_addr);
    int port = ntohs(ipv4_addr->sin_port);
    if (n < 0) {
        perror("recvfrom error");
    } /*else {
        printf("Gotten packet\n");
    }*/

    // Determine if the packet is a path packet or data packet based on sequence number
    if (packet.sequence_number == 0) {
        // Path packet
        //printf("Got a path\n");
        handle_path(sockfd, pcliaddr, len, ip_address, port, packet.file_path, packet.mtu, clients, num_clients);
        //return false;
    } else {
        //printf("got data\n");
        // Data packet
        // Simulate dropping packet based on droppc
        int drop = rand() % 100; // Generate random number between 0 and 99
        if (drop >= droppc) {
            //printf("success\n");
            // Packet not dropped, handle the data packet
            for (int i = 0; i < *num_clients; i++) {
                if (strcmp(clients[i].ip_address, ip_address) == 0 &&
                    clients[i].port == port) {
                    // Found the client
                    if (packet.sequence_number >= clients[i].expected_sequence_number) {
                        // The received packet's sequence number is higher or equal than the client's expected sn
                        //printf("handling data\n");
                        if (handle_data(sockfd, pcliaddr, len, &clients[i], packet.sequence_number, packet.payload, droppc, packet.mtu)) {
                            // Update last response time for the client
                            clients[i].last_response_time = time(NULL);
                            //printf("last response noted\n");
                            return true;
                        }
                        //printf("wait for more\n");
                        return false;
                    } else {
                        //printf("got dupl %d < %d\n", packet.sequence_number, clients[i].expected_sequence_number);
                        // The received packet's sequence number is lower to the client's expected sn
                        // Resend the last_acked packet
                        char *current_time = get_current_rfc3339_time();
                        drop = rand() % 100;
                        if (drop >= droppc) {
                            send_ack_or_nack(sockfd, pcliaddr, len, clients[i].last_acked, false); // Sending ACK
                            printf("%s, ACK, %d\n", current_time, abs(clients[i].last_acked)); //wrong?
                        } else {
                            printf("%s, DROP ACK, %d\n", current_time, clients[i].last_acked);
                        }
                        if (clients[i].last_acked < 0) {
                            // Update last response time for the client
                            clients[i].last_response_time = time(NULL);
                            return true;
                        }
                    } 
                }
            }
        } else {
            //printf("dropped\n");
            /*drop = rand() % 100; // Generate random number between 0 and 99
            if (drop >= droppc) {
                send_ack_or_nack(sockfd, pcliaddr, len, packet->sequence_number, False); // Sending NACK
                char *current_time = get_current_rfc3339_time();
                printf("%s, ACK, %d\n", current_time, clients[i].last_acked);
                //printf("Dropped packet with sequence number %d\n", packet->sequence_number);
            } else {
                printf("Dropped NACK with sequence number %d\n", packet->sequence_number);
            }*/
            send_ack_or_nack(sockfd, pcliaddr, len, packet.sequence_number, true); // Sending NACK
            char *current_time = get_current_rfc3339_time();
            printf("%s, DROP DATA, %d\n", current_time, packet.sequence_number);
        }
        return false;
        //return true;
    }
    return false;
}

void dg_process(int sockfd, SA *pcliaddr, socklen_t clilen, int droppc) {
    int n;
    socklen_t len;

    for (;;) { // iterative UDP server
        //printf("starting loop\n");
        len = clilen;
        bool is_final = process_packet(sockfd, pcliaddr, len, clients, &num_clients, droppc);

        // Check for timeout for each client
        time_t current_time = time(NULL);
        for (int i = 0; i < num_clients; i++) {
            if (clients[i].last_response_time != -1) {
                if (current_time - clients[i].last_response_time > TIMEOUT_SECONDS) {
                    // Handle timeout for this client
                    // For example, retransmit the last packet or terminate the connection
                    //printf("Timeout for client %s:%d\n", clients[i].ip_address, clients[i].port);
                }
            } else {
                continue;
            }
        }
    }
}


int main(int argc, char **argv) {
    char buf[256]; // buffer to store the formatted string

    if (argc != 3) {
        snprintf(
            buf, sizeof(buf), "Usage: %s <port> <droppc>", argv[0]);
        err_quit(buf);
    }

    int sockfd, n, port, droppc;
    struct sockaddr_in servaddr, cliaddr;

    port = atoi(argv[1]);
    droppc = atoi(argv[2]);
    
    // Validate drop percentage value
    if (droppc < 0 || droppc > 100) {
        err_quit("Drop percentage must be between 0 and 100");
    }
    // Validate port
    if (!((1 <= port && port <= 79) || (81 <= port && port <= 49151)))
        printf("Chosen port may not function properly\n Recommended range: (1-79, 81-49151)\n");

    /*
    // Print parsed components for verification
    printf("si: %s\n", server_ip);
    printf("p: %d\n", port);
    printf("mtu: %d\n", mtu);
    printf("inpath: %s\n", in_path);
    printf("outpath: %s\n", out_path);
    */

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        err_sys("socket error");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port); // Port for HTTP
    if (bind(sockfd, (SA*)&servaddr, sizeof(servaddr)) == -1) 
        err_sys("bind failed");

    
    // Get the local IP address
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sockfd, (SA*)&local_addr, &addr_len) == -1)
        err_sys("getsockname failed");

    char local_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip)) == NULL)
        err_sys("inet_ntop failed");

    //printf("Server bound to IP address: %s\n", local_ip);

    printf("This is Lab 3\n");
    

    dg_process(sockfd, (SA*)&cliaddr, sizeof(cliaddr), droppc);

    exit(0);
}
