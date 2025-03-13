#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 超出容量范围的数据直接丢弃
    if (index >= _flow_tail + _capacity) {  // capacity over
        return;
    }

    block_node elm;
    if (index + data.length() > _flow_tail) {  // 存在未处理数据
        // 数据包预处理
        if (index < _flow_tail) {   // 部分重叠
            size_t offset = _flow_tail - index;    // 已处理部分
            elm.data.assign(data.begin() + offset, data.end());
            elm.begin = index + offset;
            elm.length = elm.data.length();
        } else {    // 完全不重叠
            elm.begin = index;
            elm.length = data.length();
            elm.data = data;
        }        
        _unassembled_byte += elm.length;

        auto iter = _blocks.lower_bound(elm);   // 找到第一个起始索引大于等于elm的元素
        auto merged = true;
        // 尝试合并前驱和后继
        while (merged) {
            merged = false;
            // 尝试合并后继
            if (iter != _blocks.end()) 
                merged = merge_block(elm, *iter, iter);
            // 尝试合并前驱
            if (iter != _blocks.begin()) 
                merged = merge_block(elm, *(--iter), iter);
        }
        // 当前合并后的数据包是有序部分的直接后继（仅可能由当前到达的数据包触发）
        if (elm.begin == _flow_tail) {
            size_t write_bytes = _output.write(elm.data);
            _flow_tail += write_bytes;
            _unassembled_byte -= write_bytes;
        }
        else _blocks.insert(elm);    // 前后都合并完后插入到缓存里
    }
    // END OF FILE
    if (eof) _eof = true;
    if (_eof && empty()) _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const {
    return _unassembled_byte;
}

bool StreamReassembler::empty() const {
    return _unassembled_byte == 0;
}

bool StreamReassembler::merge_block(block_node &elm1, const block_node &elm2, std::set<block_node>::iterator &iter) {
    block_node x, y;
    long repeat_bytes = 0;
    // 找起始点小的那个，六种情况变为三种
    if (elm1.begin > elm2.begin) {
        x = elm2;
        y = elm1;
    } else {
        x = elm1;
        y = elm2;
    }

    // 有序集必须全部连续
    if (x.begin + x.length < y.begin) {   // 不相交
        return false;
    } else {
        elm1 = x;
        repeat_bytes = y.length;
        if (x.begin + x.length < y.begin + y.length) { // 部分包含
            auto offset = x.begin + x.length - y.begin;
            elm1.data += y.data.substr(offset);
            elm1.length = elm1.data.length();
            repeat_bytes = offset;            
        }
    }
    
    _unassembled_byte -= repeat_bytes;
    iter = _blocks.erase(iter); // 返回下一个元素的迭代器（同时也是lower_bound）

    return true;
}