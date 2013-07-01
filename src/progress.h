#ifndef PROGRESS_H
#define PROGRESS_H

pthread_mutex_t progress_mtx;
int progress_complete;

void clear_progress();
void update_progress(const char *progress_text);
void *progress_file_worker();

#endif
