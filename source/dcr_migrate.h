/* dcr_migrate.h -- an older install's folder, moved into this one
 * (dcr_migrate.c). MIT. */
#ifndef DCR_MIGRATE_H
#define DCR_MIGRATE_H

#include <stddef.h>
#include <stdint.h>

/* 0: no old folder; 1: something moved (or the empty old folder removed);
 * 2: the old folder is there but nothing in it could move. msg: what
 * happened, for the log. */
int dcr_migrate_folder(const char *old_root, const char *new_root, uint64_t build, const char *skip, char *msg,
                       size_t cap);

#endif
