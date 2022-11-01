/*
 * Copyright (c) 2020 Fastly, Kazuho
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef h2o__dsr_h
#define h2o__dsr_h

#include <stddef.h>
#include <inttypes.h>
#include "picotls.h"
#include "quicly.h"
#include "h2o/linklist.h"
#include "h2o/memory.h"
#include "h2o/socket.h"

/**
 * DSR request
 */
typedef struct st_h2o_dsr_req_t {
    /**
     * HTTP version of front-end connection
     */
    uint32_t http_version;
    union {
        struct st_h2o_dsr_req_http3_t {
            struct st_h2o_dsr_req_quic_t {
                /**
                 * The transport protocol for which DSR is performed (e.g., quic-29). This value indicates how the packet should be
                 * protected, aside from the cipher-suite being used.
                 */
                uint32_t version;
                /**
                 * The cipher-suite being used, as the ID registered to the TLS Cipher Suites registry.
                 */
                uint16_t cipher;
                /**
                 * address from which QUIC packets should be sent
                 */
                quicly_address_t address;
            } quic;
        } h3;
    };
} h2o_dsr_req_t;

/**
 * An DSR instruction.
 */
typedef struct st_h2o_dsr_quic_decoded_instruction_t {
    enum { H2O_DSR_DECODED_INSTRUCTION_SET_CONTEXT, H2O_DSR_DECODED_INSTRUCTION_SEND_PACKET } type;
    union {
        struct {
            quicly_address_t dest_addr;
            ptls_iovec_t header_protection_secret;
            ptls_iovec_t aead_secret;
        } set_context;
        struct {
            /**
             * the first few bytes of the packet payload generated by the QUIC endpoint
             */
            ptls_iovec_t prefix;
            /**
             * starting offset of the response to be sent
             */
            uint64_t body_off;
            /**
             * length of the chunk to be sent
             */
            uint16_t body_len;
            uint64_t _packet_number;
            uint16_t _packet_from;
            uint16_t _packet_payload_from;
        } send_packet;
    } data;
} h2o_dsr_quic_decoded_instruction_t;

typedef struct st_h2o_dsr_quic_encoder_state_t {
    unsigned context_sent : 1;
} h2o_dsr_quic_encoder_state_t;

typedef struct st_h2o_dsr_quic_packet_encryptor_t {
    quicly_context_t *ctx;
    ptls_cipher_suite_t *cipher_suite;
    ptls_cipher_context_t *header_protection_ctx;
    ptls_aead_context_t *aead_ctx;
    uint8_t aead_secret[PTLS_MAX_DIGEST_SIZE];
} h2o_dsr_quic_packet_encryptor_t;

/**
 * serializes a DSR request to header field value. The memory is allocated using `malloc`.
 */
h2o_iovec_t h2o_dsr_serialize_req(h2o_dsr_req_t *req);
/**
 * parses a DSR request and returns a boolean indicating if successful
 */
int h2o_dsr_parse_req(h2o_dsr_req_t *req, const char *value, size_t value_len, uint16_t default_port);
/**
 *
 */
void h2o_dsr_quic_add_instruction(h2o_buffer_t **buf, h2o_dsr_quic_encoder_state_t *state, struct sockaddr *dest_addr,
                                  quicly_detached_send_packet_t *detached, uint64_t body_off, uint16_t body_len);
/**
 * Decodes one DSR instruction. Returns size of the instruction, 0 if invalid, -1 if incomplete.
 */
ssize_t h2o_dsr_quic_decode_instruction(h2o_dsr_quic_decoded_instruction_t *instruction, const uint8_t *src, size_t len);
/**
 *
 */
int h2o_dsr_init_quic_packet_encryptor(h2o_dsr_quic_packet_encryptor_t *encryptor, quicly_context_t *ctx, uint32_t quic_version,
                                       uint16_t cipher_id);
/**
 *
 */
void h2o_dsr_dispose_quic_packet_encryptor(h2o_dsr_quic_packet_encryptor_t *encryptor);
/**
 * Updates the cryptographic context.
 */
int h2o_dsr_quic_packet_encryptor_set_context(h2o_dsr_quic_packet_encryptor_t *encryptor, ptls_iovec_t header_protection_secret,
                                              ptls_iovec_t aead_secret);
/**
 * Builds a QUIC packet using a given instruction, encrypting the datagram in-place. Before encrypting a packet using an
 * instruction, `h2o_dsr_setup_quic_packet_encryptor` must be called to setup the encryption context of the instruction group to
 * which the instruction belongs. It is the caller's responsibility to setup `datagram` by writing the prefix and the content as
 * specified by the instruction before invoking this function.
 */
void h2o_dsr_encrypt_quic_packet(h2o_dsr_quic_packet_encryptor_t *encryptor, h2o_dsr_quic_decoded_instruction_t *instruction,
                                 ptls_iovec_t datagram);

#endif
