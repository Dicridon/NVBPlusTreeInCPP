#include "common.hpp"
#include "list.hpp"

using pmem::obj::transaction;

// pmem::obj::make_persistent can't be called outside a transaction
PListNodePtr
new_list_node(pmem::obj::pool_base &pop)
{
    PListNodePtr node = nullptr;
    transaction::run(pop, [&] {
            node = pmem::obj::make_persistent<list_node_t>();
            node->next = node;
            node->prev = node;
        });
    
    return node; // nullptr is OK.
}

PListPtr
new_list(pmem::obj::pool_base &pop)
{
    PListPtr list = nullptr;
    auto node = new_list_node(pop);

    transaction::run(pop, [&] {
            list = pmem::obj::make_persistent<list_t>();
            list->head = node;
            list->head->next = list->head;
            list->head->prev = list->head;
        });
    
    return list;
}

// persistent_ptr will arrange persistence ensurance, so we do not need to use
// functions like pmemobj_persist
// transaction may be required
void
list_add_to_head(PListPtr list, PListNodePtr node)
{
    node->next = list->head->next;
    node->prev = list->head;
    list->head->next = node;

}

void
list_add(PListNodePtr ex, PListNodePtr node)
{
    node->next = ex->next;
    node->prev = ex;
    ex->next->prev = node;
    ex->next = node;
}

void
list_remove(PListNodePtr node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void
free_list_node(PListNodePtr n)
{
    pmem::obj::delete_persistent<list_node_t>(n);
}

void
list_destroy(PListPtr t)
{
    PListNodePtr p = t->head->next;
    while(p != t->head) {
        t->head->next = p->next;
        free_list_node(p);
        p = t->head->next;
    }
    free_list_node(t->head);
    pmem::obj::delete_persistent<list_t>(t);
    t = nullptr;
}
