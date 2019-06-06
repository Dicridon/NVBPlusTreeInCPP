#ifndef __FROST_LIST__
#define __FROST_LIST__
#include "common.hpp"

struct list_node_t;
struct list_t;

using PListNodePtr = pmem::obj::persistent_ptr<list_node_t>;
using PListPtr = pmem::obj::persistent_ptr<list_t>;

struct list_node_t {
    PListNodePtr prev;
    PListNodePtr next;
};

struct list_t {
    PListNodePtr head;
};



PListNodePtr
new_list_node(pmem::obj::pool_base& pop);

PListPtr
new_list(pmem::obj::pool_base &pop);

void
list_add_to_head(PListPtr list, PListNodePtr node);

void
list_add(PListNodePtr ex, PListNodePtr node);

void
list_remove(PListNodePtr node);

void
list_destroy(PListPtr t);

void
free_list_node(PListNodePtr l);
#endif
