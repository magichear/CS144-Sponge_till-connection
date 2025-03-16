#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    auto index = 0;
    // 处理第一个 SYN
    if (seg.header().syn && !_isn.has_value()) {
        _isn = make_optional<WrappingInt32>(seg.header().seqno);
        _syn_flag = true;
        _fin_seqno = 1;  // SYN 占用一个序列号
        // 如果同时包含 FIN 标志，直接处理 FIN
        if (seg.header().fin) {
            _fin_flag = true;
            _fin_seqno++;  // FIN 占用一个序列号
        }
    }
    else if (!_syn_flag) { // 没有 SYN 之前不接受任何值
        return false;
    }
    else {
        auto checkpoint = _reassembler.stream_out().bytes_written();
        auto abs_ackno = unwrap(ackno().value(), _isn.value(), checkpoint);
        auto abs_seqno = unwrap(seg.header().seqno, _isn.value(), checkpoint);
        auto length = seg.length_in_sequence_space();

        // FIN 之后的 ACK 也该被接收
        if (_fin_flag && abs_seqno >= _fin_seqno) return true;
        else if (length == 0) return abs_seqno == abs_ackno;
        else if (abs_seqno >= abs_ackno + window_size() || abs_seqno + length <= abs_ackno) return false;
        else if (seg.header().fin) {
            _fin_flag = true;
            _fin_seqno = abs_seqno + length;
        }
        index = abs_seqno-1;
    }
    _reassembler.push_substring(seg.payload().copy(), index, _fin_flag);
    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn.has_value()) return nullopt;
    auto abs_ackno = 1 + _reassembler.stream_out().bytes_written();
    if (abs_ackno+1 == _fin_seqno) abs_ackno++;
    
    return make_optional<WrappingInt32>(wrap(abs_ackno, _isn.value()));
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }