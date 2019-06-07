/*
  !!!!!!!!!!!!!!
  !!!! NOTE: I ALWAYS ASSUME THAT MALLOC/REALLOC/CALLOC WOULD NOT FAIL!!!!!!
  !!!!       THIS IS JUST FOR TEST
  !!!!!!!!!!!!!!
*/
#include <cstdio>
#include <queue>
#include "bplustree.hpp"
#include "common.hpp"

using pmem::obj::transaction;

BPlusTree::BPlusTree(pmem::obj::pool_base &pop)
{
    auto temp = bpt_new_leaf(pop);
    level = 1;
    // list = new_list(pop);
    head->next = temp;
    head->prev = temp;
    temp->prev = head;
    temp->next = head;
    root = temp;
    free_key = nullptr;            
}

void
BPlusTree::initialize(pmem::obj::pool_base &pop)
{
    auto temp = bpt_new_leaf(pop);

    transaction::run(pop, [&] {
            head = pmem::obj::make_persistent<bpt_leaf_t>();
        });
    level = 1;
    // list = new_list(pop);
    head->next = temp;
    head->prev = temp;
    temp->prev = head;
    temp->next = head;
    root = temp;
    free_key = nullptr;            
}


PBPTLeafPtr
BPlusTree::bpt_new_leaf(pmem::obj::pool_base &pop)
{
    PBPTLeafPtr node = nullptr;
    transaction::run(pop, [&] {
            node = pmem::obj::make_persistent<bpt_leaf_t>();
            node->parent = nullptr;
            node->num_of_keys = 0;
            node->type = LEAF;
            // node->link = new_list_node(pop);
            node->prev = node;
            node->next = node;
            for (int i = 0; i < degree; i++) {
                node->keys[i] = nullptr;
                node->data[i] = nullptr;
            }
            
        });

    return node;
}

PBPTNonLeafPtr
BPlusTree::bpt_new_non_leaf(pmem::obj::pool_base &pop)
{
    PBPTNonLeafPtr node = nullptr;
    transaction::run(pop, [&] {
            node = pmem::obj::make_persistent<bpt_non_leaf_t>();
            node->parent = nullptr;
            node->num_of_keys = 0;
            node->num_of_children = 0;
            node->type = NON_LEAF;
            for (int i = 0; i < degree; i++) {
                node->keys[i] = nullptr;
                node->children[i] = nullptr;        
            }
            node->children[degree] = nullptr;            
        });
    
    return node;
}

// outter function has determined that bpt_complex_insert should be called
// we have to split the leaf and insert new node into parent node
// adjustment to parent is necessary
int
BPlusTree::bpt_simple_insert(pmem::obj::pool_base &pop,
                             PBPTLeafPtr leaf, const std::string &key,
                             const std::string &value)
{
    unsigned long long i;
    for (i = 0; i < leaf->num_of_keys; i++) {
        // if the tree is empty, we may encounter a nullptr key
        if (leaf->keys[i] == nullptr || *leaf->keys[i] >= key)
            break;
    }

    for (unsigned long long j = leaf->num_of_keys; j > i; j--) {
        leaf->keys[j] = leaf->keys[j - 1];
        leaf->data[j] = leaf->data[j - 1];
    }
    transaction::run(pop, [&] {
            leaf->keys[i] = pmem::obj::make_persistent<std::string>(key);
            leaf->data[i] = pmem::obj::make_persistent<std::string>(value);
        });
    leaf->num_of_keys = leaf->num_of_keys + 1;
    return 1;
}

void
BPlusTree::bpt_insert_child(PBPTNonLeafPtr old, PBPTNodePtr neo)
{
    auto parent = old->parent;
    // insert
    unsigned long long i = 0;
    unsigned long long j = 0;
    // new inherits data from old, so we just need to insert new after old
    for (i = 0; i < parent->num_of_children; i++) {
        if (parent->children[i] == old)
            break;
    }

    for (j = parent->num_of_keys; j > i; j--) {
        parent->keys[j] = parent->keys[j-1];
    }
    
    parent->keys[i] = neo->keys[0];
    parent->num_of_keys = parent->num_of_keys + 1;

    i++; // insert after old
    for (j = parent->num_of_children; j > i; j--) {
        parent->children[j] = parent->children[j-1];
    }
    parent->children[i] = neo;
    parent->num_of_children = parent->num_of_children + 1;
    neo->parent = parent;
 }

int
BPlusTree::bpt_insert_adjust(pmem::obj::pool_base &pop,
                             PBPTNodePtr old, PBPTNodePtr neo)
{
    auto parent = old->parent;
    if (level == 1) {
        auto new_parent = bpt_new_non_leaf(pop);
        new_parent->num_of_children = 2;
        new_parent->num_of_keys = 1;
        new_parent->keys[0] = neo->keys[0];
        new_parent->children[0] = old;
        new_parent->children[1] = neo;
        root = new_parent;
        old->parent = new_parent;
        neo->parent = new_parent;

        level = level + 1;
        return 1;        
    }
    
    if (!parent) {
        // this is internal node split
        // we may not just create a new root and link old and new to it
        // we must pick out the smallest child in new and use this child
        // to construct a new root, or we may not ensure the relationsip
        // that num_of_childen = num_of_keys + 1
        auto new_parent = bpt_new_non_leaf(pop);
        new_parent->num_of_children = 2;
        new_parent->num_of_keys = 1;
        new_parent->keys[0] = neo->keys[0];
        new_parent->children[0] = old;
        new_parent->children[1] = neo;
        root = new_parent;
        level = level + 1;

        // delete the key copied to root in new
        for (unsigned long long i = 1; i < neo->num_of_keys; i++) {
            neo->keys[i-1] = neo->keys[i];
        }
        neo->keys[neo->num_of_keys - 1] = nullptr;
        neo->num_of_keys = neo->num_of_keys - 1;

        old->parent = new_parent;
        neo->parent = new_parent;
        // num_of_children will not be modified
        // we modify this field in else if
        
    } else if (parent->num_of_keys == degree - 1) {
        // insert
        // new will be added to parent of old
        bpt_insert_child(old, neo); 
        if (neo->type == NON_LEAF) {
            // delete the key copied to parent in new
            for (unsigned long long i = 1; i < neo->num_of_keys; i++) {
                neo->keys[i-1] = neo->keys[i];
            }
            neo->keys[neo->num_of_keys - 1] = nullptr;
            neo->num_of_keys = neo->num_of_keys - 1;
        }
        
        // split
        // [split] is NOT left to new parent
        auto new_parent = bpt_new_non_leaf(pop);
        unsigned long long split = parent->num_of_keys / 2;
        unsigned long long i = 0;
        for (i = 0; i + split < parent->num_of_keys; i++) {
            new_parent->keys[i] = parent->keys[i + split];
            new_parent->children[i] = parent->children[i + split + 1];
            new_parent->children[i ]->parent = new_parent;
            parent->keys[i + split] = nullptr;
            parent->children[i + split + 1] = nullptr;
        }

        new_parent->num_of_keys = parent->num_of_keys - split;
        new_parent->num_of_children = new_parent->num_of_keys;
        parent->num_of_keys =
            parent->num_of_keys - (parent->num_of_keys - split);
        parent->num_of_children = parent->num_of_keys + 1;

        // recursive handling
        bpt_insert_adjust(pop, parent, new_parent);
    } else {
        bpt_insert_child(old, neo);
        if (neo->type == NON_LEAF) {
            // delete the key copied to parent in new
            for (unsigned long long i = 1; i < neo->num_of_keys; i++) {
                neo->keys[i-1] = neo->keys[i];
            }
            neo->keys[neo->num_of_keys - 1] = nullptr;
            neo->num_of_keys = neo->num_of_keys - 1;
        }
    }
    return 1;
}

// outter function has determined to call this function
// so leaf->num_of_keys = degree - 1
int
BPlusTree::bpt_complex_insert(pmem::obj::pool_base &pop,
                              PBPTLeafPtr leaf,
                              const std::string &key,
                              const std::string &value)
{
    auto new_leaf = bpt_new_leaf(pop);
    // since we have make extra space
    // we may just insert the key and vlaue into old leaf and then split it
    // what good about this is that we do not have to consider where
    // new data should be inserted.
    unsigned long long i = 0;
    for (i = 0; i < leaf->num_of_keys; i++) {
        if(*leaf->keys[i] >= key)
            break;
    }

    for (unsigned long long j = leaf->num_of_keys; j > i; j--) {
        leaf->keys[j] = leaf->keys[j - 1];
        leaf->data[j] = leaf->data[j - 1];
    }

    transaction::run(pop, [&] {
            leaf->keys[i] = pmem::obj::make_persistent<std::string>(key);
            leaf->data[i] = pmem::obj::make_persistent<std::string>(value);
        });
    leaf->num_of_keys = leaf->num_of_keys + 1;
    
    // now split this leaf
    // copy the right half to new leaf (including [split])
    unsigned long long split = leaf->num_of_keys / 2;

    for (i = 0; i + split < leaf->num_of_keys; i++) {
        new_leaf->keys[i] = leaf->keys[i + split];
        new_leaf->data[i] = leaf->data[i + split];
        leaf->keys[i + split] = nullptr;
        leaf->data[i + split] = nullptr;
    }

    // we ensure that new leaf is always on the right of leaf
    new_leaf->num_of_keys = i;
    leaf->num_of_keys = leaf->num_of_keys - (leaf->num_of_keys - split);
    
    // list_add(leaf->link, new_leaf->link);
    new_leaf->next = leaf->next;
    new_leaf->prev = leaf;
    leaf->next->prev = new_leaf;
    leaf->next = new_leaf;
    
    bpt_insert_adjust(pop, leaf, new_leaf);

    return 1;
}

int
BPlusTree::bpt_get(const std::string &key, std::string &buffer) const noexcept
{
    if (root == nullptr)
        return -1;
    
    auto walk = root;
    unsigned long long i = 0;
    while(walk->type != LEAF) {
        for (i = 0; i < walk->num_of_keys; i++) {
            if (*walk->keys[i] > key)
                break;
        }
        walk = static_cast<PBPTNonLeafPtr>(walk)->children[i];
    }

    for (i = 0; i < walk->num_of_keys; i++) {
        if (*walk->keys[i] == key) {
            buffer = *static_cast<PBPTLeafPtr>(walk)->data[i];
            return static_cast<PBPTLeafPtr>(walk)->data[i]->size();
        }
    }
    return -1;
}

int
BPlusTree::bpt_range(const std::string &start,
                     const std::string &end,
                     std::string buffer[]) const noexcept
{
    if (start > end)
        return -1;
    
    auto leaf = find_leaf(start);
    unsigned long long i = 0, j = 0;

    while(leaf != head) {
        for (i = 0; i < leaf->num_of_keys; i++) {
            if (*leaf->keys[i] > end)
                return 1;

            if (*leaf->keys[i] >= start)
                buffer[j++] = *leaf->keys[i];
        }
        leaf = leaf->next;
    }
    return -1;
}

int
BPlusTree::bpt_range_test(const std::string &start, long n) const noexcept
{
    auto leaf = find_leaf(start);
    unsigned long long i = 0;

    while(n > 0 && leaf != head) {
        for (i = 0; i < leaf->num_of_keys; i++) {
            if (*leaf->keys[i] > "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz")
                return 1;

            // if (*leaf->keys[i] >= start)
            //     std::cout << *leaf->keys[i] << ", ";
            
            if (*leaf->keys[i] >= start)
                n--;
        }
        leaf = leaf->next;
    }
    return -1;
}

int
BPlusTree::bpt_destroy(pmem::obj::pool_base &pop)
{
    if (!root) {
        printf("empty tree\n");
        return 1;
    }

    auto queue = std::queue<PBPTNodePtr>();
    queue.push(root);

    while(!queue.empty()) {
        auto walk = queue.front();
        queue.pop();
        for (unsigned long long i = 0;
             walk->type == NON_LEAF && i < static_cast<PBPTNonLeafPtr>(walk)->num_of_children; i++) {
            queue.push(static_cast<PBPTNonLeafPtr>(walk)->children[i]);
        }

        if (walk->type == LEAF) {
            transaction::run(pop, [&] {
                    for (unsigned long long i = 0; i < walk->num_of_keys; i++) { 
                        pmem::obj::delete_persistent<std::string>(walk->keys[i]);
                        pmem::obj::delete_persistent<std::string>(static_cast<PBPTLeafPtr>(walk)->data[i]);
                    }
                    pmem::obj::delete_persistent<bpt_node_t>(walk);
                });
        }
    }
    return 1;
}

// find leaf will find a leaf suitable for this key
PBPTLeafPtr
BPlusTree::find_leaf(const std::string &key) const noexcept
{
    auto walk = root;
    unsigned long long i = 0;
    while(walk->type != LEAF) {
        for (i = 0; i < walk->num_of_keys; i++) {
            if (*walk->keys[i] > key)
                break;
        }
        walk = static_cast<PBPTNonLeafPtr>(walk)->children[i];
    }
    return walk;
}

PBPTNonLeafPtr
BPlusTree::find_non_leaf(const std::string &key) const noexcept
{
    auto walk = root;
    unsigned long long i = 0;
    while(walk->type != LEAF) {
        for (i = 0; i < walk->num_of_keys; i++) {
            if (*walk->keys[i] == key)
                return walk;
            if (*walk->keys[i] > key)
                break;
        }
        walk = static_cast<PBPTNonLeafPtr>(walk)->children[i];
    }
    return nullptr;
}

int
BPlusTree::bpt_insert(pmem::obj::pool_base &pop,
                      const std::string &key, const std::string &value) noexcept
{
    auto old = find_leaf(key);

    for (unsigned long long i = 0; i < old->num_of_keys; i++) {
        if (*old->keys[i] == key) {
            old = nullptr;
            break;
        }
    }

    if (!old)
        return 1;
    
    if (old->num_of_keys == degree - 1) {
        // if you want to figure out what is actually going on
        // chekc out website below
        // https://www.cs.usfca.edu/~galles/visualization/BPlustree.html
        // this page indeed benefited me so much
        bpt_complex_insert(pop, old, key, value);
    } else {
        bpt_simple_insert(pop, old, key, value);
    }
    return 1;
}

// ensure leaf has no data at all before calling this function
void
BPlusTree::bpt_free_leaf(pmem::obj::pool_base &pop, PBPTLeafPtr leaf)
{
    transaction::run(pop, [&] {
            pmem::obj::delete_persistent<bpt_leaf_t>(leaf);            
        });

}

void
BPlusTree::bpt_free_non_leaf(pmem::obj::pool_base &pop, PBPTNonLeafPtr nleaf)
{
    transaction::run(pop, [&] {
            pmem::obj::delete_persistent<bpt_non_leaf_t>(nleaf);            
        });
}

void
BPlusTree::bpt_print() const noexcept
{
    if (!root) {
        printf("empty tree\n");
        return;
    }
    
    auto queue = std::queue<PBPTNodePtr>();
    
    queue.push(root);

    while(!queue.empty()) {
        auto walk = queue.front();
        queue.pop();
        for (unsigned long long i = 0;
             walk->type == NON_LEAF && i < static_cast<PBPTNonLeafPtr>(walk)->num_of_children; i++) {
            queue.push(static_cast<PBPTNonLeafPtr>(walk)->children[i]);
        }
        printf("%p child of %p: \n", walk.get(), walk->parent.get());
        
        if (walk->type == NON_LEAF)
            printf("non leaf: %llu children, %llu keys\n",
                   static_cast<PBPTNonLeafPtr>(walk)->num_of_children.get_ro(),
                   walk->num_of_keys.get_ro());
        else
            printf("prev: %p, next: %p\n",
                   static_cast<PBPTLeafPtr>(walk)->prev.get(),
                   static_cast<PBPTLeafPtr>(walk)->next.get());
        printf("keys: \n");
        for (unsigned long long i = 0; i < walk->num_of_keys; i++) {
            printf("%s, ", walk->keys[i]->c_str());
        }
        printf("\n\n");
    }
}

void
BPlusTree::bpt_print_leaves() const noexcept
{
    auto p = head->next;
    while(p != head) {
        for (unsigned long long i = 0; i < p->num_of_keys; i++) {
            printf("%s, ", p->keys[i]->c_str());
        }
        p = p->next;
    }
    puts("");
}

// deletion

/*
  - How to delete
  1. if leaf->num_of_keys > degree / 2, just delete the key
  2. if leaf->num_of_keys = degree / 2, try to borrow a key from sibling
  3. if siblings can not offer a key, merge this leaf and the sibling which has
     only degree keys, the recursively remove
*/

inline bool
BPlusTree::bpt_is_root(const PBPTNodePtr t) const
{
    return t->parent == nullptr;
}

int
BPlusTree::bpt_remove_key_and_data(pmem::obj::pool_base &pop,
                                   PBPTLeafPtr node, const std::string &key)
{
    unsigned long long i = 0;
    for (i = 0; i < node->num_of_keys; i++) {
        if (*node->keys[i] == key)
            break;
    }

    for(; i < node->num_of_keys; i++) {
        node->keys[i] = node->keys[i + 1];
        node->data[i] = node->data[i + 1];
    }

    transaction::run(pop, [&] {
            node->num_of_keys = node->num_of_keys - 1;
            pmem::obj::delete_persistent<std::string>(node->keys[node->num_of_keys]);
            pmem::obj::delete_persistent<std::string>(node->data[node->num_of_keys]);
            node->keys[node->num_of_keys] = nullptr;
            node->data[node->num_of_keys] = nullptr;            
        });

    return 1;
}

PBPTNodePtr
BPlusTree::bpt_check_redistribute(const PBPTNodePtr t) const noexcept
{
    if (!t->parent)
        return nullptr;

    auto parent = t->parent;
    unsigned long long i = 0;
    for (i = 0; i < parent->num_of_children; i++) {
        if (parent->children[i] == t)
            break;
    }

    auto left = parent->children[(i == 0) ? 0 : i - 1];
    auto right =
        parent->children[(i == parent->num_of_children - 1) ? i : i + 1];

    if (left->num_of_keys > degree / 2)
        return left;
    else if (right->num_of_keys > degree / 2) {
        return right;
    }
    return nullptr;
}


// redistribute leaf nodes, not internal nodes

int
BPlusTree::redistribute_leaf(pmem::obj::pool_base &pop,
                             PBPTLeafPtr leaf, const std::string &key)
{
    auto parent = leaf->parent;
    auto non_leaf = find_non_leaf(key);
    unsigned long long i = 0;
    unsigned long long idx_replace_key = 0;
    unsigned long long idx_child = 0;
    std::string replace_key;

    if (non_leaf) {
        for (idx_replace_key = 0;
             idx_replace_key < non_leaf->num_of_keys; idx_replace_key++) {
            if (*non_leaf->keys[idx_replace_key] == key)
                break;                        
        }

    }
    
    for (i = 0; i < parent->num_of_children; i++)
        if (parent->children[i] == leaf){
            idx_child = i;
            break;            
        }


    unsigned long long idx_left = (i == 0) ? 0 : i - 1;
    unsigned long long idx_right =
        (i == parent->num_of_children - 1) ? i : i + 1;
    auto left = static_cast<PBPTLeafPtr>(parent->children[idx_left]);
    auto right = static_cast<PBPTLeafPtr>(parent->children[idx_right]);
 
    if (left->num_of_keys > degree / 2) {
        unsigned long long tail = left->num_of_keys - 1;
        parent->keys[idx_left] = left->keys[tail];
        
        for (i = 0; i < leaf->num_of_keys; i++) {
            if (*leaf->keys[i] == key) {
                transaction::run(pop, [&] {
                        pmem::obj::delete_persistent<std::string>(leaf->keys[i]);
                        pmem::obj::delete_persistent<std::string>(leaf->data[i]);
                    });

                for (; i > 0; i--) {
                    leaf->keys[i] = leaf->keys[i-1];
                    leaf->data[i] = leaf->data[i-1];
                }
                leaf->keys[0] = left->keys[tail];
                leaf->data[0] = left->data[tail];
                left->keys[tail] = nullptr;
                left->data[tail] = nullptr;
                left->num_of_keys = left->num_of_keys - 1;
                replace_key = *leaf->keys[0];
                break;
            }

        }
    }
    else if (right->num_of_keys > degree / 2) {
        unsigned long long tail = leaf->num_of_keys - 1;
        for (i = 0; i < leaf->num_of_keys; i++) {
            if (*leaf->keys[i] == key) {
                transaction::run(pop, [&] {
                        pmem::obj::delete_persistent<std::string>(leaf->keys[i]);
                        pmem::obj::delete_persistent<std::string>(leaf->data[i]);
                    });

                for (; i < leaf->num_of_keys - 1; i++) {
                    leaf->keys[i] = leaf->keys[i+1];
                    leaf->data[i] = leaf->data[i+1];
                }
                leaf->keys[tail] = right->keys[0];
                leaf->data[tail] = right->data[0];

                for (unsigned long long j = 0; j < right->num_of_keys; j++) {
                    right->keys[j] = right->keys[j+1];
                    right->data[j] = right->data[j+1];
                }
                parent->keys[idx_child] = right->keys[0];
                right->num_of_keys = right->num_of_keys - 1;
                replace_key = *leaf->keys[0];
                break;
            }

        }
    }

    // if the key deleted reside in grandparent's keys, replace it
    if (non_leaf)
        *non_leaf->keys[idx_replace_key] = replace_key;

    return 1;
}


int
BPlusTree::bpt_simple_delete(pmem::obj::pool_base &pop,
                             PBPTLeafPtr leaf, const std::string &key)
{
    if (!bpt_is_root(leaf)) {
        // this branch will be executed only when deletin takes place on a
        // right subtree of a key
        auto non_leaf = find_non_leaf(key);
        unsigned long long i = 0;
        if (non_leaf) {
            for (i = 0; i < non_leaf->num_of_keys; i++)
                if (*non_leaf->keys[i] == key) {
                    break;
                }
        }
        bpt_remove_key_and_data(pop, leaf, key);
        // replace the key
        if (non_leaf)
            non_leaf->keys[i] = leaf->keys[0];
        return 1;
    } else
        return bpt_remove_key_and_data(pop, leaf, key);
}



// only used to merge leaves

PBPTNodePtr
BPlusTree::merge_leaves(pmem::obj::pool_base &pop,
                        PBPTLeafPtr leaf, const std::string &key)
{
    auto parent = leaf->parent;
    unsigned long long i;
    unsigned long long idx_left;
    unsigned long long idx_right;
    // delete the key
    for (i = 0; i < leaf->num_of_keys; i++) {
        if (*leaf->keys[i] == key)
            break;
    }

    free_key = leaf->keys[i];
    transaction::run(pop, [&] {
            pmem::obj::delete_persistent<std::string>(leaf->data[i]);            
        });

    
    for (unsigned long long j = i; j <= leaf->num_of_keys - 1; j++) {
        leaf->keys[j] = leaf->keys[j+1];
        leaf->data[j] = leaf->data[j+1];
    }
    leaf->num_of_keys = leaf->num_of_keys - 1;

    // find a proper sibling
    for (i = 0; i < parent->num_of_children; i++) {
        if (parent->children[i] == leaf)
            break;
    }

    idx_left = (i == 0) ? 0 : i - 1;
    idx_right = (i == parent->num_of_children - 1) ? i : i + 1;
    auto left = static_cast<PBPTLeafPtr>(parent->children[idx_left]);
    auto right = static_cast<PBPTLeafPtr>(parent->children[idx_right]);
    unsigned long long delete_position = 0;
    PBPTNodePtr rev = nullptr;
    
    // merge left
    if (i != 0 && left->num_of_keys <= degree / 2) {
        delete_position = i;
        right = nullptr;
    } else {
        // merge to right
        delete_position = idx_right;
        left = nullptr;        
    }

    // merge
    if (left) {
        // merge leaf into its left sibling
        for (i = 0; i < leaf->num_of_keys; i++) {
            left->keys[i + left->num_of_keys] = leaf->keys[i];
            leaf->keys[i] = nullptr;
            left->data[i + left->num_of_keys] = leaf->data[i];
            leaf->data[i] = nullptr;
        }
        
        leaf->prev->next = leaf->next;
        leaf->next->prev = leaf->prev;
        left->num_of_keys = left->num_of_keys + leaf->num_of_keys;
        bpt_free_leaf(pop, leaf);
        rev = left;
    } else {
        // merge leaf into its right sibling
        // we merge right into leaf and modify the point in parent pointing
        // to right to point to leaf, so we may reduce some overhead
        for (i = 0; i < right->num_of_keys; i++) {
            leaf->keys[i + leaf->num_of_keys] = right->keys[i];
            right->keys[i] = nullptr;
            leaf->data[i + leaf->num_of_keys] = right->data[i];
            right->data[i] = nullptr;
        
        }

        
        right->prev->next = right->next;
        right->next->prev =right->prev;
        parent->children[idx_right] = leaf;
        leaf->num_of_keys = leaf->num_of_keys + right->num_of_keys;
        bpt_free_leaf(pop, right);
        rev = leaf;
    }
    
    // align children
    for (unsigned long long j = delete_position; j <= parent->num_of_children - 1; j++) {
        parent->children[j] = parent->children[j+1];
    }
    parent->num_of_children = parent->num_of_children - 1;
    return rev;
}



int
BPlusTree::bpt_insert_key(pmem::obj::pool_base &pop,
                          PBPTNodePtr t, const std::string &key)
{
    unsigned long long i = 0;
    for (i = 0; i < t->num_of_keys; i++) {
        if (*t->keys[i] == key)
            return 1;

        if (*t->keys[i] > key)
            break;
    }

    for (unsigned long long j = t->num_of_keys; j > i; j--) {
        t->keys[j] = t->keys[j-1];
    }
    transaction::run(pop, [&] {
            t->keys[i] = pmem::obj::make_persistent<std::string>(key);
        });

    t->num_of_keys = t->num_of_keys + 1;
    return 1;
}


int
BPlusTree::redistribute_internal(const std::string &split_key,
                                 PBPTNonLeafPtr node,
                                 PBPTNonLeafPtr left, PBPTNonLeafPtr right)
{

    auto parent = node->parent;
    unsigned long long i = 0;
    unsigned long long j = 0;
    unsigned long long split = 0;
    for (split = 0; split < parent->num_of_keys; split++) {
        if (*parent->keys[split] == split_key)
            break;
    }

    for (i = 0; i < parent->num_of_children; i++) {
        if (parent->children[i] == node)
            break;
    }
    
    if (!left && right->num_of_keys > degree / 2) {
        *node->keys[node->num_of_keys] = split_key;
        node->num_of_keys = node->num_of_keys + 1;
        node->children[node->num_of_children] = right->children[0];
        node->num_of_children = node->num_of_children + 1;
        right->children[0]->parent = node;
        parent->keys[split] = right->keys[0];
        for (j = 0; j <= right->num_of_keys - 1; j++) {
            right->keys[j] = right->keys[j+1];
            right->children[j] = right->children[j+1];
        }
        right->children[j] = nullptr;
        right->num_of_children = right->num_of_children - 1;
        right->num_of_keys = right->num_of_keys - 1;
    } else if (!right && left->num_of_keys > degree / 2) {
        // make some space
        node->children[node->num_of_children] =
            node->children[node->num_of_children-1];
        for (j = node->num_of_keys; j > 0; j--) {
            node->keys[j] = node->keys[j-1];
            node->children[j] = node->children[j-1];
        }
        *node->keys[0] = split_key;
        node->children[0] = left->children[left->num_of_children-1];
        left->children[left->num_of_children-1]->parent = node;
        parent->keys[split] = left->keys[left->num_of_keys-1];
        left->keys[left->num_of_keys-1] = nullptr;
        left->children[left->num_of_children-1] = nullptr;
        node->num_of_keys = node->num_of_keys + 1;
        node->num_of_children = node->num_of_children + 1;
        left->num_of_children = left->num_of_children - 1;
        left->num_of_keys = left->num_of_keys - 1;
    } else if (left->num_of_keys > degree/2 && right->num_of_keys > degree/2) {
        // borrow from right
        node->children[node->num_of_children] = right->children[0];
        node->num_of_children = node->num_of_children + 1;
        right->children[0]->parent = node;
        *node->keys[node->num_of_keys] = split_key;
        node->num_of_keys = node->num_of_keys + 1;
        parent->keys[split] = right->keys[0];
        for (j = 0; j <= right->num_of_keys - 1; j++) {
            right->keys[j] = right->keys[j+1];
            right->children[j] = right->children[j+1];
        }
        right->children[j+1] = nullptr;
        right->num_of_children = right->num_of_children - 1;
        right->num_of_keys = right->num_of_keys - 1;
    } else {
        return -1;
    }
    return 1;
}

int
BPlusTree::merge_internal(pmem::obj::pool_base &pop,
                          PBPTNonLeafPtr parent, const std::string &split_key)
{
    // merge parent into a proper sibling, incorporating a split key from parent
    // of parent which seperate this parent and the sibling
    unsigned long long i = 0;
    auto grandparent = parent->parent;


    // merge this parent and its sibling
    unsigned long long idx_left = 0;
    unsigned long long idx_right = 0;
    for (i = 0; i < grandparent->num_of_children; i++) {
        if (grandparent->children[i] == parent)
            break;
    }

    idx_left = (i == 0) ? 0 : i - 1;
    idx_right = (i == grandparent->num_of_children - 1) ? i : i + 1;
    auto left = static_cast<PBPTNonLeafPtr>(grandparent->children[idx_left]);
    auto right = static_cast<PBPTNonLeafPtr>(grandparent->children[idx_right]);
    PBPTNodePtr merged = nullptr;
    bool left_available = false;
    bool right_available = false;
    unsigned long long delete_position = i;
    if (i == 0 || left->num_of_keys > degree / 2) {
        delete_position = idx_right;
        if (left->num_of_keys > degree / 2)
            left_available = true;
        left = nullptr;
    }

    if (i == grandparent->num_of_children-1 || right->num_of_keys > degree/2) {
        delete_position = i;
        if (right->num_of_keys > degree / 2)
            right_available = true;
        right = nullptr;
    }

    // what sucks is that an internal node may have no proper sibling to merge in
    // so what should we do ?
    // we have to borrow a key from proper sibling just as what we do to leaves.
    // and notice we have to change our split key
    if (!left && !right) {  // merge is impossible
        if (left_available) {
            auto a_split_key = *grandparent->keys[i-1];
            redistribute_internal(a_split_key, parent,
                                  grandparent->children[idx_left], nullptr);
        }
        else {
            auto a_split_key = *grandparent->keys[i];
            redistribute_internal(a_split_key, parent, nullptr,
                                  grandparent->children[idx_right]);
        }
        return 1;
    }

    
    if (left) {
        for (i = 0; i < parent->num_of_keys; i++) {
            left->keys[i+left->num_of_keys] = parent->keys[i];
            parent->keys[i] = nullptr;
            left->children[i+left->num_of_children] = parent->children[i];
            parent->children[i]->parent = left;
            parent->children[i] = nullptr;
        }
        
        left->children[i+left->num_of_children] = parent->children[i];
        parent->children[i]->parent = left;
        parent->children[i] = nullptr;

        left->num_of_keys = left->num_of_keys + parent->num_of_keys;
        left->num_of_children = left->num_of_children + parent->num_of_children;
        
        bpt_free_non_leaf(pop, parent);
        bpt_insert_key(pop, left, split_key);
        merged = left;
    } else {
        for (i = 0; i < right->num_of_keys; i++) {
            parent->keys[i+parent->num_of_keys] = right->keys[i];
            right->keys[i] = nullptr;
            parent->children[i+parent->num_of_children] = right->children[i];
            right->children[i]->parent = parent;
            right->children[i] = nullptr;
        }
        
        parent->children[i+parent->num_of_children] = right->children[i];
        right->children[i]->parent = parent;
        right->children[i] = nullptr;

        parent->num_of_keys = parent->num_of_keys + right->num_of_keys;
        parent->num_of_children =
            parent->num_of_children + right->num_of_children;

        bpt_free_non_leaf(pop, right);
        grandparent->children[idx_right] = parent;
        bpt_insert_key(pop, parent, split_key);
        merged = parent;
    }

    if (bpt_is_root(grandparent) && grandparent->num_of_keys == 1) {
        bpt_free_non_leaf(pop, grandparent);
        root = merged;
        merged->parent = nullptr;
        return 1;
    }

    // align children
    for (unsigned long long j = delete_position;
         j <= grandparent->num_of_children - 1;
         j++) {
        grandparent->children[j] = grandparent->children[j+1];
    }
    grandparent->num_of_children = grandparent->num_of_children - 1;
    return 0;
}


int
BPlusTree::merge(pmem::obj::pool_base &pop,
                 PBPTNonLeafPtr parent,
                 const std::string &key, const std::string &split_key)
{
    // remove the split key
    unsigned long long i = 0;
    for (i = 0; i < parent->num_of_keys; i++) {
        if (*parent->keys[i] == split_key) {
            unsigned long long j = 0;
            for (j = i; j <= parent->num_of_keys - 1; j++) {
                parent->keys[j] = parent->keys[j+1];
            }
            break;
        }
    }
    // parent has enough keys
    parent->num_of_keys = parent->num_of_keys - 1;
    unsigned long long num_of_keys = parent->num_of_keys;

    if (num_of_keys >= degree / 2 || (bpt_is_root(parent) && num_of_keys >= 1))
        return 1;

    if (bpt_is_root(parent) && num_of_keys == 0) {
        root = parent->children[0];
        parent->children[0]->parent = nullptr;
        bpt_free_non_leaf(pop, parent);
        return 1;
    }
    // now we have to find a split key for parent

    auto grandparent = parent->parent;

    for (i = 0; i < grandparent->num_of_children; i++) {
        if (grandparent->children[i] == parent)
            break;
    }

    unsigned long long idx_left = (i == 0) ? 0 : i - 1;
    unsigned long long idx_right =
        (i == grandparent->num_of_children - 1) ? i : i + 1;
    unsigned long long split = i-1; // merge to left
    auto left = grandparent->children[idx_left];
    auto right = grandparent->children[idx_right];
    if (i == 0 || left->num_of_keys > degree / 2) {
        split = i;
    }

    if (i == grandparent->num_of_children-1 || right->num_of_keys > degree/2) {
        split = i -1;
    }

    auto a_split_key = *grandparent->keys[split];

    // 1 means everything is done, no more recursion
    if (merge_internal(pop, parent, a_split_key) == 1)
        return 1;
    merge(pop, grandparent, key, a_split_key);
    return 1;
}

// this functin first finish leaf merging
// then call merge to see if it is necessary to merge parent
/*
  procedure:
      1. find leaf and internal node which contains the key to be deleted
      2. find split key
      3. replace the key in the internal node with the split key
      4. merge leaf and its chosen sibling, 
      5. remove the split key in parent
      6. check if we need to merge parent recursively, if so , we have to 
         incorporate the split key which split parent and parent's sibling
      7. if parent is root and we have to remove the last key during merging,
         just remove the key and delete this root. Delegate root to the newly
         merged node
 */

int
BPlusTree::bpt_complex_delete(pmem::obj::pool_base &pop,
                              PBPTLeafPtr leaf, const std::string &key)
{
    // find internal node which contains the keys to be deleted
    auto non_leaf = find_non_leaf(key);
    
    // find split key
    auto parent = leaf->parent; // impossible to be nullptr
    unsigned long long i = 0;
    for (i = 0; i < parent->num_of_children; i++) {
        if (parent->children[i] == leaf)
            break;
    }
    unsigned long long split = (i == 0) ? 0 : i - 1;


    // merge
    auto merged = merge_leaves(pop, leaf, key);
    
    // replace the key
    auto replace_key = merged->keys[0];
    if (non_leaf) {
        for (unsigned long long j = 0; j < non_leaf->num_of_keys; j++)
            if (*non_leaf->keys[j] == key) {
                non_leaf->keys[j] = replace_key;
                break;
            }
    }
    std::string split_key = *parent->keys[split];
    merge(pop, parent, key, split_key);
    transaction::run(pop, [&] {
            pmem::obj::delete_persistent<std::string>(free_key);
            free_key = nullptr;
        });

    return 1;
}


int
BPlusTree::bpt_delete(pmem::obj::pool_base &pop, const std::string &key)
{
    if (!root)
        return 1;
    unsigned long long  i = 0; 
    auto leaf = find_leaf(key);
    for (i = 0; i < leaf->num_of_keys; i++) {
        if (*leaf->keys[i] ==  key)
            break;
    }

    if (i == leaf->num_of_keys)
        return 1;
    
    auto parent = leaf->parent;
    if (leaf->num_of_keys > degree / 2 || bpt_is_root(leaf)) {
        bpt_simple_delete(pop, leaf, key);

        // tree is destroyed
        if (leaf->num_of_keys == 0) {
            bpt_free_leaf(pop, leaf);
            root = nullptr;
        }

        // index key should be replaced it is resides in parent's key list
        if (parent) {
            for (i = 0; i < parent->num_of_keys; i++) {
                if (*parent->keys[i] == key) {
                    parent->keys[i] = leaf->keys[0];
                    return 1;
                }
            }
        }
    } else {
        // if one of leaf's nearest siblings has enough keys to share
        // borrow a key from this sibling
        if (bpt_check_redistribute(leaf))
            return redistribute_leaf(pop, leaf, key);
        else
            // no sibling can offer us a key
            // merge is required
            return bpt_complex_delete(pop, leaf, key);
    }
    return 1;
}
