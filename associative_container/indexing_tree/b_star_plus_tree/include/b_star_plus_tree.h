#ifndef SYS_PROG_B_STAR_PLUS_TREE_H
#define SYS_PROG_B_STAR_PLUS_TREE_H

#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>
#include "../../b_plus_tree/include/b_plus_tree.h"

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BSP_tree final : private compare
{
public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    struct bsptree_node_term
    {
        bsptree_node_term* _next;
        std::vector<tree_data_type> _data;
        std::vector<size_t> _shown_indexes;

        bsptree_node_term() : _next(nullptr) {}
    };

    BP_tree<tkey, tvalue, compare, t> _tree;
    bsptree_node_term* _head;

    bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
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
            bsptree_node_term* next = _head->_next;
            delete _head;
            _head = next;
        }
    }

    void rebuild_leaf_chain()
    {
        delete_leaf_chain();

        bsptree_node_term* tail = nullptr;
        auto it = _tree.cbegin();
        auto end_it = _tree.cend();

        while (it != end_it)
        {
            auto* leaf = new bsptree_node_term();
            size_t previous_index = it.index();

            leaf->_data.push_back({it->first, it->second});
            leaf->_shown_indexes.push_back(it.index());
            ++it;

            while (it != end_it && it.index() == previous_index + 1)
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

public:
    class bsptree_iterator;
    class bsptree_const_iterator;

    class bsptree_iterator final
    {
        bsptree_node_term* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bsptree_iterator;

        friend class BSP_tree;
        friend class bsptree_const_iterator;

        explicit bsptree_iterator(bsptree_node_term* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

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

    class bsptree_const_iterator final
    {
        const bsptree_node_term* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bsptree_const_iterator;

        friend class BSP_tree;
        friend class bsptree_iterator;

        explicit bsptree_const_iterator(const bsptree_node_term* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

        bsptree_const_iterator(const bsptree_iterator& it) noexcept
            : _node(it._node), _index(it._index)
        {
        }

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

    explicit BSP_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _tree(cmp, alloc), _head(nullptr)
    {
    }

    explicit BSP_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : BSP_tree(comp, alloc)
    {
    }

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BSP_tree(iterator begin_it, iterator end_it, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BSP_tree(cmp, alloc)
    {
        for (; begin_it != end_it; ++begin_it)
        {
            emplace(begin_it->first, begin_it->second);
        }
    }

    BSP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BSP_tree(cmp, alloc)
    {
        for (const auto& item : data)
        {
            emplace(item.first, item.second);
        }
    }

    BSP_tree(const BSP_tree& other)
        : compare(static_cast<const compare&>(other)),
          _tree(other._tree),
          _head(nullptr)
    {
        rebuild_leaf_chain();
    }

    BSP_tree(BSP_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _tree(std::move(other._tree)),
          _head(other._head)
    {
        other._head = nullptr;
    }

    BSP_tree& operator=(const BSP_tree& other)
    {
        if (this == &other)
        {
            return *this;
        }

        BSP_tree copy(other);
        *this = std::move(copy);
        return *this;
    }

    BSP_tree& operator=(BSP_tree&& other) noexcept
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

    ~BSP_tree() noexcept
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

    bsptree_iterator begin()
    {
        return _head == nullptr ? end() : bsptree_iterator(_head, 0);
    }

    bsptree_iterator end()
    {
        return bsptree_iterator(nullptr, 0);
    }

    bsptree_const_iterator begin() const
    {
        return cbegin();
    }

    bsptree_const_iterator end() const
    {
        return cend();
    }

    bsptree_const_iterator cbegin() const
    {
        return _head == nullptr ? cend() : bsptree_const_iterator(_head, 0);
    }

    bsptree_const_iterator cend() const
    {
        return bsptree_const_iterator(nullptr, 0);
    }

    size_t size() const noexcept
    {
        return _tree.size();
    }

    bool empty() const noexcept
    {
        return _tree.empty();
    }

    bsptree_iterator find(const tkey& key)
    {
        for (bsptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return bsptree_iterator(node, i);
                }
            }
        }

        return end();
    }

    bsptree_const_iterator find(const tkey& key) const
    {
        for (const bsptree_node_term* node = _head; node != nullptr; node = node->_next)
        {
            for (size_t i = 0; i < node->_data.size(); ++i)
            {
                if (!compare_keys(node->_data[i].first, key) && !compare_keys(key, node->_data[i].first))
                {
                    return bsptree_const_iterator(node, i);
                }
            }
        }

        return cend();
    }

    bsptree_iterator lower_bound(const tkey& key)
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

    bsptree_const_iterator lower_bound(const tkey& key) const
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

    bsptree_iterator upper_bound(const tkey& key)
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

    bsptree_const_iterator upper_bound(const tkey& key) const
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

    std::pair<bsptree_iterator, bool> insert(const tree_data_type& data)
    {
        return emplace(data.first, data.second);
    }

    std::pair<bsptree_iterator, bool> insert(tree_data_type&& data)
    {
        return emplace(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    std::pair<bsptree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        auto result = _tree.emplace(std::move(data));
        if (result.second)
        {
            rebuild_leaf_chain();
        }

        return {find(result.first->first), result.second};
    }

    bsptree_iterator insert_or_assign(const tree_data_type& data)
    {
        return emplace_or_assign(data.first, data.second);
    }

    bsptree_iterator insert_or_assign(tree_data_type&& data)
    {
        return emplace_or_assign(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    bsptree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        auto it = _tree.emplace_or_assign(std::move(data));
        rebuild_leaf_chain();
        return find(it->first);
    }

    bsptree_iterator erase(bsptree_iterator pos)
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

    bsptree_iterator erase(bsptree_const_iterator pos)
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

    bsptree_iterator erase(bsptree_iterator beg, bsptree_iterator en)
    {
        while (beg != en)
        {
            beg = erase(beg);
        }

        return en;
    }

    bsptree_iterator erase(bsptree_const_iterator beg, bsptree_const_iterator en)
    {
        while (beg != en)
        {
            beg = bsptree_const_iterator(erase(beg));
        }

        if (en == cend())
        {
            return end();
        }

        return find(en->first);
    }

    bsptree_iterator erase(const tkey& key)
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
BSP_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BSP_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BSP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BSP_tree<tkey, tvalue, compare, t>;

#endif
