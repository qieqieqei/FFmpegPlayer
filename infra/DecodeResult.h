#pragma once

// ============================================================
// DecodeResult - 解码结果枚举（8.4，评审七）
//
// avcodec_receive_frame() 的返回值不能用裸指针表达：
//
//   Success         : 成功取出一帧（输出参数接管该帧）
//   NeedMorePacket  : AVERROR(EAGAIN) —— 解码器需要更多包，
//                     不是错误，继续 SendPacket 后再次 Receive
//   End             : AVERROR(EOF) —— 解码真正结束（flush 完成）
//   Error           : 其他错误（内部已记录日志）
//
// 用法：
//   FramePtr frame;
//   DecodeResult r = decoder.ReceiveFrame(frame);
//   if (r == DecodeResult::Success) { ... 使用 frame ... }
//   else if (r == DecodeResult::End) { ... 播放结束 ... }
//   // NeedMorePacket / Error：回到送包循环
// ============================================================

enum class DecodeResult
{
    Success,          // 取出一帧（输出参数有效）
    NeedMorePacket,   // EAGAIN：继续送包
    End,              // EOF：解码结束
    Error             // 其他错误
};
