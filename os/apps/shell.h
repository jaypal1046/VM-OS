#ifndef SPLITOS_SHELL_H
#define SPLITOS_SHELL_H

#define SHELL_MAX_LINES 16
#define SHELL_LINE_LEN   64

void shell_init(void);
void shell_execute_cmd(const char* cmd_line);
int shell_get_history_count(void);
const char* shell_get_history_line(int index);
void shell_clear(void);

#endif // SPLITOS_SHELL_H
