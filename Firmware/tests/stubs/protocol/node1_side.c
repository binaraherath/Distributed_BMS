/* Node 1 app symbols needed by bms_can.c */
#include "bms_app.h"
BmsContext_t g_bms;
int n1_clear_calls, n1_hb_calls, n1_bal_calls; bool n1_bal_last;
bool BMS_App_ClearFaults(void) { n1_clear_calls++; return true; }
void BMS_App_SetBalancing(bool e) { n1_bal_calls++; n1_bal_last = e; }
void BMS_App_NoteControllerHeartbeat(void) { n1_hb_calls++; }
uint32_t BQ_ErrorCount(void) { return 7; }
