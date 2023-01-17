#include "list.h"
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Declaring a socket for connection between clients and server
int sockfd;

// Defining Constants 
#define MAX_CHARACTERS 4096
#define KEY 37

// Declaring our input and output lists
List *List1; // List for input
List *List2; // List for output

// Variables for Server information
static int local_port;
static int remote_port;
static char* remote_IP;

// Declaring the four threads
static pthread_t input_thread;
static pthread_t sender_thread;
static pthread_t receiver_thread;
static pthread_t output_thread;

// Declaring a fifth thread to check the online status
static pthread_t status_thread;

// Declaring the mutexes of the threads
static pthread_mutex_t mutex_input;
static pthread_mutex_t mutex_sender;
static pthread_mutex_t mutex_receiver;
static pthread_mutex_t mutex_output;
static pthread_mutex_t mutex_exit;
static pthread_mutex_t mutex_status;
static pthread_mutex_t mutex_online;

// Declaring the conditions of the threads
static pthread_cond_t input_cond;
static pthread_cond_t sender_cond;
static pthread_cond_t receiver_cond;
static pthread_cond_t output_cond;
static pthread_cond_t exit_cond;
static pthread_cond_t status_cond;
static pthread_cond_t online_cond;


// Function to encrypt the datagram
void encryption(char* datagram) {
	int len = strlen(datagram);
	if (len > MAX_CHARACTERS) {
		len = MAX_CHARACTERS;
	}
	
	for (int i = 0; i < len; i++) {
		datagram[i] = (datagram[i] + KEY) % 256;
	}
}

// Function to decrypt the datagram
void decryption(char* datagram) {
	int len = strlen(datagram);
	if (len > MAX_CHARACTERS) {
		len = MAX_CHARACTERS;
	}
	
	for (int i = 0; i < len; i++) {
		datagram[i] = datagram[i] - KEY + 256;
	}
}

// Function to read data from the input stream and,
// store it in the input list.
// This function is executed by the input thread. 
void *input() {
	while (1) {
		// Store input stream into buffer
		char keyboard_input[MAX_CHARACTERS];
		
		char *res = fgets(keyboard_input, sizeof(keyboard_input), stdin);
		if (res == NULL) { sleep(1000); exit(0); }
		// Null terminate the buffer and remove the \n
		keyboard_input[strcspn(keyboard_input, "\r\n")] = 0;
		
		char *message = (char *) malloc(MAX_CHARACTERS+1);
		strcpy(message, keyboard_input);
		
		// Checking if Lists are full
		if ((List_count(List2) + List_count(List1)) == LIST_MAX_NUM_NODES) {
			pthread_mutex_lock(&mutex_input);
			pthread_cond_wait(&input_cond, &mutex_input);
			pthread_mutex_unlock(&mutex_input);
		}
		
		// Add the input data from the buffer to the front of the input list
		List_prepend(List1, message);
		
		// Signal to the sender thread that the input list is no longer empty
		pthread_mutex_lock(&mutex_sender);
		pthread_cond_signal(&sender_cond);
		pthread_mutex_unlock(&mutex_sender);
	}
}

// Function to send the datagram in the input list to the other client.
// This function is executed by the sender thread.  
void *sender() {
	struct sockaddr_in soutRemote;
    soutRemote.sin_family = AF_INET;
    soutRemote.sin_addr.s_addr = inet_addr(remote_IP);
    soutRemote.sin_port = htons(remote_port);
	
	while (1) {
		char* send_message;
		
		// Checking if List1 is empty
		if (List_count(List1) == 0) {
			pthread_mutex_lock(&mutex_sender);
			pthread_cond_wait(&sender_cond, &mutex_sender);
			pthread_mutex_unlock(&mutex_sender);
		} 
		
		// Remove the datagram from the back of the input list AKA List1
		send_message = List_trim(List1);
		
		if (strcmp(send_message, "!status") == 0) {
			// Signal to the check status thread that the given command is "!status"
			pthread_mutex_lock(&mutex_status);
			pthread_cond_signal(&status_cond);
			pthread_mutex_unlock(&mutex_status);
		}
		
		bool isExiting = (strcmp(send_message, "!exit") == 0);
		encryption(send_message);
		
		if(sendto(sockfd, send_message, MAX_CHARACTERS, 0, (struct sockaddr *)&soutRemote, sizeof(soutRemote)) == -1){
            printf("Could not send the message");
			exit(EXIT_FAILURE);
        }   
		
		// Check if the datagram is the "!exit" command.
		// If it is, terminate cleanly. 
		if (isExiting) {
			break;
		}

		free(send_message);
		
		// Signal to the input thread that the input list is no longer full
		pthread_mutex_lock(&mutex_input);
		pthread_cond_signal(&input_cond);
		pthread_mutex_unlock(&mutex_input);
	}
	
	pthread_mutex_lock(&mutex_exit);
	pthread_cond_signal(&exit_cond);
	pthread_mutex_unlock(&mutex_exit);
	pthread_exit(0);
}

// Function to move the datagram from the input list to the output list, 
// effectively removing the datagram from input list.
// Function also receives datagram from the other client. 
// This function is executed by the receiver thread.
void *receive() {
	struct sockaddr_in sin_remote;
    unsigned int sin_len = sizeof(sin_remote);
	
	while (1) {
		char received_buffer[MAX_CHARACTERS+1];
		int bytes = recvfrom(sockfd, &received_buffer, MAX_CHARACTERS, 0, (struct sockaddr *) &sin_remote, &sin_len);
		
		// Check for an error
        if(bytes < 0){
            printf("Error in receiving!\n");
            exit(EXIT_FAILURE);
        }
		
		decryption(received_buffer);
		char *message = (char *) malloc(bytes+1);
		strcpy(message, received_buffer);

        // NULL terminate the message received
        received_buffer[bytes] = '\0';
		
		// Checking if Lists are full
		if ((List_count(List2) + List_count(List1)) == LIST_MAX_NUM_NODES) {
			pthread_mutex_lock(&mutex_receiver);
			pthread_cond_wait(&receiver_cond, &mutex_receiver);
			pthread_mutex_unlock(&mutex_receiver);
		}
		
		// Add the datagram to the front of the output list AKA List2
		List_prepend(List2, message); 
		
		// Signal to the check status thread that the other terminal is online
		pthread_mutex_lock(&mutex_online);
		pthread_cond_signal(&online_cond);
		pthread_mutex_unlock(&mutex_online);
		
		// Signal to the output thread that the output list is no longer empty
		pthread_mutex_lock(&mutex_output);
		pthread_cond_signal(&output_cond);
		pthread_mutex_unlock(&mutex_output);
	}
}

// Function to print the input of the user.
// Function is executed by the output thread.
void *print() {
	while (1) {
		char* screen_output;

		// Checking if List2 is empty
		if (List_count(List2) == 0) {
			pthread_mutex_lock(&mutex_output);
			pthread_cond_wait(&output_cond, &mutex_output);
			pthread_mutex_unlock(&mutex_output);
		}
		
		// Remove the datagram from the back of the output list AKA List2
		screen_output = List_trim(List2);
		
		// Check if the datagram is the "!exit" command.
		// If it is, terminate cleanly. 
		if (strcmp(screen_output, "!exit") == 0) {
			break;
		}
		
		if (strcmp(screen_output, "!status") == 0) {
			char *message = (char *) malloc(MAX_CHARACTERS+1);
			strcpy(message, "Online");
			List_prepend(List1, message);
			// Signal to the sender thread 
			pthread_mutex_lock(&mutex_sender);
			pthread_cond_signal(&sender_cond);
			pthread_mutex_unlock(&mutex_sender);
		}
		
		// Print the datagram
		printf("%s\n", screen_output);
		fflush(stdout);
		free(screen_output);

		// Signal to the receiver thread that the output list is no longer full
		pthread_mutex_lock(&mutex_receiver);
		pthread_cond_signal(&receiver_cond);
		pthread_mutex_unlock(&mutex_receiver);
		
		pthread_mutex_lock(&mutex_input);
		pthread_cond_signal(&input_cond);
		pthread_mutex_unlock(&mutex_input);
	}	

	pthread_mutex_lock(&mutex_exit);
	pthread_cond_signal(&exit_cond);
	pthread_mutex_unlock(&mutex_exit);
	pthread_exit(0);
}

void *check_online_status() {
	while(1) {
		pthread_mutex_lock(&mutex_status);
		pthread_cond_wait(&status_cond, &mutex_status);
		pthread_mutex_unlock(&mutex_status);

		struct timespec waiting_time;
		clock_gettime(CLOCK_REALTIME, &waiting_time);
		waiting_time.tv_sec += 5;
		
		pthread_mutex_lock(&mutex_online);
		int n = pthread_cond_timedwait(&online_cond, &mutex_online, &waiting_time);
		pthread_mutex_unlock(&mutex_online);
		
		if (n == 0) {
			// Signal. Do nothing.
		} else if (n == ETIMEDOUT) {
			char *message = (char *) malloc(MAX_CHARACTERS);
			strcpy(message, "Offline");
			List_prepend(List2, message);
			// Signal to the output thread that the output list is no longer empty
			pthread_mutex_lock(&mutex_output);
			pthread_cond_signal(&output_cond);
			pthread_mutex_unlock(&mutex_output);
		}		
	}
}

void Server_setup() {
	// Create the Socket and check for failure
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("Something went wrong with the creation of the socket.");
		exit(EXIT_FAILURE);
	}
	
	// Set up the Server
	struct sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address)); 
	
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(local_port);
	server_address.sin_addr.s_addr = INADDR_ANY;
	
	// Bind the socket to the Server and check for failure
	if ((bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address))) < 0) {
		perror("Something went wrong witht the binding.");
		exit(EXIT_FAILURE);
	}
}

// Helping Source used to write this function: https://linuxhint.com/c-getaddrinfo-function-usage/#:~:text=%E2%80%9Cgetaddrinfo%2C%E2%80%9D%20as%20the%20name,linked%20list%20of%20addrinfo%20structures.
// Function to convert the machine name to its IP address
void convert_machine_name_to_IP(char *machine_name, char *remote_port) {
	struct addrinfo hints; 
    struct addrinfo *result, *rp;
    struct sockaddr_in *socket_info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
    
    //Check for an error
	if (getaddrinfo(machine_name, remote_port, &hints, &result) != 0) {
		perror("Wrong IP address or port number!\n");
        exit(EXIT_FAILURE);
	}
	
    //Iterating through to get the correct IP address with the hostname provided
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		if (rp->ai_addr->sa_family == AF_INET) {
			socket_info = (struct sockaddr_in *)rp->ai_addr;
			remote_IP = inet_ntoa(socket_info->sin_addr);
			freeaddrinfo(result);
			return;
		}
	}
    printf("error");
    exit(EXIT_FAILURE);
    return;
}

void printUssageMessage(){
	
	printf("%s \n\t %s \n %s \n\t %s\n\t %s \n","Usage:","./lets-talk [my port number] [remote/local machine IP] [remote/local port number]",
	"Examples:", "./les-talk 3000 192.168.0.513 3001", "./les-talk 3000 some-computer-name 3001");

}

int main(int argc, char **argv) {
	if (argc < 4) {
		printUssageMessage();
		return 1;
	}
	// Initialize the program arguments
	local_port = atoi(argv[1]);
	remote_port = atoi(argv[3]);
	printf("Welcome to letS-Talk! Please type your messages now.\n");

	// Convert Machine Name to IP address
	convert_machine_name_to_IP(argv[2], argv[3]);
	
	// Set up the socket
	Server_setup();
	
	// Create the lists
	List1 = List_create();
	List2 = List_create();
	
	// Initialize the mutexes and the conditions of the threads
	pthread_mutex_init(&mutex_input, NULL);
	pthread_mutex_init(&mutex_sender, NULL);
	pthread_mutex_init(&mutex_receiver, NULL);
	pthread_mutex_init(&mutex_output, NULL);
	pthread_mutex_init(&mutex_exit, NULL);
	pthread_mutex_init(&mutex_status, NULL);
	pthread_cond_init(&input_cond, NULL);
	pthread_cond_init(&sender_cond, NULL);
	pthread_cond_init(&receiver_cond, NULL);
	pthread_cond_init(&output_cond, NULL);
	pthread_cond_init(&exit_cond, NULL);
	pthread_cond_init(&status_cond, NULL);
	
	// Create Threads
	pthread_create(&input_thread, NULL, input, NULL);
	pthread_create(&sender_thread, NULL, sender, NULL);
	pthread_create(&receiver_thread, NULL, receive, NULL);
	pthread_create(&output_thread, NULL, print, NULL);
	pthread_create(&status_thread, NULL, check_online_status, NULL);
	
	// Wait for the four threads to complete execution and terminate
	// all threads if "!exit" is typed.
	// One option is with using pthread_join for every one of the threads
	// The other option is presented below.
	
	pthread_mutex_lock(&mutex_exit);
	pthread_cond_wait(&exit_cond, &mutex_exit);
	pthread_mutex_unlock(&mutex_exit);
	
	// Now Terminate the threads			
	pthread_cancel(output_thread);
	pthread_join(output_thread, NULL);
	pthread_cancel(input_thread);
	pthread_join(input_thread, NULL);
	pthread_cancel(sender_thread);
	pthread_join(sender_thread, NULL);
	pthread_cancel(receiver_thread);
	pthread_join(receiver_thread, NULL);
	pthread_cancel(status_thread);
	pthread_join(status_thread, NULL);
	// Empty both lists
	List_free(List1, NULL);
	List_free(List2, NULL);
	
	// Destroy the mutexes and conditions of the threads
	pthread_mutex_destroy(&mutex_input);
	pthread_mutex_destroy(&mutex_sender);
	pthread_mutex_destroy(&mutex_receiver);
	pthread_mutex_destroy(&mutex_output);
	pthread_mutex_destroy(&mutex_exit);
	pthread_cond_destroy(&input_cond);
	pthread_cond_destroy(&sender_cond);
	pthread_cond_destroy(&receiver_cond);
	pthread_cond_destroy(&output_cond);
	pthread_cond_destroy(&exit_cond);
	pthread_cond_destroy(&status_cond);
	close(sockfd);
	
	return 0;
}
