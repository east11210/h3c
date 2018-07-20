#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h3c.h"

#ifndef BUILD_VERSION
#define BUILD_VERSION "v0.1.0"
#endif

#ifndef BUILD_HASH
#define BUILD_HASH "dev"
#endif

int main(int argc, char *argv[]) {
    fprintf(stdout, "h3c %s b%s : Copyright (c) 2018 Tommy Lau <tommy@gen-new.com>\n",
            BUILD_VERSION, BUILD_HASH);
    //fprintf(stdout, "Copyright (c) 2018 Tommy Lau <tommy@gen-new.com>\n\n");
    fprintf(stdout, "A command line tool for H3C 802.1X authentication\n\n");

    static struct option options[] = {
            {"help",      no_argument,       NULL, 'h'},
            {"user",      required_argument, NULL, 'u'},
            {"password",  required_argument, NULL, 'p'},
            {"interface", optional_argument, NULL, 'i'},
            {"method",    optional_argument, NULL, 'd'},
            {NULL, 0,                        NULL, 0}
    };

    int c;
    char *interface = NULL;
    char *username = NULL;
    char *password = NULL;
    bool md5 = true;

    while ((c = getopt_long(argc, argv, "hu:p:i:m:", options, NULL)) != -1) {
        switch (c) {
            case 'h':
                fprintf(stdout,
                        "Usage: h3c [options]\n"
                        "-h, --help          This help text\n"
                        "-i, --interface     Network interface (Default: en0)\n"
                        "-m, --method        EAP-MD5 CHAP Method [md5 / xor] (Default: md5)\n"
                        "-p, --password      Password\n"
                        "-u, --username      Username\n"
                );
                return EXIT_SUCCESS;

            case 'i':
                interface = optarg;
                break;

            case 'm':
                if (strcmp(optarg, "md5") == 0)
                    md5 = true;
                else if (strcmp(optarg, "xor") == 0)
                    md5 = false;
                else {
                    fprintf(stderr, "Method can only be either \"md5\" or \"xor\"\n");
                    return EXIT_FAILURE;
                }
                break;

            case 'p':
                password = optarg;
                break;

            case 'u':
                username = optarg;
                break;

            default:
                fprintf(stderr, "Invalid arguments.\n");
                return EXIT_FAILURE;
        }
    }

    // Must run as root
    if (geteuid() != 0) {
        fprintf(stderr, "You have to run this program as root.\n");
        exit(EXIT_FAILURE);
    }

    if (interface == NULL)
        interface = "en0";

    if (username == NULL) {
        fprintf(stderr, "Please specific username.\n");
        return EXIT_FAILURE;
    }

    if (password == NULL) {
        password = getpass("Password: ");
    }

    if (strlen(password) == 0) {
        fprintf(stderr, "Incorrect password.");
        return EXIT_FAILURE;
    }

    h3c_ctx_t hc = {interface, username, password, NULL};

    if (h3c_init(&hc) != H3C_OK) {
        fprintf(stderr, "Ethernet interface initialize fail.\n");
        return EXIT_FAILURE;
    }

    h3c_run();

    return EXIT_SUCCESS;
}
