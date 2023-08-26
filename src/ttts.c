#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>


#define CODE_COUNT 9
#define QUEUE_SIZE 10

#define NONE 2
#define TRUE 1
#define FALSE 0
#define BUFFER_SIZE 1024

enum codes {WAIT, BEGN, MOVD, INVL, DRAW, OVER, PLAY, MOVE, RSGN};
char *code_strings[] = {"WAIT", "BEGN", "MOVD", "INVL", "DRAW", "OVER", "PLAY", "MOVE", "RSGN"};
int code_bars[] = {0, 2, 3, 1, 1, 2, 1, 2, 0};

// Structs

// Helper for Accepting Connections
struct connection_data {
    struct sockaddr_storage address;
    socklen_t address_len;
    int fd;
    int last_char;
    char *name;
    int isActive;
    int isHead;
    char *buffer;
    struct connection_data *next;
};
typedef struct connection_data Client;

// Represents arguments to pass to game function
struct game_args {
    Client *a;
    Client *b;
};

struct message_t {
    int type;
    int argc;
    char **argv;
};
typedef struct message_t Message;

pthread_mutex_t mutex;
int total_connections = 0;
Client *head = NULL;

// Function Headers
int init_listener(char *port, int queue_size);
void *run_game(void *args);
void write_to_client(int fd, char *msg, int free_after);
void write_to_players(int a_fd, int b_fd, char *msg, int free_after);
Message *receive_and_validate(Client *client);
int validate_name(char *name);
void package_and_send(char *code, char *arg1, char *arg2, char *arg3, int target_one, int target_two);
void freeMessage(Message *msg);
int validate_move(char *board, char symbol, char *move);
int validate_response(int last, int current);
int get_packet_type(char *name);
int check_if_win(char *board, char symbol);

// Function Definitions

// Initialize the Listener - code from echoservt.c
int init_listener(char *port, int queue_size) {
    struct addrinfo hint, *info_list, *info;
    int error, socket_fd;

    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE;

    error = getaddrinfo(NULL, port, &hint, &info_list);
    if (error == 1) {
        perror("Get addr info\n");
        return -1;
    }

    for (info = info_list; info != NULL; info = info->ai_next) {

        // Try to create socket, move on to next if fail
        socket_fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        
        // Try to bind socket, move on to next if fail
        error = bind(socket_fd, info->ai_addr, info->ai_addrlen);
        if (error) {
            close(socket_fd);
            continue;
        }

        // Try to begin listening, move on to next if fail
        error = listen(socket_fd, queue_size);
        if (error) {
            close(socket_fd);
            continue;
        }

        // Everything succeeded, so break out of loop
        break;
    }

    freeaddrinfo(info_list);
    if (info == NULL) {
        perror("[-] Could not begin listening");
        return -1;
    }

    return socket_fd;
}

// Adds a player to the current list
void addPlayer(Client **head, Client *player) {
    Client *curr = *head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = player;
}

// Sets the status of the provided players to false
void remove_player(Client **head, char *a) {
    Client *ptr = (*head)->next;
    Client *prev = *head;
    while (ptr != NULL) {
        if (strcmp(ptr->name, a) == 0) {
            prev->next = ptr->next;
            return;
        }
        prev = ptr;
        ptr = ptr->next;
    }

}

// Worker Thread Function - Handles all game activity
void *run_game(void *args) {
    struct game_args *info = (struct game_args *) args;
    Client *a = info->a;
    Client *b = info->b;
    Client *current = a;
    Message *msg;
    char board[9] = ".........";
    long id = pthread_self();
    int active = TRUE, recv_size, valid_move, bytes, capacity, total, valid_response;
    char *symbol;
    char buffer[BUFFER_SIZE];
    char *to_client;
    int spots_taken = 0;
    int last_message = BEGN;
    

    package_and_send("BEGN", "X", b->name, NULL, a->fd, -1);
    package_and_send("BEGN", "O", a->name, NULL, b->fd, -1);


    // Run Game
    while (active) {

        msg = receive_and_validate(current);
        if (msg == NULL) {
            perror("Invalid message");
            package_and_send("OVER", "D", "Game terminated due to malformed message or disconnect.", NULL, a->fd, b->fd);
            break;
        }

        if (!validate_response(last_message, msg->type)) {
            package_and_send("INVL", "Invalid response to the last message", NULL, NULL, current->fd, -1);
            continue;
        }

        switch (msg->type) {
            
            case MOVE:
                valid_move = validate_move(board, *(msg->argv[0]), msg->argv[1]);

                // If move was invalid, send invalid
                if (!valid_move) {
                    package_and_send("INVL", "That stop is occupied or out of bounds.", NULL, NULL, current->fd, -1);
                    last_message = INVL;
                    continue;
                }
                
                // If move was valid, increment number of spots taken
                spots_taken++;

                // If player won, send game over
                if (check_if_win(board, *(msg->argv[0]))) {
                    package_and_send("OVER", "W", "Got 3 in a row", NULL, current->fd, -1);
                    package_and_send("OVER", "L", "Opponent got 3 in a row", NULL, current->fd == a->fd ? b->fd : a->fd, -1);
                    active = FALSE;
                }

                // Else, send over updated board
                else {
                    package_and_send("MOVD", msg->argv[0], msg->argv[1], board, a->fd, b->fd);
                    if (spots_taken == 9) {
                        package_and_send("OVER", "D", "All spots on the board have been filled", NULL, a->fd, b->fd);
                        active = FALSE;
                    }
                    last_message = MOVD;
                }

                break;
            
            case RSGN:
                package_and_send("OVER", "L", "You resigned from the match", NULL, current->fd, -1);
                package_and_send("OVER", "W", "Your opponent resigned from the match.", NULL, current->fd == a->fd ? b->fd : a->fd, -1);
                active = FALSE;
                break;
            
            case DRAW:

                // Current message is a RESPONSE to draw
                if (last_message == DRAW) {
                    if (strcmp(msg->argv[0], "A") == 0) {
                        package_and_send("OVER", "D", "Both players have agreed to a draw.", NULL, a->fd, b->fd);
                        active = FALSE;
                    }
                    else if (strcmp(msg->argv[0], "R") == 0) {
                        package_and_send("DRAW", "R", NULL, NULL, current->fd == a->fd ? b->fd : a->fd, -1);
                        last_message = BEGN; // even though this isn't true, it resets what a proper response is
                    }
                    else {
                        package_and_send("INVL", "R and A are the only valid arguments for a response to DRAW", NULL, NULL, current->fd, -1);
                        last_message = INVL;
                    }
                }
                else {
                    if (strcmp(msg->argv[0], "S") == 0) {
                        package_and_send("DRAW", "S", NULL, NULL, current->fd == a->fd ? b->fd : a->fd, -1);
                        last_message = DRAW;
                    }
                    else {
                        package_and_send("INVL", "You must only send S when suggesting a draw", NULL, NULL, current->fd, -1);
                        continue;
                    }
                }
                break;

            default:
                package_and_send("INVL", "Received message was not a valid client message", NULL, NULL, current->fd, -1);
                continue;

        }
        current = current->fd == a->fd ? b : a;
        //freeMessage(msg);
                
    }

    pthread_mutex_lock(&mutex);
    remove_player(&head, a->name);
    remove_player(&head, b->name);
    pthread_mutex_unlock(&mutex);

    // here is where we should remove their names from the list
    close(a->fd);
    close(b->fd);
}

// Write to one client
void write_to_client(int fd, char *msg, int free_after) {
    int len = strlen(msg);
    write(fd, msg, len);
    if (free_after)
        free(msg);
}

// Write to both players - two calls to write_to_client
void write_to_players(int a_fd, int b_fd, char *msg, int free_after) {
    write_to_client(a_fd, msg, FALSE);
    write_to_client(b_fd, msg, free_after);
}

// Receives message from client, verifies it is a valid protocol message, and packages it into a message object
Message *receive_and_validate(Client *client) {
    
    // Client Info
    int fd = client->fd;
    int start;
    int end = client->last_char;

    char curr;
    char *buffer = client->buffer;
    char *delim_pos;
    char *tok;
    char packet_name[5];

    int bars_needed;
    int bars_found = 0;
    int len;
    int code_type;
    int bytes = 0;
    int leftover_bytes = 0;
    int complete = FALSE;
    int found_header = FALSE;
    int chars_read = 0;

    Message *msg;
    int argc;
    char **argv;

    // Reads until we have chars to read the type of message and the length
    start = 0;
    while (TRUE) {
        for (int i = start; i < end + bytes; i++) {
            curr = buffer[i];
            if (buffer[i] == '|') {
                if (++bars_found == 2) {
                    found_header = TRUE;
                    start = i + 1;
                    end += bytes;
                    
                    break;
                }
            }
        }

        if (found_header) {
            break;
        }
        start = end + bytes;
        end += bytes;
        bytes = read(fd, buffer + end, BUFFER_SIZE - end);
        if (bytes <= 0) {
            return NULL;
        }
    }

    // Determine the type of message and validate it
    strncpy(packet_name, buffer, sizeof(int));
    packet_name[4] = '\0';
    if ((code_type = get_packet_type(packet_name)) == -1) {
        perror("Invalid packet type");
        return NULL;
    }
    bars_needed = code_bars[code_type];

    // Try and scan in the length field
    if (sscanf(buffer + 5, "%d", &len) < 0) {
        perror("Malformed length field");
        return NULL;
    }
    if (len == 0) {
        complete = TRUE;
    }
    if (len > 255) {
        perror("Length of message too long");
        return NULL;
    }

    // Validate that the # of bars matches what's expect and the # of chars match
    bars_found = 0;
    bytes = 0;
    while (TRUE) {
        for (int i = start; i < end + bytes; i++) {
            curr = buffer[i];
            chars_read++;
            if (curr == '|') {
                bars_found++;
                if (bars_found == bars_needed) {
                    if (chars_read == len) {
                        complete = TRUE;
                        leftover_bytes = (end + bytes) - (i + 1);
                        end = i + 1;
                        break;
                    }
                }
            }
        }
        
        // If you're still expecting chars, read again
        if (chars_read < len) {
            start = end + bytes;
            end += bytes;
            bytes = read(fd, buffer + end, BUFFER_SIZE - end);
            if (bytes == 0) {
                perror("Client Disconnected");
                return NULL;
            }
        }

        // If the message is finished, break
        else if (complete) {
            break;
        }

        // If either, invalid
        else {
            perror("Invalid or malformed message received");
            return NULL;
        }
    }

    msg = malloc(sizeof(Message));
    argc = bars_needed;
    argv = malloc(argc * sizeof(char *));

    tok = strtok(buffer + 5, "|");
    for (int i = 0; i < argc; i++) {
        tok = strtok(NULL, "|");
        argv[i] = malloc(strlen(tok) * sizeof(char));
        strcpy(argv[i], tok);
    }
    msg->argc = argc;
    msg->argv = argv;
    msg->type = code_type;
    memset(buffer, 0, end);
    memmove(buffer, buffer + end, leftover_bytes);
    memset(buffer + leftover_bytes , 0, leftover_bytes);
    client->last_char = leftover_bytes;

    return msg;
    
}

// Checks the names of all currently active players to validate uniqueness
int validate_name(char *name) {
    Client *ptr = head;
    while (ptr != NULL) {
        if (!ptr->isHead && ptr->isActive) {
            if (strcmp(name, ptr->name) == 0) {
                return FALSE;
            } 
        }
        ptr = ptr->next;
    }
    return TRUE;
}

// Formats Messages for Network Communication
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
    if (target_two != -1) {
        write_to_players(target_one, target_two, res, TRUE);
    }
    else {
        write_to_client(target_one, res, TRUE);
    }
}


// Frees message object
void freeMessage(Message *msg) {
    for (int i = 0; i < msg->argc; i++) {
        free(msg->argv[i]);
    }
    free(msg->argv);
    free(msg);
}


// Validates that the last made move to the TTT Board is allowed
int validate_move(char *board, char symbol, char *move) {
    
    // Validate Length
    if (strlen(move) != 3)
        return FALSE;
    
    // Validate move is between 1-3
    if ((move[0] <= '0' || move[0] >= '4') || (move[2] <= '0' || move[2] >= '4'))
        return FALSE;
    
    int r, c;
    r = move[0] - '0' - 1;
    c = move[2] - '0' - 1;

    if (board[r * 3 + c] != '.')
        return FALSE;
    
    board[r * 3 + c] = symbol;
    return TRUE;
}

// Ensures that the response to any protocol message is correct
int validate_response(int last, int current) {

    if (last == BEGN || last == MOVD) {
        if (current == MOVE || current == RSGN || current == DRAW) {
            return TRUE;
        }
        return FALSE;
    }
    else if (last == DRAW) {
        if (current == DRAW) {
            return TRUE;
        }
        return FALSE;
    }
    else if (last == INVL) {
        return TRUE;
    }
    return FALSE;

}

// Checks if the current state of game results in a win
int check_if_win(char *board, char symbol) {
    if (board[0] == symbol && board[1] == symbol && board[2] == symbol) return TRUE;
    if (board[3] == symbol && board[4] == symbol && board[5] == symbol) return TRUE;
    if (board[6] == symbol && board[7] == symbol && board[8] == symbol) return TRUE;
    if (board[0] == symbol && board[3] == symbol && board[6] == symbol) return TRUE;
    if (board[1] == symbol && board[4] == symbol && board[7] == symbol) return TRUE;
    if (board[2] == symbol && board[5] == symbol && board[8] == symbol) return TRUE;
    if (board[0] == symbol && board[4] == symbol && board[8] == symbol) return TRUE;
    if (board[2] == symbol && board[4] == symbol && board[6] == symbol)  return TRUE;
    return FALSE;
}

// Compares first 4 chars of protcol message to each type of packet
int get_packet_type(char *name) {
    for (int i = 0; i < CODE_COUNT; i++) {
        if (strcmp(name, code_strings[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv) {

    signal(SIGPIPE, SIG_IGN);
    signal(EPIPE, SIG_IGN);

    int listener, error, num_connections, send_len, recv_len, valid_name;    
    int *client_fds;                                                                    // Array to hold the two file descriptors of clients
    struct game_args *args;                                                             // Argument Struct to pass to thread
    char *port;                                                    // Buffer and Port
    Message *username;                                                                     // Holds username
    struct connection_data *client;                                                     // Connection Info

    // Initialize Dummy Head
    head = malloc(sizeof(Client));
    head->fd = -1;
    head->last_char = 0;
    head->name = NULL;
    head->isActive = FALSE;
    head->isHead = TRUE;
    head->next = NULL;

    // Initialize Mutex for Syncronization
    pthread_mutex_init(&mutex, NULL);

    // Initialize Port, array to store FDs of current game, and arguments to that game
    port = argc >= 2 ? argv[1] : "5000";
    client_fds = malloc(2 * sizeof(int));
    args = malloc(sizeof(struct game_args));
    num_connections = 0;

    // Listener will now hold the server's file descriptor
    listener = init_listener(port, QUEUE_SIZE);
    if (listener < 0) {
        perror("Listener");
        exit(1);
    }
    // Accept / Verify Connection and PLAY Message loop
    while(TRUE) {
        // Accept Connection
        client = malloc(sizeof(struct connection_data));
        client->address_len = sizeof(struct sockaddr_storage);
        client->fd = accept(listener, (struct sockaddr *)&client->address, &client->address_len);
        if (client->fd < 0) {
            perror("Failed to accept connection");
            free(client);
            continue;
        }
        client->isActive = TRUE;
        client->next = NULL;    
        client->isHead = FALSE;
        client->buffer = calloc(BUFFER_SIZE, sizeof(char));
        client->last_char = 0;
        username = receive_and_validate(client);
        if (username == NULL) {
            close(client->fd);
            package_and_send("INVL", "The message you sent was invalid", NULL, NULL, client->fd, -1);
            free(client->buffer);
            free(client);
            continue;
        }

        if (username->type != PLAY) {
            package_and_send("INVL", "You must send PLAY before connecting to a game.", NULL, NULL, client->fd, -1);
            close(client->fd);
            free(client->buffer);
            free(client);
            continue;
        }

        // Validate Name
        pthread_mutex_lock(&mutex);
        valid_name = validate_name(username->argv[0]);
        pthread_mutex_unlock(&mutex);

        if (!valid_name) {
            package_and_send("INVL", "That name is already in use", NULL, NULL, client->fd, -1);
            free(client);
            close(client->fd);
            continue;
        }

        // Log connection to STDOUT and send WAIT to client
        package_and_send("WAIT", NULL, NULL, NULL, client->fd, -1);

        client_fds[num_connections++] = client->fd;
        client->name = calloc(strlen(username->argv[0]) + 1, sizeof(char));
        strncpy(client->name, username->argv[0], strlen(username->argv[0]));

        // Add the new player
        pthread_mutex_lock(&mutex);
        addPlayer(&head, client);
        pthread_mutex_unlock(&mutex);


        strcpy(client->name, username->argv[0]);

        // If 2 Clients are connected, launch game
        if (num_connections == 2) {
            args->b = client;
            pthread_t id;
            error = pthread_create(&id, NULL, run_game, (void *)args);
            if (error != 0) {
                perror("Thread Creation Error");
                exit(1);
            }
            pthread_detach(id);
            client_fds = malloc(2 * sizeof(int));
            num_connections = 0;
        }
        else {
            args->a = client;
        }
    }
    return EXIT_SUCCESS;
}
