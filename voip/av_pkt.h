#ifndef AV_PKT_H_
#define AV_PKT_H_

typedef struct _AVPacket {
	unsigned short mt;
	unsigned short len;
}__attribute__((packed)) AVPacket;

#define HDR_SZ    sizeof(AVPacket)

#endif
