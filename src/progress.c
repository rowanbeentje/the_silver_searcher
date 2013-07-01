#include <pthread.h>

#include "progress.h"
#include "util.h"
#include "search.h"

int progress_update_required = 0;
char *last_progress_state = NULL;

void clear_progress() {
    if (!opts.show_progress) {
        return;
    }

    fprintf(out_fd, "\r\e[K");
    progress_update_required = 1;
}

void update_progress(const char *progress_text) {
    if (!opts.show_progress || progress_text == NULL || !strlen(progress_text)) {
        return;
    }

    char *old_progress_state, *new_progress_state;

    new_progress_state = ag_strdup(progress_text);

    /* Perform the bare minimum within a mutex */
    pthread_mutex_lock(&progress_mtx);
    old_progress_state = last_progress_state;
    last_progress_state = new_progress_state;
    progress_update_required = 1;
    pthread_mutex_unlock(&progress_mtx);

    if (old_progress_state != NULL) {
        free(old_progress_state);
    }
}

void *progress_file_worker() {
    while (!progress_complete) {
        if (progress_update_required) {
            pthread_mutex_lock(&print_mtx);
            fprintf(out_fd, "\r\e[0;34m   + Scanning %s...\e[0m\e[K", last_progress_state);
            fflush(out_fd);
            pthread_mutex_unlock(&print_mtx);
            progress_update_required = 0;
        }
        usleep(50000);
    }
    clear_progress();

    pthread_mutex_lock(&progress_mtx);
    if (last_progress_state != NULL) {
        free(last_progress_state);
        last_progress_state = NULL;
    }
    pthread_mutex_unlock(&progress_mtx);
    return NULL;
}
