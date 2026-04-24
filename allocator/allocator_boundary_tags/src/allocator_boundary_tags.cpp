#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"

#include "allocator_with_fit_mode.h"

// mem = parent alloc + fit mode + total size + mutex + block head
// block = size + prev + next + data

auto allocator_boundary_tags::get_parent_alloc() const
{
    return static_cast<memory_resource**>(_trusted_memory);
}

auto allocator_boundary_tags::get_fit_mode() const
{
    return reinterpret_cast<fit_mode*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*));
}

auto allocator_boundary_tags::get_full_size() const
{
    return reinterpret_cast<size_t*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode));
}

auto allocator_boundary_tags::get_mutex() const
{
    return reinterpret_cast<std::mutex*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode) + sizeof(size_t));
}

auto allocator_boundary_tags::get_head() const
{
    return reinterpret_cast<void**>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex));
}

auto allocator_boundary_tags::get_free_prev_block(void* block)
{
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
}

auto allocator_boundary_tags::get_free_next_block(void* block)
{
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + sizeof(void*));
}

auto allocator_boundary_tags::get_prev_block(void* block)
{
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + sizeof(void*) + sizeof(void*));
}

auto allocator_boundary_tags::set_prev_block(void* block, void* prev_block)
{
    *get_prev_block(block) = prev_block;
}

auto allocator_boundary_tags::get_size_block_ptr(void* block)
{
    return static_cast<size_t*>(block);
}

auto allocator_boundary_tags::get_size_block(void* block)
{
    return *get_size_block_ptr(block) & ~static_cast<size_t>(1);
}

void allocator_boundary_tags::insert_free_block(void* block) const
{
    auto head = get_head();
    void* curr = *head;
    void* prev = nullptr;

    while (curr != nullptr && curr < block)
    {
        prev = curr;
        curr = *get_free_next_block(curr);
    }

    *get_free_prev_block(block) = prev;
    *get_free_next_block(block) = curr;

    if (prev != nullptr)
    {
        *get_free_next_block(prev) = block;
    } else
    {
        *head = block;
    }

    if (curr != nullptr)
    {
        *get_free_prev_block(curr) = block;
    }
}

void allocator_boundary_tags::remove_free_block(void* block) const
{
    auto next = *get_free_next_block(block);
    auto prev = *get_free_prev_block(block);

    if (prev != nullptr)
    {
        *get_free_next_block(prev) = next;
    } else
    {
        *get_head() = next;
    }

    if (next != nullptr)
    {
        *get_free_prev_block(next) = prev;
    }
}

bool allocator_boundary_tags::is_block_free(void* block)
{
    return (*get_size_block_ptr(block) & static_cast<size_t>(1)) == 0;
}


allocator_boundary_tags::~allocator_boundary_tags()
{
    get_mutex()->~mutex();
    auto parent_alloc = *get_parent_alloc();
    parent_alloc->deallocate(_trusted_memory, *get_full_size());
}

/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr)
    {
        parent_allocator = std::pmr::get_default_resource();
    }

    size_t total_size = allocator_metadata_size + space_size;
    void* full_mem = parent_allocator->allocate(total_size);
    if (full_mem == nullptr)
    {
        throw std::bad_alloc();
    }

    _trusted_memory = full_mem;

    *get_parent_alloc() = parent_allocator;
    *get_fit_mode() = allocate_fit_mode;
    *get_full_size() = total_size;
    new (get_mutex()) std::mutex();

    void* head = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    *get_head() = head;

    if (space_size < occupied_block_metadata_size)
    {
        parent_allocator->deallocate(_trusted_memory, total_size);
        throw std::bad_alloc();
    }

    *get_free_prev_block(head) = nullptr;
    *get_free_next_block(head) = nullptr;
    *get_prev_block(head) = nullptr;
    *get_size_block_ptr(head) = space_size & ~static_cast<size_t>(1);
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    std::lock_guard lock(*get_mutex());
    size_t needed = size + occupied_block_metadata_size;

    void* found_block = nullptr;
    size_t found_block_size = 0;

    void* best_block = nullptr;
    size_t best_block_size = SIZE_MAX;

    void* worst_block = nullptr;
    size_t worst_block_size = 0;

    void* curr = *get_head();
    while (curr != nullptr)
    {
        size_t block_size = get_size_block(curr);
        if (block_size >= needed)
        {
            switch (*get_fit_mode())
            {
            case fit_mode::first_fit:
                {
                    found_block = curr;
                    found_block_size = block_size;

                    break;
                }
            case fit_mode::the_best_fit:
                {
                    if (block_size < best_block_size)
                    {
                        best_block = curr;
                        best_block_size = block_size;
                    }

                    break;
                }
            case fit_mode::the_worst_fit:
                {
                    if (block_size > worst_block_size)
                    {
                        worst_block = curr;
                        worst_block_size = block_size;
                    }

                    break;
                }
            }
            if (found_block != nullptr && *get_fit_mode() == fit_mode::first_fit)
            {
                break;
            }
        }
        curr = *get_free_next_block(curr);
    }

    switch (*get_fit_mode())
    {
        case fit_mode::first_fit:
            break;
        case fit_mode::the_best_fit:
            found_block = best_block;
            found_block_size = best_block_size;
            break;
        case fit_mode::the_worst_fit:
            found_block = worst_block;
            found_block_size = worst_block_size;
            break;
    }

    if (found_block == nullptr)
    {
        throw std::bad_alloc();
    }

    void* prev = *get_prev_block(found_block);
    remove_free_block(found_block);

    size_t remainder = found_block_size - needed;
    if (remainder >= occupied_block_metadata_size)
    {
        *get_size_block_ptr(found_block) = needed | 1;
        set_prev_block(found_block, prev);

        void* new_free = static_cast<char*>(found_block) + needed;
        *get_size_block_ptr(new_free) = remainder & ~static_cast<size_t>(1);
        *get_free_prev_block(new_free) = nullptr;
        *get_free_next_block(new_free) = nullptr;
        set_prev_block(new_free, found_block);

        void* new_block = static_cast<char*>(new_free) + remainder;
        if (new_block < static_cast<char*>(_trusted_memory) + *get_full_size())
        {
            set_prev_block(new_block, new_free);
        }

        insert_free_block(new_free);
    } else
    {
        *get_size_block_ptr(found_block) = found_block_size | 1;
        set_prev_block(found_block, prev);
    }

    return static_cast<char*>(found_block) + occupied_block_metadata_size;
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    std::lock_guard lock(*get_mutex());

    if (at == nullptr)
    {
        return;
    }

    char* mem_st = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    char* mem_end = static_cast<char*>(_trusted_memory) + *get_full_size();
    if (at < mem_st || at >= mem_end)
    {
        throw std::bad_alloc();
    }

    void* block = static_cast<char*>(at) - occupied_block_metadata_size;
    size_t block_size = get_size_block(block);
    *get_size_block_ptr(block) = block_size;

    void* prev = *get_prev_block(block);
    if (prev != nullptr && is_block_free(prev))
    {
        remove_free_block(prev);
        size_t prev_size = get_size_block(prev);
        *get_size_block_ptr(prev) = prev_size + block_size;
        block = prev;
    }

    void* next = static_cast<char*>(block) + get_size_block(block);
    if (next < mem_end && is_block_free(next))
    {
        remove_free_block(next);
        size_t next_size = get_size_block(next);
        *get_size_block_ptr(block) = next_size + get_size_block(block);
    }

    void* new_next = static_cast<char*>(block) + get_size_block(block);
    if (new_next < mem_end)
    {
        set_prev_block(new_next, block);
    }

    insert_free_block(block);
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard lock(*get_mutex());
    *get_fit_mode() = mode;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    std::lock_guard lock(*get_mutex());
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<block_info> result;

    char* mem_st = static_cast<char*>(_trusted_memory);
    char* mem_end = mem_st + *get_full_size();
    char* curr = mem_st + allocator_metadata_size;

    while (curr < mem_end)
    {
        bool is_free = is_block_free(curr);
        size_t size = get_size_block(curr);
        result.emplace_back(size, !is_free);
        curr += size;
    }

    return result;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return {_trusted_memory};
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() noexcept
{
    return {};
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    if (this == &other)
    {
        return true;
    }

    auto p = dynamic_cast<const allocator_boundary_tags*>(&other);
    if (p == nullptr)
    {
        return false;
    }

    return _trusted_memory == p->_trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied == other._occupied && _occupied_ptr == other._occupied_ptr && _trusted_memory == other._trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (_trusted_memory == nullptr || _occupied_ptr == nullptr)
    {
        return *this;
    }

    char* next = static_cast<char*>(_occupied_ptr) + get_size_block(_occupied_ptr);
    char* mem_end = static_cast<char*>(_trusted_memory) + *reinterpret_cast<size_t*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode));

    if (next >= mem_end)
    {
        _occupied_ptr = nullptr;
        _occupied = false;
        _trusted_memory = nullptr;
    }
    else
    {
        _occupied_ptr = next;
        _occupied = !is_block_free(_occupied_ptr);
    }

    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (_trusted_memory != nullptr && _occupied_ptr != nullptr)
    {
        void* prev = *get_prev_block(_occupied_ptr);
        char* mem_st = static_cast<char*>(_trusted_memory) + allocator_metadata_size;

        if (prev != nullptr && prev >= mem_st)
        {
            _occupied_ptr = prev;
            _occupied = !is_block_free(_occupied_ptr);
        }
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    auto tmp = *this;
    ++*this;
    return tmp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    auto tmp = *this;
    --*this;
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    return _occupied_ptr != nullptr ? get_size_block(_trusted_memory) : 0;
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    if (_occupied_ptr != nullptr && _occupied)
    {
        return static_cast<char*>(_occupied_ptr) + occupied_block_metadata_size;
    }
    return nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator() : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr) {}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted) : _trusted_memory(trusted)
{
    if (_trusted_memory != nullptr)
    {
        auto alloc = static_cast<allocator_boundary_tags*>(trusted);
        char* mem_st = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
        char* mem_end = static_cast<char*>(_trusted_memory) + *alloc->get_full_size();

        if (mem_st >= mem_end)
        {
            _occupied_ptr = nullptr;
            _occupied = false;
        } else
        {
            _occupied_ptr = mem_st;
            _occupied = !is_block_free(_occupied_ptr);
        }
    } else
    {
        _occupied_ptr = nullptr;
        _occupied = false;
    }
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
