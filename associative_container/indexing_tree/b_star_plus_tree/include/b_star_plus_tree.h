#ifndef SYS_PROG_B_STAR_PLUS_TREE_H
#define SYS_PROG_B_STAR_PLUS_TREE_H

#include <algorithm>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BSP_tree final : private compare
{
    static_assert(t >= 1, "invalid t for b-star-plus tree");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    static constexpr size_t minimum_leaf_keys = 2 * t - 1;
    static constexpr size_t maximum_leaf_keys = 3 * t - 1;
    static constexpr size_t minimum_children = 2 * t;
    static constexpr size_t maximum_children = 3 * t;

    struct bsptree_node
    {
        bool _is_leaf;
        bsptree_node* _parent;
        bsptree_node* _next;
        bsptree_node* _prev;
        std::vector<tree_data_type*> _entries;
        std::vector<tkey> _keys;
        std::vector<bsptree_node*> _children;

        explicit bsptree_node(bool is_leaf)
            : _is_leaf(is_leaf), _parent(nullptr), _next(nullptr), _prev(nullptr)
        {
            if (is_leaf)
            {
                _entries.reserve(maximum_leaf_keys + 1);
            }
            else
            {
                _keys.reserve(maximum_children);
                _children.reserve(maximum_children + 1);
            }
        }
    };

    pp_allocator<value_type> _allocator;
    bsptree_node* _root;
    bsptree_node* _head;
    size_t _size;

    bool key_less(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    bool key_equal(const tkey& lhs, const tkey& rhs) const
    {
        return !key_less(lhs, rhs) && !key_less(rhs, lhs);
    }

    static tree_data_type_const& pair_as_value(tree_data_type& value) noexcept
    {
        return reinterpret_cast<tree_data_type_const&>(value);
    }

    static const tree_data_type_const& pair_as_value(const tree_data_type& value) noexcept
    {
        return reinterpret_cast<const tree_data_type_const&>(value);
    }

    tree_data_type* allocate_entry(const tree_data_type& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(value);
    }

    tree_data_type* allocate_entry(tree_data_type&& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(std::move(value));
    }

    void free_entry(tree_data_type* entry) noexcept
    {
        if (entry != nullptr)
        {
            pp_allocator<tree_data_type>(_allocator).delete_object(entry);
        }
    }

    bsptree_node* allocate_node(bool is_leaf)
    {
        return new bsptree_node(is_leaf);
    }

    void free_node(bsptree_node* node) noexcept
    {
        delete node;
    }

    void free_subtree(bsptree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        if (node->_is_leaf)
        {
            for (tree_data_type* entry : node->_entries)
            {
                free_entry(entry);
            }
        }
        else
        {
            for (bsptree_node* child : node->_children)
            {
                free_subtree(child);
            }
        }

        free_node(node);
    }

    const tkey& first_key(const bsptree_node* node) const
    {
        const bsptree_node* current = node;
        while (!current->_is_leaf)
        {
            current = current->_children.front();
        }

        return current->_entries.front()->first;
    }

    void rebuild_internal_keys(bsptree_node* node)
    {
        if (node == nullptr || node->_is_leaf)
        {
            return;
        }

        node->_keys.clear();
        for (size_t i = 1; i < node->_children.size(); ++i)
        {
            node->_keys.push_back(first_key(node->_children[i]));
        }
    }

    void refresh_keys_upward(bsptree_node* node)
    {
        while (node != nullptr)
        {
            rebuild_internal_keys(node);
            node = node->_parent;
        }
    }

    size_t child_index_in_parent(const bsptree_node* node) const
    {
        const bsptree_node* parent = node->_parent;
        for (size_t i = 0; i < parent->_children.size(); ++i)
        {
            if (parent->_children[i] == node)
            {
                return i;
            }
        }

        throw std::logic_error("b-star-plus tree parent-child relationship is broken");
    }

    size_t lower_bound_index_in_leaf(const bsptree_node* leaf, const tkey& key) const
    {
        size_t left = 0;
        size_t right = leaf->_entries.size();
        while (left < right)
        {
            size_t middle = left + (right - left) / 2;
            if (key_less(leaf->_entries[middle]->first, key))
            {
                left = middle + 1;
            }
            else
            {
                right = middle;
            }
        }

        return left;
    }

    bsptree_node* leftmost_leaf() const noexcept
    {
        bsptree_node* node = _root;
        while (node != nullptr && !node->_is_leaf)
        {
            node = node->_children.front();
        }
        return node;
    }

    bsptree_node* find_leaf(const tkey& key) const noexcept
    {
        bsptree_node* node = _root;
        while (node != nullptr && !node->_is_leaf)
        {
            size_t child_index = 0;
            while (child_index < node->_keys.size() && !key_less(key, node->_keys[child_index]))
            {
                ++child_index;
            }
            node = node->_children[child_index];
        }

        return node;
    }

    void insert_child_after(bsptree_node* parent, bsptree_node* left_child, bsptree_node* right_child)
    {
        size_t index = child_index_in_parent(left_child);
        parent->_children.insert(parent->_children.begin() + static_cast<ptrdiff_t>(index + 1), right_child);
        right_child->_parent = parent;
        rebuild_internal_keys(parent);
    }

    void redistribute_internal_pair(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* right = parent->_children[left_child_index + 1];

        std::vector<bsptree_node*> all_children;
        all_children.reserve(left->_children.size() + right->_children.size());
        all_children.insert(all_children.end(), left->_children.begin(), left->_children.end());
        all_children.insert(all_children.end(), right->_children.begin(), right->_children.end());

        const size_t left_count = all_children.size() / 2;
        left->_children.assign(all_children.begin(), all_children.begin() + static_cast<ptrdiff_t>(left_count));
        right->_children.assign(all_children.begin() + static_cast<ptrdiff_t>(left_count), all_children.end());

        for (bsptree_node* child : left->_children)
        {
            child->_parent = left;
        }
        for (bsptree_node* child : right->_children)
        {
            child->_parent = right;
        }

        rebuild_internal_keys(left);
        rebuild_internal_keys(right);
        rebuild_internal_keys(parent);
    }

    void split_internal_pair_into_three(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* right = parent->_children[left_child_index + 1];
        auto* middle = allocate_node(false);

        std::vector<bsptree_node*> all_children;
        all_children.reserve(left->_children.size() + right->_children.size());
        all_children.insert(all_children.end(), left->_children.begin(), left->_children.end());
        all_children.insert(all_children.end(), right->_children.begin(), right->_children.end());

        const size_t base_count = all_children.size() / 3;
        const size_t remainder = all_children.size() % 3;
        const size_t left_count = base_count + (remainder > 0 ? 1 : 0);
        const size_t middle_count = base_count + (remainder > 1 ? 1 : 0);

        left->_children.assign(all_children.begin(), all_children.begin() + static_cast<ptrdiff_t>(left_count));
        middle->_children.assign(
                all_children.begin() + static_cast<ptrdiff_t>(left_count),
                all_children.begin() + static_cast<ptrdiff_t>(left_count + middle_count));
        right->_children.assign(
                all_children.begin() + static_cast<ptrdiff_t>(left_count + middle_count),
                all_children.end());

        for (bsptree_node* child : left->_children)
        {
            child->_parent = left;
        }
        for (bsptree_node* child : middle->_children)
        {
            child->_parent = middle;
        }
        for (bsptree_node* child : right->_children)
        {
            child->_parent = right;
        }

        middle->_parent = parent;
        parent->_children.insert(parent->_children.begin() + static_cast<ptrdiff_t>(left_child_index + 1), middle);

        rebuild_internal_keys(left);
        rebuild_internal_keys(middle);
        rebuild_internal_keys(right);
        rebuild_internal_keys(parent);
    }

    void split_root_internal_if_needed(bsptree_node* node)
    {
        while (node != nullptr && node == _root && node->_children.size() > maximum_children)
        {
            size_t left_child_count = minimum_children;
            auto* right = allocate_node(false);

            right->_children.assign(node->_children.begin() + static_cast<ptrdiff_t>(left_child_count), node->_children.end());
            for (bsptree_node* child : right->_children)
            {
                child->_parent = right;
            }

            node->_children.erase(node->_children.begin() + static_cast<ptrdiff_t>(left_child_count), node->_children.end());
            rebuild_internal_keys(node);
            rebuild_internal_keys(right);

            if (node->_parent == nullptr)
            {
                auto* new_root = allocate_node(false);
                new_root->_children.push_back(node);
                new_root->_children.push_back(right);
                node->_parent = new_root;
                right->_parent = new_root;
                rebuild_internal_keys(new_root);
                _root = new_root;
                return;
            }
        }
    }

    void rebalance_internal_after_insert(bsptree_node* node)
    {
        while (node != nullptr && node->_children.size() > maximum_children)
        {
            if (node == _root)
            {
                split_root_internal_if_needed(node);
                return;
            }

            bsptree_node* parent = node->_parent;
            size_t index = child_index_in_parent(node);

            if (index + 1 < parent->_children.size() &&
                parent->_children[index + 1]->_children.size() < maximum_children)
            {
                redistribute_internal_pair(parent, index);
                refresh_keys_upward(parent);
                return;
            }

            if (index > 0 && parent->_children[index - 1]->_children.size() < maximum_children)
            {
                redistribute_internal_pair(parent, index - 1);
                refresh_keys_upward(parent);
                return;
            }

            if (index + 1 < parent->_children.size())
            {
                split_internal_pair_into_three(parent, index);
            }
            else
            {
                split_internal_pair_into_three(parent, index - 1);
            }

            node = parent;
        }
    }

    void redistribute_leaf_pair(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* right = parent->_children[left_child_index + 1];

        std::vector<tree_data_type*> all_entries;
        all_entries.reserve(left->_entries.size() + right->_entries.size());
        all_entries.insert(all_entries.end(), left->_entries.begin(), left->_entries.end());
        all_entries.insert(all_entries.end(), right->_entries.begin(), right->_entries.end());

        const size_t left_count = all_entries.size() / 2;
        left->_entries.assign(all_entries.begin(), all_entries.begin() + static_cast<ptrdiff_t>(left_count));
        right->_entries.assign(all_entries.begin() + static_cast<ptrdiff_t>(left_count), all_entries.end());
        rebuild_internal_keys(parent);
    }

    void split_leaf_pair_into_three(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* right = parent->_children[left_child_index + 1];
        auto* middle = allocate_node(true);

        std::vector<tree_data_type*> all_entries;
        all_entries.reserve(left->_entries.size() + right->_entries.size());
        all_entries.insert(all_entries.end(), left->_entries.begin(), left->_entries.end());
        all_entries.insert(all_entries.end(), right->_entries.begin(), right->_entries.end());

        const size_t base_count = all_entries.size() / 3;
        const size_t remainder = all_entries.size() % 3;
        const size_t left_count = base_count + (remainder > 0 ? 1 : 0);
        const size_t middle_count = base_count + (remainder > 1 ? 1 : 0);

        left->_entries.assign(all_entries.begin(), all_entries.begin() + static_cast<ptrdiff_t>(left_count));
        middle->_entries.assign(
                all_entries.begin() + static_cast<ptrdiff_t>(left_count),
                all_entries.begin() + static_cast<ptrdiff_t>(left_count + middle_count));
        right->_entries.assign(
                all_entries.begin() + static_cast<ptrdiff_t>(left_count + middle_count),
                all_entries.end());

        middle->_parent = parent;
        middle->_prev = left;
        middle->_next = right;
        left->_next = middle;
        right->_prev = middle;

        parent->_children.insert(parent->_children.begin() + static_cast<ptrdiff_t>(left_child_index + 1), middle);
        rebuild_internal_keys(parent);
    }

    void split_leaf_if_needed(bsptree_node* leaf)
    {
        if (leaf->_entries.size() <= maximum_leaf_keys)
        {
            refresh_keys_upward(leaf->_parent);
            return;
        }

        auto* right = allocate_node(true);
        size_t split_index = minimum_children;

        right->_entries.assign(leaf->_entries.begin() + static_cast<ptrdiff_t>(split_index), leaf->_entries.end());
        leaf->_entries.erase(leaf->_entries.begin() + static_cast<ptrdiff_t>(split_index), leaf->_entries.end());

        right->_next = leaf->_next;
        right->_prev = leaf;
        if (leaf->_next != nullptr)
        {
            leaf->_next->_prev = right;
        }
        leaf->_next = right;

        if (_head == nullptr || _head == leaf->_next)
        {
            _head = leftmost_leaf();
        }

        if (leaf->_parent == nullptr)
        {
            auto* new_root = allocate_node(false);
            new_root->_children.push_back(leaf);
            new_root->_children.push_back(right);
            leaf->_parent = new_root;
            right->_parent = new_root;
            rebuild_internal_keys(new_root);
            _root = new_root;
            _head = leftmost_leaf();
            return;
        }

        bsptree_node* parent = leaf->_parent;
        size_t index = child_index_in_parent(leaf);
        if (index + 1 < parent->_children.size() &&
            parent->_children[index + 1]->_entries.size() < maximum_leaf_keys)
        {
            redistribute_leaf_pair(parent, index);
        }
        else if (index > 0 && parent->_children[index - 1]->_entries.size() < maximum_leaf_keys)
        {
            redistribute_leaf_pair(parent, index - 1);
        }
        else if (index + 1 < parent->_children.size())
        {
            split_leaf_pair_into_three(parent, index);
            rebalance_internal_after_insert(parent);
        }
        else
        {
            split_leaf_pair_into_three(parent, index - 1);
            rebalance_internal_after_insert(parent);
        }

        refresh_keys_upward(parent);
    }

    void merge_internal_triplet_into_two(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* middle = parent->_children[left_child_index + 1];
        bsptree_node* right = parent->_children[left_child_index + 2];

        std::vector<bsptree_node*> all_children;
        all_children.reserve(left->_children.size() + middle->_children.size() + right->_children.size());
        all_children.insert(all_children.end(), left->_children.begin(), left->_children.end());
        all_children.insert(all_children.end(), middle->_children.begin(), middle->_children.end());
        all_children.insert(all_children.end(), right->_children.begin(), right->_children.end());

        const size_t left_count = all_children.size() / 2;
        left->_children.assign(all_children.begin(), all_children.begin() + static_cast<ptrdiff_t>(left_count));
        right->_children.assign(all_children.begin() + static_cast<ptrdiff_t>(left_count), all_children.end());

        for (bsptree_node* child : left->_children)
        {
            child->_parent = left;
        }
        for (bsptree_node* child : right->_children)
        {
            child->_parent = right;
        }

        parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(left_child_index + 1));
        free_node(middle);
        rebuild_internal_keys(left);
        rebuild_internal_keys(right);
        rebuild_internal_keys(parent);
    }

    void rebalance_internal_after_erase(bsptree_node* node)
    {
        while (node != nullptr)
        {
            if (node == _root)
            {
                if (!_root->_is_leaf && _root->_children.size() == 1)
                {
                    bsptree_node* old_root = _root;
                    _root = _root->_children.front();
                    _root->_parent = nullptr;
                    old_root->_children.clear();
                    free_node(old_root);
                }
                rebuild_internal_keys(_root);
                return;
            }

            if (node->_children.size() >= minimum_children)
            {
                refresh_keys_upward(node);
                return;
            }

            bsptree_node* parent = node->_parent;
            size_t index = child_index_in_parent(node);
            bsptree_node* left = index > 0 ? parent->_children[index - 1] : nullptr;
            bsptree_node* right = index + 1 < parent->_children.size() ? parent->_children[index + 1] : nullptr;

            if (left != nullptr && left->_children.size() > minimum_children)
            {
                node->_children.insert(node->_children.begin(), left->_children.back());
                left->_children.back()->_parent = node;
                left->_children.pop_back();
                rebuild_internal_keys(left);
                refresh_keys_upward(node);
                return;
            }

            if (right != nullptr && right->_children.size() > minimum_children)
            {
                node->_children.push_back(right->_children.front());
                right->_children.front()->_parent = node;
                right->_children.erase(right->_children.begin());
                rebuild_internal_keys(right);
                refresh_keys_upward(node);
                return;
            }

            if (index + 2 < parent->_children.size())
            {
                merge_internal_triplet_into_two(parent, index);
                node = parent;
                continue;
            }

            if (index >= 2)
            {
                merge_internal_triplet_into_two(parent, index - 2);
                node = parent;
                continue;
            }

            if (left != nullptr)
            {
                left->_children.insert(left->_children.end(), node->_children.begin(), node->_children.end());
                for (bsptree_node* child : node->_children)
                {
                    child->_parent = left;
                }
                node->_children.clear();
                parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
                free_node(node);
                rebuild_internal_keys(left);
                rebuild_internal_keys(parent);
                node = parent;
                continue;
            }

            right->_children.insert(right->_children.begin(), node->_children.begin(), node->_children.end());
            for (bsptree_node* child : node->_children)
            {
                child->_parent = right;
            }
            node->_children.clear();
            parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
            free_node(node);
            rebuild_internal_keys(right);
            rebuild_internal_keys(parent);
            node = parent;
        }
    }

    void merge_leaf_triplet_into_two(bsptree_node* parent, size_t left_child_index)
    {
        bsptree_node* left = parent->_children[left_child_index];
        bsptree_node* middle = parent->_children[left_child_index + 1];
        bsptree_node* right = parent->_children[left_child_index + 2];

        std::vector<tree_data_type*> all_entries;
        all_entries.reserve(left->_entries.size() + middle->_entries.size() + right->_entries.size());
        all_entries.insert(all_entries.end(), left->_entries.begin(), left->_entries.end());
        all_entries.insert(all_entries.end(), middle->_entries.begin(), middle->_entries.end());
        all_entries.insert(all_entries.end(), right->_entries.begin(), right->_entries.end());

        const size_t left_count = all_entries.size() / 2;
        left->_entries.assign(all_entries.begin(), all_entries.begin() + static_cast<ptrdiff_t>(left_count));
        right->_entries.assign(all_entries.begin() + static_cast<ptrdiff_t>(left_count), all_entries.end());

        left->_next = right;
        right->_prev = left;

        parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(left_child_index + 1));
        free_node(middle);
        rebuild_internal_keys(parent);
    }

    void rebalance_leaf_after_erase(bsptree_node* leaf)
    {
        if (leaf == _root)
        {
            if (leaf->_entries.empty())
            {
                free_node(_root);
                _root = nullptr;
                _head = nullptr;
            }
            return;
        }

        if (leaf->_entries.size() >= minimum_leaf_keys)
        {
            refresh_keys_upward(leaf->_parent);
            return;
        }

        bsptree_node* parent = leaf->_parent;
        size_t index = child_index_in_parent(leaf);
        bsptree_node* left = index > 0 ? parent->_children[index - 1] : nullptr;
        bsptree_node* right = index + 1 < parent->_children.size() ? parent->_children[index + 1] : nullptr;

        if (left != nullptr && left->_entries.size() > minimum_leaf_keys)
        {
            leaf->_entries.insert(leaf->_entries.begin(), left->_entries.back());
            left->_entries.pop_back();
            refresh_keys_upward(parent);
            return;
        }

        if (right != nullptr && right->_entries.size() > minimum_leaf_keys)
        {
            leaf->_entries.push_back(right->_entries.front());
            right->_entries.erase(right->_entries.begin());
            refresh_keys_upward(parent);
            return;
        }

        if (index + 2 < parent->_children.size())
        {
            merge_leaf_triplet_into_two(parent, index);
            rebalance_internal_after_erase(parent);
            _head = leftmost_leaf();
            return;
        }

        if (index >= 2)
        {
            merge_leaf_triplet_into_two(parent, index - 2);
            rebalance_internal_after_erase(parent);
            _head = leftmost_leaf();
            return;
        }

        if (left != nullptr)
        {
            left->_entries.insert(left->_entries.end(), leaf->_entries.begin(), leaf->_entries.end());
            left->_next = leaf->_next;
            if (leaf->_next != nullptr)
            {
                leaf->_next->_prev = left;
            }
            parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
            free_node(leaf);
            rebuild_internal_keys(parent);
            rebalance_internal_after_erase(parent);
            _head = leftmost_leaf();
            return;
        }

        right->_entries.insert(right->_entries.begin(), leaf->_entries.begin(), leaf->_entries.end());
        right->_prev = leaf->_prev;
        if (leaf->_prev != nullptr)
        {
            leaf->_prev->_next = right;
        }
        else
        {
            _head = right;
        }
        parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
        free_node(leaf);
        rebuild_internal_keys(parent);
        rebalance_internal_after_erase(parent);
        _head = leftmost_leaf();
    }

    void insert_new_entry(tree_data_type* entry)
    {
        if (_root == nullptr)
        {
            _root = allocate_node(true);
            _root->_entries.push_back(entry);
            _head = _root;
            ++_size;
            return;
        }

        bsptree_node* leaf = find_leaf(entry->first);
        size_t index = lower_bound_index_in_leaf(leaf, entry->first);
        leaf->_entries.insert(leaf->_entries.begin() + static_cast<ptrdiff_t>(index), entry);
        ++_size;
        split_leaf_if_needed(leaf);
        _head = leftmost_leaf();
    }

public:
    class bsptree_iterator;
    class bsptree_const_iterator;

    class bsptree_iterator final
    {
        bsptree_node* _node;
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

        explicit bsptree_iterator(bsptree_node* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*_node->_entries[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*_node->_entries[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_entries.size())
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
            return _node == nullptr ? 0 : _node->_entries.size();
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    class bsptree_const_iterator final
    {
        const bsptree_node* _node;
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

        explicit bsptree_const_iterator(const bsptree_node* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

        bsptree_const_iterator(const bsptree_iterator& it) noexcept
            : _node(it._node), _index(it._index)
        {
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*_node->_entries[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*_node->_entries[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_entries.size())
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
            return _node == nullptr ? 0 : _node->_entries.size();
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    explicit BSP_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _head(nullptr), _size(0)
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
        : BSP_tree(static_cast<const compare&>(other), other._allocator.select_on_container_copy_construction())
    {
        for (auto it = other.cbegin(); it != other.cend(); ++it)
        {
            emplace(it->first, it->second);
        }
    }

    BSP_tree(BSP_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(other._allocator),
          _root(other._root),
          _head(other._head),
          _size(other._size)
    {
        other._root = nullptr;
        other._head = nullptr;
        other._size = 0;
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

        clear();
        static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
        _allocator = other._allocator;
        _root = other._root;
        _head = other._head;
        _size = other._size;
        other._root = nullptr;
        other._head = nullptr;
        other._size = 0;
        return *this;
    }

    ~BSP_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        auto it = find(key);
        if (it == end())
        {
            throw std::out_of_range("key not found");
        }
        return it->second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto it = find(key);
        if (it == cend())
        {
            throw std::out_of_range("key not found");
        }
        return it->second;
    }

    tvalue& operator[](const tkey& key)
    {
        return emplace(key, tvalue()).first->second;
    }

    tvalue& operator[](tkey&& key)
    {
        return emplace(std::move(key), tvalue()).first->second;
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
        return _size;
    }

    bool empty() const noexcept
    {
        return _size == 0;
    }

    bsptree_iterator find(const tkey& key)
    {
        bsptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size() && key_equal(leaf->_entries[index]->first, key))
        {
            return bsptree_iterator(leaf, index);
        }

        return end();
    }

    bsptree_const_iterator find(const tkey& key) const
    {
        bsptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return cend();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size() && key_equal(leaf->_entries[index]->first, key))
        {
            return bsptree_const_iterator(leaf, index);
        }

        return cend();
    }

    bsptree_iterator lower_bound(const tkey& key)
    {
        bsptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size())
        {
            return bsptree_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? end() : bsptree_iterator(leaf->_next, 0);
    }

    bsptree_const_iterator lower_bound(const tkey& key) const
    {
        bsptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return cend();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size())
        {
            return bsptree_const_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? cend() : bsptree_const_iterator(leaf->_next, 0);
    }

    bsptree_iterator upper_bound(const tkey& key)
    {
        auto it = lower_bound(key);
        if (it != end() && key_equal(it->first, key))
        {
            ++it;
        }
        return it;
    }

    bsptree_const_iterator upper_bound(const tkey& key) const
    {
        auto it = lower_bound(key);
        if (it != cend() && key_equal(it->first, key))
        {
            ++it;
        }
        return it;
    }

    bool contains(const tkey& key) const
    {
        return find(key) != cend();
    }

    void clear() noexcept
    {
        free_subtree(_root);
        _root = nullptr;
        _head = nullptr;
        _size = 0;
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
        if (auto it = find(data.first); it != end())
        {
            return {it, false};
        }

        tree_data_type* entry = allocate_entry(std::move(data));
        try
        {
            insert_new_entry(entry);
        }
        catch (...)
        {
            free_entry(entry);
            throw;
        }

        return {find(entry->first), true};
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
        if (auto it = find(data.first); it != end())
        {
            it->second = std::move(data.second);
            return it;
        }

        return emplace(std::move(data)).first;
    }

    bsptree_iterator erase(bsptree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }

        tkey key = pos->first;
        bool has_next = false;
        tkey next_key{};
        auto next = pos;
        ++next;
        if (next != end())
        {
            has_next = true;
            next_key = next->first;
        }

        erase(key);
        return has_next ? find(next_key) : end();
    }

    bsptree_iterator erase(bsptree_const_iterator pos)
    {
        if (pos == cend())
        {
            return end();
        }

        tkey key = pos->first;
        bool has_next = false;
        tkey next_key{};
        auto next = pos;
        ++next;
        if (next != cend())
        {
            has_next = true;
            next_key = next->first;
        }

        erase(key);
        return has_next ? find(next_key) : end();
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
        bsptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index >= leaf->_entries.size() || !key_equal(leaf->_entries[index]->first, key))
        {
            return end();
        }

        bool has_next = false;
        tkey next_key{};
        if (index + 1 < leaf->_entries.size())
        {
            has_next = true;
            next_key = leaf->_entries[index + 1]->first;
        }
        else if (leaf->_next != nullptr)
        {
            has_next = true;
            next_key = leaf->_next->_entries.front()->first;
        }

        tree_data_type* removed = leaf->_entries[index];
        leaf->_entries.erase(leaf->_entries.begin() + static_cast<ptrdiff_t>(index));
        free_entry(removed);
        --_size;

        rebalance_leaf_after_erase(leaf);
        return has_next ? find(next_key) : end();
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
