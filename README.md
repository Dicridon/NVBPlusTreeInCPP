# A simple B+ tree implemented in CPP with libpmemobj++

## Comment
- This B+ tree is ported from my C implementation, so there are names like bpt_node_t and a bunch of helper functions as private methods in a class. There are also a lot of printfs.

- This is a practice for me to get familiar with C++ and C++ 11.

## Hints
- libpmemobj++ offers persistent smart pointers utility, thus we have to distinguish resources and references of resource carefully.

- The life cycle of object pointed by persistent pointers may exceed that of the running program, so we have to manage resource allocation and deallocation mannually.

- Memory allocation and deallocation using make_persistent must be wrapped in a transaction.
