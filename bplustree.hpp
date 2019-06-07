#ifndef __FROST_BPTREE__
#define __FROST_BPTREE__
/*
  This code is adopted from a C version, further modification may be required
 */
#include <iostream>
#include "common.hpp"
#include "list.hpp"
#include <string>


#define DEGREE (5)

enum bpt_node_type {
    NON_LEAF,
    LEAF
};


// though it is not safe to make smart pointers of type containning unions,
// persistent_ptr does not manage object life cycle
// so it's fine to used it.
struct bpt_node_t;
struct bpt_leaf_t;
struct bpt_non_leaf_t;
class BPlusTree;

using PStringPtr = pmem::obj::persistent_ptr<std::string>;
using PBPTNodePtr = pmem::obj::persistent_ptr<bpt_node_t>;
using PBPTLeafPtr = pmem::obj::persistent_ptr<bpt_leaf_t>;
using PBPTNonLeafPtr = pmem::obj::persistent_ptr<bpt_non_leaf_t>;
using PBPTPtr = pmem::obj::persistent_ptr<BPlusTree>;



// Each node may have at most DEGREE children and DEGREE - 1 keys
// keys and children are paired
struct bpt_node_t {
    pmem::obj::p<unsigned long long> num_of_keys;
    // one extra key will reudce overhead during insertion
    PStringPtr keys[DEGREE];
    PBPTNonLeafPtr parent;
    pmem::obj::p<bpt_node_type> type;

    void
    print_keys() const {
        for (unsigned long long i = 0; i < num_of_keys; i++) {
            std::cout << *keys[i] + " ";
        }
        
    }
};

struct bpt_leaf_t : bpt_node_t {
    PBPTLeafPtr prev;
    PBPTLeafPtr next;
    PStringPtr data[DEGREE];
};

struct bpt_non_leaf_t : bpt_node_t {
    pmem::obj::p<unsigned long long> num_of_children;
    // keys and children are paired during insertion
    PBPTNodePtr children[DEGREE + 1];

    void
    print_children() const {
        for (unsigned long long i = 0; i < num_of_children; i++) {
            std::cout << children[i].get() << " ";
        }
    }
};

class BPlusTree {
private:
    pmem::obj::p<int> level;
    PBPTNodePtr root;
    // linked children
    PBPTLeafPtr head;
    // key and data to be freed, so I may avoid accessing freed memory
    PStringPtr free_key;
    static const int degree = DEGREE;
    
private:
    // insertion helpers
    PBPTLeafPtr
    bpt_new_leaf(pmem::obj::pool_base &pop);

    PBPTNonLeafPtr
    bpt_new_non_leaf(pmem::obj::pool_base &pop);

    void
    bpt_insert_child(PBPTNonLeafPtr old, PBPTNodePtr neo);

    int
    bpt_complex_insert(pmem::obj::pool_base &pop,
                       PBPTLeafPtr leaf,
                       const std::string &key,
                       const std::string &value);

    int
    bpt_simple_insert(pmem::obj::pool_base &pop,
                      PBPTLeafPtr leaf,
                      const std::string &key,
                      const std::string &value);

    int
    bpt_insert_adjust(pmem::obj::pool_base &pop,
                      PBPTNodePtr old, PBPTNodePtr neo);

    PBPTLeafPtr
    find_leaf(const std::string &key) const noexcept;

    PBPTNonLeafPtr
    find_non_leaf(const std::string &key) const noexcept;

    // deletion helpers
    inline bool
    bpt_is_root(const PBPTNodePtr t) const;

    int
    bpt_insert_key(pmem::obj::pool_base &pop,
                   PBPTNodePtr t, const std::string &key);

    PBPTNodePtr
    bpt_check_redistribute(const PBPTNodePtr t) const noexcept;

    int
    redistribute_leaf(pmem::obj::pool_base &pop,
                      PBPTLeafPtr leaf, const std::string &key);
    
    int
    redistribute_internal(const std::string &split_key,
                          PBPTNonLeafPtr node,
                          PBPTNonLeafPtr left, PBPTNonLeafPtr right);
    
    PBPTNodePtr
    merge_leaves(pmem::obj::pool_base &pop,
                 PBPTLeafPtr leaf, const std::string &key);
    
    int
    merge_internal(pmem::obj::pool_base &pop,
                   PBPTNonLeafPtr parent, const std::string &split_key);

    // split key may be changed
    int
    merge(pmem::obj::pool_base &pop,
          PBPTNonLeafPtr parent, const std::string &key,
          const std::string &split_key);
    
    int
    bpt_remove_key_and_data(pmem::obj::pool_base &pop,
                            PBPTLeafPtr node, const std::string &key);
    
    int
    bpt_complex_delete(pmem::obj::pool_base &pop,
                       PBPTLeafPtr leaf, const std::string &key);
    
    int
    bpt_simple_delete(pmem::obj::pool_base &pop,
                      PBPTLeafPtr leaf, const std::string &key);
    
    void
    bpt_free_leaf(pmem::obj::pool_base &pop, PBPTLeafPtr leaf);
    
    void
    bpt_free_non_leaf(pmem::obj::pool_base &pop, PBPTNonLeafPtr nleaf);
    
public:
    BPlusTree(pmem::obj::pool_base &pop);
    BPlusTree(BPlusTree &) = delete;
    BPlusTree(BPlusTree &&) = delete;
    ~BPlusTree(); // not for inheritence;

    // persistent_ptr requires to the object to be default constructable
    // so I seperate the initialize step here.
    void initialize(pmem::obj::pool_base &pop);
    
    bool empty() const noexcept {return root == nullptr;}

    int
    bpt_insert(pmem::obj::pool_base &pop,
               const std::string &key, const std::string &value) noexcept;

    int
    bpt_delete(pmem::obj::pool_base &pop, const std::string &key);

    int
    bpt_get(const std::string &key, std::string &buffer)const noexcept;

    int
    bpt_range(const std::string &start,
              const std::string &end,
              std::string buffer[]) const noexcept;

    int
    bpt_destroy(pmem::obj::pool_base &pop);

    void
    bpt_print() const noexcept;

    void
    bpt_print_leaves() const noexcept;
    
    int
    bpt_range_test(const std::string &start, long n) const noexcept;
};
#endif

