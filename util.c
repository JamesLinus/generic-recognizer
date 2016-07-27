#include <stdio.h>
#include <stdlib.h>

unsigned hash(char *s)
{
    unsigned hash_val;

    for (hash_val = 0; *s != '\0'; s++)
        hash_val = (unsigned)*s + 31*hash_val;
    return hash_val;
}

char *read_file(char *path)
{
    FILE *fp;
    char *buf;
    unsigned len;

    if ((fp=fopen(path, "rb")) == NULL)
        return NULL;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    buf = malloc(len+1);
    len = fread(buf, 1, len, fp);
    buf[len] = '\0';
    fclose(fp);
    return buf;
}
