#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#define PORT 1234


static int	sd, sd_current;

void func(int signo)
{
	close(sd_current);
	close(sd);
	exit(0);
}

int
main()
{
	int	addrlen;
	int	input_fd;
	fd_set	read_fd;
	char	buf[512];

	struct sockaddr_in sin;
	struct sockaddr_in pin;
	int sock_opt = 1;

	struct sigaction sa;
	sa.sa_handler = func;

	sigaction(SIGINT, &sa, &sa);

	if ((input_fd = open("/dev/stdin", O_RDONLY)) < 0) {
		perror("cannot open stdin");
		exit(1);
	}

	/* get an internet domain socket */
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (void*)&sock_opt, sizeof(sock_opt)) == -1) {
		perror("setsockopt");
		exit(1);
	}

	/* complete the socket structure */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(PORT);

	/* bind the socket to the port number */
	if (bind(sd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
		perror("bind");
		exit(1);
	}

	/* show that we are willing to listen */
	if (listen(sd, 5) == -1) {
		perror("listen");
		exit(1);
	}

	/* wait for a client to talk to us */
        addrlen = sizeof(pin);
        if ((sd_current = accept(sd, (struct sockaddr *)  &pin, &addrlen)) == -1) {
                perror("accept");
                exit(1);
        }
	
	printf("Connected!!\n");
	while (1) {
		int n;
		FD_ZERO(&read_fd);
		FD_SET(input_fd, &read_fd);
		FD_SET(sd_current, &read_fd);
		if ((select(sd_current+1, &read_fd, NULL, NULL, NULL)) < 0) {
			perror("select");
			exit(1);
		}

		if (FD_ISSET(input_fd, &read_fd)) {
			n = read(input_fd, buf, sizeof(buf));
			write(sd_current, buf, n);
		}

		if (FD_ISSET(sd_current, &read_fd)) {
			n = read(sd_current, buf, sizeof(buf));
			write(STDOUT_FILENO, buf, n);
		}
	}
}
