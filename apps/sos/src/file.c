#include <sys/panic.h>
#include "file.h"

void open_files_init(void) {
    for (int i = 0; i < OPEN_FILE_MAX; ++i) {
        open_files[i].ref_count = 0;
        open_files[i].file_obj.cache_entry_head = NULL;
        open_files[i].file_obj.file_lock = sync_create_mutex();
        conditional_panic(!open_files[i].file_obj.file_lock, "Cannot create file lock");
    }

    open_files_lock = sync_create_mutex();
}
