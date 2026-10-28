// Adapter for the pinned libssh2 1.11.1, which has no public BREAK API.
// Runs on the connection thread; no other signal request may run concurrently.
#include "libssh2_priv.h"
#include "transport.h"
#include "sshbreak.h"

void bancho_encode_ssh_break(unsigned char packet[19], unsigned int channel, unsigned int milliseconds) {
    unsigned char* cursor = packet;
    *cursor++ = SSH_MSG_CHANNEL_REQUEST;
    _libssh2_store_u32(&cursor, channel);
    _libssh2_store_str(&cursor, "break", 5);
    *cursor++ = 0; // No reply requested; support depends on the server.
    _libssh2_store_u32(&cursor, milliseconds);
}

int bancho_ssh_channel_break(LIBSSH2_CHANNEL* channel, unsigned int milliseconds) {
    LIBSSH2_SESSION* session;
    int result;
    if (!channel)
        return LIBSSH2_ERROR_BAD_USE;
    session = channel->session;
    if (channel->sendsignal_state == libssh2_NB_state_idle) {
        channel->sendsignal_packet_len = 19;
        channel->sendsignal_packet = LIBSSH2_ALLOC(session, 19);
        if (!channel->sendsignal_packet)
            return LIBSSH2_ERROR_ALLOC;
        bancho_encode_ssh_break(channel->sendsignal_packet, channel->remote.id, milliseconds);
        channel->sendsignal_state = libssh2_NB_state_created;
    }
    result = _libssh2_transport_send(session, channel->sendsignal_packet, channel->sendsignal_packet_len, NULL, 0);
    if (result == LIBSSH2_ERROR_EAGAIN)
        return result;
    LIBSSH2_FREE(session, channel->sendsignal_packet);
    channel->sendsignal_packet = NULL;
    channel->sendsignal_state = libssh2_NB_state_idle;
    return result;
}
