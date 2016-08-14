#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

struct StrBuf {
    char *buf;
    int siz, pos;
};

int strbuf_get_pos(StrBuf *sbuf)
{
    return sbuf->pos;
}

void strbuf_set_pos(StrBuf *sbuf, int pos)
{
    sbuf->pos = pos;
}

StrBuf *strbuf_new(int n)
{
    StrBuf *sbuf;

    sbuf = malloc(sizeof(*sbuf));
    sbuf->buf = malloc(n);
    sbuf->buf[0] = '\0';
    sbuf->siz = n;
    sbuf->pos = 0;
    return sbuf;
}

void strbuf_destroy(StrBuf *sbuf)
{
    free(sbuf->buf);
    free(sbuf);
}

int strbuf_printf(StrBuf *sbuf, char *fmt, ...)
{
    int a, n;
	va_list ap;

	va_start(ap, fmt);
    a = sbuf->siz-sbuf->pos;
    if ((n=vsnprintf(sbuf->buf+sbuf->pos, a, fmt, ap)) >= a) {
        sbuf->siz = sbuf->siz*2+n;
        if ((sbuf->buf=realloc(sbuf->buf, sbuf->siz)) == NULL) {
            fprintf(stderr, "Out of memory");
            exit(EXIT_FAILURE);
        }
        va_end(ap);
        va_start(ap, fmt);
        vsprintf(sbuf->buf+sbuf->pos, fmt, ap);
    }
    sbuf->pos += n;
	va_end(ap);
    return n;
}

void strbuf_flush(StrBuf *sbuf)
{
    fwrite(sbuf->buf, 1, sbuf->pos, stdout);
    sbuf->pos = 0;
}

void strbuf_clear(StrBuf *sbuf)
{
    sbuf->pos = 0;
}

char *strbuf_str(StrBuf *sbuf)
{
    return sbuf->buf;
}

int strbuf_length(StrBuf *sbuf)
{
    return sbuf->pos;
}
