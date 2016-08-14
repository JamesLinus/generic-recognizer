#ifndef UTIL_H_
#define UTIL_H_

#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

unsigned hash(char *s);
char *read_file(char *path);

typedef struct StrBuf StrBuf;
StrBuf *strbuf_new(int n);
void strbuf_destroy(StrBuf *sbuf);
int strbuf_printf(StrBuf *sbuf, char *fmt, ...);
void strbuf_clear(StrBuf *sbuf);
void strbuf_flush(StrBuf *sbuf);
int strbuf_get_pos(StrBuf *sbuf);
void strbuf_set_pos(StrBuf *sbuf, int pos);
char *strbuf_str(StrBuf *sbuf);
int strbuf_length(StrBuf *sbuf);

#endif
