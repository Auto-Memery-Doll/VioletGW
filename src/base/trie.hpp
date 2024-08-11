#ifndef FLOW_GATEWAY_TRIE_HPP
#define FLOW_GATEWAY_TRIE_HPP

#include "base/closure.hpp"
#include "base/noncopyable.hpp"
#include "base/util.hpp"
#include <cstddef>
#include <string>

namespace fg {
namespace base {

/** COW的无锁字典树 */
template <typename T>
class Trie : public noncopyable {
public:
    Trie() {
        for (int i = 0; i < 128; ++ i) {
            child[i] = NULL;
        }
    };
    ~Trie() {
        for (int i = 0; i < 128; ++ i) {
            if (child[i])
                clean(child[i]);
        }
    }

    const T* get(const std::string& key) {
        int _;
        std::shared_lock<util::AtomicRWLock> lock(_mtx);
        const node* _node = read(key, &_);
        lock.unlock();
        if (_node == nullptr) {
            return nullptr;
        }
        return _node->obj;
    }

    bool set(const std::string& key, T* value) {
        int index;
        std::shared_lock<util::AtomicRWLock> lock(_mtx);
        node* _node = read(key, &index);

        if (_node == NULL) {
            child[key[0]] = new node;
            _node = child[key[0]];
            index++;
        }

        if (index == key.size() && 
            _node->childs[key[index]] && 
            _node->childs[key[index]]->vaild) {
            return false;
        }

        node *clone_node;
        if (_node->childs[key[index]])
            clone_node = _node->childs[key[index]];
        else 
            clone_node = _node, index--/** 需要从上一个所以开始构建 */;

        node* copy_node = copy(clone_node);
        lock.unlock();

        build(copy_node, key, value, index);

        std::unique_lock<util::AtomicRWLock> _lock(_mtx);
        updata(copy_node, _node, key[index]);

        return true;
    }

    const T* get_lockfree(const std::string& key) {
        node* _node = read(key);
        if (_node == nullptr) {
            return nullptr;
        }
        return _node->obj;
    }

    bool set_lockfree(const std::string& key, T* value) {
        int index;
        node* _node = read(key, &index);

        if (_node == NULL) {
            child[key[0]] = new node;
            _node = child[key[0]];
        }

        if (index == key.size() && 
            _node->childs[key[index]] && 
            _node->childs[key[index]]->valid) {
            return false;
        }

        node *clone_node;
        if (_node->childs[key[index]])
            clone_node = _node->childs[key[index]];
        else 
            clone_node = const_cast<node*>(_node), index--/** 需要从上一个所以开始构建 */;

        build(clone_node, key, value, index);

        return true;
    }

private:

    struct node {
        /** 生成一个节点副本 */
        node() {
            for (int i = 0; i < 128; ++ i) {
                childs[i] = NULL;
            }
            prev = NULL;
            obj = NULL;
            vaild = false;
        }
        node* clone() const {
            node* _copy = new node;
            for (int i = 0; i < 128; ++ i) {
                _copy->childs[i] = this->childs[i];
            }
            _copy->vaild = this->vaild;
            _copy->obj = this->obj;
            _copy->prev = this->prev;
        }

        T *obj;
        bool vaild;
        node* prev; 
        node* childs[128];
    };
    using node_iter_t = node*;

    class CleanNodeClosure : public base::Closure {
    public:
        CleanNodeClosure(node * node_) 
        :   _node(node_)
        {}
        void* Run() override {
            delete _node;
        }

        node *_node;
    };

    void clean(node* clean_node) {
        for (int i = 0; i < 128; ++ i) {
            if (clean_node->childs[i]) {
                clean(clean_node->childs[i]);
            }
        }
        if (clean_node->vaild) {
            delete clean_node->obj;
        }
        delete clean_node;
    }

    void build(node * _node, const std::string& key, T* value, int index)  {
        node_iter_t it = _node;

        for (; index < key.size()-1; ) {
            ++index;
            
            node* new_node = new node;
            new_node->prev = it;
            it->childs[key[index]] = new_node;

            it = new_node;
        }

        it->vaild = true;
        T *new_vlaue = new T(*value);
        it->obj = new_vlaue;
    }

    /// @param index key[index]在trie中的最后位置
    // 如果需要构造新节点，就从index开始构造
    node* read(const std::string& key, int* index) {
        int idx = 0;
        node_iter_t iter = child[key[0]];
        if (!iter)  goto null;
        idx++;
        for (; iter && idx < key.size(); ++ idx) {
            if (iter->childs[key[idx]] == NULL) 
                break;
            iter = iter->childs[key[idx]];
        }
null:
        *index = idx;
        return iter;
    }

    node*  copy(const node* _node) {
        return _node->clone();
    }

    void updata(node* _node, node* raw_node, char idx) {

        for (int i = 0; i < 128; ++ i) {
            if (_node->childs[i]) {
                _node->childs[i]->prev = _node;
            }
        }

        if (raw_node->prev) {
            raw_node->prev->childs[idx] = _node;
        } else {
            child[idx] = _node;
        }

        auto done = new CleanNodeClosure(raw_node);
        if (done) {
            if (!closure_queue()->commit(new CleanNodeClosure(raw_node))) {
                done->Run();
            }
        } else {
            delete raw_node;
        }
    }

private:
    util::AtomicRWLock _mtx;
    node *child[128];
};


}   // base
}   // fg

#endif // !FLOW_GATEWAY_TRIE_HPP