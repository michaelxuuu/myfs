/**
 * @file testprog.c
 * @author
 * @brief 
 * @version 0.1
 * @date 2024-05-03
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include "fs.h"
#include "file.h"

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int arg_len(char *arg) {
    for (int i = 0; ; i++)
        if (!arg[i] || arg[i] == ' ')
            return i;
}

static char *get_arg(char *buf, int n) {
    int nn = 0, s = 0;
    for (int i = 0; buf[i]; i++) {
        // Start of an arg
        if (buf[i] != ' ' && !s) {
            if (nn++ == n)
                return &buf[i];
            s = !s;
        }
        // Start of spaces
        if (buf[i] == ' ' && s)
            s = !s;
    }
    return 0;
}

static int parse_args(char *buf, char *args[], int argct) {
    int i = 0;
    int cnt = 0;
    // Get 'argct' args or fewer if not as many
    for (; i < argct; i++)
        if (!(args[i] = get_arg(buf, i)))
            break;
    cnt = i;
    // Null-terminate the args in buf[].
    // Do this after since nulling buf elements interferes get_arg()
    for (i-- ; i >= 0; i--)
        args[i][arg_len(args[i])] = 0;
    return cnt;
}

static void cmd_ls(char *path) {
    int fd;
    if (!path)
        return;
    if ((fd = myfs_open(path, O_RDONLY)) == -1) {
        fprintf(stderr, "myfs_open failed\n");
        return;
    }
    struct dirent de;
    while (myfs_read(fd, &de, sizeof de)) {
        if (!de.inum)
            continue;
        printf("%s\n", de.name);
    }
    assert(!myfs_close(fd));
}

static void cmd_mkdir(char *path) {
    if (myfs_mknod(path, T_DIR)) {
        fprintf(stderr, "myfs_mkdir failed\n");
        return;
    }
}

static void cmd_migrate(char *mypath, char *hostpath) {
    int hostfd = open(hostpath, O_RDONLY, 0644);
    if (hostfd < 0) {
        fprintf(stderr, "%s not found in host fs\n", hostpath);
        return;
    }
    int myfd;
    if (myfs_mknod(mypath, T_REG)) {
        fprintf(stderr, "failed to create %s in myfs\n", mypath);
        return;
    }
    assert((myfd = myfs_open(mypath, O_WRONLY)) >= 0);
    char c;
    for (;;) {
        int n;
        assert((n = read(hostfd, &c, 1)) >= 0);
        if (!n)
            break;
        assert(myfs_write(myfd, &c, 1));
    }
    assert(close(hostfd) >= 0);
    assert(myfs_close(myfd) >= 0);
}

static void cmd_retrieve(char *hostpath, char *mypath) {
    int hostfd = open(hostpath, O_CREAT | O_TRUNC | O_WRONLY , 0644);
    int myfd = myfs_open(mypath, O_RDONLY);
    if (hostfd < 0) {
        perror("host open");
        return;
    }
    if (myfd < 0) {
        fprintf(stderr, "%s not found in myfs\n", mypath);
        close(hostfd);
        return;
    }
    char c;
    for (;;) {
        int n;
        assert((n = myfs_read(myfd, &c, 1)) >= 0);
        if (!n)
            break;
        assert(write(hostfd, &c, 1));
    }
    assert(close(hostfd) >= 0);
    assert(myfs_close(myfd) >= 0);
}

#define CMDLEN 32
int main(int argc, char *argv[]) 
{
    if (argc < 2) {
        fprintf(stderr, "usage: test <vhd_path>\n");
        exit(1);
    }
    fs_init(argv[1]);
    for (;;) {
        char cmd[CMDLEN];
        printf("> "), fflush(stdout);
        fgets(cmd, CMDLEN, stdin);
        cmd[strlen(cmd) - 1] = 0;
        char *args[3];
        int cnt = parse_args(cmd, args, 3);
        if (!cnt)
            continue;
        if (!strncmp(args[0], "ls", 2)) {
            if (cnt < 2)
                fprintf(stdout, "usage: ls <path>\n");
            else
                cmd_ls(args[1]);
        } else if (!strncmp(args[0], "mkdir", 5)) {
            if (cnt < 2)
                fprintf(stdout, "usage: mkdir <path>\n");
            else
                cmd_mkdir(args[1]);
        } else if (!strncmp(args[0], "migrate", 7)) {
            if (cnt < 3)
                fprintf(stdout, "usage: migrate <myfs_path> <host_path>\n");
            else
                cmd_migrate(args[1], args[2]);
        } else if (!strncmp(args[0], "retrieve", 8)) {
            if (cnt < 3)
                fprintf(stdout, "usage: retrieve <host_path> <myfs_path>\n");
            else
                cmd_retrieve(args[1], args[2]);
        } else if (!strncmp(args[0], "quit", 4))
            exit(0);
    }
}
