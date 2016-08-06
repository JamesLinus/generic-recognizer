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

#endif
