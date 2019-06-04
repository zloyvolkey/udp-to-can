#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_CONNECTIONS 16

pthread_t can_threads[MAX_CONNECTIONS];
pthread_t udp_threads[MAX_CONNECTIONS];

void *udp_listener(void *arg);
void *can_listener(void *arg);
void print_can_frame(struct can_frame frame);

typedef struct
{
    int udp_socket_from;
    int udp_socket_to;
    int can_socket_from;
    int can_socket_to;
    unsigned short port_from;
    unsigned short port_to;
    char *can_device_from;
    char *can_device_to;
    struct in_addr inet_addr_to;
    struct in_addr inet_addr_from;
    struct sockaddr_in udp_socket_address_from;
    struct sockaddr_in udp_socket_address_to;
    struct sockaddr_can can_socket_address_from;
    struct sockaddr_can can_socket_address_to;
} Connection;

Connection connections[MAX_CONNECTIONS];

int main()
{

    connections[0].can_device_from = "vcan0";
    connections[0].can_device_to = "vcan1";
    connections[0].port_from = 8081;
    connections[0].port_to = 8080;
    connections[0].inet_addr_to.s_addr = inet_addr("127.0.0.1");
    connections[0].inet_addr_from.s_addr = inet_addr("127.0.0.1");

    connections[1].can_device_from = "vcan1";
    connections[1].can_device_to = "vcan0";
    connections[1].port_from = 8080;
    connections[1].port_to = 8081;
    connections[1].inet_addr_to.s_addr = inet_addr("127.0.0.1");
    connections[1].inet_addr_from.s_addr = inet_addr("127.0.0.1");

    int i = 0;
    for (i = 0; i < MAX_CONNECTIONS; i++)
    {

        // Банальная проверка конфига
        if (connections[i].can_device_from == NULL)
        {
            break;
        }

        if ((connections[i].udp_socket_from = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            fprintf(stderr, "udp socket open error");
            return 0;
        }
        if ((connections[i].udp_socket_to = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            fprintf(stderr, "udp socket open error");
            return 0;
        }

        int opt_from = true;
        if (setsockopt(connections[i].udp_socket_from, SOL_SOCKET, SO_REUSEADDR, (char *)&opt_from, sizeof(opt_from)) < 0)
        {
            fprintf(stderr, "setsockopt udp error");
            return 0;
        }
        int opt_to = true;
        if (setsockopt(connections[i].udp_socket_to, SOL_SOCKET, SO_REUSEADDR, (char *)&opt_to, sizeof(opt_to)) < 0)
        {
            fprintf(stderr, "setsockopt udp error");
            return 0;
        }

        connections[i].udp_socket_address_from.sin_family = AF_INET;
        connections[i].udp_socket_address_from.sin_port = connections[i].port_from;
        connections[i].udp_socket_address_from.sin_addr.s_addr = connections[i].inet_addr_from.s_addr;

        connections[i].udp_socket_address_to.sin_family = AF_INET;
        connections[i].udp_socket_address_to.sin_port = connections[i].port_to;
        connections[i].udp_socket_address_to.sin_addr.s_addr = connections[i].inet_addr_to.s_addr;

        if (bind(connections[i].udp_socket_from, (struct sockaddr *)&connections[i].udp_socket_address_from, sizeof(connections[i].udp_socket_address_from)) < 0)
        {
            fprintf(stderr, "bind error udp");
            return 0;
        }
        if (bind(connections[i].udp_socket_to, (struct sockaddr *)&connections[i].udp_socket_address_to, sizeof(connections[i].udp_socket_address_to)) < 0)
        {
            fprintf(stderr, "bind error udp");
            return 0;
        }

        if ((connections[i].can_socket_from = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
        {
            fprintf(stderr, "can socket open error");
            return 0;
        }
        if ((connections[i].can_socket_to = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
        {
            fprintf(stderr, "can socket open error");
            return 0;
        }

        int recv_own_msgs = 1;
        if (setsockopt(connections[i].can_socket_from, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs)) < 0)
        {
            fprintf(stderr, "setsockopt can error");
            return 0;
        }
        if (setsockopt(connections[i].can_socket_to, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs)) < 0)
        {
            fprintf(stderr, "setsockopt can error");
            return 0;
        }

        struct ifreq ifr_from;
        strcpy(ifr_from.ifr_name, connections[i].can_device_from);
        if (ioctl(connections[i].can_socket_from, SIOCGIFINDEX, &ifr_from) < 0)
        {
            fprintf(stderr, "ioctl error");
            return 0;
        }
        struct ifreq ifr_to;
        strcpy(ifr_to.ifr_name, connections[i].can_device_to);
        if (ioctl(connections[i].can_socket_to, SIOCGIFINDEX, &ifr_to) < 0)
        {
            fprintf(stderr, "ioctl error");
            return 0;
        }

        connections[i].can_socket_address_from.can_family = AF_CAN;
        connections[i].can_socket_address_from.can_ifindex = ifr_from.ifr_ifindex;

        connections[i].can_socket_address_to.can_family = AF_CAN;
        connections[i].can_socket_address_to.can_ifindex = ifr_to.ifr_ifindex;

        if (bind(connections[i].can_socket_from, (struct sockaddr *)&connections[i].can_socket_address_from,
                 sizeof(connections[i].can_socket_address_from)) < 0)
        {
            fprintf(stderr, "bind error can");
            return 0;
        }
        if (bind(connections[i].can_socket_to, (struct sockaddr *)&connections[i].can_socket_address_to,
                 sizeof(connections[i].can_socket_address_to)) < 0)
        {
            fprintf(stderr, "bind error can");
            return 0;
        }

        if (pthread_create(&udp_threads[i], NULL, &udp_listener, (void *)&connections[i]) < 0)
        {
            fprintf(stderr, "pthread create error");
            return 0;
        }

        if (pthread_create(&can_threads[i], NULL, &can_listener, (void *)&connections[i]) < 0)
        {
            fprintf(stderr, "pthread create error");
            return 0;
        }
    }

    for (i = 0; i < 2; i++)
    {
        if (udp_threads[i] != 0)
        {
            pthread_join(udp_threads[i], NULL);
            pthread_join(can_threads[i], NULL);
        }
    }

    return 0;
}

void *udp_listener(void *arg)
{
    Connection params = *(Connection *)arg;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0x00);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0x00);

    struct can_frame frame;

    fprintf(stderr, "Start transfer from %s:%d to %s\n", inet_ntoa(params.inet_addr_from), params.port_from, params.can_device_to);

    socklen_t udp_address_len = sizeof(params.udp_socket_address_from);
    while (1)
    {
        if (recvfrom(params.udp_socket_from, (struct frame *)&frame, sizeof(frame), 0,
                     (struct sockaddr *)&params.udp_socket_address_from, &udp_address_len) < 0)
        {
            fprintf(stderr, "recvfrom udp error");
        }

        fprintf(stderr, "[%ld] <- UDP %s:%d %s ", pthread_self(), inet_ntoa(params.inet_addr_from), params.port_from, params.can_device_to);
        print_can_frame(frame);

        frame.data[0]++;

        if (sendto(params.can_socket_to, &frame, sizeof(struct can_frame), 0,
                   (struct sockaddr *)&params.can_socket_address_to, sizeof(params.can_socket_address_to)) < 0)
        {
            fprintf(stderr, "send to can error");
            break;
        }
    }

    shutdown(params.udp_socket_from, 2);
    shutdown(params.can_socket_to, 2);
    pthread_exit(0);
}

void *can_listener(void *arg)
{

    Connection params = *(Connection *)arg;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0x00);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0x00);

    fprintf(stderr, "Start transfer from %s to %s:%d \n", params.can_device_from,
            inet_ntoa(params.inet_addr_to), params.port_to);

    struct can_frame frame;

    socklen_t can_addr_len = sizeof(params.can_socket_address_from);
    while (1)
    {
        if (recvfrom(params.can_socket_from, &frame, sizeof(frame), 0, (struct sockaddr *)&params.can_socket_address_from, &can_addr_len) < 0)
        {
            fprintf(stderr, "receive from can error");
            break;
        }

        fprintf(stderr, "[%ld] <- CAN %s:%d %s ", pthread_self(), inet_ntoa(params.inet_addr_to), params.port_to, params.can_device_from);
        print_can_frame(frame);

        if (sendto(params.udp_socket_to, (struct can_frame *)&frame, (sizeof(struct can_frame) + 1), 0,
                   (struct sockaddr *)&params.udp_socket_address_to, sizeof(params.udp_socket_address_to)) < 0)
        {
            fprintf(stderr, "send to udp error");
            break;
        }
        sleep(1);
    }

    shutdown(params.udp_socket_to, 2);
    shutdown(params.can_socket_from, 2);
    pthread_exit(0);
}

void print_can_frame(struct can_frame frame)
{

    fprintf(stderr, "ID %03X [%d] ", frame.can_id, frame.can_dlc);
    for (int i = 0; i < frame.can_dlc; i++)
    {
        fprintf(stderr, "%02X", frame.data[i]);
    }
    fprintf(stderr, "\n");
}