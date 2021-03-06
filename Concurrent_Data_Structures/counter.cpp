//
// Created by wu061 on 4/06/18.
//


//a shared counter implemented by combining tree.
//a shared counter implemented by combining tree.

//the simplest counter, sequential


#include <mutex>
#include <shared_mutex>
#include <thread>
#include <iostream>
#include <condition_variable>
#include <functional>
#include <list>
#include <vector>
#include <assert.h>


class counter {
public:
    counter() {
        count = 0;
    }

    int increase() {
        int old_val = count;
        count = old_val + 1;
        return old_val;
    }

private:
    int count;
};


class counter_lock {
public:
    counter_lock() {
        count = 0;
    }

    int increase() {
        cnt_lock.lock();
        int old_val = count;
        count = count + 1;
        cnt_lock.unlock();
        return old_val;
    }

    int get_count() {
        return count;
    }

private:
    int count;
    std::mutex cnt_lock;
};

/*
 * class combing_tree {
private:

};

 */


enum node_status {
    IDLE, FIRST, SECOND, RESULT, ROOT
};

bool DEBUG = false;
std::mutex output_lock;

class node {
public:
    node(int id) {
        node_id = id;
        locked = false;
        status = IDLE;
        result = 0;
        first_value = 0;
        second_value = 0;
    }

    int get_parent_id() {
        if (node_id == 1) return 1;
        return node_id / 2;
    }

    void set_status(node_status st) {
        status = st;
    }

    bool pre_combine() {
        /*
         * if a node is available, the active thread can continously climb towards the root.
         * first lock, at the same time, only one thread can change the status of the node.
         * return: bool whether the current node can be combined.
         */
        std::unique_lock <std::shared_mutex> l(this->node_lock);
        //another lock, if the passive thread is waiting for results, we cannot climb
        //even though no other thread is trying to modify the status of the node, it might be waiting the results
        while (this->locked || this->status == SECOND) {
            //wait unlocks node_lock and wait here.
            this->cv.wait(l, [&] { return (!this->locked) && (this->status != SECOND); });
        }
        if (DEBUG) {
            std::unique_lock <std::mutex> ol(output_lock);
            std::cout << "pre_combine" << std::endl;
        }
        assert(this->status != SECOND);
        switch (this->status) {
            case IDLE:
                status = FIRST;
                return true;
            case FIRST:
                /*
                 * if the current status is FIRST, it means the current thread would be the loser thread so that
                 * it would not carry the combined to the root but wait on the current node.
                 */
                status = SECOND;
                locked = true;
                if (DEBUG) {
                    std::unique_lock <std::mutex> ol(output_lock);
                    std::cout << "lock node" << this->node_id << std::endl;
                }
                return false;
            case ROOT:
                return false;
            default:
                if (DEBUG) {
                    std::unique_lock <std::mutex> ol(output_lock);
                    std::cerr << "[in pre_combine]unexpected node status" << this->status << std::endl;
                    exit(1);
                }

        }
    }

    //if a node is combinable, we do the combine.
    int combine(int combined) {
        //parameters: combined [int] is the combined values from all the nodes rooted on the current one
        std::unique_lock <std::shared_mutex> l(this->node_lock);
        while (this->locked) {
            this->cv.wait(l, [&] { return (!this->locked); });
        }
        if (DEBUG) {
            std::unique_lock <std::mutex> ol(output_lock);
            std::cout << "combine" << std::endl;
        }
        //as long as combinable, the current thread is the winner, put the values it combined from the children to first value.
        this->first_value = combined;
        this->locked = true; //lock since the node will need to wait for the result then.
        switch (this->status) {
            case FIRST:
                return this->first_value; //itself has no value yet, accumulate directly.
            case SECOND:
                //when does the second_value get set?????? those combines which got stuck here.
                return this->first_value + this->second_value; //there were already some value ??????
            default:
                if (DEBUG) {
                    std::unique_lock <std::mutex> ol(output_lock);
                    std::cerr << "[in combine]unexpected node status" << this->status << std::endl;
                    exit(1);
                }
        }
    }

    int op(int combined) {
        std::unique_lock <std::shared_mutex> l(this->node_lock);

        int old_value;
        if (DEBUG) {
            std::unique_lock <std::mutex> ol(output_lock);
            std::cout << "op" << std::endl;
        }
        switch (this->status) {
            case ROOT:
                //active thread
                old_value = this->result;
                this->result += combined;
                return old_value;

            case SECOND:
                //passive thread
                this->second_value = combined;
                //??????why do locked = false and notify_all()??????
                this->locked = false;
                this->cv.notify_all();
                while (this->status != RESULT) {
                    if (DEBUG) {
                        std::unique_lock <std::mutex> ol(output_lock);
                        std::cout << "get stuck for waiting result" << std::endl;
                    }
                    cv.wait(l, [&] { return this->status == RESULT; });
                    if (DEBUG) {
                        std::unique_lock <std::mutex> ol(output_lock);
                        std::cout << "end stuck" << std::endl;
                    }
                }
                //??????why do locked = false and notify_all()??????
                old_value = this->result;
                this->locked = false;
                this->cv.notify_all();
                this->status = IDLE;
                return old_value;
            default:
                if (DEBUG) {
                    std::unique_lock <std::mutex> ol(output_lock);
                    std::cerr << "[in op]unexpected node status" << this->status << std::endl;
                    exit(1);
                }

        }
    }

    void distribute(int prior) {
        std::unique_lock <std::shared_mutex> l(this->node_lock);
        if (DEBUG) {
            std::unique_lock <std::mutex> ol(output_lock);
            std::cout << "distribute" << std::endl;
        }
        switch (this->status) {
            case FIRST:
                this->status = IDLE;
                this->locked = false;
                //this->cv.notify_all();
                break;
            case SECOND:
                this->result = prior + this->second_value;
                this->status = RESULT;
                this->cv.notify_all();
                break;
            default:
                if (DEBUG) {
                    std::unique_lock <std::mutex> ol(output_lock);
                    std::cerr << "[in distribute]unexpected node status" << this->status << std::endl;
                    exit(1);
                }
        }
        this->cv.notify_all();
    }

    int get_result() {
        std::shared_lock <std::shared_mutex> l(node_lock);
        return result;
    }

private:
    int node_id;
    bool locked;
    std::shared_mutex node_lock;
    node_status status;
    std::condition_variable_any cv;
    //active thread: first value, passive thread: second value
    int first_value, second_value;
    //result fetched by the active thread.
    int result;
};

class combining_tree {

public:
    combining_tree(int n_thread) {
        /*
         * n_thread is the max number of threads which are supported by the combining tree. we assume n_thread must be
         * power of 2 here. we can use an array to store the complete binary tree.
         */
        tree = std::vector<node *>(2 * n_thread + 1);
        for (int i = 1; i < 2 * n_thread + 1; i++) {
            tree[i] = new node(i);
        }
        tree[1]->set_status(ROOT);
        leaf_num = n_thread;
    }

    ~combining_tree() {
    }

    int get_and_increment(int thread_id) {
        {
            std::unique_lock <std::mutex> ol(output_lock);
            std::cout << "total count = " << get_count() << std::endl;
        }
        std::list<int> nodes_stack;
        int original_leaf_id = leaf_num + thread_id;
        int leaf_id = original_leaf_id;
        while (tree[leaf_id]->pre_combine()) {
            //std::cout << leaf_id << std::endl;
            //if the leaf is not the root, we find its parent. otherwise stop
            if (leaf_id != tree[leaf_id]->get_parent_id())
                leaf_id = tree[leaf_id]->get_parent_id();
            else
                break;
        }
        int stop_node_id = leaf_id;
        int tmp_id = original_leaf_id;
        //std::cout << "stop_node_id = " << stop_node_id << std::endl;
        int combined = 1;
        while (tmp_id != stop_node_id) {
            combined = tree[tmp_id]->combine(combined);
            nodes_stack.push_back(tmp_id);
            if (tmp_id != tree[tmp_id]->get_parent_id())
                tmp_id = tree[tmp_id]->get_parent_id();
            else
                break;
        }
        int prior = tree[tmp_id]->op(combined);
        while (!nodes_stack.empty()) {
            tmp_id = nodes_stack.back();
            nodes_stack.pop_back();
            tree[tmp_id]->distribute(prior);
        }
        return prior;
    }

    int get_count() {
        std::cout << "current count = " << tree[1]->get_result() << std::endl;
        return tree[1]->get_result();
    }

private:
    std::vector<node *> tree;
    int leaf_num;

};

#define THREAD_NUM 8

int main() {
    combining_tree counter = combining_tree(THREAD_NUM / 2);
    std::thread t[THREAD_NUM];
    std::thread tr[THREAD_NUM];
    for (int i = 0; i < THREAD_NUM; i++) {
        t[i] = std::thread(std::bind(&combining_tree::get_and_increment, &counter, (i + 1) % 2));
    }
    for (int j = 0; j < THREAD_NUM; j++) {
        tr[j] = std::thread(std::bind(&combining_tree::get_count, &counter));

    }
    for (int i = 0; i < THREAD_NUM; i++) {
        t[i].join();
    }

    for (int i = 0; i < THREAD_NUM; i++) {
        tr[i].join();
    }
    std::cout << "final count = " << counter.get_count() << std::endl;

}