#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#define PORT 5000
#define BUFFER_SIZE 512
#define MAX_CONENCTIONS 200
#define TRUE 1
#define FALSE 2
#define CODE_COUNT 9

enum codes {WAIT, BEGN, MOVD, INVL, DRAW, OVER, PLAY, MOVE, RSGN};
char *code_strings[] = {"WAIT", "BEGN", "MOVD", "INVL", "DRAW", "OVER", "PLAY", "MOVE", "RSGN"};
int code_bars[] = {0, 2, 3, 1, 1, 2, 1, 2, 0};

void write_to_client(int fd, char *msg, int free_after);
void write_to_players(int a_fd, int b_fd, char *msg, int free_after);

struct sockaddr_in *createIPAddress(char *ip, unsigned int port);

struct sockaddr_in *createIPAddress(char *ip, unsigned int port) {
    struct sockaddr_in *address = malloc(sizeof(struct sockaddr_in));
    memset(address, '\0', sizeof(*address));
    address->sin_port = htons(port);
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = inet_addr(ip);
    return address;
}

struct read_args {
    int fd;
};

void *read_and_print(void *args) {
    struct read_args *info = (struct read_args *)args;
    int fd = info->fd;
    int bytes = 0;

    char buffer[BUFFER_SIZE];

    while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
        printf("<-%s\n", buffer);
        memset(buffer, 0, BUFFER_SIZE);
    }
}
// Write to both players - two calls to write_to_client
void write_to_players(int a_fd, int b_fd, char *msg, int free_after) {
    write_to_client(a_fd, msg, FALSE);
    write_to_client(b_fd, msg, free_after);
}

// Write to one client
void write_to_client(int fd, char *msg, int free_after) {
    int len = strlen(msg);

    write(fd, msg, len);
    
    //write(fd, msg, len);
    if (free_after)
        free(msg);
}

void package_and_send(char *code, char *arg1, char *arg2, char *arg3, int target_one, int target_two) {
    int code_idx;
    int len = 5, chars_left;
    char *res;
    char int_to_char[10];
    char *args[] = {arg1, arg2, arg3};
    for (code_idx = 0; code_idx < CODE_COUNT; code_idx++) {
        if (strcmp(code, code_strings[code_idx]) == 0)
            break;
    }

    for (int i = 0; i < code_bars[code_idx]; i++) {
        len += strlen(args[i]) + 1;
    }
    sprintf(int_to_char, "%d", len - 5);
    len += strlen(int_to_char);
    res = calloc(len, sizeof(char));
    strcpy(res, code_strings[code_idx]);
    strcat(res, "|");
    strcat(res, int_to_char);
    strcat(res, "|");
    for (int i = 0; i < code_bars[code_idx]; i++) {
        strcat(res, args[i]);
        strcat(res, "|");
    }

    printf("%s ->\n", res);
    if (target_two != -1) {
        write_to_players(target_one, target_two, res, TRUE);
    }
    else {
        write_to_client(target_one, res, TRUE);
    }
}


int main(int argc, char **argv) {
    int socket_fd, connection_result, recv_result, msg_len, bytes, stop = 0;
    struct sockaddr_in *address;
    char *ip, buffer[BUFFER_SIZE], *msg;

    ip = "127.0.0.1";
    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    address = createIPAddress(ip, PORT);
    char move[4];
    char draw[5];
    char username[100];

    if (socket_fd < 0) {
        printf("[-] Failed to create socket\n");
        exit(1);
    }

    connection_result = connect(socket_fd, (struct sockaddr *)address, sizeof(*address));
    if (connection_result < 0) {
        printf("[-] Failed to establish a connection\n");
        exit(1);
    }

    /*
    CLIENT USAGE:
        When you call the client, do ./ttt SYMBOL (on the client that's gonna be X, do X, else O)
            - ./ttt "X"
        
        The client will then create a thread that constantly takes in and sends whatever the server sends you
        Now, in order to send commands to the server, they must be sent in the following format
            For PLAY
                "PLAY Name"
            
            For MOVE
                "MOVE 2,2", "MOVE 1,2", etc
            
            For RSGN
                "RSGN"
            
            For DRAW
                "DRAW R" or "DRAW S" or "DRAW A"
        
        Using the package_and_send function, it will send the correctly formatted message to server
    */
    struct read_args *a = malloc(sizeof(struct read_args));
    pthread_t id;
    a->fd = socket_fd;
    pthread_create(&id, NULL, read_and_print, (void*)a);
    pthread_detach(id);
    
    
    while (TRUE) {
        bytes = read(STDIN_FILENO, buffer, BUFFER_SIZE);
        if (bytes == 0) {
            break;
        }
        buffer[bytes - 1] = '\0';

        if (strncmp(buffer, "MOVE", 4) == 0) {
            strncpy(move, buffer + 5, sizeof(move));
            package_and_send("MOVE", argv[1], move, NULL, socket_fd, -1);
        }

        else if (strncmp(buffer, "RSGN", 4) == 0) {
            package_and_send("RSGN", NULL, NULL, NULL, socket_fd, -1);
        }
        else if (strncmp(buffer, "DRAW", 4) == 0) {
            strncpy(draw, buffer + 5, sizeof(draw));
            package_and_send("DRAW", draw, NULL, NULL, socket_fd, -1);
        }

        else if (strncmp(buffer, "PLAY", 4) == 0) {
            strncpy(username, buffer + 5, sizeof(username));
            package_and_send("PLAY", username, NULL, NULL, socket_fd, -1);
        }
        memset(buffer, 0, BUFFER_SIZE);
    }


    

}
