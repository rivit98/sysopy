#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"


int received_sig_counter = 0;
int sender_pid = 0;
MODE mode;

void sigusr1_handler(int signo, siginfo_t *info, void *context){
	printf("sent reply to: %d\n", info->si_pid);
	send_signal(received_sig_counter, info->si_pid, mode, get_signal1(mode));
	received_sig_counter++;
}

void sigusr2_handler(int signo, siginfo_t *info, void *context){
	sender_pid = info->si_pid;
}

void setup_signals(MODE mode){
	sigset_t block_mask;
	sigfillset(&block_mask);
	sigdelset(&block_mask, get_signal1(mode));
	sigdelset(&block_mask, get_signal2(mode));

	if(sigprocmask(SIG_SETMASK, &block_mask, NULL) != 0){
		puts("sigprocmask error");
		exit(EXIT_FAILURE);
	}


	struct sigaction action;
	action.sa_sigaction = sigusr1_handler;
	action.sa_flags = SA_SIGINFO;
	sigemptyset(&action.sa_mask);

	if(sigaction(get_signal1(mode), &action, NULL) == -1){
		printf("Cant catch signal: %d\n", get_signal1(mode));
		return;
	}


	action.sa_sigaction = sigusr2_handler;
	action.sa_flags = SA_SIGINFO;
	sigemptyset(&action.sa_mask);

	if(sigaction(get_signal2(mode), &action, NULL) == -1){
		printf("Cant catch signal: %d\n", get_signal2(mode));
		return;
	}
}

int main(int argc, char** argv) {
	if(argc != 2){
		printf("Usage: catcher mode\n");
		return 1;
	}

	printf("%d\n", getpid());
	mode = parse_mode(argv[1]);

	setup_signals(mode);

	while(!sender_pid){

	}

	printf("Total received signals = %d\n", received_sig_counter);
	send_signal(0, sender_pid, mode, get_signal2(mode));

	return 0;
}
