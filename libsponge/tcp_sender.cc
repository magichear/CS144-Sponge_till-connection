#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timeout(retx_timeout)
    , _unresponsed_msg() {}

uint64_t TCPSender::bytes_in_flight() const { return max(_next_seqno - _recv_seg, 0ul); }

/**
 * [FIX] ：
 *       - 填充主逻辑不变，依然分三个阶段，但是中间的数据段改为可以重复填充（附加FIN的情况移动到segGen中处理）
 *       - 新增零窗口探测逻辑（发送后立即中断循环）
 */
void TCPSender::fill_window() {
    // 处理SYN段的发送
    if (!_syn_exist) {
        TCPSegment syn_seg = segGen("SYN");
        push_segment(syn_seg);
    }

    // 发送数据段，可能包含FIN
    while (_stream.buffer_size() && (bytes_in_flight() < _window_size || !_window_size)) {
        TCPSegment data_seg = segGen("DATA");
        if (data_seg.length_in_sequence_space() == 0) {
            break; // 无法生成更多数据段
        }
        push_segment(data_seg);
        // 零窗口探测时发送一个段后停止
        if (!_window_size) {
            break;
        }
    }

    // 发送单独的FIN段（当没有数据且窗口允许时）
    if (!_fin_exist && _stream.eof() && (bytes_in_flight() < _window_size || !bytes_in_flight())) {
        TCPSegment fin_seg = segGen("FIN");
        push_segment(fin_seg);
    }
}

/**
 * 生成TCP段的核心逻辑，支持SYN/FIN/数据三种类型
 * [FIX] 自动处理FIN标志条件判断：
 *       1. 需要流已结束且窗口空间允许（正常窗口或零窗口探测）
 *       2. FIN标志与数据段合并发送，避免单独FIN段占用序列号空间
 */
TCPSegment TCPSender::segGen(std::string type) {
    TCPSegment seg;
    seg.header().seqno = next_seqno();

    if (type == "SYN") {
        seg.header().syn = true;
        _syn_exist = true;
    } else if (type == "FIN") {
        seg.header().fin = true;
        _fin_exist = true;
    } else {
        // 生成数据段，并可能添加FIN
        size_t len = calc_seg_length();
        if (len == 0) {
            return seg; // 无数据可读
        }
        seg.payload() = Buffer(_stream.read(len));

        // 检查是否可以添加FIN
        if (!_fin_exist && _stream.eof()) {
            size_t current_flight = bytes_in_flight();
            size_t seg_length = seg.length_in_sequence_space();
            bool window_ok = (current_flight + seg_length + 1 <= _window_size);
            bool zero_window_probe = (_window_size == 0 && current_flight == 0 && seg_length == 1);
            if (window_ok || zero_window_probe) {
                seg.header().fin = true;
                _fin_exist = true;
            }
        }
    }
    return seg;
}

/**
 * 总数据量、窗口大小、TCP允许发送的最大数据量，三者取最小
 * [FIX] 窗口大小计算错误，没有考虑零窗口探测（最少发送一字节）
 */
size_t TCPSender::calc_seg_length() {
    return min(TCPConfig::MAX_PAYLOAD_SIZE,
                min(_stream.buffer_size(),
                    max(static_cast<size_t>(1), static_cast<size_t>(_window_size)) - bytes_in_flight()));
}

void TCPSender::push_segment(const TCPSegment &tcp_segment) {
    // 将段加入待发送队列和未确认队列
    _segments_out.push(tcp_segment);
    _unresponsed_msg.push(tcp_segment);
    _next_seqno += tcp_segment.length_in_sequence_space();

    // 开启定时器
    if (!_timer) {
        _timer          = true;
        _timer_lastTime = _current_time; // 设置定时器的最后超时时间
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    auto abs_ackno = unwrap(ackno, _isn, _recv_seg);

    // ackno无效（确认了尚未发送的数据）
    if (abs_ackno > _next_seqno) return false;
    // 已经被确认过了
    if (abs_ackno < _recv_seg) return true;

    _recv_seg    = abs_ackno;  // 最大确认序列号（左端点）
    _window_size = window_size;

    // 根据ack将未响应报文队列中已被响应的删除
    while (!_unresponsed_msg.empty() && unwrap(_unresponsed_msg.front().header().seqno, _isn, _recv_seg) < abs_ackno) {
        _unresponsed_msg.pop();
    }

    // 填充发送窗口，发送更多数据
    fill_window();

    _retransmission_timeout = _initial_retransmission_timeout; // 重置重传超时时间为初始值
    _timer_lastTime         = _current_time;
    _consecutive_retrans    = 0;                               // 收到有效的ack，重置连续重传计数器

    // 未确认报文队列为空，关闭重传计时
    if (!_unresponsed_msg.size()) _timer = false;

    // 返回true，表示ack处理成功
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 更新当前时间，增加自上次调用以来的时间（单位：毫秒）
    _current_time += ms_since_last_tick;

    // 1. 定时器是否开启
    // 2. 定时器是否超时
    // 3. 是否有未回复的
    if (_timer && (_current_time-_timer_lastTime) >= _retransmission_timeout && !_unresponsed_msg.empty()) {
        // 超时重传
        _segments_out.push(_unresponsed_msg.front());

        _consecutive_retrans++;             // 连续重传计数器自增
        _retransmission_timeout <<= 1;      // 指数退避
        _timer_lastTime = _current_time;    // 更新定时器的最后超时时间为当前时间
    }
    else if (_unresponsed_msg.empty()) {    // 全发完了，关闭定时器
        _timer = false;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retrans; }

/**
 * 补充声明中未完善的成员函数
 */
void TCPSender::send_empty_segment() {
    TCPSegment tcp_segment;
    tcp_segment.header().seqno = next_seqno();
    _segments_out.push(tcp_segment);
}