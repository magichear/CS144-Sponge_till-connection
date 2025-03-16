#include "wrapping_integers.hh"

#include <iostream>
// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) { return WrappingInt32{static_cast<uint32_t>((n + isn.raw_value()) % (1ul << 32))}; }

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    int64_t  offset = static_cast<int64_t>(n.raw_value()) - static_cast<int64_t>(isn.raw_value());
    uint64_t base   = checkpoint & ~((1ul << 32) - 1);
    uint64_t case1  = base + offset;
    uint64_t case2  = case1 + (1ul << 32);
    uint64_t case3  = case1 - (1ul << 32);

    // 选取与索引值参考值之差的绝对值最小的那个
    uint64_t abs1 = case1 > checkpoint ? case1 - checkpoint : checkpoint - case1;
    uint64_t abs2 = case2 > checkpoint ? case2 - checkpoint : checkpoint - case2;
    uint64_t abs3 = case3 > checkpoint ? case3 - checkpoint : checkpoint - case3;

    if      (abs1 <= abs2 && abs1 <= abs3) return case1;
    else if (abs2 <= abs1 && abs2 <= abs3) return case2;
    else                                   return case3;
}