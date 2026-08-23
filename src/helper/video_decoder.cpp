#include "video_decoder.hpp"

#include <VideoToolbox/VideoToolbox.h>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

std::vector<std::span<const uint8_t>> split_nal(std::span<const uint8_t> data) {
  std::vector<std::span<const uint8_t>> nals;
  size_t i = 0;
  auto start = [&](size_t p) {
    if (p + 3 < data.size() && data[p] == 0 && data[p + 1] == 0 && data[p + 2] == 1)
      return 3;
    if (p + 4 < data.size() && data[p] == 0 && data[p + 1] == 0 && data[p + 2] == 0 &&
        data[p + 3] == 1)
      return 4;
    return 0;
  };
  while (i < data.size()) {
    int sc = start(i);
    if (!sc) {
      ++i;
      continue;
    }
    size_t nal_start = i + (size_t)sc;
    size_t j = nal_start;
    while (j < data.size()) {
      int nsc = start(j);
      if (nsc)
        break;
      ++j;
    }
    if (nal_start < j)
      nals.emplace_back(data.subspan(nal_start, j - nal_start));
    i = j;
  }
  return nals;
}

struct Ff {
  const AVCodec *codec = nullptr;
  AVCodecContext *ctx = nullptr;
  AVFrame *yuv = nullptr;
  AVFrame *bgra = nullptr;
  AVPacket *pkt = nullptr;
  SwsContext *sws = nullptr;
  uint8_t *buf = nullptr;
  int last_w = 0, last_h = 0;
  bool hevc = false;

  bool ensure(bool want_hevc) {
    if (ctx && hevc == want_hevc)
      return true;
    reset();
    hevc = want_hevc;
    codec = avcodec_find_decoder(want_hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!codec)
      return false;
    ctx = avcodec_alloc_context3(codec);
    yuv = av_frame_alloc();
    bgra = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!ctx || !yuv || !bgra || !pkt)
      return false;
    return avcodec_open2(ctx, codec, nullptr) >= 0;
  }

  void reset() {
    if (sws)
      sws_freeContext(sws);
    sws = nullptr;
    if (buf)
      av_free(buf);
    buf = nullptr;
    avcodec_free_context(&ctx);
    av_frame_free(&yuv);
    av_frame_free(&bgra);
    av_packet_free(&pkt);
    codec = nullptr;
  }

  ~Ff() { reset(); }

  bool decode(std::span<const uint8_t> data, bool want_hevc, std::vector<uint8_t> &out, int &w,
              int &h) {
    if (!ensure(want_hevc))
      return false;
    pkt->data = const_cast<uint8_t *>(data.data());
    pkt->size = (int)data.size();
    if (avcodec_send_packet(ctx, pkt) < 0)
      return false;
    int r = avcodec_receive_frame(ctx, yuv);
    if (r < 0)
      return false;
    if (yuv->width != last_w || yuv->height != last_h) {
      if (sws)
        sws_freeContext(sws);
      if (buf)
        av_free(buf);
      sws = nullptr;
      buf = nullptr;
      last_w = yuv->width;
      last_h = yuv->height;
    }
    if (!sws) {
      sws = sws_getContext(yuv->width, yuv->height, (AVPixelFormat)yuv->format, yuv->width,
                           yuv->height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, nullptr, nullptr,
                           nullptr);
      buf = (uint8_t *)av_malloc((size_t)yuv->width * (size_t)yuv->height * 4);
      av_image_fill_arrays(bgra->data, bgra->linesize, buf, AV_PIX_FMT_BGRA, yuv->width,
                           yuv->height, 1);
    }
    if (!sws || !buf)
      return false;
    sws_scale(sws, yuv->data, yuv->linesize, 0, yuv->height, bgra->data, bgra->linesize);
    w = yuv->width;
    h = yuv->height;
    out.resize((size_t)w * (size_t)h * 4);
    if (bgra->linesize[0] == w * 4) {
      memcpy(out.data(), bgra->data[0], out.size());
    } else {
      for (int y = 0; y < h; ++y)
        memcpy(out.data() + (size_t)y * (size_t)w * 4, bgra->data[0] + y * bgra->linesize[0],
               (size_t)w * 4);
    }
    return true;
  }
};

uint8_t nal_type(bool hevc, std::span<const uint8_t> nal) {
  if (nal.empty())
    return 0xff;
  return hevc ? (uint8_t)((nal[0] >> 1) & 0x3f) : (uint8_t)(nal[0] & 0x1f);
}

bool nal_is_param(bool hevc, uint8_t t) {
  if (!hevc)
    return t == 7 || t == 8;
  return t == 32 || t == 33 || t == 34;
}

bool nal_is_idr(bool hevc, uint8_t t) {
  if (!hevc)
    return t == 5;
  return t >= 16 && t <= 21;
}

int first_vcl_type(bool hevc, const std::vector<std::span<const uint8_t>> &nals) {
  for (auto nal : nals) {
    uint8_t t = nal_type(hevc, nal);
    if (nal_is_param(hevc, t))
      continue;
    if (!hevc && (t == 6 || t == 9 || t == 14))
      continue;
    return (int)t;
  }
  return -1;
}

bool assign_if_changed(std::vector<uint8_t> &dst, std::span<const uint8_t> nal) {
  if (dst.size() == nal.size() && memcmp(dst.data(), nal.data(), nal.size()) == 0)
    return false;
  dst.assign(nal.begin(), nal.end());
  return true;
}

} // namespace

struct VideoDecoder::Impl {
  Ff ff;
  VTDecompressionSessionRef session = nullptr;
  CMVideoFormatDescriptionRef fmt = nullptr;
  std::vector<uint8_t> sps, pps, vps;
  bool hevc = false;
  std::mutex mu;
  std::vector<uint8_t> last_bgra;
  int last_w = 0, last_h = 0;
  bool vt_failed = false;
  bool got_frame = false;
  bool need_idr = false;

  void destroy_session() {
    if (session) {
      VTDecompressionSessionInvalidate(session);
      CFRelease(session);
      session = nullptr;
    }
    if (fmt) {
      CFRelease(fmt);
      fmt = nullptr;
    }
  }

  bool create_session(bool want_hevc) {
    destroy_session();
    if (sps.empty() || pps.empty())
      return false;
    if (want_hevc && vps.empty())
      return false;
    OSStatus st = -1;
    if (!want_hevc) {
      const uint8_t *sets[2] = {sps.data(), pps.data()};
      const size_t sizes[2] = {sps.size(), pps.size()};
      st = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets, sizes,
                                                               4, &fmt);
    } else {
      const uint8_t *sets[3] = {vps.data(), sps.data(), pps.data()};
      const size_t sizes[3] = {vps.size(), sps.size(), pps.size()};
      st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, 3, sets, sizes,
                                                               4, nullptr, &fmt);
    }
    if (st != noErr || !fmt)
      return false;
    hevc = want_hevc;
    VTDecompressionOutputCallbackRecord cb{};
    cb.decompressionOutputCallback =
        [](void *ref, void *, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef img, CMTime,
           CMTime) {
          auto *self = static_cast<Impl *>(ref);
          if (status != noErr || !img)
            return;
          CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
          int w = (int)CVPixelBufferGetWidth(img);
          int h = (int)CVPixelBufferGetHeight(img);
          size_t stride = CVPixelBufferGetBytesPerRow(img);
          auto *base = (const uint8_t *)CVPixelBufferGetBaseAddress(img);
          self->last_w = w;
          self->last_h = h;
          self->last_bgra.resize((size_t)w * (size_t)h * 4);
          for (int y = 0; y < h; ++y)
            memcpy(self->last_bgra.data() + (size_t)y * (size_t)w * 4, base + (size_t)y * stride,
                   (size_t)w * 4);
          CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
          self->got_frame = true;
        };
    cb.decompressionOutputRefCon = this;
    CFMutableDictionaryRef dst = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int32_t pix = kCVPixelFormatType_32BGRA;
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pix);
    CFDictionarySetValue(dst, kCVPixelBufferPixelFormatTypeKey, n);
    CFRelease(n);
    st = VTDecompressionSessionCreate(kCFAllocatorDefault, fmt, nullptr, dst, &cb, &session);
    CFRelease(dst);
    return st == noErr && session;
  }

  ~Impl() { destroy_session(); }
};

VideoDecoder::VideoDecoder() : impl_(new Impl) {}
VideoDecoder::~VideoDecoder() { delete impl_; }

void VideoDecoder::reset_session() {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->destroy_session();
  impl_->ff.reset();
  impl_->last_bgra.clear();
  impl_->last_w = 0;
  impl_->last_h = 0;
  impl_->got_frame = false;
  impl_->vt_failed = false;
  impl_->need_idr = true;
}

bool VideoDecoder::decode(std::span<const uint8_t> annexb, bool hevc, std::vector<uint8_t> &bgra,
                          int &width, int &height, DecodeDiag *diag) {
  if (annexb.empty())
    return false;
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (diag)
    *diag = {};
  if (impl_->vt_failed)
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  auto nals = split_nal(annexb);
  if (nals.empty())
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  if (diag)
    diag->vcl_nal = first_vcl_type(hevc, nals);

  bool params_changed = false;
  for (auto nal : nals) {
    if (nal.empty())
      continue;
    uint8_t t = nal_type(hevc, nal);
    if (!hevc && t == 7)
      params_changed |= assign_if_changed(impl_->sps, nal);
    else if (!hevc && t == 8)
      params_changed |= assign_if_changed(impl_->pps, nal);
    else if (hevc && t == 32)
      params_changed |= assign_if_changed(impl_->vps, nal);
    else if (hevc && t == 33)
      params_changed |= assign_if_changed(impl_->sps, nal);
    else if (hevc && t == 34)
      params_changed |= assign_if_changed(impl_->pps, nal);
  }

  const bool have_params =
      !impl_->sps.empty() && !impl_->pps.empty() && (!hevc || !impl_->vps.empty());
  if (have_params && (params_changed || !impl_->session)) {
    if (!impl_->create_session(hevc)) {
      impl_->vt_failed = true;
      return impl_->ff.decode(annexb, hevc, bgra, width, height);
    }
    impl_->need_idr = true;
    if (diag)
      diag->params_recreated = true;
  }
  if (!impl_->session)
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  if (impl_->need_idr) {
    bool idr = false;
    for (auto nal : nals) {
      if (nal_is_idr(hevc, nal_type(hevc, nal))) {
        idr = true;
        break;
      }
    }
    if (!idr)
      return false;
    impl_->need_idr = false;
  }

  // AVCC length-prefixed sample from Annex-B NALs (skip parameter sets).
  std::vector<uint8_t> avcc;
  for (auto nal : nals) {
    if (nal.empty())
      continue;
    uint8_t t = nal_type(hevc, nal);
    if (nal_is_param(hevc, t))
      continue;
    uint32_t len = OSSwapHostToBigInt32((uint32_t)nal.size());
    auto *p = (const uint8_t *)&len;
    avcc.insert(avcc.end(), p, p + 4);
    avcc.insert(avcc.end(), nal.begin(), nal.end());
  }
  if (avcc.empty())
    return false;

  CMBlockBufferRef bb = nullptr;
  if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, avcc.data(), avcc.size(),
                                         kCFAllocatorNull, nullptr, 0, avcc.size(), 0, &bb) !=
      noErr) {
    impl_->vt_failed = true;
    return impl_->ff.decode(annexb, hevc, bgra, width, height);
  }
  CMSampleBufferRef sb = nullptr;
  size_t slen = avcc.size();
  if (CMSampleBufferCreateReady(kCFAllocatorDefault, bb, impl_->fmt, 1, 0, nullptr, 1, &slen, &sb) !=
      noErr) {
    CFRelease(bb);
    impl_->vt_failed = true;
    return impl_->ff.decode(annexb, hevc, bgra, width, height);
  }
  impl_->got_frame = false;
  OSStatus st = VTDecompressionSessionDecodeFrame(impl_->session, sb, 0, nullptr, nullptr);
  VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
  CFRelease(sb);
  CFRelease(bb);
  if (diag)
    diag->vt_status = (int)st;
  if (st != noErr || !impl_->got_frame)
    return false;
  bgra = impl_->last_bgra;
  width = impl_->last_w;
  height = impl_->last_h;
  return true;
}
