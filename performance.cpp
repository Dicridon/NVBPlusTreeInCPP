#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "common.hpp"
#include "bplustree.hpp"

// #define POOL_SIZE (((long)2 * 1024 * 1024 * 1024))
#define K ((long long)1024)
#define M (K * K)
#define G (K * K * K)
#define POOL_SIZE (3 * G)
#define DEBUG
// #define PERF

#define DN ((long)26)
#define PRE ((long)2000)
#define K_LEN ((long)32)
#define V_LEN ((long)32)

char dict[DN] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
                 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
                 'y', 'z',
};

void query(PBPTPtr t, char **cases, long size) {
    std::string temp;
    clock_t start, end;
    printf("querying 50\%% keys...\n");
    srand(size / 233 * 45 > 4);
    long s  = rand() % size;
    start = clock();
    for (long i = 0; i < size / 2; i++) {
        t->bpt_get(cases[(s++) % size], temp);
    }
    end = clock();
    printf("time consumed in querying %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size/ 2) / (((double)(end - start)) / CLOCKS_PER_SEC));

    printf("querying 90\%% keys...\n");
    srand(size / 233 * 45 > 4);
    s  = rand() % size;
    start = clock();
    for (long i = 0; i < size * 9 / 10; i++) {
        t->bpt_get(cases[(s++) % size], temp);
    }
    end = clock();
    printf("time consumed in querying %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size / 10 * 9) / (((double)(end - start)) / CLOCKS_PER_SEC));

    printf("querying all keys...\n");
    srand(size / 233 * 45 > 4);
    start = clock();
    for (long i = 0; i < size; i++) {
        t->bpt_get(cases[i], temp);
    }
    end = clock();
    printf("time consumed in querying %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size) / (((double)(end - start)) / CLOCKS_PER_SEC));
}

void scan(PBPTPtr t, const char *key, long size) {
    int scale[] = {5, 10, 25, 50, 75, 100};
    clock_t start, end;
    printf("scanning 1\%% keys starting with %s\n", key);
    
    start = clock();
    t->bpt_range_test(key, size / 100);
    end = clock();
    printf("time consumed in scanning: %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size / 100) / (((double)(end - start)) / CLOCKS_PER_SEC));

    for (int i = 0; i < 6; i++) {
        printf("scanning %d\%% keys(%ld) starting with %s\n",
               scale[i], size / 100 * scale[i], key);
        start = clock();
        t->bpt_range_test(key, size / 100 * scale[i]);
        end = clock();
        printf("time consumed in scanning: %lf, %lf ops per second\n",
               (double)(end - start) / CLOCKS_PER_SEC,
               (size / 100 * scale[i]) / (((double)(end-start)) / CLOCKS_PER_SEC));
    }
}

char *generate_key(long seed) {
    srand(seed);
    long length = K_LEN;

    char * str = new char[length+1];
    memset(str, 0, length+1);
    for (int i = 0; i < length; i++) {
        str[i] = dict[rand() % DN];
    }
    return str;
}

char *generate_value(long seed) {
    srand(seed);
    long length = V_LEN;

    char * str = new char[length+1];
    memset(str, 0, length+1);
    for (int i = 0; i < length; i++) {
        str[i] = dict[rand() % DN];
    }
    return str;
}

bool validate(PBPTPtr t, PBPTNodePtr node) {
    if (node == NULL) {
        if (t->empty()) {
            printf("null inside B+ tree\n"); 
            return false;            
        } else {
            return true;
        }
    }

    if (node->type == NON_LEAF) {
        PBPTNodePtr child = nullptr;
        auto temp_node = static_cast<PBPTNonLeafPtr>(node);
        if (temp_node->num_of_children != node->num_of_keys + 1) {
            printf("this node %p is illegal: ", temp_node.get());
            printf("num_of_children = %llu, num_of_keys = %llu\n",
                   temp_node->num_of_children.get_ro(),
                   node->num_of_keys.get_ro());
            printf("keys are: ");
            temp_node->print_keys();
            puts("");
            return false;
        }

        for (unsigned long long i = 0; i < node->num_of_keys; i++) {
            child = temp_node->children[i];
            if (node->keys[i] <= child->keys[child->num_of_keys-1]) {
                printf("illegal child %p\n", child.get());
                printf("parent keys: ");
                temp_node->print_keys();
                puts("");
                printf("children of parent\n");
                temp_node->print_children();
                puts("");
                printf("child keys: ");
                child->print_keys();
                puts("");
                return false;
            }
        }
        child = temp_node->children[temp_node->num_of_children - 1];
        if (node->keys[node->num_of_keys-1] > child->keys[0]) {
            printf("illegal child %p\n", child.get());
            printf("parent keys: ");
            temp_node->print_keys();
            puts("");
            printf("children of parent\n");
            temp_node->print_children();
            puts("");
            printf("child keys: ");
            child->print_keys();
            puts("");
            return false;
        }
        
        for (unsigned long long i = 0; i < temp_node->num_of_children; i++)
            if (!validate(t, temp_node->children[i]))
                return false;
    }
    return true;
}



int run_test(pmem::obj::pool_base &pop, PBPTPtr t, long size) {
    char **keys;
    char **values;
    clock_t start, end;
    keys = new char*[size * sizeof(char *)];
    values = new char*[size * sizeof(char *)];
    printf("\n****** Preparing %ld data, please wait... ******\n", size);
    for (long i = 0; i < size; i++) {
        // temp = generate_key(i + 1323);
        // temp = generate_key(size + i * 33 / 45 << 3 - 147);
        keys[i] = generate_key(i + 1323);
        values[i] = generate_value(i + 1323);
    }


    // Insertion test
    printf("Insertion started, please wait...\n");
    // warm up
    for (long i = 0; i < size / 2; i++) {
#ifdef DEBUG
        printf("inserting %s, %ld/%ld\n", keys[i], i + 1, size);
#endif
//         if (i % 10000)
//             printf("%ld\%%/%ld finished\n", i / 10000, size / 10000);
        t->bpt_insert(pop, keys[i], values[i]);
    }

    puts("Warm up finished");
    // bpt_print(t);
    // test starts here
    start = clock();
    for (long i = size / 2; i < size; i++) {
#ifdef DEBUG
        printf("inserting %s, %ld/%ld\n", keys[i], i + 1, size);
#endif
        t->bpt_insert(pop, keys[i], values[i]);
    }
    end = clock();
    t->bpt_print();
#ifndef PERF
    // if (!validate(t, &D_RW(t->t)->root)) {
    //     bpt_print(t);
    //         
    //     printf("!!!! VALIDATION FAILED DURING INSERTION\n");
    //     bpt_print(t); 
    //     return -1;
    // }
#endif
    printf("time consumed in insertion %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size / 2) / (((double)(end - start)) / CLOCKS_PER_SEC));

    // Query test
    query(t, keys, size);

    // scanning
    scan(t, "aaaaaaaaaa", size);
    
    // Deletion test
    printf("Deletion started, please wait...\n");
    start = clock();
    for (long i = 0; i < size; i++) {
#ifdef DEBUG
        printf("deleting %s, %ld/%ld\n", keys[i], i + 1, size);
#endif
        t->bpt_delete(keys[i]);
#ifdef DEBUG
        std::string temp;
        if (t->bpt_get(keys[i], temp) != -1) {
            printf("deleting %s failed\n", keys[i]);
            assert(0);
        }
#endif
            
#ifndef PERF
        // if (i % 1000 == 0) {
        //     if (!validate(t, &D_RW(t->t)->root)) {  
        //         bpt_print(t);
        //         printf("!!!! VALIDATION FAILED WHEN DELETING %s, %ld/%ld\n",
        //                keys[i], i+1, size);
        //         assert(0);
        //     }
        // }
#endif
    }
    end = clock();
    printf("time consumed in deletion %lf, %lf ops per second\n",
           (double)(end - start) / CLOCKS_PER_SEC,
           (size) / (((double)(end - start)) / CLOCKS_PER_SEC));


    if (t->empty()) {
        puts("PASS!!!");
        printf("You successfully covered %ld keys\n\n", size);
    } else {
	printf("tree is not empty after deletion\n\n");
        t->bpt_print();
    }

    for (long i = 0; i < size; i++) {
        free(keys[i]);
        free(values[i]);
    }
    free(keys);
    free(values);
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("fjdsifjisoadjfaosjfiosajfioasjfioasjfioasjfdoi\n");
        return -1;
    }
    
    pmem::obj::pool<BPlusTree> pop =
        pmem::obj::pool<BPlusTree>::create(argv[1],
                                           "B+Tree",
                                           (long)3 * (1 << 30) + 500 * (1 << 20),
                                           S_IWUSR | S_IRUSR);

    auto t = pop.root();
    t->initialize(pop);
    for (int i = 1; i <= 10; i++) {
        run_test(pop, t, PRE / 10 * i);
    }
}
