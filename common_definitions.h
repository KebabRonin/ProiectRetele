#define MSG_MAX_SIZE 205
#define MSG_HEADER_SIZE 0
#define AGENT_INBOUND_PORT 2077
#define AGENT_CONTROL_PORT 2078
#define CLIENT_PORT 2048
#define CLMSG_AGLIST 'l'
#define CLMSG_ADDSRC 'a'
#define CLMSG_AGPROP 'p'

//char* htonstr(char* buf) {
//    for(int i = 0; i < strlen(buf); ++i) {
//        buf[i] = htons((uint16_t)buf[i])
//    }
//    return buf;
//}