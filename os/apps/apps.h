#ifndef GUEST_APPS_H
#define GUEST_APPS_H

void app_dialer_trigger_call(const char* phone_number);
void app_messages_send_sms(const char* phone_number, const char* message);

#endif // GUEST_APPS_H
