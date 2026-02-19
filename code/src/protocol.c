#include "pico/stdlib.h"

#include "protocol.h"

void proto_init(){
    stream_set_
}

void handle_input()
{
    while (1)
    {
        CommandHeader_t hdr;
        read_bytes(&hdr, sizeof(hdr))

        switch (hdr.command)
        {
        case GET_STATUS:
            /* code */
            break;
        
        default:
            CommandHeader_t
        }
    }
}