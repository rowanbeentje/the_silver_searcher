#include <pcre.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"

#include "log.h"
#include "options.h"
#include "search.h"
#include "util.h"
#include "progress.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern void exitCleanly() {
    printf("\r\e[0m\e[K");
    exit(1);
}
int main(int argc, char **argv) {
    char **base_paths = NULL;
    char **paths = NULL;
    int i;
    int pcre_opts = PCRE_MULTILINE;
    int study_opts = 0;
    double time_diff;
    pthread_t *workers = NULL;
    pthread_t progress_worker = NULL;
    int workers_len;
    int casing_was_smart = 0;

    set_log_level(LOG_LEVEL_WARN);

    work_queue = NULL;
    work_queue_tail = NULL;
    memset(&stats, 0, sizeof(stats));
    root_ignores = init_ignore(NULL);
    out_fd = stdout;
#ifdef USE_PCRE_JIT
    int has_jit = 0;
    pcre_config(PCRE_CONFIG_JIT, &has_jit);
    if (has_jit) {
        study_opts |= PCRE_STUDY_JIT_COMPILE;
    }
#endif

    gettimeofday(&(stats.time_start), NULL);

    parse_options(argc, argv, &base_paths, &paths);
    log_debug("PCRE Version: %s", pcre_version());

#ifdef _WIN32
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        workers_len = si.dwNumberOfProcessors;
    }
#else
    workers_len = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (opts.literal)
        workers_len--;
    if (opts.workers)
        workers_len = opts.workers;
    if (workers_len < 1)
        workers_len = 1;

    log_debug("Using %i workers", workers_len);
    done_adding_files = FALSE;
    workers = ag_calloc(workers_len, sizeof(pthread_t));
    if (pthread_cond_init(&files_ready, NULL))
        die("pthread_cond_init failed!");
    if (pthread_mutex_init(&print_mtx, NULL))
        die("pthread_mutex_init failed!");
    if (pthread_mutex_init(&stats_mtx, NULL))
        die("pthread_mutex_init failed!");
    if (pthread_mutex_init(&work_queue_mtx, NULL))
        die("pthread_mutex_init failed!");
    if (pthread_mutex_init(&progress_mtx, NULL))
        die("pthread_cond_init failed!");

    signal(SIGTERM, exitCleanly);
    signal(SIGINT, exitCleanly);

    if (opts.casing == CASE_SMART) {
        casing_was_smart = 1;
        opts.casing = is_lowercase(opts.query) ? CASE_INSENSITIVE : CASE_SENSITIVE;
    }

    if (opts.literal) {
        if (opts.casing == CASE_INSENSITIVE) {
            /* Search routine needs the query to be lowercase */
            char *c = opts.query;
            for (; *c != '\0'; ++c) {
                *c = (char) tolower(*c);
            }
        }
        generate_skip_lookup(opts.query, opts.query_len, skip_lookup, opts.casing == CASE_SENSITIVE);
        if (opts.word_regexp) {
            init_wordchar_table();
            opts.literal_starts_wordchar = is_wordchar(opts.query[0]);
            opts.literal_ends_wordchar = is_wordchar(opts.query[opts.query_len - 1]);
        }
    } else {
        if (opts.casing == CASE_INSENSITIVE) {
            pcre_opts = pcre_opts | PCRE_CASELESS;
        }
        if (opts.word_regexp) {
            char *word_regexp_query;
            ag_asprintf(&word_regexp_query, "\\b%s\\b", opts.query);
            free(opts.query);
            opts.query = word_regexp_query;
            opts.query_len = strlen(opts.query);
        }
        compile_study(&opts.re, &opts.re_extra, opts.query, pcre_opts, study_opts);
    }

    if (opts.show_progress) {
        int rv = pthread_create(&progress_worker, NULL, &progress_file_worker, NULL);
        if (rv != 0) {
            die("error in pthread_create(): %s", strerror(rv));
        }
    }

    if (opts.search_stream) {
        search_stream(stdin, "");
    } else {
        for (i = 0; i < workers_len; i++) {
            int rv = pthread_create(&(workers[i]), NULL, &search_file_worker, NULL);
            if (rv != 0) {
                die("error in pthread_create(): %s", strerror(rv));
            }
        }
        for (i = 0; paths[i] != NULL; i++) {
            log_debug("searching path %s for %s", paths[i], opts.query);
            symhash = NULL;
            search_dir(root_ignores, base_paths[i], paths[i], 0);
        }
        done_adding_files = TRUE;
        pthread_cond_broadcast(&files_ready);
        for (i = 0; i < workers_len; i++) {
            if (pthread_join(workers[i], NULL)) {
                die("pthread_join failed!");
            }
        }
    }

    if (opts.show_progress) {
        progress_complete = 1;
        if (pthread_join(progress_worker, NULL)) {
            die("pthread_join failed!");
        }
    }

    if (opts.stats) {
        gettimeofday(&(stats.time_end), NULL);
        time_diff = ((long)stats.time_end.tv_sec * 1000000 + stats.time_end.tv_usec) -
                    ((long)stats.time_start.tv_sec * 1000000 + stats.time_start.tv_usec);
        time_diff /= 1000000;

        pthread_mutex_lock(&print_mtx);
        printf("%ld matches\n%ld files searched\n%ld bytes searched\n%f seconds\n", stats.total_matches, stats.total_files, stats.total_bytes, time_diff);
        pthread_mutex_unlock(&print_mtx);
    }

    if (opts.stats_summary) {
        char *casetype;
        switch (opts.casing) {
            case CASE_SENSITIVE:
                casetype = "sensitive";
                break;
            case CASE_INSENSITIVE:
                casetype = "insensitive";
                break;
            default:
                casetype = "UNKNOWN";
                break;
        }
        pthread_mutex_lock(&print_mtx);
        printf("\e[1;34m # A total of \e[1;31m%ld\e[1;34m lines with matches found.\e[0;34m Case \e[1;34m%s\e[0;34m%s, symlinks \e[1;34m%s\e[0;34m.\e[0m\n", stats.total_matches, casetype, casing_was_smart ? " (was smart)" : "", opts.follow_symlinks ? "on" : "off");
        pthread_mutex_unlock(&print_mtx);
    }

    if (opts.pager) {
        pclose(out_fd);
    }
    pthread_cond_destroy(&files_ready);
    pthread_mutex_destroy(&work_queue_mtx);
    pthread_mutex_destroy(&stats_mtx);
    pthread_mutex_destroy(&print_mtx);
    pthread_mutex_destroy(&progress_mtx);
    cleanup_ignore(root_ignores);
    free(workers);
    for (i = 0; paths[i] != NULL; i++) {
        free(paths[i]);
        free(base_paths[i]);
    }
    free(base_paths);
    free(paths);
    return 0;
}
