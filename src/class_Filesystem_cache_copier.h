#ifndef HEADER_FILESYSTEM_CACHE_COPIER
#define HEADER_FILESYSTEM_CACHE_COPIER
#include <memory>
#include "class_Filesystem_file_data.h"

/*
An utility class to move cached data between two files.
It is possible that the files have different data types
*/

class Filesystem_cache_copier
{
    Filesystem_file_data &dest_file_data;
    Filesystem_file_data &source_file_data;
    size_t source_cache_id = 0;
    const char *source_cache_ptr = nullptr;
    size_t dest_cache_id = 0;
    char * dest_cache_ptr = nullptr;
    void load_source_cache(size_t source_data_offset);
    void load_dest_cache(size_t dest_data_offset);
public:
    Filesystem_cache_copier(Filesystem_file_data &dest_file_data, Filesystem_file_data &source_file_data);
    ~Filesystem_cache_copier();
    void copy(size_t dest_index, size_t source_index);
    void flush_buffer();
};
#endif