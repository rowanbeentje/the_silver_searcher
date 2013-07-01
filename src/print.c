#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <math.h>
#include <unistd.h>

#include "ignore.h"
#include "log.h"
#include "options.h"
#include "print.h"
#include "util.h"
#include "progress.h"

int first_file_match = 1;

const char *color_reset = "\e[0m\e[K";

void print_path(const char* path, const char sep) {
    log_debug("printing path");
    path = normalize_path(path);

    /* If matching files or printing filenames only, clear any visible progress */
    if (opts.print_filename_only || opts.match_files) {
        clear_progress();
    }

    if (opts.ackmate) {
        fprintf(out_fd, ":%s%c", path, sep);
    } else {
        if (opts.color) {
            fprintf(out_fd, "%s%s%s%c", opts.color_path, path, color_reset, sep);
        } else {
            fprintf(out_fd, "%s%c", path, sep);
        }
    }
}

void print_binary_file_matches(const char* path) {
    path = normalize_path(path);
    clear_progress();
    print_file_separator();
    fprintf(out_fd, "Binary file %s matches.\n", path);
}

void print_file_matches(const char* path, const char* buf, const int buf_len, const match matches[], const int matches_len) {
    int line = 1;
    char **context_prev_lines = NULL;
    int prev_line = 0;
    int last_prev_line = 0;
    int prev_line_offset = 0;
    int pre_match_offset = 0;
    int current_wrap_line_indent;
    int current_wrap_line_end = 0;
    int lines_for_current_match = 0;
    int cur_match = 0;
    /* TODO the line below contains a terrible hack */
    int lines_since_last_match = 1000000; /* if I initialize this to INT_MAX it'll overflow */
    int lines_to_print = 0;
    int last_printed_match = 0;
    char sep = '-';
    int i, j;
    int in_a_match = FALSE;
    int printing_a_match = FALSE;

    if (opts.ackmate) {
        sep = ':';
    }

    clear_progress();
    print_file_separator();

    if (opts.print_heading == TRUE) {
        print_path(path, '\n');
    }

    context_prev_lines = ag_calloc(sizeof(char*), (opts.before + 1));

    for (i = 0; i <= buf_len && (cur_match < matches_len || lines_since_last_match <= opts.after); i++) {
        if (cur_match < matches_len && i == matches[cur_match].end) {
            /* We found the end of a match. */
            cur_match++;
            in_a_match = FALSE;
        }

        if (cur_match < matches_len && i == matches[cur_match].start) {
            in_a_match = TRUE;
            /* We found the start of a match */
            if (cur_match > 0 && opts.context && lines_since_last_match > (opts.before + opts.after + 1)) {
                fprintf(out_fd, "--\n");
            }

            if (lines_since_last_match > 0 && opts.before > 0) {
                /* TODO: better, but still needs work */
                /* print the previous line(s) */
                lines_to_print = lines_since_last_match - (opts.after + 1);
                if (lines_to_print < 0) {
                    lines_to_print = 0;
                } else if (lines_to_print > opts.before) {
                    lines_to_print = opts.before;
                }

                for (j = (opts.before - lines_to_print); j < opts.before; j++) {
                    prev_line = (last_prev_line + j) % opts.before;
                    if (context_prev_lines[prev_line] != NULL) {
                        if (opts.print_heading == 0) {
                            print_path(path, ':');
                        }
                        print_line_number(line - (opts.before - j), sep);
                        fprintf(out_fd, "%s\n", context_prev_lines[prev_line]);
                    }
                }
            }
            lines_since_last_match = 0;
        }

        /* We found the end of a line. */
        if (buf[i] == '\n' && opts.before > 0) {
            if (context_prev_lines[last_prev_line] != NULL) {
                free(context_prev_lines[last_prev_line]);
            }
            /* We don't want to strcpy the \n */
            context_prev_lines[last_prev_line] =
                ag_strndup(&buf[prev_line_offset], i - prev_line_offset);
            last_prev_line = (last_prev_line + 1) % opts.before;
        }

        if (buf[i] == '\n' || i == buf_len) {
            if (lines_since_last_match == 0) {
                if (opts.print_heading == 0 && !opts.search_stream) {
                    print_path(path, ':');
                }

                if (opts.ackmate) {
                    /* print headers for ackmate to parse */
                    print_line_number(line, ';');
                    for (; last_printed_match < cur_match; last_printed_match++) {
                        fprintf(out_fd, "%i %i",
                              (matches[last_printed_match].start - prev_line_offset),
                              (matches[last_printed_match].end - matches[last_printed_match].start)
                        );
                        last_printed_match == cur_match - 1 ? fputc(':', out_fd) : fputc(',', out_fd);
                    }
                    j = prev_line_offset;
                    /* print up to current char */
                    for (; j <= i; j++) {
                        fputc(buf[j], out_fd);
                    }
                } else {
                    print_line_number(line, ':');
                    if (opts.column) {
                        fprintf(out_fd, "%i:", (matches[last_printed_match].start - prev_line_offset) + 1);
                    }

                    /* standardise preceding whitespace if shorter output option set */
                    pre_match_offset = 0;
                    if (opts.shorter_output) {
                        fprintf(out_fd, "   ");
                        for (j = prev_line_offset; j <= i; j++) {
                            if (buf[j] != ' ' && buf[j] != '\t') {
                                break;
                            }
                            pre_match_offset++;
                        }
                        current_wrap_line_indent = ceil(log10(line + 1)) + 1 + 3;
                        if (opts.print_heading == 0 && !opts.search_stream) {
                            current_wrap_line_indent += strlen(path) + 1;
                        }
                        current_wrap_line_end = get_next_line_break_position(buf, prev_line_offset + pre_match_offset, i, current_wrap_line_indent);
                        lines_for_current_match = 1;
                    }

                    if (printing_a_match && opts.color) {
                        fprintf(out_fd, "%s", opts.color_match);
                    }
                    for (j = prev_line_offset + pre_match_offset; j <= i; j++) {
                        if (j == matches[last_printed_match].end && last_printed_match < matches_len) {
                            if (opts.color) {
                                fprintf(out_fd, "%s", color_reset);
                            }
                            printing_a_match = FALSE;
                            last_printed_match++;
                        }
                        if (j == matches[last_printed_match].start && last_printed_match < matches_len) {
                            if (opts.color) {
                                fprintf(out_fd, "%s", opts.color_match);
                            }
                            printing_a_match = TRUE;
                        }
                        if (opts.shorter_output && j == current_wrap_line_end) {
                            if (lines_for_current_match == 4) {
                                fprintf(out_fd, "... %s(long line truncated)%s\n", opts.color_truncate, color_reset);
                                j = i+1;
                                break;
                            }
                            fprintf(out_fd, "\n   ");
                            lines_for_current_match++;
                            current_wrap_line_end = get_next_line_break_position(buf, j+1, i, (lines_for_current_match == 4) ? 30 : 3);
                            if (buf[j] == ' ' || buf[j] == '\t') {
                                continue;
                            }
                        }
                        fputc(buf[j], out_fd);
                    }
                    if (printing_a_match && opts.color) {
                        fprintf(out_fd, "%s", color_reset);
                    }
                    if (i == buf_len) {
                        fputc('\n', out_fd);
                    }
                }
            } else if (lines_since_last_match <= opts.after) {
                /* print context after matching line */
                if (opts.print_heading == 0) {
                    print_path(path, ':');
                }
                print_line_number(line, sep);

                for (j = prev_line_offset; j < i; j++) {
                    fputc(buf[j], out_fd);
                }
                fputc('\n', out_fd);
            }

            prev_line_offset = i + 1; /* skip the newline */
            line++;
            if (!in_a_match) {
                lines_since_last_match++;
            }
        }
    }

    for (i = 0; i < opts.before; i++) {
        if (context_prev_lines[i] != NULL) {
            free(context_prev_lines[i]);
        }
    }
    free(context_prev_lines);
}

void print_line_number(const int line, const char sep) {
    if (!opts.print_line_numbers) {
        return;
    }
    log_debug("printing line number");

    if (opts.color) {
        fprintf(out_fd, "%s%i%s%c", opts.color_line_number, line, color_reset, sep);
    } else {
        fprintf(out_fd, "%i%c", line, sep);
    }
}

void print_file_separator() {
    if (first_file_match == 0 && opts.print_break) {
        log_debug("printing file separator");
        fprintf(out_fd, "\n");
    }
    first_file_match = 0;
}

const char* normalize_path(const char* path) {
    if (strlen(path) >= 3 && path[0] == '.' && path[1] == '/') {
        return path + 2;
    } else {
        return path;
    }
}

int get_next_line_break_position(const char *buf, const int line_start_position, const int line_end_position, const int current_indent) {
    static int max_line_length = 0;
    if (!max_line_length) {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        if (w.ws_col) {
            max_line_length = w.ws_col;
        } else {
            max_line_length = 72;
        }
    }

    if (line_end_position - line_start_position <= max_line_length - current_indent) {
        return -1;
    }

    int i;
    int last_breakable_position = 0;
    for (i = line_start_position; i < line_end_position; i++) {
        switch (buf[i]) {
            case ' ':
            case '\t':
            case '-':
                last_breakable_position = i;
                break;
        }
        if (i - line_start_position >= max_line_length - current_indent) {
            break;
        }
    }
    if (last_breakable_position) {
        return last_breakable_position;
    }
    return line_start_position + max_line_length - current_indent;
}
