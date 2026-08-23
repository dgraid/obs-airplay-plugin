#include "idle_stub.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

CTFontRef ui_font(CGFloat size, bool bold) {
  return CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, size, nullptr);
}

CFAttributedStringRef make_attr(const std::string &utf8, CTFontRef font, CGColorRef color,
                                CTTextAlignment align = kCTTextAlignmentNatural) {
  CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, utf8.c_str(), kCFStringEncodingUTF8);
  const bool owned = s != nullptr;
  if (!s)
    s = CFSTR("");
  CTParagraphStyleSetting settings[] = {
      {kCTParagraphStyleSpecifierAlignment, sizeof(align), &align}};
  CTParagraphStyleRef style = CTParagraphStyleCreate(settings, 1);
  const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName, kCTParagraphStyleAttributeName};
  const void *vals[] = {font, color, style};
  CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 3, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef attr = CFAttributedStringCreate(kCFAllocatorDefault, s, dict);
  CFRelease(dict);
  CFRelease(style);
  if (owned)
    CFRelease(s);
  return attr;
}

CGFloat measure_text(const std::string &utf8, CTFontRef font, CGFloat max_w,
                     CTTextAlignment align = kCTTextAlignmentNatural) {
  if (utf8.empty())
    return 0;
  CFAttributedStringRef attr = make_attr(utf8, font, CGColorGetConstantColor(kCGColorWhite), align);
  CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(attr);
  CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr,
                                                           CGSizeMake(max_w, CGFLOAT_MAX), nullptr);
  CFRelease(fs);
  CFRelease(attr);
  return std::ceil(sz.height) + 1;
}

CGFloat draw_wrapped(CGContextRef ctx, const std::string &utf8, CTFontRef font, CGColorRef color, CGFloat x,
                     CGFloat top, CGFloat canvas_h, CGFloat max_w,
                     CTTextAlignment align = kCTTextAlignmentNatural) {
  if (utf8.empty())
    return 0;
  CFAttributedStringRef attr = make_attr(utf8, font, color, align);
  CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(attr);
  CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr,
                                                           CGSizeMake(max_w, CGFLOAT_MAX), nullptr);
  CGFloat box_h = std::ceil(sz.height) + 1;
  CGMutablePathRef path = CGPathCreateMutable();
  CGPathAddRect(path, nullptr, CGRectMake(x, canvas_h - top - box_h, max_w, box_h));
  CTFrameRef frame = CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path, nullptr);
  CTFrameDraw(frame, ctx);
  CFRelease(frame);
  CFRelease(path);
  CFRelease(fs);
  CFRelease(attr);
  return box_h;
}

void fill_round_rect(CGContextRef ctx, CGRect r, CGFloat rad, CGColorRef fill, CGColorRef stroke, CGFloat lw) {
  rad = std::min(rad, std::min(r.size.width, r.size.height) * 0.5f);
  CGPathRef p = CGPathCreateWithRoundedRect(r, rad, rad, nullptr);
  CGContextAddPath(ctx, p);
  CGContextSetFillColorWithColor(ctx, fill);
  CGContextFillPath(ctx);
  if (stroke && lw > 0) {
    CGContextAddPath(ctx, p);
    CGContextSetStrokeColorWithColor(ctx, stroke);
    CGContextSetLineWidth(ctx, lw);
    CGContextStrokePath(ctx);
  }
  CGPathRelease(p);
}

void draw_divider(CGContextRef ctx, CGFloat x, CGFloat y_top, CGFloat canvas_h, CGFloat w, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetLineWidth(ctx, 1);
  CGFloat y = canvas_h - y_top;
  CGContextMoveToPoint(ctx, x, y);
  CGContextAddLineToPoint(ctx, x + w, y);
  CGContextStrokePath(ctx);
  CGContextRestoreGState(ctx);
}

void draw_airplay_video_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetFillColorWithColor(ctx, color);
  CGFloat lw = std::max<CGFloat>(2.0, r.size.width / 11.0);
  CGContextSetLineWidth(ctx, lw);
  CGContextSetLineJoin(ctx, kCGLineJoinRound);
  CGContextSetLineCap(ctx, kCGLineCapRound);

  CGRect screen = CGRectMake(r.origin.x + lw * 0.5, r.origin.y + r.size.height * 0.36, r.size.width - lw,
                             r.size.height * 0.62 - lw);
  CGFloat rad = std::max<CGFloat>(3.0, r.size.width / 8.0);
  CGPathRef screen_p = CGPathCreateWithRoundedRect(screen, rad, rad, nullptr);
  CGContextAddPath(ctx, screen_p);
  CGContextStrokePath(ctx);
  CGPathRelease(screen_p);

  CGFloat cx = r.origin.x + r.size.width * 0.5;
  CGFloat tri_w = r.size.width * 0.50;
  CGFloat tri_top = r.origin.y + r.size.height * 0.44;
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, cx - tri_w * 0.5, r.origin.y);
  CGContextAddLineToPoint(ctx, cx + tri_w * 0.5, r.origin.y);
  CGContextAddLineToPoint(ctx, cx, tri_top);
  CGContextClosePath(ctx);
  CGContextFillPath(ctx);
  CGContextRestoreGState(ctx);
}

void draw_airplay_audio_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetFillColorWithColor(ctx, color);
  CGFloat lw = std::max<CGFloat>(2.0, r.size.width / 12.0);
  CGContextSetLineWidth(ctx, lw);
  CGContextSetLineCap(ctx, kCGLineCapRound);

  CGFloat cx = r.origin.x + r.size.width * 0.5;
  CGFloat cy = r.origin.y + r.size.height * 0.58;
  for (int i = 1; i <= 3; ++i) {
    CGFloat rad = r.size.width * (0.14f + (CGFloat)i * 0.12f);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, cx, cy, rad, (CGFloat)M_PI * 0.78f, (CGFloat)M_PI * 0.22f, 1);
    CGContextStrokePath(ctx);
  }

  CGFloat tri_w = r.size.width * 0.42;
  CGFloat tri_top = r.origin.y + r.size.height * 0.40;
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, cx - tri_w * 0.5, r.origin.y);
  CGContextAddLineToPoint(ctx, cx + tri_w * 0.5, r.origin.y);
  CGContextAddLineToPoint(ctx, cx, tri_top);
  CGContextClosePath(ctx);
  CGContextFillPath(ctx);
  CGContextRestoreGState(ctx);
}

void draw_chevron(CGContextRef ctx, CGPoint tip, CGFloat size, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetLineWidth(ctx, std::max<CGFloat>(1.6, size / 10.0));
  CGContextSetLineCap(ctx, kCGLineCapRound);
  CGContextSetLineJoin(ctx, kCGLineJoinRound);
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, tip.x - size * 0.35, tip.y + size * 0.42);
  CGContextAddLineToPoint(ctx, tip.x + size * 0.18, tip.y);
  CGContextAddLineToPoint(ctx, tip.x - size * 0.35, tip.y - size * 0.42);
  CGContextStrokePath(ctx);
  CGContextRestoreGState(ctx);
}

struct CardFonts {
  CTFontRef label;
  CTFontRef value;
  CTFontRef hint;
};

CGFloat card_content_h(const IdleStubCopy &copy, const std::string &name, const CardFonts &f, CGFloat inner_w,
                       CGFloat pad, CGFloat gap_sm, CGFloat gap_md) {
  CGFloat h = pad;
  h += measure_text(copy.device_label, f.label, inner_w) + gap_sm;
  h += measure_text(name, f.value, inner_w) + gap_md;
  h += 1 + gap_md;
  h += measure_text(copy.network_label, f.label, inner_w) + gap_sm;
  h += measure_text(copy.network_value, f.value, inner_w);
  h += pad;
  return h;
}

void draw_card(CGContextRef ctx, const IdleStubCopy &copy, const std::string &name, const CardFonts &f,
               CGColorRef white, CGColorRef muted, CGColorRef card_fill, CGColorRef card_stroke, CGFloat x,
               CGFloat y, CGFloat canvas_h, CGFloat w, CGFloat h, CGFloat rad, CGFloat pad, CGFloat gap_sm,
               CGFloat gap_md) {
  fill_round_rect(ctx, CGRectMake(x, canvas_h - y - h, w, h), rad, card_fill, card_stroke, 1);
  const CGFloat inner_x = x + pad;
  const CGFloat inner_w = w - pad * 2;
  CGFloat cy = y + pad;
  cy += draw_wrapped(ctx, copy.device_label, f.label, muted, inner_x, cy, canvas_h, inner_w) + gap_sm;
  cy += draw_wrapped(ctx, name, f.value, white, inner_x, cy, canvas_h, inner_w) + gap_md;
  draw_divider(ctx, inner_x, cy, canvas_h, inner_w, card_stroke);
  cy += 1 + gap_md;
  cy += draw_wrapped(ctx, copy.network_label, f.label, muted, inner_x, cy, canvas_h, inner_w) + gap_sm;
  draw_wrapped(ctx, copy.network_value, f.value, white, inner_x, cy, canvas_h, inner_w);
}

void draw_pill(CGContextRef ctx, CGRect r, const std::string &text, CTFontRef font, CGColorRef fill,
               CGColorRef text_color, CGColorRef chevron_color, CGFloat canvas_h) {
  fill_round_rect(ctx, r, r.size.height * 0.5f, fill, nullptr, 0);
  const CGFloat pad = r.size.height * 0.32f;
  const CGFloat text_x = r.origin.x + pad;
  const CGFloat text_w = r.size.width - pad * 2 - r.size.height * 0.45f;
  const CGFloat top = canvas_h - r.origin.y - r.size.height;
  const CGFloat th = measure_text(text, font, text_w);
  draw_wrapped(ctx, text, font, text_color, text_x, top + (r.size.height - th) * 0.5f, canvas_h, text_w);
  draw_chevron(ctx, CGPointMake(CGRectGetMaxX(r) - pad * 0.85f, r.origin.y + r.size.height * 0.5f),
               r.size.height * 0.32f, chevron_color);
}

} // namespace

std::vector<uint8_t> render_idle_stub(uint32_t w, uint32_t h, const std::string &receiver_name,
                                      const IdleStubCopy &copy) {
  if (w < 16 || h < 16)
    return {};
  std::vector<uint8_t> out((size_t)w * h * 4);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  const uint32_t bitmap_info =
      (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst;
  CGContextRef ctx =
      CGBitmapContextCreate(out.data(), w, h, 8, (size_t)w * 4, cs, (CGBitmapInfo)bitmap_info);
  CGColorSpaceRelease(cs);
  if (!ctx)
    return out;

  CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
  CGContextFillRect(ctx, CGRectMake(0, 0, w, h));

  const CGFloat canvas_w = (CGFloat)w;
  const CGFloat canvas_h = (CGFloat)h;
  const CGFloat s = std::min(canvas_w / 1920.0f, canvas_h / 1080.0f);
  const CGFloat layout_w = 1920.0f * s;
  const CGFloat layout_h = 1080.0f * s;
  const CGFloat ox = (canvas_w - layout_w) * 0.5f;
  const CGFloat oy = (canvas_h - layout_h) * 0.5f;

  const CGFloat title_sz = 84 * s;
  const CGFloat sub_sz = 24 * s;
  const CGFloat label_sz = 15 * s;
  const CGFloat value_sz = 26 * s;
  const CGFloat hint_sz = 15 * s;
  const CGFloat pill_sz = 17 * s;
  const CGFloat icon_s = 56 * s;
  const CGFloat icon_gap = 36 * s;
  const CGFloat pad = 28 * s;
  const CGFloat gap_sm = 8 * s;
  const CGFloat gap_md = 20 * s;
  const CGFloat after_title = 14 * s;
  const CGFloat after_sub = 36 * s;
  const CGFloat rad = 14 * s;
  const CGFloat pill_h = 48 * s;
  const CGFloat after_card = 16 * s;
  const CGFloat after_pill = 18 * s;
  const CGFloat after_hint = 8 * s;
  const std::string name = receiver_name.empty() ? "AirPlay Receiver" : receiver_name;

  CTFontRef title_f = ui_font(title_sz, true);
  CTFontRef sub_f = ui_font(sub_sz, false);
  CTFontRef pill_f = ui_font(pill_sz, false);
  CardFonts cf{ui_font(label_sz, false), ui_font(value_sz, true), ui_font(hint_sz, false)};
  CGColorRef white = CGColorCreateGenericRGB(1, 1, 1, 1);
  CGColorRef muted = CGColorCreateGenericRGB(1, 1, 1, 0.62);
  CGColorRef card_fill = CGColorCreateGenericRGB(1, 1, 1, 0.12);
  CGColorRef card_stroke = CGColorCreateGenericRGB(1, 1, 1, 0.18);
  CGColorRef pill_fill = CGColorCreateGenericRGB(1, 1, 1, 1);
  CGColorRef pill_text = CGColorCreateGenericRGB(0.22, 0.22, 0.24, 1);

  const CGFloat left_pane = layout_w * 0.5f;
  const CGFloat brand_w = 620 * s;
  const CGFloat card_w = 500 * s;
  const CGFloat brand_x = ox + (left_pane - brand_w) * 0.5f;
  const CGFloat card_x = ox + left_pane + (layout_w * 0.5f - card_w) * 0.5f;
  const CGFloat inner_w = std::max<CGFloat>(1, card_w - pad * 2);

  const CGFloat bh = measure_text("AirPlay", title_f, brand_w) + after_title +
                     measure_text(copy.subtitle, sub_f, brand_w) + after_sub + icon_s;
  const CGFloat ch = card_content_h(copy, name, cf, inner_w, pad, gap_sm, gap_md);
  const CGFloat hint_h = measure_text(copy.how_hint, cf.hint, inner_w);
  const CGFloat foot_h = measure_text(copy.footer, cf.hint, inner_w);
  const CGFloat rh = ch + after_card + pill_h + after_pill + hint_h + after_hint + foot_h;
  const CGFloat cluster_h = std::max(bh, rh);
  const CGFloat y0 = oy + (layout_h - cluster_h) * 0.5f;
  const CGFloat brand_y = y0 + (cluster_h - bh) * 0.5f;
  const CGFloat card_y = y0 + (cluster_h - rh) * 0.5f;

  CGFloat cy = brand_y;
  cy += draw_wrapped(ctx, "AirPlay", title_f, white, brand_x, cy, canvas_h, brand_w) + after_title;
  cy += draw_wrapped(ctx, copy.subtitle, sub_f, white, brand_x, cy, canvas_h, brand_w) + after_sub;
  draw_airplay_video_icon(ctx, CGRectMake(brand_x, canvas_h - cy - icon_s, icon_s, icon_s), white);
  draw_airplay_audio_icon(
      ctx, CGRectMake(brand_x + icon_s + icon_gap, canvas_h - cy - icon_s, icon_s, icon_s), white);

  draw_card(ctx, copy, name, cf, white, muted, card_fill, card_stroke, card_x, card_y, canvas_h, card_w, ch,
            rad, pad, gap_sm, gap_md);
  CGFloat ry = card_y + ch + after_card;
  draw_pill(ctx, CGRectMake(card_x, canvas_h - ry - pill_h, card_w, pill_h), copy.how_value, pill_f, pill_fill,
            pill_text, pill_text, canvas_h);
  ry += pill_h + after_pill;
  ry += draw_wrapped(ctx, copy.how_hint, cf.hint, white, card_x, ry, canvas_h, card_w) + after_hint;
  draw_wrapped(ctx, copy.footer, cf.hint, muted, card_x, ry, canvas_h, card_w);

  CGColorRelease(white);
  CGColorRelease(muted);
  CGColorRelease(card_fill);
  CGColorRelease(card_stroke);
  CGColorRelease(pill_fill);
  CGColorRelease(pill_text);
  CFRelease(title_f);
  CFRelease(sub_f);
  CFRelease(pill_f);
  CFRelease(cf.label);
  CFRelease(cf.value);
  CFRelease(cf.hint);
  CGContextRelease(ctx);
  return out;
}

void draw_lock_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGFloat lw = std::max<CGFloat>(2.0, r.size.width / 12.0);
  CGContextSetLineWidth(ctx, lw);
  CGContextSetLineCap(ctx, kCGLineCapRound);
  CGFloat body_h = r.size.height * 0.52;
  CGRect body = CGRectMake(r.origin.x, r.origin.y, r.size.width, body_h);
  CGFloat rad = std::max<CGFloat>(3.0, r.size.width / 8.0);
  CGPathRef body_p = CGPathCreateWithRoundedRect(body, rad, rad, nullptr);
  CGContextAddPath(ctx, body_p);
  CGContextStrokePath(ctx);
  CGPathRelease(body_p);
  CGFloat shackle_w = r.size.width * 0.56;
  CGFloat shackle_x = r.origin.x + (r.size.width - shackle_w) * 0.5;
  CGFloat shackle_y = r.origin.y + body_h - lw * 0.5;
  CGFloat shackle_h = r.size.height - body_h;
  CGContextBeginPath(ctx);
  CGContextAddArc(ctx, shackle_x + shackle_w * 0.5, shackle_y + shackle_h * 0.15, shackle_w * 0.5, 0,
                  (CGFloat)M_PI, 0);
  CGContextStrokePath(ctx);
  CGContextRestoreGState(ctx);
}

std::vector<uint8_t> render_pause_stub(uint32_t w, uint32_t h, const PauseStubCopy &copy) {
  if (w < 16 || h < 16)
    return {};
  std::vector<uint8_t> out((size_t)w * h * 4);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  const uint32_t bitmap_info =
      (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst;
  CGContextRef ctx =
      CGBitmapContextCreate(out.data(), w, h, 8, (size_t)w * 4, cs, (CGBitmapInfo)bitmap_info);
  CGColorSpaceRelease(cs);
  if (!ctx)
    return out;

  CGContextSetRGBFillColor(ctx, 0.09, 0.09, 0.11, 1);
  CGContextFillRect(ctx, CGRectMake(0, 0, w, h));

  const CGFloat canvas_h = (CGFloat)h;
  const CGFloat s = std::max<CGFloat>(0.7, std::min((CGFloat)w, (CGFloat)h) / 390.0f);
  const CGFloat title_sz = 22 * s;
  const CGFloat body_sz = 16 * s;
  const CGFloat block_w = std::min((CGFloat)w * 0.78f, 340 * s);
  const CGFloat x = ((CGFloat)w - block_w) * 0.5f;
  const CGFloat lock_s = 36 * s;
  const CTTextAlignment center = kCTTextAlignmentCenter;

  CTFontRef title_f = ui_font(title_sz, true);
  CTFontRef body_f = ui_font(body_sz, false);
  CGColorRef white = CGColorCreateGenericRGB(0.93, 0.93, 0.94, 1);
  CGColorRef muted = CGColorCreateGenericRGB(0.62, 0.62, 0.66, 1);

  auto measure = [&](const std::string &t, CTFontRef f) -> CGFloat {
    if (t.empty())
      return 0;
    CFAttributedStringRef attr = make_attr(t, f, white, center);
    CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(attr);
    CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr,
                                                             CGSizeMake(block_w, CGFLOAT_MAX), nullptr);
    CFRelease(fs);
    CFRelease(attr);
    return std::ceil(sz.height) + 1;
  };

  const CGFloat gap = 14 * s;
  CGFloat total = lock_s + gap;
  total += measure(copy.header, title_f) + gap;
  total += measure(copy.body, body_f);
  CGFloat y = std::max<CGFloat>(24 * s, ((CGFloat)h - total) * 0.5f);

  draw_lock_icon(ctx, CGRectMake(x + (block_w - lock_s) * 0.5f, canvas_h - y - lock_s, lock_s, lock_s),
                 white);
  y += lock_s + gap;
  y += draw_wrapped(ctx, copy.header, title_f, white, x, y, canvas_h, block_w, center) + gap;
  draw_wrapped(ctx, copy.body, body_f, muted, x, y, canvas_h, block_w, center);

  CGColorRelease(white);
  CGColorRelease(muted);
  CFRelease(title_f);
  CFRelease(body_f);
  CGContextRelease(ctx);
  return out;
}

void letterbox_bgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh) {
  const size_t dst_bytes = (size_t)dw * dh * 4;
  std::memset(dst, 0, dst_bytes);
  if (!src || sw == 0 || sh == 0 || dw == 0 || dh == 0)
    return;
  if (sw == dw && sh == dh) {
    std::memcpy(dst, src, dst_bytes);
    return;
  }
  const double scale = std::min((double)dw / sw, (double)dh / sh);
  const uint32_t tw = std::max(1u, (uint32_t)std::lround(sw * scale));
  const uint32_t th = std::max(1u, (uint32_t)std::lround(sh * scale));
  const uint32_t ox = (dw - tw) / 2;
  const uint32_t oy = (dh - th) / 2;
  for (uint32_t y = 0; y < th; ++y) {
    const uint32_t sy = y * sh / th;
    const uint8_t *srow = src + (size_t)sy * sw * 4;
    uint8_t *drow = dst + ((size_t)(oy + y) * dw + ox) * 4;
    for (uint32_t x = 0; x < tw; ++x) {
      const uint32_t sx = x * sw / tw;
      std::memcpy(drow + (size_t)x * 4, srow + (size_t)sx * 4, 4);
    }
  }
}
