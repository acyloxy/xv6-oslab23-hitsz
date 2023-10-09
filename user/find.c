#include <kernel/types.h>

#include <kernel/fs.h>
#include <kernel/stat.h>
#include <user/user.h>

void find(char *haystack, char *needle) {
    int fd = open(haystack, 0);
    struct dirent de;
    char entry_path[512], *p;
    strcpy(entry_path, haystack);
    p = entry_path + strlen(haystack);
    *p++ = '/';
    while (read(fd, &de, sizeof(struct dirent)) == sizeof(struct dirent)) {
        if (!de.inum) continue;
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
        memcpy(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        struct stat st;
        if (stat(entry_path, &st) < 0) {
            fprintf(2, "cannot stat %s, skipped\n", haystack);
            continue;
        }
        switch (st.type) {
            case T_DIR:
                find(entry_path, needle);
                break;
            case T_FILE:
                if (!strcmp(de.name, needle)) {
                    printf("%s\n", entry_path);
                }
                break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: find <haystack> <needle>\n");
        exit(-1);
    }
    char *haystack = argv[1];
    char *needle = argv[2];
    struct stat st;
    if (stat(haystack, &st) < 0) {
        fprintf(2, "cannot stat %s\n", haystack);
        exit(-1);
    }
    if (st.type != T_DIR) {
        fprintf(2, "%s is not a directory\n", haystack);
        exit(-1);
    }
    find(haystack, needle);
    exit(0);
}
