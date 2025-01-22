#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <stdint.h> // For int32_t

#define MAXLINE 1024
#define MAX_PATH_SIZE 256
#define MAX_RETRIES 5
#define err_sys perror
#define err_quit(msg)                                                                              \
    do {                                                                                           \
        fprintf(stderr, "%s\n", msg);                                                              \
        exit(EXIT_FAILURE);                                                                        \
    } while (0)
#define SA struct sockaddr

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

/*// Function to add a packet to the buffer
void add_to_buffer(struct Packet packet) {
    if (buffer_count < BUFFER_SIZE) {
        buffer[buffer_count++] = packet;
    } else {
        // Buffer overflow, handle error or discard packet
        return;
    } // NOT IN FUNCTIONAL COND
}*/

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
/*
// Function to resend all packets in the buffer
void resend_packets(int sockfd) {
    for (int i = 0; i < buffer_count; i++) {
        send_packet_to_server(sockfd, &buffer[i]);
    }
}

// Function to handle NACK or timeout event
void handle_nack_or_timeout(int sockfd, int nack_number) {
    if (nack_number >= 0) {
        adjust_sliding_window(nack_number);
    } // if nack_number = -1, then assuming timeout
    resend_packets(sockfd);
}
*/
int fill_window_buffer(struct Packet *buffer, int *buffer_count, int winsz, FILE *file, int *seq_num, int mtu) {
    if (*buffer_count == winsz) {
        return -2; // Buffer already full
    }
    //printf("fill time\n");
    int packets_added = 0; // Track the number of packets added to the buffer
    for (int i = *buffer_count; i < winsz; i++) {
        // Create packet with sequence number and payload
        struct Packet packet;
        packet.sequence_number = *seq_num;
        packet.mtu = -1;

        // Read data from file into the payload
        size_t bytesRead = fread(packet.payload, 1, mtu, file);
        if (bytesRead < mtu) {
            // End of file reached
            printf("%zu < %d, STOPPIN at seqnum %d\n", bytesRead, mtu, packet.sequence_number);
            packet.mtu = bytesRead;
            buffer[i] = packet;
            packets_added++;
            (*buffer_count)++; // Increment buffer_count as packets are added
            return -1;
        }


        // Update sequence number for the next packet
        (*seq_num)++;

        // Add packet to the buffer
        buffer[i] = packet;
        packets_added++;
        (*buffer_count)++; // Increment buffer_count as packets are added
    }
    return packets_added;
}

void adjust_sliding_window(int ack_number, int *base_seq_num, int *window_end, int *buffer_count, struct Packet *buffer) {
    if (ack_number < *base_seq_num) {
        return; // Duplicate ack received
    }
    // Move the sliding window forward
    int increment = ack_number - *base_seq_num + 1;
    *base_seq_num += increment;
    *window_end += increment;
    printf("acked %d, baseseqnum now %d\n", ack_number, *base_seq_num);

    // Remove acknowledged packets from the buffer
    int i, j = 0;
    for (i = 0; i < *buffer_count; ++i) {
        if (buffer[i].sequence_number > ack_number) {
            // Shift remaining packets to the left to fill the gap
            memcpy(&buffer[j], &buffer[i], sizeof(struct Packet));
            j++;
        }
    }
    *buffer_count = j; // Update buffer count
}



void send_packet_to_server(int sockfd, struct sockaddr *servaddr, socklen_t servlen, const struct Packet *packet, FILE *file) {
    // Serialize the packet structure into a byte stream
    char buffer[sizeof(struct Packet)];
    memcpy(buffer, packet, sizeof(struct Packet));
    printf("sending\n");
    // Send the byte stream to the server
    ssize_t sent = sendto(sockfd, buffer, sizeof(struct Packet), 0, servaddr, servlen);
    if (sent != sizeof(buffer)) {
        fclose(file);
        perror("Error sending packet");
        exit(EXIT_FAILURE);
    }

    /*// Size of each field
    size_t size_of_is_nack = sizeof(packet->is_nack);
    size_t size_of_sequence_number = sizeof(packet->sequence_number);
    size_t size_of_payload = sizeof(packet->payload);
    size_t size_of_file_path = sizeof(packet->file_path);
    size_t size_of_mtu = sizeof(packet->mtu);

    // Total size of the structure
    size_t total_size_of_struct = sizeof(packet);

    // Print the sizes of individual fields
    printf("Size of is_nack: %zu bytes\n", size_of_is_nack);
    printf("Size of sequence_number: %zu bytes\n", size_of_sequence_number);
    printf("Size of payload: %zu bytes\n", size_of_payload);
    printf("Size of file_path: %zu bytes\n", size_of_file_path);
    printf("Size of mtu: %zu bytes\n", size_of_mtu);

    // Print the total size of the structure
    printf("Total size of struct Packet: %zu bytes\n", total_size_of_struct);

    // Check for padding
    size_t expected_total_size = size_of_is_nack + size_of_sequence_number + size_of_payload +
                                 size_of_file_path + size_of_mtu;
    if (total_size_of_struct != expected_total_size) {
        printf("Padding detected: Total size of struct Packet (%zu bytes) "
               "is larger than the sum of individual fields (%zu bytes).\n",
               total_size_of_struct, expected_total_size);
    } else {
        printf("No padding detected.\n");
    }*/
    printf("sent\n");
}

void dg_cli(int sockfd, const char *ip, int port, int mtu, int winsz, const char *in_path, const char *out_path, struct Packet *window_buffer, int buffer_count) {
    FILE *file = fopen(in_path, "rb");
    if (!file) {
        err_quit("Error opening file");
    }

    // Create and initialize the server address structure
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);

    char buffer[MAXLINE];

    // Set initial receive timeout
    struct timeval init_tv;
    init_tv.tv_sec = 60;  // 60 seconds timeout for initial connection
    init_tv.tv_usec = 0;

    // Set up socket options
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &init_tv, sizeof(init_tv)) < 0) {
        perror("Error setting socket options");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Allocate memory for reply address
    struct sockaddr *preply_addr = malloc(sizeof(struct sockaddr_in));
    if (!preply_addr) {
        perror("Error allocating memory for reply address");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    socklen_t servlen = sizeof(struct sockaddr_in);

    // Send the path name as the first message
    struct Packet path_packet;
    path_packet.is_nack = false;
    path_packet.sequence_number = 0;  // Assuming this is the initial sequence number
    strcpy(path_packet.payload, "");  // No payload for the path packet
    strcpy(path_packet.file_path, out_path); // Populate file path
    path_packet.mtu = mtu;  // Populate mtu field

    int window_end = winsz + 1; // Sequence number right after the window
    send_packet_to_server(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr), &path_packet, file);
    char *current_time = get_current_rfc3339_time();
    printf("%s, DATA, 0, 0, 1, %d\n", current_time, window_end);

    /*// Send the path packet
    ssize_t sent = sendto(sockfd, &path_packet, sizeof(path_packet), 0, (struct sockaddr *)&servaddr, servlen);
    if (sent != sizeof(path_packet)) {
        perror("Error sending path packet");
        fclose(file);
        free(preply_addr);
        exit(EXIT_FAILURE);
    }*/

    // Receive the echoed data with timeout
    //printf("now listen\n");
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, preply_addr, &servlen);
    struct Packet received_packet;
    int next_seq_num = 1; // Sequence number to be sent next

    if (n < 0) {
        if (errno == EWOULDBLOCK) {
            // Handle timeout for initial connection
            fprintf(stderr, "Cannot detect server within 60 seconds\n");
            fclose(file);
            free(preply_addr);
            exit(EXIT_FAILURE);
        } else {
            err_quit("recvfrom error");
        }
    } else if (n != sizeof(struct ACK_Packet)) {
            fprintf(stderr, "Received packet size mismatch. Quitting.\n");
            printf("Size of Packet: %zu\n", sizeof(struct ACK_Packet));
            printf("size of sent: %zu\n", sizeof(n));
            fclose(file);
            free(preply_addr);
            exit(EXIT_FAILURE);
            return; // Exit
    } else {
        // Packet received
        //printf("got first ack -> ");
        // Deserialize the byte stream back into the packet structure
        memcpy(&received_packet, buffer, sizeof(struct Packet));
        // Extract the sequence number from the received packet
        if (received_packet.sequence_number == 0) {
            char *current_time = get_current_rfc3339_time();
            printf("%s, ACK, 0, 1, 1, %d\n", current_time, window_end);
        } else {
            fprintf(stderr, "Wrong sequence number. Quitting.\n");
            fclose(file);
            free(preply_addr);
            exit(EXIT_FAILURE);
            return;
        }
    }

    // Reset timeout to 1 second for subsequent responses
    struct timeval tv;
    tv.tv_sec = 0;  // 0 second timeout for subsequent responses +
    tv.tv_usec = 2000;  // 2 milliseconds 
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Error setting socket options");
        fclose(file);
        free(preply_addr);
        exit(EXIT_FAILURE);
    }

    // Initialization code
    int seq_num = 1; // Current sequence number (sending)
    int base_seq_num = 1; // Base sequence number of the window (not ACKed)
    int acked_seq_num = 0; // Last acknowledged sequence number
    int timeout_count = 0; // Count for retransmission timeout
    int end_of_file = 0; // Flag to indicate end of file

    // Fill the window buffer with packets
    end_of_file = fill_window_buffer(window_buffer, &buffer_count, winsz, file, &seq_num, mtu);

    // Main loop to send data packets
    while (1) {

        if (end_of_file != -1) {
            next_seq_num = seq_num + 1;
        } else {
            next_seq_num = seq_num;
        }

        if (timeout_count > -1) {
            // Send packets in the window
            for (int i = 0; i < buffer_count; i++) {
                // Packet available to send
                send_packet_to_server(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr), &window_buffer[i], file);
                char *current_time = get_current_rfc3339_time();
                printf("%s, DATA, %d, %d, %d, %d\n", current_time, window_buffer[i].sequence_number, base_seq_num, next_seq_num, window_end);
            }
        }

        // Listen for responses (ACK or NACK)
        int ack_num = -1; // Initialize ack number
        struct Packet received_packet;
        int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, preply_addr, &servlen);
        if (n < 0) {
            if (errno == EWOULDBLOCK) {
                // Handle timeout, e.g., retransmit or other error handling
                fprintf(stderr, "No response detected\n");
                timeout_count++;
                if (timeout_count > MAX_RETRIES) {
                    fprintf(stderr, "Max retries reached. Exiting.\n");
                    break;
                } else if (timeout_count > MAX_RETRIES - 2) {
                    // Introduce a delay of 1,000,000 microseconds (1 second)
                    printf("\n\nwaiting a full second...\n\n");
                    usleep(1000000);
                } else if (timeout_count > 2) {
                    // Delay of 50,000 microseconds (.05 second)
                    usleep(50000);
                }
                continue; // Skip further processing and continue with the loop
            } else {
                err_quit("recvfrom error");
                // Handle other errors (e.g., continue to the next iteration or exit the loop)
                continue;
            }
        } else if (n != sizeof(struct ACK_Packet)) {
            //fprintf(stderr, "Received packet size mismatch. Ignoring.\n");
            continue; // Retry
        } else {
            // Packet received
            //printf("got packet -> ");
            // Deserialize the byte stream back into the packet structure
            memcpy(&received_packet, buffer, sizeof(struct Packet));
            timeout_count = -1; // Reset timeout count
        }

        if (!received_packet.is_nack) {
            // ACK received
            // Extract the sequence number from the received packet
            ack_num = received_packet.sequence_number;
            char *current_time = get_current_rfc3339_time();
            printf("%s, ACK, %d, %d, %d, %d\n", current_time, ack_num, base_seq_num, next_seq_num, window_end);
            adjust_sliding_window(ack_num, &base_seq_num, &window_end, &buffer_count, window_buffer);
            end_of_file = fill_window_buffer(window_buffer, &buffer_count, winsz, file, &seq_num, mtu);
        } else {
            // NACK received
            //printf("gotten nack\n");
            continue;
            /*// Adjust the sliding window based on the sequence number in the NACK packet
            adjust_sliding_window(ack_num, &base_seq_num, &window_end, &buffer_count, window_buffer);
            end_of_file = fill_window_buffer(window_buffer, &buffer_count, winsz, file, &seq_num, mtu);*/
        }

        // Check for end of file while all sent packets acked
        if (ack_num == seq_num && end_of_file == -1) {
            // All packets have been ACKed and end of file reached
            //printf("All packets ACKed and end of file reached. Exiting.\n");
            break;
        }

    }

    free(preply_addr);
    fclose(file);
}


int main(int argc, char **argv) {
    char buf[256]; // buffer to store the formatted string

    if (argc != 7) {
        snprintf(
            buf, sizeof(buf), "Usage: %s <ip> <port> <mtu> <winsz> <in_path> <out_path>", argv[0]);
        err_quit(buf);
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int mtu = atoi(argv[3]);
    int winsz = atoi(argv[4]);
    const char *in_path = argv[5];
    const char *out_path = argv[6];

    struct Packet window_buffer[winsz];
    int buffer_count = 0;

    /*
    // Print parsed components for verification
    printf("si: %s\n", server_ip);
    printf("p: %d\n", port);
    printf("mtu: %d\n", mtu);
    printf("inpath: %s\n", in_path);
    printf("outpath: %s\n", out_path);
    */

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
     if (sockfd < 0)
        err_quit("socket error");

    dg_cli(sockfd, ip, port, mtu, winsz, in_path, out_path, window_buffer, buffer_count);

    close(sockfd);

    return 0;

    exit(0);
}
