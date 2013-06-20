#ifndef PRINT_H
#define PRINT_H

#include "util.h"

void print_path(const char* path, const char sep);
void print_binary_file_matches(const char* path);
void print_file_matches(const char* path, const char* buf, const int buf_len, const match matches[], const int matches_len);
void print_line_number(const int line, const char sep);
void print_file_separator();
const char* normalize_path(const char* path);
int get_next_line_break_position(const char *buf, const int line_start_position, const int line_end_position, const int current_indent);

#endif
