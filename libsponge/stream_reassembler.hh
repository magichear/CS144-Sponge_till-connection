#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"
#include <cstddef>
#include <set>

#include <cstdint>
#include <string>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.
    struct block_node {
        size_t begin = 0;
        size_t length = 0;
        std::string data = "";
        bool operator<(const block_node t) const { return begin < t.begin; }
    };

    ByteStream _output;                 //!< The reassembled in-order byte stream
    size_t _capacity;                   //!< The maximum number of bytes
    size_t _flow_tail = 0;             //!< The index of the end of flow
    size_t _unassembled_byte = 0;       //!< The number of unassembled bytes
    bool _eof = false;                  //!< Flag indicating if the end of the stream has been reached
    std::set<block_node> _blocks = {};  //!< Set to store unassembled byte blocks

    bool merge_block(block_node &elm1, const block_node &elm2, std::set<block_node>::iterator &iter);

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receives a substring and writes any newly contiguous bytes into the stream.
    //!
    //! If accepting all the data would overflow the `capacity` of this
    //! `StreamReassembler`, then only the part of the data that fits will be
    //! accepted. If the substring is only partially accepted, then the `eof`
    //! will be disregarded.
    //!
    //! \param data the string being added
    //! \param index the index of the first byte in `data`
    //! \param eof whether or not this segment ends with the end of the stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been submitted twice, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;
    size_t flow_tail() const { return _flow_tail; }

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
