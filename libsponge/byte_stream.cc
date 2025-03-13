#include "byte_stream.hh"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) {  }

size_t ByteStream::write(const string &data) {
    size_t wLen = min(data.size(), remaining_capacity());
    _buffer.append(data.substr(0, wLen));
    _write_count += wLen;
    return wLen;
}

string ByteStream::read(const size_t len) {
    const auto ret = peek_output(len);
    pop_output(len);
    return ret;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    if (len > _buffer.size()) {
        throw std::invalid_argument("len is greater than the buffer size");
    }
    return _buffer.concatenate().substr(0, len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    if (len > _buffer.size()) {
        throw std::invalid_argument("len is greater than the buffer size");
    }
    _buffer.remove_prefix(len);
    _read_count += len;
}

void ByteStream::end_input() {
    _input_ended_flag = true;
}

bool ByteStream::input_ended() const { return _input_ended_flag; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.size() == 0; }

bool ByteStream::eof() const { 
    return _buffer.size() == 0 && ByteStream::input_ended(); 
}

bool ByteStream::error() const { return _error; }

size_t ByteStream::bytes_written() const { return _write_count; }

size_t ByteStream::bytes_read() const { return _read_count; }

size_t ByteStream::remaining_capacity() const { 
    return _capacity - _buffer.size(); 
}
