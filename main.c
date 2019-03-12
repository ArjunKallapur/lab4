//
//  main.c
//  lab4b
//
//  Created by Arjun_Kallapur on 3/8/19.
//  Copyright © 2019 Arjun Kallapur. All rights reserved.
//
#define _GNU_SOURCE
#define SCALE 1
#define PERIOD 2
#define LOG 3
#define FAHRENHEIT 'F'
#define CELSIUS 'C'
#define R0 100000
#define B 275
#define READ_SIZE 1
#ifdef DUMMY

#else
#include <mraa.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mraa.h"

// globals
sig_atomic_t volatile run_flag = 1;
int period = 1;
char scale = FAHRENHEIT;
int log_fd = 0;
int generate_reports = 1;
char *command_buffer = NULL;
char current_command[1000];
char buffer_size = 0;

// Helper functions
void button_handler(int signum);
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
                if ((log_fd = open(optarg, O_CREAT)) == -1) {
                    fprintf(stderr, "Error opening log file: %s\n", strerror(errno));
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Missing argument or invalid option!\n");
                exit(1);
        }
    }
    
    // For polling
    struct pollfd fds = {
        STDIN_FILENO,
        POLLIN,
        0
    };
    
    // Start initializing the BeagleBone stuff here
    
    // register the signal handler
    signal(SIGINT, button_handler);
    
    // declare, initialize button
    mraa_gpio_context button;
    button = mraa_gpio_init(60);
    
    if (button == NULL) {
        fprintf(stderr, "Error initializing button! \n");
        exit(2);
    }
    
    mraa_gpio_dir(button, MRAA_GPIO_IN);
    mraa_gpio_isr(button, MRAA_EDGE_RISING, &button_handler, NULL);
    
    //declare, initialize temperature sensor:
    mraa_aio_context temp_sensor;
    temp_sensor = mraa_aio_init(1);
    
    if (temp_sensor == NULL) {
        fprintf(stderr, "Error initializing temperature sensor\n");
        exit(2);
    }
    
    // Do timing shenanigans
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t current_time = tv.tv_sec;
    time_t next_print_time = current_time + period;
    struct tm *local_time = NULL;
    while (run_flag) {
        gettimeofday(&tv, NULL);
        current_time = tv.tv_sec;
        if ((current_time == next_print_time) && generate_reports) {
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
            write(STDOUT_FILENO, buf, 20);
            if (log_fd) {
                write(log_fd, buf, 20);
            }
            next_print_time = current_time + period;
        }
        poll(&fds, 1, POLLIN);
        if (fds.revents == 1) {
            read_commands();
        }
    }
    print_shutdown_msg();
    fflush(stdout);
    fsync(log_fd);
    mraa_aio_close(temp_sensor);
    mraa_gpio_close(button);
    
    return 0;
}

// Signal handling function from the button
void button_handler(int signum) {
    if (signum == SIGINT) {
        run_flag = 0;
    }
}
void run_command(void) {
    if (strcmp(current_command, "SCALE=F") == 0) {
        scale = FAHRENHEIT;
    }
    if (strcmp(current_command, "SCALE=C") == 0) {
        scale = CELSIUS;
    }
    if (strcmp(current_command, "STOP") == 0) {
        if ((!generate_reports) && log_fd) {
            write(log_fd, "STOP\n", 5);
        }
        generate_reports = 0;
    }
    if (strcmp(current_command, "START") == 0) {
        if (generate_reports && log_fd) {
            write(log_fd, "START\n", 6);
        }
        generate_reports = 1;
    }
    // compare first 7 characters of current command to PERIOD=
    if (strncmp(current_command, "PERIOD=", 7) == 0) {
        period = atoi(&current_command[7]);
    }
    if (strncmp(current_command, "LOG", 3) == 0) {
        if (log_fd) {
            write(log_fd, current_command, strlen(current_command));
        }
    }
    if (strcmp(current_command, "OFF") == 0) {
        print_shutdown_msg();
        fflush(stdout);
        fsync(log_fd);
        exit(0);
    }
    return;
}

void read_commands(void) {
    ssize_t err;
    for(; ;) {
        //printf("Running for loop\n");
        command_buffer = realloc(command_buffer, buffer_size + READ_SIZE);
        err = read(STDIN_FILENO, &command_buffer[buffer_size], READ_SIZE);
        if (err == 0) {
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error reading commands in from STDIN: %s", strerror(errno));
        }
        buffer_size += READ_SIZE;
        if (command_buffer[buffer_size - 1] == '\n') {
            command_buffer[buffer_size - 1] = '\0';
            strcpy(current_command, command_buffer);
            run_command();
            free(command_buffer);
            command_buffer = NULL;
            buffer_size = 0;
            for (int i = 0; i < 1000; i++) {
                current_command[i] = '\0';
            }
        }
        
    }
}

void print_shutdown_msg(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t current_time = tv.tv_sec;
    struct tm *local_time = localtime(&current_time);
    char msg[20];
    sprintf(msg, "SHUTDOWN %02d:%02d:%02d\n", local_time->tm_hour,
            local_time->tm_min,
            local_time->tm_sec);
    printf("%s", msg);
    if (log_fd) {
        write(log_fd, msg, 20);
    }
    return;
}

float c2f(float c) { return (1.8 * c + 32.0); }

