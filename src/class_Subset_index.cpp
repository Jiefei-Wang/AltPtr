#include <algorithm>
#include <Rcpp.h>
#include "class_Subset_index.h"
#include "utils.h"
#include "altrep_macros.h"


/*
============================================
utility functions
============================================
*/
template<class T>
std::string vector_to_string(std::vector<T> &vec, size_t n_print)
{
    if (vec.size() == 0)
    {
        return "[]";
    }
    if (n_print == 0)
    {
        return "[...]";
    }

    std::string result;
    result = "[";
    size_t i;
    for (i = 0; i < vec.size() - 1 && i < n_print - 1; i++)
    {
        result += std::to_string(vec[i]) + ",";
    }
    result += std::to_string(vec[i]) + (n_print<vec.size()?",...":"") + "]";
    return result;
}

/*
============================================
class member functions
============================================
*/
Subset_index::Subset_index(size_t start, size_t length, int64_t stride)
{
    push_back(start, length, stride);
}

void Subset_index::push_back(size_t start, size_t length, int64_t stride)
{
    if (length == 0)
        return;

    if (starts.size() != 0 &&
        strides.back() == stride &&
        starts.back() + lengths.back() * strides.back() == start)
    {
        lengths.back() += length;
    }
    else
    {
        starts.push_back(start);
        lengths.push_back(length);
        strides.push_back(stride);
        partial_lengths.push_back(total_length);
    }
    total_length += length;
}
bool Subset_index::is_consecutive() const
{
    if (starts.size() == 0)
        return true;
    if (starts.size() > 1)
        return false;

    return strides[0] == 1;
}
size_t Subset_index::get_subset_block_offset(size_t subset_index) const
{
    throw_if_not(subset_index < total_length);
    auto it = std::lower_bound(partial_lengths.begin(), partial_lengths.end(), subset_index);
    if (it == partial_lengths.end() ||
        subset_index < *it)
    {
        --it;
    }
    return it - partial_lengths.begin();
}

size_t Subset_index::get_source_index(size_t subset_index) const
{
    throw_if_not(subset_index < total_length);
    size_t block_offset = get_subset_block_offset(subset_index);
    size_t source_index = starts[block_offset] + strides[block_offset] * (subset_index - partial_lengths[block_offset]);
    return source_index;
}

std::vector<size_t> Subset_index::get_subset_index(size_t source_index) const
{
    std::vector<size_t> idx;
    for (size_t i = 0; i < starts.size(); i++)
    {
        const size_t &cur_start = starts[i];
        const int64_t &cur_stride = strides[i];
        const size_t &cur_length = lengths[i];
        const size_t &cur_partial_length = partial_lengths[i];
        if (cur_stride == 0)
        {
            if (cur_start == source_index)
            {
                for (size_t j = 0; j < cur_length; j++)
                {
                    idx.push_back(cur_partial_length + j);
                }
            }
            continue;
        }
        int64_t within_block_idx = ((int64_t)source_index - cur_start) / cur_stride;
        int remainder = ((int64_t)source_index - cur_start) % cur_stride;
        if (remainder == 0 && within_block_idx >= 0 && within_block_idx < (int64_t)cur_length)
        {
            idx.push_back(cur_partial_length + within_block_idx);
        }
    }
    return idx;
}

bool Subset_index::contains_index(size_t source_index) const
{
    if (starts.size() == 0)
    {
        return false;
    }
    for (size_t i = 0; i < starts.size(); i++)
    {
        const size_t &cur_start = starts[i];
        const int64_t &cur_stride = strides[i];
        const size_t &cur_length = lengths[i];
        if (cur_stride == 0)
        {
            if (cur_start == source_index)
            {
                return true;
            }
            continue;
        }
        int64_t within_block_idx = ((int64_t)source_index - cur_start) / cur_stride;
        int remainder = ((int64_t)source_index - cur_start) % cur_stride;
        if (remainder == 0 && within_block_idx >= 0 && within_block_idx < (int64_t)cur_length)
        {
            return true;
        }
    }
    return false;
}

#define GET_SRC_INDEX(idx, idx_idx, i) idx.get_source_index(GET_INDEX(idx_idx, i))
//Turn idx to the Subset_index object
Subset_index Subset_index::to_subset_index(SEXP idx, Subset_index &old_index)
{
    size_t index_length = XLENGTH(idx);
    Subset_index index;
    size_t i = 0;
    while (i < index_length)
    {
        size_t start = GET_SRC_INDEX(old_index, idx, i);
        //If the start is the last element
        if (i + 1 == index_length)
        {
            index.push_back(start, 1, 1);
            break;
        }
        //Otherwise, we compute stride and length
        size_t next_index = GET_SRC_INDEX(old_index, idx, i + 1);
        size_t stride = next_index - start;
        size_t next_i = i + 2;
        while (next_i < index_length)
        {
            size_t cur_src_idx = GET_SRC_INDEX(old_index, idx, next_i);
            size_t pre_src_idx = GET_SRC_INDEX(old_index, idx, next_i - 1);
            if ((int64_t)cur_src_idx - pre_src_idx != stride)
            {
                break;
            }
            ++next_i;
        }
        index.push_back(start, next_i - i, stride);
        i = next_i;
    }
    return index;
}

size_t Subset_index::get_index_size(SEXP idx, Subset_index &index)
{
    const static size_t compression_ratio = 4;
    size_t index_length = XLENGTH(idx);

    if (XLENGTH(idx) == 0)
    {
        return 0;
    }
    if (XLENGTH(idx) == 2)
    {
        return compression_ratio;
    }
    size_t elt_num = 0;
    size_t i = 0;
    while (i < index_length)
    {
        size_t start = GET_INDEX(idx, i);
        //If the start is the last element
        if (i + 1 == index_length)
        {
            break;
        }
        //Otherwise, we compute stride and length
        int64_t stride = (int64_t)GET_INDEX(idx, i + 1) - start;
        size_t length = 2;
        size_t next_i = i + length;
        while (next_i < index_length)
        {
            if ((int64_t)(GET_INDEX(idx, next_i) - GET_INDEX(idx, next_i - 1)) != stride)
            {
                break;
            }
            else
            {
                length++;
            }
            next_i = i + length;
        }
        elt_num++;
        i = next_i;
    }
    return elt_num * compression_ratio;
}

std::string Subset_index::summarize(size_t n_print)
{

    std::string summary;
    summary = "Total length: " + std::to_string(total_length) + ", ";
    summary += "Starts: " + vector_to_string(starts, n_print) + ", ";
    summary += "Lengths: " + vector_to_string(lengths, n_print) + ", ";
    summary += "Strides: " + vector_to_string(strides, n_print);
    return summary;
}

size_t Subset_index::get_serialize_size()
{
    size_t size = 0;
    size += sizeof(total_length);
    size += sizeof(starts.size());
    size += sizeof(size_t) * starts.size();
    size += sizeof(size_t) * lengths.size();
    size += sizeof(size_t) * partial_lengths.size();
    size += sizeof(uint64_t) * strides.size();
    return size;
}
void Subset_index::serialize(char *ptr)
{
    *(size_t *)ptr = total_length;
    ptr += sizeof(size_t);
    *(size_t *)ptr = starts.size();
    ptr += sizeof(size_t);
    size_t vec_size = sizeof(size_t) * starts.size();
    memcpy(ptr, starts.data(), vec_size);
    ptr += vec_size;
    memcpy(ptr, lengths.data(), vec_size);
    ptr += vec_size;
    memcpy(ptr, partial_lengths.data(), vec_size);
    ptr += vec_size;
    memcpy(ptr, strides.data(), vec_size);
    ptr += vec_size;
}
void Subset_index::unserialize(char *ptr)
{
    total_length = *(size_t *)ptr;
    ptr += sizeof(size_t);
    size_t vec_length = *(size_t *)ptr;
    ptr += sizeof(size_t);
    size_t vec_size = sizeof(size_t) * vec_length;
    starts.reserve(vec_length);
    lengths.reserve(vec_length);
    partial_lengths.reserve(vec_length);
    strides.reserve(vec_length);
    memcpy(starts.data(), ptr, vec_size);
    ptr += vec_size;
    memcpy(lengths.data(), ptr, vec_size);
    ptr += vec_size;
    memcpy(partial_lengths.data(), ptr, vec_size);
    ptr += vec_size;
    memcpy(strides.data(), ptr, vec_size);
    ptr += vec_size;
}
