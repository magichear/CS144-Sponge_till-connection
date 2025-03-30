#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

/**
 * @brief 处理接收到的 TCP 段
 * 
 * 原先的错误原因：
 * 1. 没有正确处理 FIN 标志，导致流结束状态未正确通知 `_reassembler`。
 * 2. 对接收窗口的检查逻辑不够严谨，可能导致接受非法段或拒绝合法段。
 * 3. 空段的特殊处理逻辑不完善，未正确处理空段的序列号。
 * 
 * 修改内容：
 * 1. 在接收到 FIN 标志时，确保 `_reassembler.push_substring` 的 `eof` 参数正确设置为 `true`，通知流结束。
 * 2. 修正接收窗口的检查逻辑，确保只有位于窗口内的段才会被接受。
 * 3. 对空段的特殊处理逻辑进行了明确化，确保其序列号必须等于当前的 `ackno`。
 */
bool TCPReceiver::segment_received(const TCPSegment &seg) {
    // 处理第一个 SYN
    if (seg.header().syn && !_isn.has_value()) {
        _isn = make_optional<WrappingInt32>(seg.header().seqno);
        _syn_flag = true;
        _fin_seqno = 1;  // SYN 占用一个序列号
        // 如果同时包含 FIN 标志，直接处理 FIN
        if (seg.header().fin) {
            _fin_flag = true;
            _fin_seqno++;  // FIN 占用一个序列号
            _reassembler.push_substring("", 0, true);  // 通知流结束
        }
    } else if (!_syn_flag) { // 没有 SYN 之前不接受任何值
        return false;
    } else {
        auto checkpoint = _reassembler.stream_out().bytes_written();
        auto abs_ackno = unwrap(ackno().value(), _isn.value(), checkpoint);
        auto abs_seqno = unwrap(seg.header().seqno, _isn.value(), checkpoint);
        auto length = seg.length_in_sequence_space();

        // 检查段是否在接收窗口内
        if (!(abs_seqno < abs_ackno + window_size() && abs_seqno + length > abs_ackno)) {
            // 空段的特殊处理
            return length == 0 && abs_seqno == abs_ackno;
        }

        // 处理 FIN 标志
        if (seg.header().fin) {
            _fin_flag = true;
            _fin_seqno = abs_seqno + length;
        }

        // 计算索引
        auto index = abs_seqno - 1;
        _reassembler.push_substring(seg.payload().copy(), index, _fin_flag && abs_seqno + length == _fin_seqno);
    }

    return true;
}

/**
 * @brief 返回当前的 ACK 序列号
 * 
 * 原先的错误原因：
 * 1. 未正确处理 FIN 标志的序列号，导致 ACK 序列号可能不包含 FIN。
 * 
 * 修改内容：
 * 1. 修正 ACK 序列号的计算逻辑，确保在接收到 FIN 标志后，ACK 序列号正确包含 FIN 的序列号。
 */
optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn.has_value()) return nullopt;
    auto abs_ackno_without_fin = 1 + _reassembler.stream_out().bytes_written();  // 1 for SYN
    if (_fin_flag && abs_ackno_without_fin + 1 == _fin_seqno) {
        return make_optional<WrappingInt32>(wrap(abs_ackno_without_fin + 1, _isn.value()));
    }

    return make_optional<WrappingInt32>(wrap(abs_ackno_without_fin, _isn.value()));
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }