#include <file.h>

void open_files_init(void) {
    for (int i = 0; i < OPEN_FILE_MAX; ++i) {
        open_files[i].ref_count = 0;
    }

    open_files_lock = sync_create_mutex();
}
