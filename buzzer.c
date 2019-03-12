#include <signal.h>
#include <mraa/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
sig_atomic_t volatile run_flag = 1;

void do_when_interrupted(int signum) {
    if(signum == SIGINT)
        run_flag = 0;
}

int main(void) {
    mraa_gpio_context buzzer;
    buzzer = mraa_gpio_init(62);
    
    if (buzzer == NULL) {
        fprintf(stderr, "Buzzer is null\n");
        exit(2);
    }
    mraa_gpio_dir(buzzer, MRAA_GPIO_OUT);
    signal(SIGINT, do_when_interrupted);
    
    while (run_flag) {
        mraa_gpio_write(buzzer, 1);
        sleep(1);
        
        mraa_gpio_write(buzzer, 0);
        sleep(1);
    }
    
    mraa_gpio_write(buzzer, 0);
    mraa_gpio_close(buzzer);
    
    return 0;
    
}

