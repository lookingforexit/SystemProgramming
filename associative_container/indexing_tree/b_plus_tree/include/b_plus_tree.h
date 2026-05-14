#ifndef SYS_PROG_B_PLUS_TREE_H
#define SYS_PROG_B_PLUS_TREE_H

#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>
#include "../../b_tree/include/b_tree.h"

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BP_tree final : private compare
{
public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    struct bptree_node_term
    {
        bptree_node_term* _next;
        std::vector<tree_data_type> _data;
        std::vector<size_t> _shown_indexes;

        bptree_node_term() : _next(nullptr) {}
    };

    B_tree<tkey, tvalue, compare, t> _tree;
    bptree_node_term* _head;

    bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const
    {
        return compare_keys(lhs.first, rhs.first);
    }

    static tree_data_type_const& pair_as_value(tree_data_type& value) noexcept
    {
        return reinterpret_cast<tree_data_type_const&>(value);
    }

    static const tree_data_type_const& pair_as_value(const tree_data_type& value) noexcept
    {
        return reinterpret_cast<const tree_data_type_const&>(value);
    }

    void delete_leaf_chain() noexcept
    {
        while (_head != nullptr)
        {
            bptree_node_term* next = _head->_next;
            delete _head;
            _head = next;
        }
    }

    void rebuild_leaf_chain()
    {
        delete_leaf_chain();

        bptree_node_term* tail = nullptr;
        auto it = _tree.cbegin();
        auto end_it = _tree.cend();

        while (it != end_it)
        {
            auto* leaf = new bptree_node_term();
            size_t previous_depth = it.depth();
            size_t previous_index = it.index();

            leaf->_data.push_back({it->first, it->second});
            leaf->_shown_indexes.push_back(it.index());
            ++it;

            while (it != end_it && it.depth() == previous_depth && it.index() == previous_index + 1)
            {
                leaf->_data.push_back({it->first, it->second});
                leaf->_shown_indexes.push_back(it.index());
                previous_index = it.index();
                ++it;
            }

            if (_head == nullptr)
            {
                _head = leaf;
            }
            else
            {
                tail->_next = leaf;
            }

            tail = leaf;
        }
    }

    bptree_node_term* leaf_for_key(const tkey& key) noexcept
    {
        for (bptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return node;
                }
            }
        }

        return nullptr;
    }

    const bptree_node_term* leaf_for_key(const tkey& key) const noexcept
    {
        for (const bptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return node;
                }
            }
        }

        return nullptr;
    }

public:
    class bptree_iterator;
    class bptree_const_iterator;

    class bptree_iterator final
    {
        bptree_node_term* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bptree_iterator;

        friend class BP_tree;
        friend class bptree_const_iterator;

        explicit bptree_iterator(bptree_node_term* node = nullptr, size_t index = 0) : _node(node), _index(index) {}

        reference operator*() const noexcept
        {
            return pair_as_value(_node->_data[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(_node->_data[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_data.size())
            {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t current_node_keys_count() const noexcept
        {
            return _node == nullptr ? 0 : _node->_data.size();
        }

        size_t index() const noexcept
        {
            return _node->_shown_indexes[_index];
        }
    };

    class bptree_const_iterator final
    {
        const bptree_node_term* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bptree_const_iterator;

        friend class BP_tree;
        friend class bptree_iterator;

        explicit bptree_const_iterator(const bptree_node_term* node = nullptr, size_t index = 0) : _node(node), _index(index) {}

        bptree_const_iterator(const bptree_iterator& it) noexcept : _node(it._node), _index(it._index) {}

        reference operator*() const noexcept
        {
            return pair_as_value(_node->_data[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(_node->_data[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_data.size())
            {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t current_node_keys_count() const noexcept
        {
            return _node == nullptr ? 0 : _node->_data.size();
        }

        size_t index() const noexcept
        {
            return _node->_shown_indexes[_index];
        }
    };

    explicit BP_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _tree(cmp, alloc), _head(nullptr) {}

    explicit BP_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : BP_tree(comp, alloc) {}

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BP_tree(iterator begin_it, iterator end_it, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BP_tree(cmp, alloc)
    {
        for (; begin_it != end_it; ++begin_it)
        {
            emplace(begin_it->first, begin_it->second);
        }
    }

    BP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BP_tree(cmp, alloc)
    {
        for (const auto& item : data)
        {
            emplace(item.first, item.second);
        }
    }

    BP_tree(const BP_tree& other)
        : compare(static_cast<const compare&>(other)),
          _tree(other._tree),
          _head(nullptr)
    {
        rebuild_leaf_chain();
    }

    BP_tree(BP_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _tree(std::move(other._tree)),
          _head(other._head)
    {
        other._head = nullptr;
    }

    BP_tree& operator=(const BP_tree& other)
    {
        if (this == &other)
        {
            return *this;
        }

        BP_tree copy(other);
        *this = std::move(copy);
        return *this;
    }

    BP_tree& operator=(BP_tree&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        delete_leaf_chain();
        static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
        _tree = std::move(other._tree);
        _head = other._head;
        other._head = nullptr;
        return *this;
    }

    ~BP_tree() noexcept
    {
        delete_leaf_chain();
    }

    tvalue& at(const tkey& key)
    {
        return _tree.at(key);
    }

    const tvalue& at(const tkey& key) const
    {
        return _tree.at(key);
    }

    tvalue& operator[](const tkey& key)
    {
        tvalue& value = _tree[key];
        rebuild_leaf_chain();
        return value;
    }

    tvalue& operator[](tkey&& key)
    {
        tvalue& value = _tree[std::move(key)];
        rebuild_leaf_chain();
        return value;
    }

    bptree_iterator begin()
    {
        return _head == nullptr ? end() : bptree_iterator(_head, 0);
    }

    bptree_iterator end()
    {
        return bptree_iterator(nullptr, 0);
    }

    bptree_const_iterator begin() const
    {
        return cbegin();
    }

    bptree_const_iterator end() const
    {
        return cend();
    }

    bptree_const_iterator cbegin() const
    {
        return _head == nullptr ? cend() : bptree_const_iterator(_head, 0);
    }

    bptree_const_iterator cend() const
    {
        return bptree_const_iterator(nullptr, 0);
    }

    size_t size() const noexcept
    {
        return _tree.size();
    }

    bool empty() const noexcept
    {
        return _tree.empty();
    }

    bptree_iterator find(const tkey& key)
    {
        for (bptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return bptree_iterator(node, i);
                }
            }
        }

        return end();
    }

    bptree_const_iterator find(const tkey& key) const
    {
        for (const bptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return bptree_const_iterator(node, i);
                }
            }
        }

        return cend();
    }

    bptree_iterator lower_bound(const tkey& key)
    {
        for (auto it = begin(); it != end(); ++it)
        {
            if (!compare_keys(it->first, key))
            {
                return it;
            }
        }

        return end();
    }

    bptree_const_iterator lower_bound(const tkey& key) const
    {
        for (auto it = cbegin(); it != cend(); ++it)
        {
            if (!compare_keys(it->first, key))
            {
                return it;
            }
        }

        return cend();
    }

    bptree_iterator upper_bound(const tkey& key)
    {
        for (auto it = begin(); it != end(); ++it)
        {
            if (compare_keys(key, it->first))
            {
                return it;
            }
        }

        return end();
    }

    bptree_const_iterator upper_bound(const tkey& key) const
    {
        for (auto it = cbegin(); it != cend(); ++it)
        {
            if (compare_keys(key, it->first))
            {
                return it;
            }
        }

        return cend();
    }

    bool contains(const tkey& key) const
    {
        return _tree.contains(key);
    }

    void clear() noexcept
    {
        _tree.clear();
        delete_leaf_chain();
    }

    std::pair<bptree_iterator, bool> insert(const tree_data_type& data)
    {
        return emplace(data.first, data.second);
    }

    std::pair<bptree_iterator, bool> insert(tree_data_type&& data)
    {
        return emplace(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    std::pair<bptree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        auto result = _tree.emplace(std::move(data));
        if (result.second)
        {
            rebuild_leaf_chain();
        }
        return {find(result.first->first), result.second};
    }

    bptree_iterator insert_or_assign(const tree_data_type& data)
    {
        return emplace_or_assign(data.first, data.second);
    }

    bptree_iterator insert_or_assign(tree_data_type&& data)
    {
        return emplace_or_assign(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    bptree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        auto it = _tree.emplace_or_assign(std::move(data));
        rebuild_leaf_chain();
        return find(it->first);
    }

    bptree_iterator erase(bptree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }

        tkey key = pos->first;
        auto next = pos;
        ++next;
        erase(key);
        if (next == end())
        {
            return end();
        }
        return find(next->first);
    }

    bptree_iterator erase(bptree_const_iterator pos)
    {
        if (pos == cend())
        {
            return end();
        }

        tkey key = pos->first;
        auto next = pos;
        ++next;
        erase(key);
        if (next == cend())
        {
            return end();
        }
        return find(next->first);
    }

    bptree_iterator erase(bptree_iterator beg, bptree_iterator en)
    {
        while (beg != en)
        {
            beg = erase(beg);
        }
        return en;
    }

    bptree_iterator erase(bptree_const_iterator beg, bptree_const_iterator en)
    {
        while (beg != en)
        {
            beg = bptree_const_iterator(erase(beg));
        }
        if (en == cend())
        {
            return end();
        }
        return find(en->first);
    }

    bptree_iterator erase(const tkey& key)
    {
        auto it = _tree.erase(key);
        if (it == _tree.end())
        {
            return end();
        }

        tkey next_key = it->first;
        rebuild_leaf_chain();
        return find(next_key);
    }
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5, typename U>
BP_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BP_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BP_tree<tkey, tvalue, compare, t>;

#endif
