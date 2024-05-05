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

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>

static void to_myfs(const char *name, u32 inum) {
    int fd = open(name, O_RDWR, 0644);
    assert(fd >= 0);
    char c;
    u32 off = 0;
    for (;;) {
        int n;
        assert((n = read(fd, &c, 1)) >= 0);
        if (!n)
            break;
        assert(inode_write(inum, &c, 1, off++));
    }
    close(fd);
}

static void to_hostfs(u32 inum, const char *name) {
    int fd = open(name, O_CREAT | O_TRUNC | O_RDWR , 0644);
    assert(fd >= 0);
    char c;
    u32 off = 0;
    for (;;) {
        int n;
        assert((n = inode_read(inum, &c, 1, off++)) >= 0);
        if (!n)
            break;
        assert(write(fd, &c, 1));
    }
    close(fd);
}

int arg_len(char *arg) {
    for (int i = 0; ; i++)
        if (!arg[i] || arg[i] == ' ')
            return i;
}

char *get_arg(char *buf, int n) {
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

int get_args(char *buf, char *args[], int argct) {
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

void simu_ls(char *path) {
    u32 inum;
    if (!path)
        return;
    int l = strlen(path);
    if (!l)
        inum = ROOTINUM;
    else
        inum = fs_lookup(path);
    if (!inum) {
        printf("invalid path\n");
        return;
    }
    struct stat st;
    struct dirent de;
    u32 off = 0;
    stat_inode(inum, &st);
    if (st.type != T_DIR) {
        printf("not a dir\n");
        return;
    }
    while (inode_read(inum, &de, sizeof de, off)) {
        printf("%s\n", de.name);
        off += sizeof de;
    }
}

char *get_name(char *path) {
    // simu_mkdir() ensures path is neither null nor empty, and is preceded by a /
    // interactive_test() ensures path is null-terminated
    int l = strlen(path) - 1;
    for (; l >= 0; l--)
        if (path[l] == '/')
            return &path[l + 1];
    return 0; // logically unreachable
}

void simu_mkdir(char *path) {
    struct stat st;
    struct dirent de;
    u32 i0; // inode of new dir
    u32 i1; // inode of dir under which this new dir is placed
    if (!path) {
        printf("missing path\n");
        return;
    }
    int len = strlen(path);
    if (path[0] != '/') {
        printf("invalid path\n");
        return;
    }
    char *name = get_name(path);
    // separate the target dir path and the new dir name
    if (name - 1 != path)
        name[-1] = 0;
    else
        path = "/";
    printf("make new dir %s under %s\n", name, path);
    if (strlen(name) > MAX_FILE_NAME - 1) {
        printf("name too long\n");
        return;
    }
    i1 = fs_lookup(path);
    if (!i1) {
        printf("path not found\n");
        return;
    }
    stat_inode(i1, &st);
    if (st.type != T_DIR) {
        printf("not a dir\n");
        return;
    }
    i0 = alloc_inode(T_DIR);
    if (!i0) {
        printf("inodes ran out\n");
        return;
    }
    de.inum = i0;
    strcpy(de.name, name);
    inode_write(i1, &de, sizeof de, st.size);
}

#define CMDLEN 16
void interactive_test() {
    for (;;) {
        char buf[CMDLEN];
        printf("> "), fflush(stdout);
        int n = read(STDIN_FILENO, buf, CMDLEN);
        if (n > CMDLEN - 1) {
            int tmp;
            while ((tmp = read(STDIN_FILENO, buf, CMDLEN)) == CMDLEN);
            printf("command too long\n");
            continue;
        }
        buf[n - 1] = 0; // minus 1 to be rid of \n
        char *args[2];
        if (!get_args(buf, args, 2))
            continue;
        if (!strncmp(args[0], "ls", 2))
            simu_ls(args[1]);
        else if (!strncmp(args[0], "mkdir", 2))
            simu_mkdir(args[1]);
        else if (!strncmp(args[0], "quit", 2))
            exit(0);
    }
}

#define RUN(...) \
    do { \
        if (!fork()) { \
            if (execlp(__VA_ARGS__, NULL) == -1) { \
                perror("execl"); \
                exit(EXIT_FAILURE); \
            } \
        } else { \
            wait(NULL); \
        } \
    } while (0)

// random file generator :)
#define MAXWORD 23
// Generate a random number in the range [0, 24) which 
// will be used to determine (1) the length of a word
// and (2) letters in a word
int random24() {
    return rand() % 24;
}
// Generate a random word
int randomword(char word[]) {
    int wlen = random24();
    for (int i = 0; i < wlen; i++)
        word[i] = 'a' + random24();
    word[wlen] = 0;
    return wlen;
}
// Generate a file of random words with the specified size
void randomfile(const char *name, int size) {
    FILE *f;
    srand(time(0));
    if (!(f = fopen(name, "w"))) {
        perror("fopen");
        exit(1);
    }
    char word[MAXWORD + 1];
    int written = 0;
    for (;;) {
        int wlen = randomword(word);
        if (wlen + written + 1 > size)
            break;
        fprintf(f, "%s\n", word);
        written += (wlen + 1);
    }
    if (written < size) {
        char c = '\n';
        for (int i = 0; i < written - size; i++)
            fwrite(&c, sizeof(char), 1, f);
    }
}

void random_test() {
    // create a file
    u32 i0 = alloc_inode(T_REG);
    // generate a random file
    randomfile("random.txt", 1024);
    // migrate the test file from the host fs to my fs
    to_myfs("random.txt", i0);
    // put the file under root dir
    struct dirent de = {
        .inum = i0,
        .name = "random",
    };
    inode_write(ROOTINUM, &de, sizeof(de), 0);
    // look up the file
    u32 i1 = fs_lookup("/random");
    // read from the file to see if it gives us the original text
    to_hostfs(i1, "random1.txt");
    // compare random.txt and random1.txt
    RUN("diff", "diff", "random.txt", "random1.txt");
    // cleanup
    remove("random.txt");
    remove("random1.txt");
}

int main(int argc, char *argv[]) 
{
    if (argc < 3) {
        fprintf(stderr, "usage: test <test_mode> <vhd_path>\n test_mode [interactive random]");
        exit(1);
    }
    char *tmod = argv[1];
    char *path = argv[2];
    if (!strncmp(tmod, "interactive", strlen("interactive"))) {
        fs_init(path);
        interactive_test();
    } else if (!strncmp(tmod, "random", strlen("random"))) {
        fs_init(path);
        random_test();
    } else {
        fprintf(stderr, "test_mode can either be interactive or random\n");
        exit(1);
    }
}
