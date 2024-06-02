#ifndef __mesh_h
#define __mesh_h
#include "dmb_message_type.h"
#define MAX_NAME_LEN MAX_MESSAGE_LEN

#ifdef __cplusplus
extern "C" {    
#endif

void mesh_begin(uint16_t addr);
void sendDMBMessage(uint16_t target, dmb_message * dmbmsg);
void sendDMBGameMessage(uint16_t the_target, dmb_message * dmbmsg);
void sendDMBGameResponseMessage(uint16_t the_target, dmb_message * dmbmsg); 
void mesh_clearReplay();
void mesh_suspend();
void mesh_resume();

#ifdef __cplusplus
}
#endif 

extern volatile uint32_t DMBMessageReceived;
extern volatile dmb_message DMBMailBox;
extern volatile uint32_t DMBGameMessageReceived;
extern volatile dmb_message DMBGameMailBox;
extern volatile uint32_t DMBGameMessageResponseReceived;
extern volatile dmb_message DMBGameResponseMailBox;

#endif
