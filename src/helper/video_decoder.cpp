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

  ~Impl() {
    if (session)
      VTDecompressionSessionInvalidate(session);
    if (session)
      CFRelease(session);
    if (fmt)
      CFRelease(fmt);
  }
};

VideoDecoder::VideoDecoder() : impl_(new Impl) {}
VideoDecoder::~VideoDecoder() { delete impl_; }

bool VideoDecoder::decode(std::span<const uint8_t> annexb, bool hevc, std::vector<uint8_t> &bgra,
                          int &width, int &height) {
  if (annexb.empty())
    return false;
  if (impl_->vt_failed)
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  auto nals = split_nal(annexb);
  if (nals.empty())
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  bool params_changed = false;
  for (auto nal : nals) {
    if (nal.empty())
      continue;
    uint8_t t = hevc ? (uint8_t)((nal[0] >> 1) & 0x3f) : (uint8_t)(nal[0] & 0x1f);
    if (!hevc && t == 7) {
      impl_->sps.assign(nal.begin(), nal.end());
      params_changed = true;
    } else if (!hevc && t == 8) {
      impl_->pps.assign(nal.begin(), nal.end());
      params_changed = true;
    } else if (hevc && t == 32) {
      impl_->vps.assign(nal.begin(), nal.end());
      params_changed = true;
    } else if (hevc && t == 33) {
      impl_->sps.assign(nal.begin(), nal.end());
      params_changed = true;
    } else if (hevc && t == 34) {
      impl_->pps.assign(nal.begin(), nal.end());
      params_changed = true;
    }
  }

  if (params_changed && !impl_->sps.empty() && !impl_->pps.empty()) {
    if (impl_->session) {
      VTDecompressionSessionInvalidate(impl_->session);
      CFRelease(impl_->session);
      impl_->session = nullptr;
    }
    if (impl_->fmt) {
      CFRelease(impl_->fmt);
      impl_->fmt = nullptr;
    }
    OSStatus st = -1;
    if (!hevc) {
      const uint8_t *sets[2] = {impl_->sps.data(), impl_->pps.data()};
      const size_t sizes[2] = {impl_->sps.size(), impl_->pps.size()};
      st = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets, sizes,
                                                               4, &impl_->fmt);
    } else if (!impl_->vps.empty()) {
      const uint8_t *sets[3] = {impl_->vps.data(), impl_->sps.data(), impl_->pps.data()};
      const size_t sizes[3] = {impl_->vps.size(), impl_->sps.size(), impl_->pps.size()};
      st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, 3, sets, sizes,
                                                               4, nullptr, &impl_->fmt);
    }
    if (st != noErr || !impl_->fmt) {
      impl_->vt_failed = true;
      return impl_->ff.decode(annexb, hevc, bgra, width, height);
    }
    CMVideoCodecType codec = hevc ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264;
    impl_->hevc = hevc;
    VTDecompressionOutputCallbackRecord cb{};
    cb.decompressionOutputCallback =
        [](void *ref, void *, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef img,
           CMTime, CMTime) {
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
        };
    cb.decompressionOutputRefCon = impl_;
    CFMutableDictionaryRef dst = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int32_t pix = kCVPixelFormatType_32BGRA;
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pix);
    CFDictionarySetValue(dst, kCVPixelBufferPixelFormatTypeKey, n);
    CFRelease(n);
    st = VTDecompressionSessionCreate(kCFAllocatorDefault, impl_->fmt, nullptr, dst, &cb,
                                      &impl_->session);
    CFRelease(dst);
    if (st != noErr) {
      impl_->vt_failed = true;
      return impl_->ff.decode(annexb, hevc, bgra, width, height);
    }
  }

  if (!impl_->session)
    return impl_->ff.decode(annexb, hevc, bgra, width, height);

  // AVCC length-prefixed sample from Annex-B NALs (skip parameter sets).
  std::vector<uint8_t> avcc;
  for (auto nal : nals) {
    if (nal.empty())
      continue;
    uint8_t t = hevc ? (uint8_t)((nal[0] >> 1) & 0x3f) : (uint8_t)(nal[0] & 0x1f);
    if (!hevc && (t == 7 || t == 8))
      continue;
    if (hevc && (t == 32 || t == 33 || t == 34))
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
  OSStatus st = VTDecompressionSessionDecodeFrame(impl_->session, sb, 0, nullptr, nullptr);
  VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
  CFRelease(sb);
  CFRelease(bb);
  if (st != noErr || impl_->last_bgra.empty()) {
    impl_->vt_failed = true;
    return impl_->ff.decode(annexb, hevc, bgra, width, height);
  }
  bgra = impl_->last_bgra;
  width = impl_->last_w;
  height = impl_->last_h;
  return true;
}
