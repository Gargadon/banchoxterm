#pragma once
#include <libssh2.h>
#ifdef __cplusplus
extern "C" {
#endif
// RFC 4335, including a raw uint32 duration (not an SSH string).
void bancho_encode_ssh_break(unsigned char packet[19], unsigned int channel, unsigned int milliseconds);
int bancho_ssh_channel_break(LIBSSH2_CHANNEL* channel, unsigned int milliseconds);
#ifdef __cplusplus
}
#endif
