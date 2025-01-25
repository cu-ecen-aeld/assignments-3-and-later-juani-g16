#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "queue.h"

#define SERVER_PORT "9000"
#define FILEPATH "/var/tmp/aesdsocketdata"
#define BUFFSIZE 100 * 1024

bool caught_signal = false;
pthread_mutex_t file_mutex;
ssize_t total_bytes = 0;

typedef struct slist_data_s slist_data_t;
struct slist_data_s
{
    pthread_t thread_id;
    int acceptedsocket;
    bool thread_complete;
    char ipaddress[INET_ADDRSTRLEN];
    SLIST_ENTRY(slist_data_s)
    entries;
};
SLIST_HEAD(slisthead, slist_data_s)
head;
slist_data_t *tdatap = NULL;

static void signal_handler(int signal_number);
static int init_socket();
static void *handle_connection(void *arg);
void *append_timestamp(void *args);
void write_to_file(const char *buffer);
void list_cleanup(void);

int main(int argc, char const *argv[])
{
    int socketfd;
    struct sockaddr cli_addr;
    socklen_t clilen;
    struct sigaction new_action;
    pid_t daemonpid;
    pthread_t timer_thread;
    slist_data_t *datap = NULL;

    pthread_mutex_init(&file_mutex, NULL);

    openlog(NULL, 0, LOG_USER);

    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    if (sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        printf("Error %d (%s) registering for SIGTERM", errno, strerror(errno));
    }
    if (sigaction(SIGINT, &new_action, NULL) != 0)
    {
        printf("Error %d (%s) registering for SIGINT", errno, strerror(errno));
    }

    if ((socketfd = init_socket()) == -1)
    {
        return -1;
    }

    if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "-d") == 0)
    {
        syslog(LOG_INFO, "Running program as daemon");

        daemonpid = daemon(0, 0);
        if (daemonpid == -1)
        {
            syslog(LOG_ERR, "Error running program as daemon: %s.", strerror(errno));
            return -1;
        }
    }

    if (listen(socketfd, 10) == -1)
    {
        syslog(LOG_ERR, "Error with listen: %s.", strerror(errno));
        return -1;
    }

    SLIST_INIT(&head);

    // Create a thread for appending timestamps
    if (pthread_create(&timer_thread, NULL, append_timestamp, NULL) != 0)
    {
        fprintf(stderr, "Error creating thread\n");
        return -1;
    }

    while (1)
    {

        datap = malloc(sizeof(slist_data_t));
        clilen = sizeof(cli_addr);

        if (datap == NULL)
        {
            perror("Error allocating datap");
            continue;
        }

        datap->acceptedsocket = accept(socketfd, &cli_addr, &clilen);

        if (datap->acceptedsocket == -1)
        {
            if (caught_signal)
            {
                list_cleanup();
                pthread_join(timer_thread, NULL);
                close(socketfd);
                remove(FILEPATH);
                syslog(LOG_DEBUG, "Caught signal, exiting");
                pthread_mutex_destroy(&file_mutex);
                break;
            }
            free(datap);
            syslog(LOG_ERR, "Error accepting connection: %s.", strerror(errno));
            continue;
        }
        else
        {
            datap->thread_complete = false;
            inet_ntop(AF_INET, cli_addr.sa_data, datap->ipaddress, INET_ADDRSTRLEN);
            syslog(LOG_INFO, "Accepted connection from %s", datap->ipaddress);
            pthread_create(&datap->thread_id, NULL, &handle_connection, datap);
            SLIST_INSERT_HEAD(&head, datap, entries);
        }
        slist_data_t *elem = malloc(sizeof(slist_data_t));
        SLIST_FOREACH_SAFE(elem, &head, entries, tdatap)
        {
            if (elem->thread_complete == true)
            {
                pthread_join(elem->thread_id, NULL);
                SLIST_REMOVE(&head, elem, slist_data_s, entries);
            }
        }
        free(elem);
    }

    return 0;
}

void list_cleanup(void)
{
    slist_data_t *elem = malloc(sizeof(slist_data_t));
    SLIST_FOREACH_SAFE(elem, &head, entries, tdatap)
    {
        pthread_join(elem->thread_id, NULL);
        SLIST_REMOVE(&head, elem, slist_data_s, entries);
    }
    free(elem);
}

static void *handle_connection(void *arg)
{
    int datafd;
    slist_data_t *datap = (slist_data_t *)arg;
    ssize_t bytes_received, bytes_written, bytes_sent;
    char *buffer = (char *)calloc(BUFFSIZE, sizeof(char));
    if (buffer == NULL)
    {
        syslog(LOG_ERR, "Error reserving buffer size: %s.", strerror(errno));
        close(datap->acceptedsocket);
        datap->thread_complete = true;
        return NULL;
    }

    while ((bytes_received = read(datap->acceptedsocket, buffer, BUFFSIZE - 1)) > 0)
    {
        if (bytes_received == -1)
        {
            syslog(LOG_ERR, "Error receiving data from client: %s.", strerror(errno));
            break;
        }

        if (buffer[bytes_received - 1] == '\n')
        {
            /*File operations */
            pthread_mutex_lock(&file_mutex);
            datafd = open(FILEPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
            if (datafd < 0)
            {
                syslog(LOG_ERR, "Error opening local file: %s.", strerror(errno));
                break;
            }
            bytes_written = write(datafd, buffer, bytes_received);
            total_bytes += bytes_written;
            lseek(datafd, 0, SEEK_SET);
            if (bytes_written == -1)
            {
                syslog(LOG_ERR, "Error writing local file: %s.", strerror(errno));
                break;
            }
            memset(buffer, 0, BUFFSIZE);
            bytes_received = read(datafd, buffer, total_bytes);
            close(datafd);
            pthread_mutex_unlock(&file_mutex);
            /*End of file operations */
            if (bytes_received == -1)
            {
                syslog(LOG_ERR, "Error reading local file: %s.", strerror(errno));
                break;
            }
            bytes_sent = write(datap->acceptedsocket, buffer, bytes_received);
            if (bytes_sent == -1)
            {
                syslog(LOG_ERR, "Error reading file: %s.", strerror(errno));
                break;
            }
            syslog(LOG_INFO, "Closed connection from %s", datap->ipaddress);
            break;
        }
    }
    free(buffer);
    close(datap->acceptedsocket);
    pthread_mutex_lock(&file_mutex);
    close(datafd);
    pthread_mutex_unlock(&file_mutex);
    datap->thread_complete = true;
    return NULL;
}

static int init_socket()
{
    int socketfd, reuseval = 1;
    struct addrinfo hints, *results, *record;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((getaddrinfo(NULL, SERVER_PORT, &hints, &results)) != 0)
    {
        syslog(LOG_ERR, "Failed to translate client socket.");
        return -1;
    }

    printf("Client socket translated.\n");

    // loop through all the results and bind to the first we can
    for (record = results; record != NULL; record = record->ai_next)
    {
        if ((socketfd = socket(record->ai_family, record->ai_socktype, record->ai_protocol)) == -1)
        {
            syslog(LOG_ERR, "Error creating socket: %s.", strerror(errno));
            continue;
        }

        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &reuseval, sizeof(int)) == -1)
        {
            syslog(LOG_ERR, "Error with setsockopt: %s.", strerror(errno));
            return -1;
        }

        if (bind(socketfd, record->ai_addr, record->ai_addrlen) == -1)
        {
            close(socketfd);
            syslog(LOG_ERR, "Error with bind: %s.", strerror(errno));
            continue;
        }

        break;
    }

    freeaddrinfo(results);

    if (record == NULL)
    {
        fprintf(stderr, "server: failed to bind \n");
        return -1;
    }

    return socketfd;
}

static void signal_handler(int signal_number)
{
    int errno_saved = errno;

    switch (signal_number)
    {
    case SIGINT:
    case SIGTERM:
        caught_signal = true;
        break;
    default:
        break;
    }
    errno = errno_saved;
}

void *append_timestamp(void *args)
{
    char timestamp[100];
    time_t current_time;
    struct tm *time_info;

    // for (int i = 0; i < 10; i++)
    while (!caught_signal)
    {
        // Get current system time
        current_time = time(NULL);
        if (current_time == ((time_t)-1))
        {
            perror("Error getting current time");
            return NULL;
        }

        // Convert to local time and format as RFC 2822
        time_info = localtime(&current_time);
        if (time_info == NULL)
        {
            perror("Error converting to local time");
            return NULL;
        }

        // Format timestamp: "timestamp:Thu, 23 Jan 2025 19:00:00\n"
        if (strftime(timestamp, sizeof(timestamp), "timestamp: %a, %d %b %Y %T\n", time_info) == 0)
        {
            fprintf(stderr, "Error formatting timestamp\n");
            return NULL;
        }

        // Append timestamp to file
        // printf("Appending timestamp: %s\n", timestamp);
        write_to_file(timestamp);

        // Sleep for 10 seconds before the next append
        sleep(10);
    }
    return NULL;
}

void write_to_file(const char *buffer)
{
    FILE *file;

    // Open file in append mode
    pthread_mutex_lock(&file_mutex);
    file = fopen(FILEPATH, "a");
    if (file == NULL)
    {
        perror("Error opening file");
        pthread_mutex_unlock(&file_mutex);
    }
    else
    {
        if (fputs(buffer, file) == EOF)
        {
            perror("Error writing to file");
            fclose(file);
            pthread_mutex_unlock(&file_mutex);
        }
        else
        {
            // Close the file
            fclose(file);
            pthread_mutex_unlock(&file_mutex);
        }
    }
}