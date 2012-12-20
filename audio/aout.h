#ifndef AOUT_H_
#define AOUT_H_

extern int   mono2stero(unsigned char *dst, unsigned char *src, int bytes);
extern int   aout_open(int rate, int channels);
extern void  aout_close(int imed);
extern void  aout_pause(void);
extern void  aout_resume(void);
extern void  aout_flush(void);
extern int   aout_write(char *buffer, int count);
extern int   aout_write_nonblock(char *buffer, int count);

#endif

