#include "calculator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_display_str[32] = "0";
static double g_accumulator = 0.0;
static char g_pending_op = '\0';
static int g_new_entry = 1;

void calculator_init(void) {
    strcpy(g_display_str, "0");
    g_accumulator = 0.0;
    g_pending_op = '\0';
    g_new_entry = 1;
}

const char* calculator_get_display(void) {
    return g_display_str;
}

static void evaluate_pending(void) {
    double current = atof(g_display_str);
    if (g_pending_op == '+') {
        g_accumulator += current;
    } else if (g_pending_op == '-') {
        g_accumulator -= current;
    } else if (g_pending_op == '*') {
        g_accumulator *= current;
    } else if (g_pending_op == '/') {
        if (current != 0.0) {
            g_accumulator /= current;
        } else {
            strcpy(g_display_str, "Error");
            g_new_entry = 1;
            g_pending_op = '\0';
            return;
        }
    } else {
        g_accumulator = current;
    }

    snprintf(g_display_str, sizeof(g_display_str), "%.15g", g_accumulator);
}

void calculator_press_key(char key) {
    if (key == 'C') {
        calculator_init();
        return;
    }

    if (key >= '0' && key <= '9') {
        if (g_new_entry || strcmp(g_display_str, "0") == 0 || strcmp(g_display_str, "Error") == 0) {
            g_display_str[0] = key;
            g_display_str[1] = '\0';
            g_new_entry = 0;
        } else {
            size_t len = strlen(g_display_str);
            if (len < sizeof(g_display_str) - 2) {
                g_display_str[len] = key;
                g_display_str[len + 1] = '\0';
            }
        }
    } else if (key == '.') {
        if (g_new_entry) {
            strcpy(g_display_str, "0.");
            g_new_entry = 0;
        } else if (!strchr(g_display_str, '.')) {
            size_t len = strlen(g_display_str);
            if (len < sizeof(g_display_str) - 2) {
                g_display_str[len] = '.';
                g_display_str[len + 1] = '\0';
            }
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (!g_new_entry && g_pending_op != '\0') {
            evaluate_pending();
        } else {
            g_accumulator = atof(g_display_str);
        }
        g_pending_op = key;
        g_new_entry = 1;
    } else if (key == '=') {
        if (g_pending_op != '\0') {
            evaluate_pending();
            g_pending_op = '\0';
            g_new_entry = 1;
        }
    }
}
