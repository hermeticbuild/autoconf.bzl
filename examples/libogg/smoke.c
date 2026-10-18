// SPDX-License-Identifier: Apache-2.0

#include <ogg/ogg.h>
#include <string.h>
int main(void) {
    ogg_stream_state stream;
    ogg_page page;
    unsigned char bytes[] = "measured configuration";
    ogg_packet packet = {0};
    if (ogg_stream_init(&stream, 42)) return 1;
    packet.packet = bytes;
    packet.bytes = sizeof(bytes);
    packet.b_o_s = 1;
    packet.e_o_s = 1;
    if (ogg_stream_packetin(&stream, &packet)) return 2;
    if (!ogg_stream_flush(&stream, &page)) return 3;
    if (ogg_page_serialno(&page) != 42 || !ogg_page_bos(&page)) return 4;
    if (page.body_len != sizeof(bytes) || memcmp(page.body, bytes, sizeof(bytes))) return 5;
    return ogg_stream_clear(&stream);
}
