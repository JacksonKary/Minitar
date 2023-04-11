#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }
    file_list_t files;
    file_list_init(&files);
    // parse command-line arguments and invoke functions from 'minitar.h'
    // to execute archive operations
    for (int i = 4; i < argc; i++) {  // iterate through all of the file_name_i arguments
        if (file_list_add(&files, argv[i]) != 0) {
            printf("Error: file_list_add failed in main");
            file_list_clear(&files);
            return 1;
        }
    }

    if (strcmp(argv[1], "-c") == 0) {  // create mode to call create_archive
        if (create_archive(argv[3], &files) != 0) {
            printf("Error: create_archive failed in main");
            file_list_clear(&files);
            return 1;
        }
    } else if (strcmp(argv[1], "-a") == 0) {  // append mode to call append_files_to_archive
        if (append_files_to_archive(argv[3], &files) != 0) {
            printf("Error: append_files_to_archive failed in main");
            file_list_clear(&files);
            return 1;
        }
    } else if (strcmp(argv[1], "-t") == 0) {  // list mode to call get_archive_file_list
        if (get_archive_file_list(argv[3], &files) != 0) {
            printf("Error: get_archive_file_list failed in main");
            file_list_clear(&files);
            return 1;
        }
        node_t *current = files.head;
        for(int i = 0; i <= files.size - 1; i++){  // prints names out in main as opposed to in get_archive_file_list
            printf("%s\n", current->name);
            current = current->next;
        }
    } else if (strcmp(argv[1], "-u") == 0) {  // update mode to append updated versions of files to the archive
        int checker = 0;
        file_list_t new_list;
        file_list_init(&new_list);
        node_t *current = files.head;
        if (get_archive_file_list(argv[3], &new_list) != 0) {  // load file names into new_list
            printf("Error: get_archive_file_list failed in main");
            file_list_clear(&new_list);
            file_list_clear(&files);
            return 1;
        }
        for(int i = 0; i <= files.size - 1; i++){  // check to see that every supplied file name is already in the archive
            if (file_list_contains(&new_list, current->name) == 0) {  // current->name is not in the archive
                printf("Error: One or more of the specified files is not already present in archive");
                checker = 1;
                break;  // all files must be inthe archive already. current->name isn't. break
            }
            current = current->next;
        }
        if (checker == 0) {  // if all supplied file names exist in the archive
            if (append_files_to_archive(argv[3], &files) != 0) {  // appends the new files to the end of the archive
                printf("Error: append_files_to_archive failed in main");
                file_list_clear(&new_list);
                file_list_clear(&files);
                return 1;
            }
        }
        file_list_clear(&new_list);  // void, no need to error check
    } else if (strcmp(argv[1], "-x") == 0) {
        if (extract_files_from_archive(argv[3]) != 0) {
            printf("Error: extract_files_from_archive failed in main");
            file_list_clear(&files);
            return 1;
        }
    }
    file_list_clear(&files);
    return 0;
}
