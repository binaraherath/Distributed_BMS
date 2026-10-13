#include "ctrl_app.h"
CtrlContext_t g_ctrl;
int n2_stop_calls, n2_clear_calls, n2_bal_calls, n2_fan_calls; int16_t n2_fan_last; bool n2_bal_last;
void Ctrl_App_RequestStop(void) { n2_stop_calls++; }
void Ctrl_App_ClearFaults(void) { n2_clear_calls++; }
void Ctrl_App_SetBalancing(bool a) { n2_bal_calls++; n2_bal_last = a; }
void Ctrl_App_FanOverride(int16_t d) { n2_fan_calls++; n2_fan_last = d; }
