//
// Created by 尾崎耀一 on 2019-01-13.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include "myftp.h"

#define COMMANDLINE_LEN         1024
#define COMMANDLINE_MAX_ARGC    128
#define DELIM                   " \t\n"

void print_welcome();
void print_help();

void dump_message(struct myftph *);
void dump_data_message(struct myftph_data *, int);

int
main(int argc, char *argv[])
{
    char *server_host_name;
    struct sockaddr_in server_socket_address;
    int client_socket;
    unsigned int **addrptr;
    int connected = 0;

    if (argc != 2) {
        fprintf(stderr, "%s: Usage: %s <server host name>\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }
    server_host_name = argv[1];

    print_welcome();

    // create client's socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // prepare server_socket_address and connect to server
    server_socket_address.sin_family = AF_INET;
    server_socket_address.sin_port = htons(MYFTP_PORT);
    server_socket_address.sin_addr.s_addr = inet_addr(server_host_name);
    if (server_socket_address.sin_addr.s_addr == 0xffffffff) {
        struct hostent *host;
        host = gethostbyname(server_host_name);
        if (host == NULL) {
            perror("gethostbyname");
            exit(EXIT_FAILURE);
        }
        addrptr = (unsigned int **)host->h_addr_list;
        while (*addrptr != NULL) {
            server_socket_address.sin_addr.s_addr = *(*addrptr);
            connected = connect(client_socket, (struct sockaddr *)&server_socket_address, sizeof(struct sockaddr_in));
            if (connected == 0) {
                break;
            }
            addrptr++;
        }
    } else {
        if (connect(client_socket, (struct sockaddr *)&server_socket_address, sizeof(struct sockaddr_in)) != 0) {
            perror("connect");
            exit(EXIT_FAILURE);
        }
    }
    if (connected < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    char current_dir[SIZE];

    for (;;) {
        char commandline[COMMANDLINE_LEN];
        char *commandline_argv[COMMANDLINE_MAX_ARGC];
        memset(commandline, 0, sizeof(commandline));
        fprintf(stdout, "[local@%s]\n", getcwd(current_dir, SIZE));
        fprintf(stdout, "myftp%% ");
        if (fgets(commandline, COMMANDLINE_LEN, stdin) == NULL) {
            fprintf(stderr, "Bye\n");
            exit(EXIT_SUCCESS);
        }
        char *cp = commandline;
        int commandline_argc;
        for (commandline_argc = 0; commandline_argc < COMMANDLINE_MAX_ARGC; commandline_argc++) {
            if ((commandline_argv[commandline_argc] = strtok(cp, DELIM)) == NULL) {
                break;
            }
            cp = NULL;
        }

        if (commandline_argv[0] == NULL) {
            continue;
        }

        // QUIT command
        if (strcmp(commandline_argv[0], "quit") == 0) {
            // fprintf(stderr, "[DEBUG] QUIT\n");
            if (commandline_argc != 1) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            struct myftph quit_message;
            memset(&quit_message, 0, sizeof(quit_message));
            quit_message.Type = TYPE_QUIT;
            if (send(client_socket, &quit_message, sizeof(quit_message), 0) < 0) {
                perror("send @ QUIT");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send QUIT message\n");
            dump_message(&quit_message);
            struct myftph reply;
            memset(&reply, 0, sizeof(reply));
            if (recv(client_socket, &reply, sizeof(reply), 0) < 0) {
                perror("recv @ QUIT");
                exit(EXIT_FAILURE);
            }
            if (reply.Length != 0) {
                fprintf(stderr, "<- recv unexpected message\n");
                fprintf(stderr, "ERROR: recv unexpected message, so do not quit\n");
                continue;
            } else {
                if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_WITH_NO_DATA) {
                    if (close(client_socket) < 0) {
                        perror("close @ QUIT");
                        exit(EXIT_FAILURE);
                    }
                    fprintf(stderr, "<- recv OK message\n");
                    dump_message(&reply);
                    fprintf(stderr, "... quiting ...\n");
                    exit(EXIT_SUCCESS);
                } else {
                    fprintf(stderr, "<- recv unexpected message\n");
                    fprintf(stderr, "ERROR: recv unexpected message, so do not quit\n");
                    continue;
                }
            }
        }

        // PWD command
        if (strcmp(commandline_argv[0], "pwd") == 0) {
            // fprintf(stderr, "[DEBUG] PWD\n");
            if (commandline_argc != 1) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            struct myftph pwd_message;
            memset(&pwd_message, 0, sizeof(pwd_message));
            pwd_message.Type = TYPE_PWD;
            pwd_message.Length = 0;
            if (send(client_socket, &pwd_message, sizeof(struct myftph), 0) < 0) {
                perror("send @ PWD");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send PWD message\n");
            dump_message(&pwd_message);
            struct myftph_data reply;
            memset(&reply, 0, sizeof(reply));

            if ((recv(client_socket, &reply, sizeof(struct myftph), 0)) < 0) {
                perror("recv @ PWD header");
                exit(EXIT_FAILURE);
            }
            // fprintf(stderr, "recved: %ld\n", recved);

            if (reply.Length == 0) {
                fprintf(stderr, "<- recv unexpected message\n");
                fprintf(stderr, "ERROR: recv unexpected message, so do not quit\n");
                continue;
            }

            fprintf(stderr, "<- recv OK message\n");
            if ((recv(client_socket, &reply.Data, reply.Length, 0)) < 0) {
                perror("recv @ PWD data");
                exit(EXIT_FAILURE);
            }
            char garbage[DATASIZE - reply.Length];
            if ((recv(client_socket, garbage, (size_t) (DATASIZE - reply.Length), 0)) < 0) {
                perror("recv @ PWD garbage");
                exit(EXIT_FAILURE);
            }

            dump_data_message(&reply, CODE_DATA_FOLLOW_S_TO_C);

            if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_WITH_NO_DATA) {
                char tmp[reply.Length + 1];
                strncpy(tmp, reply.Data, reply.Length+1);
                fprintf(stderr, "%s\n", reply.Data);
                continue;
            } else {
                fprintf(stderr, "<- recv unexpected message\n");
                fprintf(stderr, "ERROR: recv unexpected message, so do not quit\n");
                continue;
            }
        }

        // CD command
        if (strcmp(commandline_argv[0], "cd") == 0) {
            // fprintf(stderr, "[DEBUG] CD\n");
            if (commandline_argc != 2) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            struct myftph_data cd_message;
            memset(&cd_message, 0, sizeof(cd_message));
            cd_message.Type = TYPE_CWD;
            cd_message.Length = (uint16_t) strlen(commandline_argv[1]);
            strncpy(cd_message.Data, commandline_argv[1], cd_message.Length);
            if (send(client_socket, &cd_message, sizeof(cd_message), 0) < 0) {
                perror("send @ CD");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send CD message\n");
            dump_data_message(&cd_message, 99);
            struct myftph reply;
            memset(&reply, 0, sizeof(struct myftph));
            if (recv(client_socket, &reply, sizeof(struct myftph), 0) < 0) {
                perror("recv @ CD reply");
                exit(EXIT_FAILURE);
            }

            if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_WITH_NO_DATA) {
                if (reply.Length != 0) {
                    fprintf(stderr, "ERROR: message error\n");
                    dump_message(&reply);
                    continue;
                }
                fprintf(stderr, "<- recv OK message\n");
                dump_message(&reply);
                continue;
            }

            switch (reply.Type) {
                case TYPE_CMD_ERR:
                    switch (reply.Code) {
                        case CODE_SYNTAX_ERROR:
                            fprintf(stderr, "<- recv ERROR message\n");
                            dump_message(&reply);
                            break;
                        case CODE_UNDEFINED_COMMAND:
                            fprintf(stderr, "<- recv ERROR message\n");
                            dump_message(&reply);
                            break;
                        case CODE_PROTOCOL_ERROR:
                            fprintf(stderr, "<- recv ERROR message\n");
                            dump_message(&reply);
                            break;
                        default:
                            fprintf(stderr, "ERROR: recv unknown code message\n");
                            dump_message(&reply);
                            break;
                    }
                    break;
                case TYPE_FILE_ERR:
                    switch (reply.Code) {
                        case CODE_NO_SUCH_FILES:
                            fprintf(stderr, "<- recv ERROR message\n");
                            dump_message(&reply);
                            break;
                        case CODE_NO_ACCESS:
                            fprintf(stderr, "<- recv ERROR message\n");
                            dump_message(&reply);
                            break;
                        default:
                            fprintf(stderr, "ERROR: recv unknown code message\n");
                            dump_message(&reply);
                            break;
                    }
                    break;
                case TYPE_UNKWN_ERR:
                    fprintf(stderr, "<- recv ERROR message\n");
                    dump_message(&reply);
                    break;
                default:
                    fprintf(stderr, "ERROR: recv unknown type message\n");
                    dump_message(&reply);
                    break;
            }
            continue;
        }

        // DIR command
        if (strcmp(commandline_argv[0], "dir") == 0) {
            // fprintf(stderr, "[DEBUG] DIR\n");
            if (!(commandline_argc == 2 || commandline_argc == 1)) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            struct myftph_data dir_message;
            memset(&dir_message, 0, sizeof(struct myftph_data));
            char target_dir[SIZE];
            memset(&target_dir, 0, SIZE);
            dir_message.Type = TYPE_LIST;
            if (commandline_argc == 1) {
                strncpy(target_dir, ".", 1);
            } else {
                strncpy(target_dir, commandline_argv[1], strlen(commandline_argv[1]) + 1);
            }
            dir_message.Length = (uint16_t) strlen(target_dir);
            strncpy(dir_message.Data, target_dir, dir_message.Length);
            if (send(client_socket, &dir_message, sizeof(struct myftph_data), 0) < 0) {
                perror("send @ DIR");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send DIR message\n");
            dump_data_message(&dir_message, 99);
            struct myftph reply;
            memset(&reply, 0, sizeof(struct myftph));
            if (recv(client_socket, &reply, sizeof(struct myftph), 0) < 0) {
                perror("recv @ DIR");
                exit(EXIT_FAILURE);
            }

            if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_DATA_FOLLOW_S_TO_C) {
                fprintf(stderr, "<- recv OK message\n");
                dump_message(&reply);
                struct myftph_data buf;
                char result[DATASIZE*10];
                memset(&result, 0, sizeof(result));
                for (;;) {
                    memset(&buf, 0, sizeof(struct myftph_data));
                    if (recv(client_socket, &buf, sizeof(struct myftph_data), 0) < 0) {
                        perror("recv @ DIR");
                        exit(EXIT_FAILURE);
                    }
                    fprintf(stderr, "<- recv DATA message\n");
                    dump_data_message(&buf, CODE_DATA_FOLLOW_S_TO_C);
                    strcat(result, buf.Data);
                    if (buf.Code == CODE_DATA_NO_FOLLOW) {
                        break;
                    }
                }
                fprintf(stderr, "\n");
                fprintf(stderr, "%s\n", result);
            } else {
                switch (reply.Type) {
                    case TYPE_CMD_ERR:
                        switch (reply.Code) {
                            case CODE_SYNTAX_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_UNDEFINED_COMMAND:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_PROTOCOL_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_FILE_ERR:
                        switch (reply.Code) {
                            case CODE_NO_SUCH_FILES:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_NO_ACCESS:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_UNKWN_ERR:
                        fprintf(stderr, "<- recv ERROR message\n");
                        dump_message(&reply);
                        break;
                    default:
                        fprintf(stderr, "ERROR: recv unknown type message\n");
                        dump_message(&reply);
                        break;
                }
            }
            continue;
        }

        // LPWD command
        if (strcmp(commandline_argv[0], "lpwd") == 0) {
            // fprintf(stderr, "[DEBUG] LPWD\n");
            if (commandline_argc != 1) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            char pathname[SIZE];
            memset(pathname, 0, SIZE);
            getcwd(pathname, SIZE);
            fprintf(stdout,"%s\n", pathname);
            continue;
        }

        // LCD command
        if (strcmp(commandline_argv[0], "lcd") == 0) {
            // fprintf(stderr, "[DEBUG] LCD\n");
            if (commandline_argc != 2) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            chdir(commandline_argv[1]);
            char path[SIZE];
            memset(path, 0, SIZE);
            getcwd(path, SIZE);
            fprintf(stdout,"%s\n", path);
            continue;
        }

        // LDIR command
        if (strcmp(commandline_argv[0], "ldir") == 0) {
            // fprintf(stderr, "[DEBUG] LDIR\n");
            if (!(commandline_argc == 2 || commandline_argc == 1)) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            DIR *dir;
            struct dirent *dp;
            struct stat statbuf;
            char path[SIZE];
            memset(path, '\0', SIZE);
            if (commandline_argc == 1) { // ldir
                strcpy(path,".");
            } else { // ldir path/to/file/or/dir
                strcpy(path, commandline_argv[1]);
            }
            if((dir = opendir(path)) == NULL) {
                perror("opendir");
                fprintf(stderr, "ERROR: command execution error\n");
                continue;
            }
            for(dp = readdir(dir); dp != NULL; dp = readdir(dir)) {
                stat(dp->d_name, &statbuf);
                printf((S_ISDIR(statbuf.st_mode)) ? "d" : "-");
                if(S_ISDIR(statbuf.st_mode)) { // dir
                    printf((statbuf.st_mode & S_IRUSR) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWUSR) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXUSR) ? "x" : "-");
                    printf((statbuf.st_mode & S_IRGRP) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWGRP) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXGRP) ? "x" : "-");
                    printf((statbuf.st_mode & S_IROTH) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWOTH) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXOTH) ? "x" : "-");
                    printf("\t%s/\n\tlast access: %s\tlast modified: %s\tsize: %lld bytes\n",
                           dp->d_name,
                           asctime(localtime(&statbuf.st_atime)),
                           asctime(localtime(&statbuf.st_mtime)),
                           statbuf.st_size
                    );
                } else {
                    printf((statbuf.st_mode & S_IRUSR) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWUSR) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXUSR) ? "x" : "-");
                    printf((statbuf.st_mode & S_IRGRP) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWGRP) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXGRP) ? "x" : "-");
                    printf((statbuf.st_mode & S_IROTH) ? "r" : "-");
                    printf((statbuf.st_mode & S_IWOTH) ? "w" : "-");
                    printf((statbuf.st_mode & S_IXOTH) ? "x" : "-");
                    printf("\t%s\n\tlast access: %s\tlast modified: %s\tsize: %lld bytes\n",
                           dp->d_name,
                           asctime(localtime(&statbuf.st_atime)),
                           asctime(localtime(&statbuf.st_mtime)),
                           statbuf.st_size
                    );
                }
            }
            closedir(dir);
            continue;
        }

        // GET command
        if (strcmp(commandline_argv[0], "get") == 0) {
            // fprintf(stderr, "[DEBUG] GET\n");
            if (!(commandline_argc == 2 || commandline_argc == 3)) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            FILE *result_file;
            struct myftph_data get_message;
            memset(&get_message, 0, sizeof(struct myftph_data));
            get_message.Type = TYPE_RETR;
            char target_file_name[SIZE];
            memset(&target_file_name, 0, SIZE);
            char result_file_name[SIZE];
            memset(&result_file_name, 0, SIZE);
            if (commandline_argc == 2) {
                strncpy(target_file_name, commandline_argv[1], strlen(commandline_argv[1]));
                strncpy(result_file_name, commandline_argv[1], strlen(commandline_argv[1]));
            } else {
                strncpy(target_file_name, commandline_argv[1], strlen(commandline_argv[1]));
                strncpy(result_file_name, commandline_argv[2], strlen(commandline_argv[2]));
            }
            get_message.Length = (uint16_t) strlen(target_file_name);
            strncpy(get_message.Data, target_file_name, get_message.Length);
            if (send(client_socket, &get_message, sizeof(get_message), 0) < 0) {
                perror("send @ GET");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send GET message\n");
            dump_data_message(&get_message, 99);

            struct myftph reply;
            memset(&reply, 0, sizeof(struct myftph));
            if (recv(client_socket, &reply, sizeof(struct myftph), 0) < 0) {
                perror("recv @ GET");
                exit(EXIT_FAILURE);
            }

            if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_DATA_FOLLOW_S_TO_C) {
                fprintf(stderr, "<- recv OK message\n");
                dump_message(&reply);

                strncpy(get_message.Data, target_file_name, get_message.Length);
                if ((result_file = fopen(result_file_name, "w+")) == NULL) {
                    perror("fopen result file");
                    exit(EXIT_FAILURE);
                }

                struct myftph_data buf;

                for (;;) {
                    memset(&buf, 0, sizeof(struct myftph_data));
                    if (recv(client_socket, &buf, sizeof(struct myftph_data), 0) < 0) {
                        perror("recv @ DATA");
                        exit(EXIT_FAILURE);
                    }
                    fprintf(stderr, "<- recv DATA message\n");
                    dump_data_message(&buf, CODE_DATA_FOLLOW_S_TO_C);
                    fwrite(buf.Data, 1, buf.Length, result_file);
                    if (buf.Code == CODE_DATA_NO_FOLLOW) {
                        break;
                    }
                }
                if (fclose(result_file) < 0) {
                    perror("fclose @ GET");
                    exit(EXIT_FAILURE);
                }

            } else {
                switch (reply.Type) {
                    case TYPE_CMD_ERR:
                        switch (reply.Code) {
                            case CODE_SYNTAX_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_UNDEFINED_COMMAND:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_PROTOCOL_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_FILE_ERR:
                        switch (reply.Code) {
                            case CODE_NO_SUCH_FILES:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_NO_ACCESS:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_UNKWN_ERR:
                        fprintf(stderr, "<- recv ERROR message\n");
                        dump_message(&reply);
                        break;
                    default:
                        fprintf(stderr, "ERROR: recv unknown type message\n");
                        dump_message(&reply);
                        break;
                }
            }
            continue;
        }

        // PUT command
        if (strcmp(commandline_argv[0], "put") == 0) {
            fprintf(stderr, "[DEBUG] PUT\n");
            if (!(commandline_argc == 2 || commandline_argc == 3)) {
                fprintf(stderr, "ERROR: command syntax error\n");
                continue;
            }
            struct myftph_data put_message;
            memset(&put_message, 0, sizeof(struct myftph_data));
            put_message.Type = TYPE_STOR;

            FILE *source_file;
            char source_file_name[SIZE];
            memset(&source_file_name, 0, SIZE);
            char destination_file_name[SIZE];
            memset(&destination_file_name, 0, SIZE);
            if (commandline_argc == 2) {
                strncpy(source_file_name, commandline_argv[1], strlen(commandline_argv[1]));
                strncpy(destination_file_name, commandline_argv[1], strlen(commandline_argv[1]));
            } else {
                strncpy(source_file_name, commandline_argv[1], strlen(commandline_argv[1]));
                strncpy(destination_file_name, commandline_argv[2], strlen(commandline_argv[2]));
            }

            put_message.Length = (uint16_t) strlen(destination_file_name);
            strncpy(put_message.Data, destination_file_name, put_message.Length);
            if (send(client_socket, &put_message, sizeof(put_message), 0) < 0) {
                perror("send @ GET");
                exit(EXIT_FAILURE);
            }
            fprintf(stderr, "\t-> send PUT message\n");
            dump_data_message(&put_message, 99);

            struct myftph reply;
            memset(&reply, 0, sizeof(struct myftph));
            if (recv(client_socket, &reply, sizeof(struct myftph), 0) < 0) {
                perror("recv @ PUT");
                exit(EXIT_FAILURE);
            }
            if (reply.Type == TYPE_OK_COMMAND && reply.Code == CODE_DATA_FOLLOW_C_TO_S) {
                fprintf(stderr, "<- recv OK message\n");
                dump_message(&reply);

                if((source_file = fopen(source_file_name, "r")) == NULL) {
                    perror("fopen");
                    fprintf(stderr, "ERROR: command execution error\n");
                }
                struct myftph_data reply_data;
                size_t read;
                for (;;) {
                    memset(&reply_data, 0, sizeof(struct myftph_data));
                    reply_data.Type = TYPE_DATA;
                    read = fread(&reply_data.Data, 1, DATASIZE, source_file);
                    // fprintf(stderr, "%s\n", reply_data.Data);
                    if (read < DATASIZE) {
                        if (feof(source_file) != 0) {
                            reply_data.Code = CODE_DATA_NO_FOLLOW;
                        } else {
                            fprintf(stderr, "ERROR: command execution error\n");
                            exit(EXIT_FAILURE);
                        }
                    } else {
                        reply_data.Code = CODE_DATA_FOLLOW;
                    }
                    reply_data.Length = (uint16_t) read;
                    if (send(client_socket, &reply_data, sizeof(struct myftph_data), 0) < 0) {
                        perror("send @ STOR DATA");
                        exit(EXIT_FAILURE);
                    }
                    fprintf(stderr, "\t-> send DATA message\n");
                    dump_data_message(&reply_data, CODE_DATA_FOLLOW_C_TO_S);
                    if (reply_data.Code == CODE_DATA_NO_FOLLOW) {
                        break;
                    }
                }
            } else {
                switch (reply.Type) {
                    case TYPE_CMD_ERR:
                        switch (reply.Code) {
                            case CODE_SYNTAX_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_UNDEFINED_COMMAND:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_PROTOCOL_ERROR:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_FILE_ERR:
                        switch (reply.Code) {
                            case CODE_NO_SUCH_FILES:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            case CODE_NO_ACCESS:
                                fprintf(stderr, "<- recv ERROR message\n");
                                dump_message(&reply);
                                break;
                            default:
                                fprintf(stderr, "ERROR: recv unknown code message\n");
                                dump_message(&reply);
                                break;
                        }
                        break;
                    case TYPE_UNKWN_ERR:
                        fprintf(stderr, "<- recv ERROR message\n");
                        dump_message(&reply);
                        break;
                    default:
                        fprintf(stderr, "ERROR: recv unknown type message\n");
                        dump_message(&reply);
                        break;
                }
            }
            continue;
        }

        // HELP command
        if (strcmp(commandline_argv[0], "help") == 0) {
            // fprintf(stderr, "[DEBUG] HELP\n");
            if (commandline_argc != 1) {
                fprintf(stderr, "command execution error: Usage: help\n");
                continue;
            }
            print_help();
            continue;
        }

        fprintf(stderr, "ERROR: no such command: %s\n", commandline_argv[0]);
    }
}

void
print_welcome()
{
    fprintf(stderr, "\t\n");
    fprintf(stderr, "\t __       __                  ________  ________  _______  \n");
    fprintf(stderr, "\t/  \\     /  |                /        |/        |/       \\ \n");
    fprintf(stderr, "\t$$  \\   /$$ | __    __       $$$$$$$$/ $$$$$$$$/ $$$$$$$  |\n");
    fprintf(stderr, "\t$$$  \\ /$$$ |/  |  /  |      $$ |__       $$ |   $$ |__$$ |\n");
    fprintf(stderr, "\t$$$$  /$$$$ |$$ |  $$ |      $$    |      $$ |   $$    $$/ \n");
    fprintf(stderr, "\t$$ $$ $$/$$ |$$ |  $$ |      $$$$$/       $$ |   $$$$$$$/  \n");
    fprintf(stderr, "\t$$ |$$$/ $$ |$$ \\__$$ |      $$ |         $$ |   $$ |      \n");
    fprintf(stderr, "\t$$ | $/  $$ |$$    $$ |      $$ |         $$ |   $$ |      \n");
    fprintf(stderr, "\t$$/      $$/  $$$$$$$ |      $$/          $$/    $$/       \n");
    fprintf(stderr, "\t             /  \\__$$ |                                    \n");
    fprintf(stderr, "\t             $$    $$/                                     \n");
    fprintf(stderr, "\t              $$$$$$/                                      \n");
    fprintf(stderr, "\t\n");
}

void
print_help()
{
    print_welcome();
    fprintf(stderr, "ABSTRACT\n");
    fprintf(stderr, "\tThis is the client software that implements the myFTP protocol.\n");
    fprintf(stderr, "\tmyftpc establishes a TCP connection with myfptd running on the host specified by the argument. \n"
                    "\tWhen the TCP connection is established, myftpc displays \"myFTP%%\" as a prompt \n"
                    "\tand waits for command input from the user. When a command is input, it sends a command message \n"
                    "\tto the server as necessary and waits for reception of a reply message. \n"
                    "\tThen it displays the prompt again and waits for command input from the user.\n\n");

    fprintf(stderr, "USAGE\n");
    fprintf(stderr, "\t./myftpc <server host name>\n\n");

    fprintf(stderr, "PORT\n");
    fprintf(stderr, "\tmyFTP protocol uses 50021 port.\n\n");

    fprintf(stderr, "COMMANDS\n");
    fprintf(stderr, "\tThe command list is as follows.\n");
    fprintf(stderr, "\tcommand                     arguments                                                              functionality                                         \n");
    fprintf(stderr, "\t-------- ------------------------------------------------ -----------------------------------------------------------------------------------------------\n");
    fprintf(stderr, "\tquit      -                                                Quit myftpc.                                                                                  \n");
    fprintf(stderr, "\tpwd       -                                                Print working directory at the server.                                                        \n");
    fprintf(stderr, "\tcwd       path/to/directory                                Change working directory at the server.                                                       \n");
    fprintf(stderr, "\tdir       [path/to/directory/or/file]                      Show some info of the file/directory at the server.                                           \n");
    fprintf(stderr, "\tlpwd      -                                                Print working directory at local.                                                             \n");
    fprintf(stderr, "\tlcd       path/to/directory                                Change directory at local.                                                                    \n");
    fprintf(stderr, "\tldir      [path/to/directory/or/file]                      Show some info of the file/directory at local.                                                \n");
    fprintf(stderr, "\tget       path/to/file/at/server [path/to/file/at/local]   Transfer the file on the server specified by path/to/file/at/server to path/to/file/at/local. \n");
    fprintf(stderr, "\tput       path/to/file/at/local [path/to/file/at/server]   Transfer the local file specified by path/to/file/at/local to path/to/file/at/server.         \n");
}

void
dump_message(struct myftph *message)
{
    switch (message->Type) {
        case TYPE_QUIT:         // client -> server
            fprintf(stderr, "\t+-- [ (->) QUIT ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_PWD:          // client -> server
            fprintf(stderr, "\t+-- [ (->) PWD ]---------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_CWD:          // client -> server
            break;
        case TYPE_LIST:         // client -> server
            break;
        case TYPE_RETR:         // client -> server
            break;
        case TYPE_STOR:         // client -> server
            break;
        case TYPE_OK_COMMAND:   // client <- server
            fprintf(stderr, "+-- [ (<-) OK ]--------\n");
            fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "|\tCode: %s\n", OK_CODE_NAME[message->Code]);
            fprintf(stderr, "|\tLength: %d\n", message->Length);
            fprintf(stderr, "+------------------------\n");
            break;
        case TYPE_CMD_ERR:      // client <- server
            fprintf(stderr, "+-- [ (<-) CMD ERR ]--------\n");
            fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "|\tCode: %s\n", CMD_ERROR_CODE_NAME[message->Code]);
            fprintf(stderr, "|\tLength: %d\n", message->Length);
            fprintf(stderr, "+------------------------\n");
            break;
        case TYPE_FILE_ERR:    // client <- server
            fprintf(stderr, "+-- [ (<-) FILE ERR ]--------\n");
            fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "|\tCode: %s\n", FILE_ERROR_CODE_NAME[message->Code]);
            fprintf(stderr, "|\tLength: %d\n", message->Length);
            fprintf(stderr, "+------------------------\n");
            break;
        case TYPE_UNKWN_ERR:
            fprintf(stderr, "+-- [ (<-) UNKWN ERR ]--------\n");
            fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "|\tCode: %s\n", "CODE_UNKNOWN_ERROR(0x05)");
            fprintf(stderr, "|\tLength: %d\n", message->Length);
            fprintf(stderr, "+------------------------\n");
            break;
        case TYPE_DATA:         // client <- server
            break;
        default:
            return;
    }
}

void
dump_data_message(struct myftph_data *message, int direction)
{
    char tmp[DATASIZE + 1];
    memset(&tmp, 0, sizeof(tmp));
    switch (message->Type) {
        case TYPE_QUIT:         // client -> server
            fprintf(stderr, "\t+-- [ (->) QUIT ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_PWD:          // client -> server
            fprintf(stderr, "\t+-- [ (->) PWD ]---------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_CWD:          // client -> server
            strncpy(tmp, message->Data, message->Length + 1);
            fprintf(stderr, "\t+-- [ (->) CD ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t|\tCode: %s\n", OK_CODE_NAME[message->Code]);
            fprintf(stderr, "\t|\tLength: %d\n", message->Length);
            fprintf(stderr, "\t|\tData: %s\n", tmp);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_LIST:         // client -> server
            strncpy(tmp, message->Data, message->Length + 1);
            fprintf(stderr, "\t+-- [ (->) DIR ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t|\tCode: %s\n", OK_CODE_NAME[message->Code]);
            fprintf(stderr, "\t|\tLength: %d\n", message->Length);
            fprintf(stderr, "\t|\tData: %s\n", tmp);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_RETR:         // client -> server
            strncpy(tmp, message->Data, message->Length + 1);
            fprintf(stderr, "\t+-- [ (->) RETR ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t|\tLength: %d\n", message->Length);
            fprintf(stderr, "\t|\tData: %s\n", tmp);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_STOR:         // client -> server
            strncpy(tmp, message->Data, message->Length + 1);
            fprintf(stderr, "\t+-- [ (->) STOR ]--------\n");
            fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "\t|\tLength: %d\n", message->Length);
            fprintf(stderr, "\t|\tData: %s\n", tmp);
            fprintf(stderr, "\t+------------------------\n");
            break;
        case TYPE_OK_COMMAND:   // client <- server
            strncpy(tmp, message->Data, message->Length + 1);
            fprintf(stderr, "+-- [ (<-) OK ]--------\n");
            fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
            fprintf(stderr, "|\tCode: %s\n", OK_CODE_NAME[message->Code]);
            fprintf(stderr, "|\tLength: %d\n", message->Length);
            fprintf(stderr, "|\tData: %s\n", tmp);
            fprintf(stderr, "+------------------------\n");
            break;
        case TYPE_CMD_ERR:      // client <- server
            break;
        case TYPE_FILE_ERR:    // client <- server
            break;
        case TYPE_UNKWN_ERR:
            break;
        case TYPE_DATA:         // client <- server
            strncpy(tmp, message->Data, message->Length);
            if (direction == CODE_DATA_FOLLOW_S_TO_C) {
                fprintf(stderr, "+-- [ (<-) DATA ]--------\n");
                fprintf(stderr, "|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
                fprintf(stderr, "|\tCode: %s\n", DATA_CODE_NAME[message->Code]);
                fprintf(stderr, "|\tLength: %ld\n", strlen(message->Data));
                fprintf(stderr, "|\tData: %.*s(omitted below...)\n", 30, tmp);
                fprintf(stderr, "+------------------------\n");
                break;
            } else {
                fprintf(stderr, "\t+-- [ (->) DATA ]--------\n");
                fprintf(stderr, "\t|\tType: %s\n", MESSAGE_TYPE_NAME[message->Type]);
                fprintf(stderr, "\t|\tCode: %s\n", DATA_CODE_NAME[message->Code]);
                fprintf(stderr, "\t|\tLength: %ld\n", strlen(message->Data));
                fprintf(stderr, "\t|\tData: %.*s(omitted below...)\n", 30, tmp);
                fprintf(stderr, "\t+------------------------\n");
                break;
            }

        default:
            return;
    }
}