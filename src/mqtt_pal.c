#include <mqtt.h>

/** 
 * @file 
 * @brief Implements @ref mqtt_pal_sendall and @ref mqtt_pal_recvall and 
 *        any platform-specific helpers you'd like.
 * @cond Doxygen_Suppress
 */


#ifdef __unix__

#ifdef USE_OPENSSL

int openssl_loaded = 0;

mqtt_pal_socket_handle mqtt_pal_sockopen(const char* addr, const char* port) {
    if (!openssl_loaded) {
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();
        openssl_loaded = 1;
    }

    SSL_CTX * ctx = SSL_CTX_new(SSLv23_client_method());
    SSL * ssl;

    if(! SSL_CTX_load_verify_locations(ctx, "/home/liam/Downloads/mosquitto.org.crt", NULL))
    {
        /* Handle failed load here */
        printf("Failed to load PEM\n");
        exit(1);
    }


    BIO* bio = BIO_new_ssl_connect(ctx);
    BIO_get_ssl(bio, &ssl);
    printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
    BIO_set_conn_hostname(bio, addr);
    printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
    BIO_set_nbio(bio, 1);
    printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
    BIO_set_conn_port(bio, port);
    printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));

    int start_time = time(NULL);
    while(BIO_do_connect(bio) <= 0 && (int)time(NULL) - start_time < 10);

    if (BIO_do_connect(bio) <= 0) {
        printf("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
        fprintf(stderr, "Failed to open socket: BIO_do_connect returned <= 0\n");
        return NULL;
    }

    if(SSL_get_verify_result(ssl) != X509_V_OK)
    {
        /* Handle the failed verification */
        printf("Failed to verify x509 cert\n");
        exit(1);
    }

    return bio;
}


void mqtt_pal_sockclose(mqtt_pal_socket_handle socketfd) {
    BIO_free_all(socketfd);
}

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle bio, const void* buf, size_t len, int flags) {
    size_t sent = 0;
    while(sent < len) {
        int tmp = BIO_write(bio, buf + sent, len - sent);
        if (tmp > 0) {
            sent += (size_t) tmp;
        } else if (tmp <= 0 && !BIO_should_retry(bio)) {
            return MQTT_ERROR_SOCKET_ERROR;
        }
    }
    return sent;
}

ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle bio, void* buf, size_t bufsz, int flags) {
    const void const *start = buf;
    int rv;
    do {
        rv = BIO_read(bio, buf, bufsz);
        if (rv > 0) {
            /* successfully read bytes from the socket */
            buf += rv;
            bufsz -= rv;
        } else if (!BIO_should_retry(bio)) {
            /* an error occurred that wasn't "nothing to read". */
            return MQTT_ERROR_SOCKET_ERROR;
        }
    } while (!BIO_should_read(bio));

    return (ssize_t)(buf - start);
}


#else /* don't use OPENSSL */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>


ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void* buf, size_t len, int flags) {
    size_t sent = 0;
    while(sent < len) {
        ssize_t tmp = send(fd, buf + sent, len - sent, flags);
        if (tmp < 1) {
            return MQTT_ERROR_SOCKET_ERROR;
        }
        sent += (size_t) tmp;
    }
    return sent;
}

ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void* buf, size_t bufsz, int flags) {
    const void const *start = buf;
    ssize_t rv;
    do {
        rv = recv(fd, buf, bufsz, flags);
        if (rv > 0) {
            /* successfully read bytes from the socket */
            buf += rv;
            bufsz -= rv;
        } else if (rv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            /* an error occurred that wasn't "nothing to read". */
            return MQTT_ERROR_SOCKET_ERROR;
        }
    } while (rv > 0);

    return buf - start;
}


mqtt_pal_socket_handle mqtt_pal_sockopen(const char* addr, const char* port) {
    struct addrinfo hints = {0};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; /* Must be TCP */
    int sockfd = -1;
    int rv;
    struct addrinfo *p, *servinfo;

    /* get address information */
    rv = getaddrinfo(addr, port, &hints, &servinfo);
    if(rv != 0) {
        fprintf(stderr, "Failed to open socket (getaddrinfo): %s\n", gai_strerror(rv));
        return -1;
    }

    /* open the first possible socket */
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        /* connect to server */
        rv = connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
        if(rv == -1) continue;
        break;
    }  

    /* free servinfo */
    freeaddrinfo(servinfo);

    /* make non-blocking */
    if (sockfd != -1) fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);

    /* return the new socket fd */
    return sockfd;  
}

void mqtt_pal_sockclose(mqtt_pal_socket_handle socketfd) {
    close(socketfd);
}

#endif /* don't use OPENSSL */

#endif

/** @endcond */