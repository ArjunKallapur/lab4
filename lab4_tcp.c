/*
 #ifndef mraa_h
 #define mraa_h
 
 #define MRAA_GPIO_IN 0
 #define MRAA_GPIO_EDGE_RISING 2
 typedef int mraa_aio_context;
 
 int mraa_aio_init(int pin) { return 69;}
 int mraa_aio_read(mraa_aio_context context) {return 98;}
 #endif
 */
#define _GNU_SOURCE
#define SCALE 1
#define PERIOD 2
#define LOG 3
#define ID 4
#define HOST 5

#define FAHRENHEIT 'F'
#define CELSIUS 'C'
#define R0 100000
#define B 4275
#define READ_SIZE 1

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mraa.h>
// globals:

// flags:
int log_flag = 0;
int id_flag = 0;
int host_flag = 0;
int port_num = 0;

//default values:
sig_atomic_t volatile run_flag = 1;
int period = 1;
char scale = FAHRENHEIT;
FILE *log_file = NULL;
int socket_fd = 0;
char *host = NULL;
char *port = NULL;
char *id = NULL;
int generate_reports = 1;
char *command_buffer = NULL;
char *current_command;
int buffer_size = 0;


// helper functions
void run_command(void);
void read_commands(void);
void print_shutdown_msg(void);
float c2f(float c);

int main(int argc, char * argv[]) {
    
    char *buf;
    // default values, if no options are given
    
    struct option longopts[] = {
        {"scale", required_argument, 0, SCALE},
        {"period", required_argument, 0, PERIOD},
        {"log", required_argument, 0, LOG},
        {"id", required_argument, 0, ID},
        {"host", required_argument, 0, HOST},
        {0, 0, 0, 0}
    };
    
    int optind;
    int ret;
    
    while ((ret = getopt_long(argc, argv, "", longopts, &optind)) != -1) {
        switch (ret) {
            case SCALE:
                switch (optarg[0]) {
                    case FAHRENHEIT:
                    case CELSIUS:
                        scale = optarg[0];
                        break;
                    default:
                        fprintf(stderr, "Incorrect argument passed to --scale: %s\n", optarg);
                        exit(1);
                        break;
                }
                break;
            case PERIOD:
                if (atoi(optarg) < 1) {
                    fprintf(stderr, "Time period must be greater than 1 second!\n");
                    exit(1);
                }
                period = atoi(optarg);
                break;
            case LOG:
                log_flag = 1;
                log_file = fopen(optarg, "w+");
                if (log_file == NULL) {
                    fprintf(stderr, "Error opening log file: %s\n", strerror(errno));
                    exit(1);
                }
                break;
            case ID:
                id_flag = 1;
                if (strlen(optarg) != 9) {
                    fprintf(stderr, "ID number is of invalid length: %lu", strlen(optarg));
                    exit(1);
                }
                id = malloc(10 * sizeof(char));
		strcpy(id, optarg);
                break;
            case HOST:
                host_flag = 1;
                host = malloc((strlen(optarg) + 1) * sizeof(char));
                strcpy(host, optarg);
                break;
            default:
                fprintf(stderr, "Missing argument or invalid option!\n");
                exit(1);
        }
    }
    if (host_flag + id_flag + log_flag != 3) {
        fprintf(stderr, "--host, --id, and --log are mandatory options, %d of the 3 given\n", (host_flag + id_flag + log_flag));
        exit(1);
    }
    if (argv[argc - 1] == NULL) {
        fprintf(stderr, "Mandatory Port Number not specified!\n");
        exit(1);
    }
    port = malloc(strlen(argv[argc - 1]) + 1);
    strcpy(port, argv[argc - 1]);
    // socket shenanigans, courtesy Tim Gu
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    
    int s;
    
    s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo() failed with error: %s", gai_strerror(errno));
        exit(2);
    }
    
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        
        if (socket_fd == -1)
            continue;
        if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
	close(socket_fd);
        
    }
    if (rp == NULL) {
        fprintf(stderr, "Couldn't connect with anyone :(\n");
        exit(2);
    }
    
    freeaddrinfo(result);
    char *id_buf = malloc(14 * sizeof(char));
    sprintf(id_buf, "ID=%s\n", id);
    write(socket_fd, id_buf, 13);
    fprintf(log_file, "ID=%s", id_buf);
    free(id_buf);
    // For polling
    struct pollfd fds = {
        socket_fd,
        POLLIN,
        0
    };
    
    // Start initializing the BeagleBone stuff here
    
    //declare, initialize temperature sensor:
    mraa_aio_context temp_sensor;
    temp_sensor = mraa_aio_init(1);
    
    if (temp_sensor == NULL) {
        fprintf(stderr, "Error initializing temperature sensor\n");
        exit(2);
    }
    //socket_fd = STDOUT_FILENO;
    // Do timing shenanigans
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t current_time = tv.tv_sec;
    time_t next_print_time = current_time;
    struct tm *local_time = NULL;
    int poll_ret;
    while (run_flag) {
        gettimeofday(&tv, NULL);
        current_time = tv.tv_sec;
        if ((current_time >= next_print_time) && generate_reports) {
            local_time = localtime(&current_time);
            int reading = mraa_aio_read(temp_sensor);
            float R = 1023.0/(reading - 1.0);
            R = R0 * R;
            float temp = 1.0 / (log(R/R0)/B + 1/298.15) - 273.15;
            temp = (scale == CELSIUS) ? temp : c2f(temp);
            buf = calloc(20, sizeof(char));
            sprintf(buf, "%02d:%02d:%02d %.1f\n", local_time->tm_hour,
                    local_time->tm_min,
                    local_time->tm_sec,
                    temp);
            write(socket_fd, buf, strlen(buf));
            if (log_flag) {
                fprintf(log_file, "%s", buf);
            }
            free(buf);
            next_print_time = current_time + period;
        }
        poll_ret = poll(&fds, 1, 500);
        if (poll_ret && (fds.revents & POLLIN)) {
            read_commands();
            fds.revents = 0;
        }
    }
    print_shutdown_msg();
    fsync(socket_fd);
    fflush(log_file);
    close(socket_fd);
    return 0;
}

void run_command(void) {
    if (strcmp(current_command, "SCALE=F") == 0) {
        scale = FAHRENHEIT;
        if (log_flag) {
            fprintf(log_file, "SCALE=F\n");
        }
    }
    if (strcmp(current_command, "SCALE=C") == 0) {
        scale = CELSIUS;
        if (log_flag) {
            fprintf(log_file, "SCALE=C\n");
        }
    }
    if (strcmp(current_command, "STOP") == 0) {
        fsync(socket_fd);
        if (log_flag) {
            fprintf(log_file, "STOP\n");
        }
        generate_reports = 0;
    }
    if (strcmp(current_command, "START") == 0) {
        if (log_flag) {
            fprintf(log_file, "START\n");
        }
        generate_reports = 1;
    }
    // compare first 7 characters of current command to PERIOD=
    if (strncmp(current_command, "PERIOD=", 7) == 0) {
        period = atoi(&current_command[7]);
        if (log_flag) {
            fprintf(log_file, "%s\n", current_command);
        }
    }
    if (strncmp(current_command, "LOG", 3) == 0) {
        if (log_flag) {
            fprintf(log_file, "%s\n", current_command);
        }
    }
    if (strcmp(current_command, "OFF") == 0) {
        fprintf(log_file, "OFF\n");
        print_shutdown_msg();
        fsync(socket_fd);
        fflush(log_file);
        close(socket_fd);
        exit(0);
    }
    return;
}

void read_commands(void) {
    ssize_t err;
    for(; ;) {
        command_buffer = realloc(command_buffer, buffer_size + READ_SIZE);
        err = read(socket_fd, &command_buffer[buffer_size], READ_SIZE);
        if (err == 0) {
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error reading commands in from STDIN: %s", strerror(errno));
        }
        buffer_size += READ_SIZE;
        if (command_buffer[buffer_size - 1] == '\n') {
            command_buffer[buffer_size - 1] = '\0';
            current_command = malloc(sizeof(char) * (strlen(command_buffer) + 1));
            strcpy(current_command, command_buffer);
            run_command();
            free(command_buffer);
            command_buffer = NULL;
            buffer_size = 0;
            free(current_command);
            break;
        }
        
    }
}

void print_shutdown_msg(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t current_time = tv.tv_sec;
    struct tm *local_time = localtime(&current_time);
    char msg[20];
    sprintf(msg, "%02d:%02d:%02d SHUTDOWN\n", local_time->tm_hour,
            local_time->tm_min,
            local_time->tm_sec);
    write(socket_fd, msg, strlen(msg));
    if (log_flag) {
        fprintf(log_file, "%s\n", msg);
    }
    return;
}

float c2f(float c) { return (1.8 * c + 32.0); }




