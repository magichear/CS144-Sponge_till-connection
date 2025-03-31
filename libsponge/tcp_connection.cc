#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 返回剩余的待发送数据量（可以发送的数据大小）
size_t TCPConnection::remaining_outbound_capacity()      const { return _sender.stream_in().remaining_capacity(); }

// 返回发送后未响应的数据量（已发送，尚未收到ACK的数据大小）
size_t TCPConnection::bytes_in_flight()                  const { return _sender.bytes_in_flight(); }

// 返回接收方未按序整理好的数据量
size_t TCPConnection::unassembled_bytes()                const { return _receiver.unassembled_bytes(); }

// 返回自上次接收到段以来经过的时间（毫秒）
size_t TCPConnection::time_since_last_segment_received() const { return _current_time - _last_time; }

// 处理接收到的TCP段
/**
 * 不是很理解文档里描述的第一步处理（如果当前TCP连接不活跃则直接返回），那对方要是发syn也不处理？
 * 这里同时处理主动连接和被动连接，基于希冀文档与PPT做了一些调整
 */
void TCPConnection::segment_received(const TCPSegment &seg) {
    _last_time = _current_time; // 更新最后接收时间
    auto &recv_header = seg.header(); 

    // 让TCP接收器进行报文接收
    bool recv_seg = _receiver.segment_received(seg);

    // 处理被动连接，第一次握手（不需要PPT里那样判别（会出错），连接器只要不活跃就行）
    if (!_active) {
        if (recv_header.syn) {
            _active = true;
            _sender.fill_window();  // 填充发送窗口，会自动设置syn
            auto _seg = _sender.segments_out().front();
            _seg.header().ack     = true;
            _seg.header().ackno   = _receiver.ackno().value();
            _seg.header().win     = _receiver.window_size();
            _segments_out.push(_seg);
            _sender.segments_out().pop();        
        }
        return;
    }

    // 处理RST（理论上，只要连接器处于活跃状态，那RST的优先级最高）      需要严格验证，避免伪造攻击
    else if (recv_header.rst) {
        if (_receiver.ackno().has_value() &&
            (recv_header.seqno.raw_value() < _receiver.ackno().value().raw_value() ||
             recv_header.seqno.raw_value() >= _receiver.ackno().value().raw_value() + _receiver.window_size())) {
            return;
        }
    
        if (recv_seg || (recv_header.ack && (_sender.next_seqno() == recv_header.ackno))) {
            unclean_shutdown();
        }
        return;
    }

    // 如果当前TCP曾经发送过包含有效数据的报文，或者当前发送者是主动连接（客户端，syn_send）
    // 主动连接的握手会在下方clean_shutdown（不一定会关闭连接，详情看下面）中进行
    if (recv_header.ack && (_receiver.ackno().has_value() || recv_header.syn)) {
        if (_sender.ack_received(recv_header.ackno, recv_header.win)) {
            _sender.fill_window(); // 填充发送窗口
        } else {
            _sender.send_empty_segment();     // 收到了尚未发送的确认（希冀文档第一步），直接忽略（但是按PPT里的说法，必须响应空报文（可能有心跳检测手段））
        }
    }

    // ============确保连接活跃（心跳）、通知对端窗口大小、确认收到的数据，发送空报文=============
    // 接收有效，发送队列为空且段有数据    没什么可发的，但需要确认自己收到了
    if (recv_seg) {
        if (_segments_out.empty() && seg.length_in_sequence_space())
            _sender.send_empty_segment();
    }
    // 接收无效，但接收端有ACK号          越界的数据，但依然需要保持连接
    else if (_receiver.ackno().has_value()) {
        _sender.send_empty_segment();
    }
    clean_shutdown(); // 优雅结束本次接收（连接未必被关闭）
}

bool TCPConnection::active() const {
    // ================== unclean shutdown ==================
    // 发送或接收数据包带有 rst flag
    if (_recv_rst || _send_rst) {
        return false;
    }

    // ================== clean shutdown ==================
    // 接收到的报文已经完全重组（自动满足）且不再接收到最新的报文
    const bool inbound_closed = _receiver.stream_out().input_ended();
    // 发送的数据已经完全发送，且已经被对端完全接收
    const bool outbound_closed = _sender.stream_in().eof() 
                              && !bytes_in_flight();

    // 不再收到数据包，说明对端已经完全接收（对端如果没收到会超时重传）                              
    const bool completely_recv = (!_linger_after_streams_finish || 
                                time_since_last_segment_received() >= _MAX_WAIT_TIME);
    
    return !(inbound_closed && outbound_closed && completely_recv);
}

// 将数据封装成报文发送出去
size_t TCPConnection::write(const string &data) {
    if (data.empty()) return 0; // 如果数据为空，直接返回0
    size_t bytes_succ_write = _sender.stream_in().write(data); // 写入数据到发送流
    _sender.fill_window(); // 填充发送窗口
    __send_segment();      // 填充发送队列
    return bytes_succ_write;
}

/**
 * 超过超时重传最大次数就断开连接
 * 发送要发送的报文段       全部在__send_segment中处理
 */
//! \param[in] ms_since_last_tick 自上次调用以来经过的毫秒数
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _current_time += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        auto &sender_out = _sender.segments_out();
        if (!sender_out.empty()) {
            auto seg = sender_out.front();
            seg.header().rst = true; 
            sender_out.pop();
            _segments_out.push(seg);
        }
        unclean_shutdown();
        return;
    }

    __send_segment();
}

/**
 * 发送方向 clean shutdown
 *      关闭sender的输入流
 *      立即触发数据发送，发送处于缓冲区的剩余数据
 */
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input(); // 标记发送流结束
    _sender.fill_window();           // 填充发送窗口
    __send_segment();                // 填充发送队列
}

// 主动连接
void TCPConnection::connect() {
    _active = true;         // 标记连接已初始化
    _sender.fill_window();  // 借助sender填充发送窗口     此时会自动发送syn
    __send_segment();       // 填充发送队列
}

/**
 * 统一处理各种情况下的发送报文
 */
void TCPConnection::__send_segment() {
    auto &sender_segment_out = _sender.segments_out();   // 获取发送端的输出队列
    while (!sender_segment_out.empty()) {
        auto first_segment = sender_segment_out.front();               // 获取队列中的第一个段         
        auto recv_ackno = _receiver.ackno();         // 获取接收端的ACK号           

        if (recv_ackno.has_value()) {
            first_segment.header().ackno = recv_ackno.value();
            first_segment.header().ack = true;           
        }
        first_segment.header().win = _receiver.window_size();

        sender_segment_out.pop();
        _segments_out.push(first_segment);
    }
}

/**
 * 不优雅的结束
 * 1. 对面发过来rst
 * 2. 调用了连接器的析构函数
 * 3. 重传次数超过最大次数
 */
void TCPConnection::unclean_shutdown() {
    _send_rst = true;
    _recv_rst = true;
    _active   = false;
    // 将发送器和接收器的缓冲置为error
    _sender.stream_in().set_error();
    inbound_stream().set_error();   
}

/**
 * 优雅的结束（不一定被调用）
 */
void TCPConnection::clean_shutdown() {
    __send_segment();                    // 填充发送队列
    if (_receiver.stream_out().input_ended()) {
        if (!_sender.stream_in().eof()) {
            _linger_after_streams_finish = false;
        }
        if (_sender.stream_in().eof() && !bytes_in_flight()) {
            if (!_linger_after_streams_finish || (time_since_last_segment_received() >= _MAX_WAIT_TIME)) {
                _active = false;
            }
        }
    }

}

TCPConnection::~TCPConnection() {
    try {
        if (active()) { // 如果连接仍然活跃
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            unclean_shutdown();
            _sender.send_empty_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
