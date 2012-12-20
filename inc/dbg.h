#ifndef DBG_H_
#define DBG_H_

#ifdef   DEBUG
#define  DBG(fmt, ...)   \
         do {               \
            printf("File:%s,Line:%d,Func:%s-", __FILE__, __LINE__, __FUNCTION__); \
            printf(fmt, ##__VA_ARGS__); \
         } while (0)
#else
#define  DBG(fmt, ...)
#endif

#endif
