/*
 turborama_mount (Multi-source ROMs mounting utility for Turborama) is licensed under [**GPL3**](https://gnu.org/licenses/gpl.html)

 Copyright (C) 2022 Guoxin "7Ji" Pu (pugokushin@gmail.com)

 This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version * of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

*/
#include "turborama_mount_p.h"

struct turborama_mount_table* turborama_mount_get_table() {
    struct turborama_mount_table *table = malloc(sizeof(struct turborama_mount_table));
    if (table == NULL) {
        logging(LOGGING_ERROR, "Failed to allocate memory for mount table");
        return NULL;
    }
    if ((table->entries = malloc(sizeof(struct turborama_mount_entry)*16)) == NULL) {
        logging(LOGGING_ERROR, "Failed to allocate memory for mount infos");
        goto free_table;
    }
    table->count = 0;
    table->alloc_entries = ALLOC_BASE_SIZE;
    FILE *fp = fopen(TURBORAMA_MOUNT_MOUNTINFO, "r");
    if (fp == NULL) {
        logging(LOGGING_ERROR, "Failed to open '"TURBORAMA_MOUNT_MOUNTINFO"' to read mounted table");
        goto free_entries;
    }
    char *line;
    size_t size_line = 0;
    size_t len_line;
    char *token;
    const char delim[] = " ";
    unsigned int segments = 0;
    struct turborama_mount_entry *entry;
    bool optional_end;
    char *endptr;
    /* An example line:
    36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue
    */
    while (getline(&line, &size_line, fp) != -1) {
        if ((len_line = strcspn(line, "\r\n")) == 0) {
            continue;
        }
        if (++(table->count) > table->alloc_entries) {
            table->alloc_entries*=ALLOC_MULTIPLIER;
            if ((entry = realloc(table->entries, sizeof(struct turborama_mount_entry)*(table->alloc_entries))) == NULL) { // Pure evilness, entry is just buffer here
                logging(LOGGING_ERROR, "Failed to reallocate memory for table entries");
                goto free_fp;
            }
            table->entries = entry;
        }
        entry = table->entries + (table->count-1);
        if ((entry->line = strdup(line)) == NULL) {
            logging(LOGGING_ERROR, "Failed to allocate memory for internal line of mount info");
            goto free_fp;
        }
        // We do these on our dupped line, so we don't need to malloc all strings
        optional_end = false;
        entry->line[len_line] = '\0';
        token = strtok(entry->line, delim);
        segments = 0;
        while (token) {
            switch(++segments) {
                case 1: // mount ID
                    entry->mount_id = util_uint_from_ulong(strtoul(token, &endptr, 10));
                    break;
                case 2: // parent ID
                    entry->parent_id = util_uint_from_ulong(strtoul(token, &endptr, 10));
                    break;
                case 3: // major:minor
                    entry->major_minor = token;
                    entry->major = util_uint_from_ulong(strtoul(token, &endptr, 10));
                    entry->minor = util_uint_from_ulong(strtoul(endptr+1, &endptr, 10));
                    break;
                case 4: // root
                    entry->root = token;
                    util_unesacpe_mountinfo_in_place(entry->root);
                    break;
                case 5: // mount point
                    entry->mount_point = token;
                    util_unesacpe_mountinfo_in_place(entry->mount_point);
                    break;
                case 6: // mount options
                    entry->mount_options = token;
                    break;
                case 7:
                    if (optional_end) {
                        entry->fstype = token;
                    } else {
                        if (!strcmp(token, "-")) {
                            optional_end = true;
                        }
                        --segments;
                    }
                    break;
                case 8:
                    entry->mount_source = token;
                    break;
                case 9:
                    entry->super_options = token;
                    break;
                default:
                    break;
            }
            token = strtok(NULL, delim);
        }
    }
    fclose(fp);
    return table;
free_fp:
    fclose(fp);
free_entries:
    free(table->entries);
free_table:
    free(table);
    return NULL;
}

int turborama_mount_umount_entry(struct turborama_mount_entry *entry) {
    if (umount2(entry->mount_point, MNT_FORCE)) {
        logging(LOGGING_ERROR, "Failed to umount %s", entry->mount_point);
        return 1;
    } else {
        logging(LOGGING_INFO, "Successfully umounted %s", entry->mount_point);
        return 0;
    }
}

unsigned int turborama_mount_umount_entry_recursive(struct turborama_mount_entry *entry, struct turborama_mount_table *table, unsigned int entry_id) {
    unsigned int ret = 0;
    for (unsigned int i=entry_id+1; i<table->count; ++i) {
        if ((table->entries+i)->parent_id == entry->mount_id) {
            ret += turborama_mount_umount_entry_recursive((table->entries+i), table, i);
        }
    }
    ret += turborama_mount_umount_entry(entry);
    return ret;
}

struct turborama_mount_entry *turborama_mount_find_entry_by_mount_point(const char *mount_point, struct turborama_mount_table *table) {
    struct turborama_mount_entry *entry;
    for (unsigned i=0; i<table->count; ++i) {
        entry = table->entries + i;
        if (!strcmp(mount_point, entry->mount_point)) {
            return entry;
        }
    }
    return NULL;
}

struct turborama_mount_entry *turborama_mount_find_entry_by_mount_point_start_with(const char *mount_point, struct turborama_mount_table *table, size_t len) {
    if (len == 0) {
        len = strlen(mount_point);
    }
    struct turborama_mount_entry *entry;
    for (unsigned i=0; i<table->count; ++i) {
        entry = table->entries + i;
        if (!strncmp(mount_point, entry->mount_point, len)) {
            return entry;
        }
    }
    return NULL;
}

bool turborama_mount_is_mount_point(const char* path, struct turborama_mount_table *table) {
    logging(LOGGING_DEBUG, "Checking if '%s' is a mount point", path);
    bool free_table;
    if (table == NULL) {
        if ((table = turborama_mount_get_table()) == NULL) {
            return false;
        }
        free_table = true;
    } else {
        free_table = false;
    }
    for (unsigned i=0; i<table->count; ++i) {
        if (!strcmp(path, (table->entries+i)->mount_point)) {
            if (free_table) {
                turborama_mount_free_table(&table);
            }
            return true;
        }
    }
    if (free_table) {
        turborama_mount_free_table(&table);
    }
    return false;
}

void turborama_mount_free_table(struct turborama_mount_table **table) {
    if (*table) {
        for (unsigned int i=0; i<(*table)->count; ++i) {
            free(((*table)->entries+i)->line);
        }
        free((*table)->entries);
        free(*table);
        (*table)=NULL;
    }
}

static struct libmnt_context *turborama_mount_context_reset_or_new(struct libmnt_context *cxt) {
    if (mnt_reset_context(cxt)) {
        logging(LOGGING_ERROR, "Failed to reset mount context");
        mnt_free_context(cxt);
        if ((cxt = mnt_new_context()) == NULL) {
            logging(LOGGING_ERROR, "Failed to allocate another mount context");
        }
    } 
    return cxt;
}

static int turborama_mount_mount_dir_update(const char* path) {
    /* ONLY call this after TURBOROMS is successfully mounted 
       Otherwise this must be a util_mkdir_recursive() call since the dir itself (path) is not guaranteed to exist
    */
    logging(LOGGING_INFO, "Trying to mount '%s' to '"PATH_DIR_UPDATE "'", path);
    if (turborama_mount_is_mount_point(PATH_DIR_UPDATE , NULL)) {
        logging(LOGGING_INFO, "'"PATH_DIR_UPDATE "' already a mount point, no need to mount");
        return 0;
    }
    if (util_mkdir(path, 0755)) {
        logging(LOGGING_ERROR, "Failed to create/confirm folder '%s'", path);
        return 1;
    }
    if (mount(path, PATH_DIR_UPDATE , NULL, MS_BIND, NULL)) {
        logging(LOGGING_ERROR, "Failed to bind '%s' to '"PATH_DIR_UPDATE "'");
        return 1;
    } else {
        logging(LOGGING_INFO, "Successfully mounted '"PATH_DIR_UPDATE "'");
        return 0;
    }
}

static int turborama_mount_mount_partition_turboroms(const char *mount_point) {
    logging(LOGGING_INFO, "Trying to mount TURBOROMS to '%s'", mount_point);
    if (util_mkdir(mount_point, 0755)) {
        logging(LOGGING_ERROR, "Failed to confirm mount point '%s' exists", mount_point);
        return 1;
    }
    if (turborama_mount_is_mount_point(mount_point, NULL)) {
        logging(LOGGING_INFO, "%s is already mounted, no need to mount it again", mount_point);
        return 0;
    }
    struct libmnt_context *cxt = mnt_new_context();
    if (cxt == NULL) {
        logging(LOGGING_ERROR, "Failed to allocate mount context");
        return 1;
    }
    struct turborama_mount_table *table = turborama_mount_get_table();
    if (table) { // If we can get partition table, try the one providing .update and the 3rd partition of boot drive, this is optimal
        logging(LOGGING_INFO, "Trying to get the underlying partition of "PATH_DIR_UPDATE );
        struct turborama_mount_entry *entry = turborama_mount_find_entry_by_mount_point(PATH_DIR_UPDATE , table);
        if (entry) {
            logging(LOGGING_INFO, "'"PATH_DIR_UPDATE "' is mounted, using the underlying partition %s as TURBOROMS", entry->mount_source);
            if (mnt_context_set_source(cxt, entry->mount_source) || mnt_context_set_options(cxt, entry->mount_options) || mnt_context_set_target(cxt, mount_point) || mnt_context_mount(cxt)) {
                logging(LOGGING_ERROR, "Failed to mount the partition %s", entry->mount_source);
            } else {
                mnt_free_context(cxt);
                turborama_mount_free_table(&table);
                return 0;
            }
            if ((cxt = turborama_mount_context_reset_or_new(cxt)) == NULL) {
                turborama_mount_free_table(&table);
                return 1;
            }
        }
        // /storage/.update is not mounted, then check the drive providing /flash and /storage, get the 3rd partition of that drive. (use /flash first, then /storage, as /flash is mounted earlier than /storage during init)
        char mount_points[2][9] = {
            "/flash",
            "/storage"
        };
        size_t len_partition;
        char *partition;
        for (int i=0; i<2; ++i) {
            logging(LOGGING_INFO, "Trying to get the underlying disk of '%s' and use the 3rd partition there", mount_points[i]);
            if ((entry = turborama_mount_find_entry_by_mount_point(mount_points[i], table)) == NULL) {
                logging(LOGGING_ERROR, "Can not find the partitions under %s", mount_points[i]);
                continue;
            }
            partition = entry->mount_source;
            if ((len_partition = strlen(partition)) < 5) { //  /dev/
                continue;
            }
            if (entry->mount_source[len_partition-1] < '1' || entry->mount_source[len_partition-1] > '9') {
                continue;
            }
            if ((partition = strdup(entry->mount_source)) == NULL) {
                logging(LOGGING_ERROR, "Failed to allocate memory for partition name");
                continue;
            }
            partition[len_partition-1] = '3';
            if (mnt_context_set_source(cxt, partition) || mnt_context_set_mflags(cxt, MS_NOATIME) || mnt_context_set_target(cxt, mount_point) || mnt_context_mount(cxt)) {
                logging(LOGGING_ERROR, "Failed to mount the partition %s", partition);
            } else {
                free(partition);
                mnt_free_context(cxt);
                turborama_mount_free_table(&table);
                return 0;
            }
            if ((cxt = turborama_mount_context_reset_or_new(cxt)) == NULL) {
                free(partition);
                turborama_mount_free_table(&table);
                return 1;
            }
        }
        turborama_mount_free_table(&table);
    }
    // Can not find any, at least try to find any other partition with LABEL=TURBOROMS
    logging(LOGGING_INFO, "Last hope, mounting LABEL=TURBOROMS directly, we cannot guarantee it's the correct TURBOROMS");
    if (mnt_context_set_source(cxt, "LABEL=TURBOROMS") || mnt_context_set_mflags(cxt, MS_NOATIME) || mnt_context_set_target(cxt, mount_point) || mnt_context_mount(cxt)) {
        logging(LOGGING_ERROR, "Failed to mount LABEL=TURBOROMS");
    } else {
        mnt_free_context(cxt);
        return 0;
    }
    mnt_free_context(cxt);
    logging(LOGGING_ERROR, "All tries to mount '%s' failed", mount_point);
    return 1;
}

static int turborama_mount_umount_roms() {
    logging(LOGGING_INFO, "Umounting all mount points under "PATH_DIR_ROMS"...");
    struct turborama_mount_table *table = turborama_mount_get_table();
    if (table) {
        struct turborama_mount_entry *entry;
        for (int i=0; i<2; ++i) {
            switch (i) {
                case 0:
                    entry = turborama_mount_find_entry_by_mount_point(PATH_DIR_ROMS, table);
                    break;
                case 1:
                    entry = turborama_mount_find_entry_by_mount_point_start_with(PATH_DIR_ROMS"/", table, len_path_dir_roms+1);
                    break;
            }
            while (entry) {
                if (turborama_mount_umount_entry_recursive(entry, table, 0)) {
                    turborama_mount_free_table(&table);
                    logging(LOGGING_INFO, "Failed to umount all mount points under "PATH_DIR_ROMS"");
                    return 1;
                }
                turborama_mount_free_table(&table);
                if ((table = turborama_mount_get_table()) == NULL) {
                    logging(LOGGING_INFO, "Failed to umount all mount points under "PATH_DIR_ROMS"");
                    return 1;
                }
                switch (i) {
                    case 0:
                        entry = turborama_mount_find_entry_by_mount_point(PATH_DIR_ROMS, table);
                        break;
                    case 1:
                        entry = turborama_mount_find_entry_by_mount_point_start_with(PATH_DIR_ROMS"/", table, len_path_dir_roms+1);
                        break;
                }
            }
        }
        turborama_mount_free_table(&table);
        logging(LOGGING_INFO, "Successfully umounted all mount points under "PATH_DIR_ROMS"");
        return 0;
    } else {
        logging(LOGGING_INFO, "Failed to umount all mount points under "PATH_DIR_ROMS"");
        return 1;
    }
}

static int turborama_mount_mount_root(struct systemd_mount_unit_helper *shelper, struct drive_helper *dhelper) {
    logging(LOGGING_INFO, "Mounting "PATH_DIR_ROMS"...");
    if (util_mkdir(PATH_DIR_ROMS, 0755)) {
        logging(LOGGING_ERROR, "Can not create/valid directory '"PATH_DIR_ROMS"', all mount operations cancelled");
        return -1;
    }
    // Only one providing /storage/roms, that is storage-roms.mount
    if (shelper && shelper->root && !systemd_start_unit(shelper->root->name)) {
        logging(LOGGING_INFO, "Successfully mounted "PATH_DIR_ROMS" through systemd");
        if (!turborama_mount_mount_partition_turboroms(PATH_DIR_EXTERNAL_TURBOROMS)) {
            turborama_mount_mount_dir_update(PATH_DIR_UPDATE_EXT);
        }
        return 0;
    }
    if (dhelper) {
        char path[len_path_dir_external + 262];
        char *name;
        // Multiple can provide /storage/roms, we go alphabetically
        for (unsigned i=0; i<dhelper->count; ++i) {
            if ((dhelper->drives+i)->count == 0) { 
                name = (dhelper->drives+i)->name;
                snprintf(path, len_path_dir_external + strlen(name) + 7, PATH_DIR_EXTERNAL"/%s/roms", name);
                logging(LOGGING_INFO, "Binding '%s' to '"PATH_DIR_ROMS"'", path);
                if (!mount(path, PATH_DIR_ROMS, NULL, MS_BIND, NULL)) {
                    logging(LOGGING_INFO, "Successfully binded "PATH_DIR_ROMS);
                    if (!turborama_mount_mount_partition_turboroms(PATH_DIR_EXTERNAL_TURBOROMS)) {
                        turborama_mount_mount_dir_update(PATH_DIR_UPDATE_EXT);
                    }
                    return 0;
                }
            }
        }
    }
    // Since all failed, try to get TURBOROMS back
    if (!turborama_mount_mount_partition_turboroms(PATH_DIR_ROMS)) {
        logging(LOGGING_INFO, "Successfully re-mounted "PATH_NAME_TURBOROMS" back to "PATH_DIR_ROMS);
        turborama_mount_mount_dir_update(PATH_DIR_UPDATE_INT); // Optionally mount .update
        return 0;
    }
    return 1;
}

static void turborama_mount_free_finished_helper(struct turborama_mount_finished_helper **mhelper) {
    if (*mhelper) {
        if ((*mhelper)->count) {
            free((*mhelper)->systems);
        }
        free(*mhelper);
        *mhelper = NULL;
    }
}

static struct turborama_mount_finished_helper *turborama_mount_mount_drive_systems(struct drive_helper *dhelper, struct turborama_mount_finished_helper *mhelper) {
    unsigned int i, j, k;
    struct drive *drive;
    char *dsystem, **buffer;
    char path_source[len_path_dir_external+517]; // +1 for /, +255 for drive name, +1 for /, +4 for roms, +255 for system name, +1 for null
    char path_target[len_path_dir_roms+257]; // +1 for /, +255 for name, +1 for null
    size_t len_system;
    size_t len_drive;
    bool unique;
    for (i=0; i<dhelper->count; ++i) {
        drive = dhelper->drives+i;
        len_drive = 0;
        for (j=0; j<drive->count; ++j) {
            dsystem = drive->systems[j];
            logging(LOGGING_INFO, "Checking drive '%s' system '%s'", drive->name, dsystem);
            unique = true;
            if (mhelper) {
                for (k=0; k<mhelper->count; ++k) {
                    if (!strcmp(mhelper->systems[k], dsystem)) {
                        logging(LOGGING_WARNING, "System '%s' already provided by other mount units/external drives, omitting it", dsystem);
                        unique = false;
                        break;
                    }
                }
            }
            if (unique) {
                logging(LOGGING_WARNING, "System '%s' not mounted yet, trying to bind it", dsystem);
                len_system = strlen(dsystem);
                snprintf(path_target, len_path_dir_roms + len_system + 2, PATH_DIR_ROMS"/%s", dsystem);
                if (util_mkdir(path_target, 0755)) {
                    logging(LOGGING_ERROR, "Failed to binding system %s under drive %s on %s as we failed to confirm the target is a folder and further create if not", dsystem, drive->name, path_target);
                    continue;
                }
                if (len_drive == 0) {
                    len_drive = strlen(drive->name);
                }
                snprintf(path_source, len_path_dir_external + len_drive + len_system + 8, PATH_DIR_EXTERNAL"/%s/roms/%s", drive->name, dsystem);
                logging(LOGGING_DEBUG, "Binding '%s' to '%s'", path_source, path_target);
                if (mount(path_source, path_target, NULL, MS_BIND, NULL)) {
                    logging(LOGGING_ERROR, "Failed to bind '%s' to '%s'", path_source, path_target);
                    continue;
                }
                logging(LOGGING_INFO, "Successfully binded '%s' to '%s'", path_source, path_target);
                if (mhelper == NULL) {
                    if ((mhelper = malloc(sizeof(struct turborama_mount_finished_helper))) == NULL) {
                        logging(LOGGING_ERROR, "Failed to allocate memory for finished systems helper");
                        return NULL;
                    }
                    if ((mhelper->systems = malloc(sizeof(char *) * ALLOC_BASE_SIZE)) == NULL) {
                        logging(LOGGING_ERROR, "Failed to allocate memory for system names array in finished systems helper");
                        free(mhelper);
                        return NULL;
                    }
                    mhelper->alloc_systems = ALLOC_BASE_SIZE;
                    mhelper->count = 0;
                }
                if (++(mhelper->count) > mhelper->alloc_systems) {
                    mhelper->alloc_systems *= ALLOC_MULTIPLIER;
                    if ((buffer = realloc(mhelper->systems, sizeof(char *) * mhelper->alloc_systems)) == NULL) {
                        free(mhelper->systems);
                        free(mhelper);
                        return NULL;
                    }
                    mhelper->systems = buffer;
                }
                mhelper->systems[mhelper->count-1] = dsystem;
            }
        }
    }
    return mhelper;
}

static struct turborama_mount_finished_helper *turborama_mount_mount_systems(struct systemd_mount_unit_helper *shelper, struct drive_helper *dhelper) {
    logging(LOGGING_INFO, "Trying to mount systems");
    struct turborama_mount_finished_helper *mhelper = NULL;
    if (shelper) {
        if (dhelper) {
            logging(LOGGING_INFO, "Trying to mount layer1 systems provided by systemd mount units");
            mhelper = systemd_start_unit_systems(shelper, SYSTEMD_START_UNIT_SYSTEMS_LAYER_1, NULL);
        } else {
            logging(LOGGING_INFO, "Trying to mount systems provided by systemd mount units");
            mhelper = systemd_start_unit_systems(shelper, SYSTEMD_START_UNIT_SYSTEMS_LAYER_ALL, NULL);
        }
    }
    if (dhelper) {
        logging(LOGGING_INFO, "Trying to mount systems provided by external drives");
        mhelper = turborama_mount_mount_drive_systems(dhelper, mhelper);
        if (shelper) {
            logging(LOGGING_INFO, "Trying to mount multi-layer systems provided by systemd mount units");
            mhelper = systemd_start_unit_systems(shelper, SYSTEMD_START_UNIT_SYSTEMS_LAYER_2_AND_MORE, mhelper);
        }
    }
    logging(LOGGING_INFO, "Finished mounting systems");
    if (mhelper && mhelper->count) {
        logging(LOGGING_DEBUG, "The following systems are mounted from systemd mount units/external drives:");
        for (unsigned int i=0; i<mhelper->count; ++i) {
            logging(LOGGING_DEBUG, " -> %s", mhelper->systems[i]);
        }
    }
    return mhelper;
}

static int turborama_mount_mount_ports_scripts() {
    if (util_mkdir(PATH_DIR_PSCRIPTS, 0755) || util_mkdir(PATH_DIR_TURBORAMA_PORTS, 0755) || util_mkdir_recursive(PATH_DIR_PSCRIPTS_WORKDIR, 0755)) {
        logging(LOGGING_ERROR, "Failed to create essential folders for "PATH_NAME_PSCRIPTS);
        return 1;
    }
    struct libmnt_context *cxt = mnt_new_context();
    if (cxt == NULL) {
        logging(LOGGING_ERROR, "Failed to obtain mount context for "PATH_DIR_PSCRIPTS);
        return 1;
    }
    if (mnt_context_set_source(cxt, PATH_NAME_PORTS) || mnt_context_set_fstype(cxt, TURBORAMA_MOUNT_PORTS_SCRIPTS_FS) || mnt_context_set_target(cxt, PATH_DIR_PSCRIPTS) || mnt_context_set_options(cxt, TURBORAMA_MOUNT_PORTS_SCRIPTS_OPTIONS) || mnt_context_mount(cxt)) {
        logging(LOGGING_ERROR, "Failed to mount "PATH_NAME_PORTS);
        mnt_free_context(cxt);
        return 1;
    }
    logging(LOGGING_INFO, "Successfully mounted "PATH_NAME_PORTS);
    mnt_free_context(cxt);
    return 0;
}


int turborama_mount_routine() {
    if (turborama_mount_umount_roms())  {
        logging(LOGGING_ERROR, "Failed to prepare mounting");
        return 1;
    }
    struct systemd_mount_unit_helper *shelper = systemd_get_units();
    struct drive_helper *dhelper = drive_get_mounts();
    if (shelper) {
        systemd_reload();
    }
    if (turborama_mount_mount_root(shelper, dhelper) < 0) {
        systemd_mount_unit_helper_free(&shelper);
        drive_helper_free(&dhelper);
        return 1;
    }
    //turborama_mount_mount_ports_scripts();
    struct turborama_mount_finished_helper *mhelper = turborama_mount_mount_systems(shelper, dhelper);
    turborama_mount_free_finished_helper(&mhelper);
    systemd_mount_unit_helper_free(&shelper);
    drive_helper_free(&dhelper);
    return 0;
}
