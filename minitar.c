#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "minitar.h"

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 512

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *)header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100); // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o", stat_buf.st_mode & 07777); // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid); // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid); // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32); // Owner  name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid); // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid); // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32); // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o", (unsigned)stat_buf.st_size); // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o", (unsigned)stat_buf.st_mtime); // Modification time, 0-padded octal
    header->typeflag = REGTYPE; // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6); // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2); // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o", major(stat_buf.st_dev)); // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o", minor(stat_buf.st_dev)); // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio)
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];
    // Note: ftruncate does not work with O_APPEND
    int fd = open(file_name, O_WRONLY);
    if (fd == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file_name);
        perror(err_msg);
        return -1;
    }
    //  Seek to end of file - nbytes
    off_t current_pos = lseek(fd, -1 * nbytes, SEEK_END);
    if (current_pos == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    // Remove all contents of file past current position
    if (ftruncate(fd, current_pos) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    if (close(fd) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}
/*
 * helper function for create_archive and append_archive
 * will either create/overwrite archive or append to the end of existing archive depending on mode
 */
int helper(const char *archive_name, const file_list_t *files, char mode) {
    // char mode helps distinguish between "a" for appending and "w" for writing
    char buffer[MAX_MSG_LEN];
    char err_msg[MAX_MSG_LEN];  // stores error message for printing
    FILE *temp_ptr;  // used for individual files to move into the archive
    FILE *archive_ptr;  // used for the archive ONLY
    node_t *current = files->head;  // used to navigate the file_list_t *files
    tar_header temp_header;

    if (mode == 'c') {  // for creating, open archive for writing
        archive_ptr = fopen(archive_name, "w");
        if (archive_ptr == NULL) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open archive %s", archive_name);
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }  
    } else {  // to avoid overwriting, open archive for appending
        archive_ptr = fopen(archive_name, "a");
        if (archive_ptr == NULL) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open archive %s", archive_name);
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }
        if (remove_trailing_bytes(archive_name, MAX_MSG_LEN * NUM_TRAILING_BLOCKS) != 0) {  // make sure remove_trailing_bytes doesn't return error
            snprintf(err_msg, MAX_MSG_LEN, "Failed to remove trailing bytes from %s", archive_name);
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }
    }

    for (int i = 0; i < files->size; i++) {
        temp_ptr = fopen(current->name, "r");
        if (temp_ptr == NULL) {  // fopen error check
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s in %s", current->name, archive_name);
            perror(err_msg);
            fclose(temp_ptr);
            fclose(archive_ptr);
            return -1;
        }
        if (fill_tar_header(&temp_header, current->name) != 0) {  // calls fill_tar_header and checks for error
            snprintf(err_msg, MAX_MSG_LEN, "Failed to fill tar header for %s in %s", current->name, archive_name);
            perror(err_msg);
            fclose(temp_ptr);
            fclose(archive_ptr);
            return -1;
        }
        if (fwrite(&temp_header, 1, MAX_MSG_LEN, archive_ptr) < MAX_MSG_LEN) {  // fwrite error check
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write the entire tar header for %s in %s", current->name, archive_name);
            perror(err_msg);
            fclose(temp_ptr);
            fclose(archive_ptr);
            return -1;
        }
        // The following (fseek, size, rewind) are used to count the remaining bytes of a file to know when we can/can't fread and fwrite a full 512 bytes
        fseek(temp_ptr, 0L, SEEK_END);  // seek to end of temp_ptr's file
        int size = ftell(temp_ptr);  // store the offset from beginning (aka. total bytes of the file)
        if (size == -1) {  // error checking fseek/ftell
            snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s in archive %s after successfully writing the tar header", current->name, archive_name);
            perror(err_msg);
            fclose(temp_ptr);
            fclose(archive_ptr);
            return -1;
        }
        rewind(temp_ptr);  // rewind the pointer to the start of the file
        if (ftell(temp_ptr) != 0) {  // error checking rewind -- should point to beginning of temp_ptr's file
            snprintf(err_msg, MAX_MSG_LEN, "Failed to rewind in file %s in archive %s after successfully writing the tar header", current->name, archive_name);
            perror(err_msg);
            fclose(temp_ptr);
            fclose(archive_ptr);
            return -1;
        }

        while (size > 0) {  // while temp_ptr's file has bytes left to read
            if (size >= MAX_MSG_LEN) {  // if remaining bytes exceed 512 bytes, we're good to read and write a full block
                fread(buffer, MAX_MSG_LEN, 1, temp_ptr);
                if (ferror(temp_ptr)) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to read file %s in archive %s while copying contents. ferror value: %d", current->name, archive_name, ferror(temp_ptr));
                    perror(err_msg);
                    fclose(temp_ptr);
                    fclose(archive_ptr);
                    return -1;
                }   
                if (fwrite(buffer, MAX_MSG_LEN, 1, archive_ptr) < 1) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to write a block from %s to %s", current->name, archive_name);
                    perror(err_msg);
                    fclose(temp_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
                size -= MAX_MSG_LEN;  // update how many bytes are remaining by subtracting the block we just read/wrote
            } else {  // if remaining bytes to read is less than a full 512 block
                memset(buffer, 0, MAX_MSG_LEN);  // set buffer to 512 zero chars
                fread(buffer, 1, size, temp_ptr);  // read in size (which is less than 512) bytes to buffer
                if (ferror(temp_ptr)) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to read file %s in archive %s while copying contents for FINAL block. ferror value: %d", current->name, archive_name, ferror(temp_ptr));
                    perror(err_msg);
                    fclose(temp_ptr);
                    fclose(archive_ptr);
                    return -1;
                }   
                if (fwrite(buffer, 1, MAX_MSG_LEN, archive_ptr) < MAX_MSG_LEN) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to write the FINAL block from %s to %s", current->name, archive_name);
                    perror(err_msg);
                    fclose(temp_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
                size = 0;  // already wrote final block, make sure while loop stops
            }
        } 
        current = current->next;  // finished copying contents from a file to the archive, so move on to next file
        if (fclose(temp_ptr) == -1) {  // close file/error check
            snprintf(err_msg, MAX_MSG_LEN, "Failed to close file %s", current->name);
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }
    }  // finished file copying loop
    // create footer (two 0 char blocks)
    memset(buffer, 0, MAX_MSG_LEN);  // set buffer to all 0 chars
    if (fwrite(buffer, MAX_MSG_LEN, 1, archive_ptr) < 1) { // write first block
        snprintf(err_msg, MAX_MSG_LEN, "Failed to write footer blocks for %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    if (fwrite(buffer, MAX_MSG_LEN, 1, archive_ptr) < 1) { // write second block
        snprintf(err_msg, MAX_MSG_LEN, "Failed to write footer blocks for %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    if (fclose(archive_ptr) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close archive %s after creating footer", archive_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

int create_archive(const char *archive_name, const file_list_t *files) {
    if (helper(archive_name, files, 'c') != 0) { // calls helper function with the "create" mode, checks for return status of helper function
        printf("Error occured while creating %s", archive_name);
        return -1;
    }  
    return 0;  // no errors, return success
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    if (helper(archive_name, files, 'a') != 0) {  // calls helper fucntion with the "append" mode (more accurately, NOT in "create" mode), checks for errors
        printf("Error occured while appending to %s", archive_name);
        return -1;
    }
    return 0;  // no errors, return success
}


int get_archive_file_list(const char *archive_name, file_list_t *files) {
    tar_header header[MAX_MSG_LEN];
    char err_msg[MAX_MSG_LEN];  // stores error message for printing
    int size;
    FILE *archive_ptr = fopen(archive_name, "r");
    if (archive_ptr == NULL) {  // fopen error check
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    } 
    fseek(archive_ptr, 0L, SEEK_END);
    int end = ftell(archive_ptr);
    if (end == -1) {  // error checking fseek/ftell
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    rewind(archive_ptr);
    if (ftell(archive_ptr) != 0) {  // error checking rewind -- should point to beginning of archive_ptr's file
        snprintf(err_msg, MAX_MSG_LEN, "Failed to rewind in archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    // read header block to get the size attribute
    fread(header, MAX_MSG_LEN, 1, archive_ptr);
    if (ferror(archive_ptr)) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to read archive %s ferror value: %d", archive_name, ferror(archive_ptr));
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }   
    sscanf(header->size, "%i", &size);  // scan the header size attribute into size variable
    if (file_list_add(files, header->name) != 0) {  // add file to list, error check
        snprintf(err_msg, MAX_MSG_LEN, "Failed to add file %s to file list", header->name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    while (ftell(archive_ptr) + size + (MAX_MSG_LEN * 2) < end) {  // while there is room left in the archive past the end of this file,...
                                                                   // ...excluding footer blocks, then there must be another file in the archive
        if (size <= MAX_MSG_LEN) {  // if this file's contents are contained within a single block
            fseek(archive_ptr, MAX_MSG_LEN, SEEK_CUR);  // move pointer one block over, which should be the beginning of a tar_header
        }
        else if (size % MAX_MSG_LEN == 0) {  // if this file's contents align perfectly with the end of a block/ every block is full
            fseek(archive_ptr, size, SEEK_CUR);  // seek past all of the content -- ends up at the beginning of the next tar_header
        }
        else {  // this file's contents don't fully fill its last block, so we can't directly seek (+size bytes) to the next tar_header
            fseek(archive_ptr, MAX_MSG_LEN * ((size/MAX_MSG_LEN) + 1), SEEK_CUR);  // seek to the end of the final, semi-filled block
        }
        // archive_ptr should now be pointing at the start of another tar_header
        fread(header, MAX_MSG_LEN, 1, archive_ptr);  // read the entire tar_header into header
        if (ferror(archive_ptr)) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to read archive %s after seeking to a tar_header. ferror value: %d", archive_name, ferror(archive_ptr));
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }  
        sscanf(header->size, "%i", &size);  // grab the size attribute of the header
        if (file_list_add(files, header->name) != 0) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to add file %s to file list", header->name);
            perror(err_msg);
            fclose(archive_ptr);
            return -1;
        }
    }
    if (fclose(archive_ptr) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    char buffer[MAX_MSG_LEN];
    char err_msg[MAX_MSG_LEN];  // stores error message for printing
    tar_header header[MAX_MSG_LEN];
    int size;
    FILE *archive_ptr = fopen(archive_name, "r");
    if (archive_ptr == NULL) {  // fopen error check
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    } 
    FILE *new_ptr;
    fseek(archive_ptr, 0L, SEEK_END);
    int end = ftell(archive_ptr);
    if (end == -1) {  // error checking fseek/ftell
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    rewind(archive_ptr);
    if (ftell(archive_ptr) != 0) {  // error checking rewind -- should point to beginning of archive_ptr's file
        snprintf(err_msg, MAX_MSG_LEN, "Failed to rewind in archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    fread(header, MAX_MSG_LEN, 1, archive_ptr);
    if (ferror(archive_ptr)) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to read archive %s ferror value: %d", archive_name, ferror(archive_ptr));
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }   
    sscanf(header->size, "%i", &size);
    while (ftell(archive_ptr) + size + (MAX_MSG_LEN * 2) < end) {  // while there is room left in the archive past the end of this file,...
                                                                   // ...excluding footer blocks, then there must be another file in the archive
        new_ptr = fopen(header->name, "w");
        if (new_ptr == NULL) {  // fopen error check
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s in %s", header->name, archive_name);
            perror(err_msg);
            fclose(new_ptr);
            fclose(archive_ptr);
            return -1;
        }
        if (size < MAX_MSG_LEN) {  // if this file's contents are contained within a single block
            fread(buffer, 1, size, archive_ptr);  // read size bytes into buffer
            if (ferror(archive_ptr)) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to read archive %s ferror value: %d", archive_name, ferror(archive_ptr));
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            if (fwrite(buffer, 1, size, new_ptr) < size) {  // write buffer into new_ptr's file
                snprintf(err_msg, MAX_MSG_LEN, "Failed to write to file %s", header->name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            fseek(archive_ptr, -size, SEEK_CUR);  // seek back to the start of the tar_header
            int err = ftell(archive_ptr);
            if (err % MAX_MSG_LEN != 0) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek back to the start of a tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            fseek(archive_ptr, MAX_MSG_LEN, SEEK_CUR);  // seek past the tar_header
            if (ftell(archive_ptr) == err) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek past a tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
        }
        else if (size % MAX_MSG_LEN == 0) {  // if this file's contents align perfectly with the end of a block/ every block is full
            for (int i = 0; i < (size/MAX_MSG_LEN); i++) {  // for each block of the file in the archive
                fread(buffer, 1, MAX_MSG_LEN, archive_ptr);  // read a block into buffer
                if (ferror(archive_ptr)) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to read a block from archive %s ferror value: %d", archive_name, ferror(archive_ptr));
                    perror(err_msg);
                    fclose(new_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
                if (fwrite(buffer, 1, MAX_MSG_LEN, new_ptr) < MAX_MSG_LEN) {  // write buffer into new_ptr's file
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to fully write a block from archive to file");
                    perror(err_msg);
                    fclose(new_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
            }
            fseek(archive_ptr, -size, SEEK_CUR);  // seek back to the tar_header
            int err = ftell(archive_ptr);  // get offset of tar_header
            if (err % MAX_MSG_LEN != 0) {  // if the offset doesn't align with the start of a block, error
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek back to the start of a tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            fseek(archive_ptr, MAX_MSG_LEN * (size/MAX_MSG_LEN), SEEK_CUR);  // seek to the next tar_header
            if (ftell(archive_ptr) == err) {  // failed to seek past file
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek to the next tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
        }
        else {  // this file's contents don't fully fill its last block, so we can't directly seek (+size bytes) to the next tar_header
            for (int i = 0; i < (size/MAX_MSG_LEN); i++) {  // for each FULL block of the file in the archive
                fread(buffer, 1, MAX_MSG_LEN, archive_ptr);
                if (ferror(archive_ptr)) {
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to read a block from archive %s ferror value: %d", archive_name, ferror(archive_ptr));
                    perror(err_msg);
                    fclose(new_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
                if (fwrite(buffer, 1, MAX_MSG_LEN, new_ptr) < MAX_MSG_LEN) {  // write buffer into new_ptr's file, check error
                    snprintf(err_msg, MAX_MSG_LEN, "Failed to fully write a block from archive to file");
                    perror(err_msg);
                    fclose(new_ptr);
                    fclose(archive_ptr);
                    return -1;
                }
            }
            // still have to read the not-full block
            fread(buffer, 1, (size % MAX_MSG_LEN), archive_ptr);  // read ONLY the data bytes from the FINAL block into buffer
            if (ferror(archive_ptr)) {
                snprintf(err_msg, MAX_MSG_LEN, "Failed to read FINAL block of a file from archive %s ferror value: %d", archive_name, ferror(archive_ptr));
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            if (fwrite(buffer, 1, (size % MAX_MSG_LEN), new_ptr) < (size % MAX_MSG_LEN)) {  // write ONLY the data bytes from buffer into new_ptr's file, check error
                snprintf(err_msg, MAX_MSG_LEN, "Failed to fully write the data of the block from archive to file");
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            fseek(archive_ptr, -size, SEEK_CUR);  // seek back to the start of the tar_header
            int err = ftell(archive_ptr);  // get offset of tar_header
            if (err % MAX_MSG_LEN != 0) {  // if the offset doesn't align with the start of a block, error
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek back to the start of a tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
            fseek(archive_ptr, MAX_MSG_LEN * ((size/MAX_MSG_LEN) + 1), SEEK_CUR);  // seek to the next tar_header
            if (ftell(archive_ptr) == err) {  // failed to seek past file
                snprintf(err_msg, MAX_MSG_LEN, "Failed to seek to the next tar_header in archive %s", archive_name);
                perror(err_msg);
                fclose(new_ptr);
                fclose(archive_ptr);
                return -1;
            }
        }
        fread(header, MAX_MSG_LEN, 1, archive_ptr);
        if (ferror(archive_ptr)) {
            snprintf(err_msg, MAX_MSG_LEN, "Failed to read tar_header from archive %s into header. ferror value: %d", archive_name, ferror(archive_ptr));
            perror(err_msg);
            fclose(new_ptr);
            fclose(archive_ptr);
            return -1;
        }
        sscanf(header->size, "%i", &size);  // grab size from header to reevaluate loop condition
    }
    if (fclose(new_ptr) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close archive %s", archive_name);
        perror(err_msg);
        fclose(archive_ptr);
        return -1;
    }
    if (fclose(archive_ptr) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close archive %s", archive_name);
        perror(err_msg);
        // new_ptr should be closed by now, archive_ptr failed to close, no close calls necessary here
        return -1;
    }
    return 0;  // success
}
