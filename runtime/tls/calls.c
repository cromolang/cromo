// The functions of TLS that std.net calls for every Connection (D235): they move
// the bytes between the socket and the buffers of a session, and pass the rest to
// the calls of the session (session.h), without referring to mbedTLS.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "glue.h"
#include "session.h"

// What the operations return when they cannot finish: -1 when mbedTLS needs more
// bytes from the socket, -2 for an error, which stays: cromo_tls_error says it.

int64_t cromo_tls_handshake(void* session) { return ((Session*)session)->status.calls->handshake(session); }

// Up to `most` bytes of data, or 0 when the other side has closed the connection.
int64_t cromo_tls_read(void* session, uint8_t* out, int64_t most) {
    return ((Session*)session)->status.calls->read(session, out, most);
}

// The bytes taken, maybe fewer than `size`: they are in the output, to be sent.
int64_t cromo_tls_write(void* session, const uint8_t* data, int64_t size) {
    return ((Session*)session)->status.calls->write(session, data, size);
}

// The alert close_notify in the output, after a handshake that went well.
void cromo_tls_close(void* session) { ((Session*)session)->status.calls->close(session); }

void cromo_tls_end(void* session) { ((Session*)session)->status.calls->end(session); }

// A connection accepted by a server; the handshake happens with its first read
// or write, so that a slow client does not stop the others.
void* cromo_tls_accept(void* server) { return ((Server*)server)->status.calls->accept(server); }

void cromo_tls_server_end(void* server) { ((Server*)server)->status.calls->serverEnd(server); }

// The text of the error of a session or of a server, in `out`: its length.
int64_t cromo_tls_error(void* object, uint8_t* out, int64_t most) {
    Status* status = object;
    return status->calls->error(status, out, most);
}

// Room for `most` bytes from the socket, after those that wait.
uint8_t* cromo_tls_input(void* object, int64_t most) {
    Session* session = object;
    int64_t waiting = session->inputEnd - session->inputStart;
    if (session->inputStart > 0) {
        memmove(session->input, session->input + session->inputStart, (size_t)waiting);
        session->inputStart = 0;
        session->inputEnd = waiting;
    }
    if (waiting + most > session->inputCapacity) {
        session->input = cromo_realloc(session->input, waiting + most);
        session->inputCapacity = waiting + most;
    }
    return session->input + session->inputEnd;
}

// The bytes put in the room of cromo_tls_input; 0 at the end of the socket.
void cromo_tls_received(void* object, int64_t count) {
    Session* session = object;
    if (count <= 0) session->ended = 1;
    else session->inputEnd += count;
}

// The bytes that mbedTLS has written and that wait to be sent.
int64_t cromo_tls_pending(void* object) {
    Session* session = object;
    return session->outputEnd - session->outputStart;
}

uint8_t* cromo_tls_output(void* object) {
    Session* session = object;
    return session->output + session->outputStart;
}

void cromo_tls_sent(void* object, int64_t count) {
    Session* session = object;
    session->outputStart += count;
    if (session->outputStart >= session->outputEnd) session->outputStart = session->outputEnd = 0;
}
