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

void draw_airplay_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  CGContextSaveGState(ctx);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGFloat lw = std::max<CGFloat>(1.6, r.size.width / 9.0);
  CGContextSetLineWidth(ctx, lw);
  CGFloat rad = std::max<CGFloat>(1.5, r.size.width / 10.0);
  CGRect back = r;
  CGPathRef back_p = CGPathCreateWithRoundedRect(back, rad, rad, nullptr);
  CGContextAddPath(ctx, back_p);
  CGContextStrokePath(ctx);
  CGPathRelease(back_p);
  CGFloat inset = r.size.width * 0.22;
  CGRect front = CGRectInset(r, inset, inset);
  front.origin.x += inset * 0.45;
  front.origin.y -= inset * 0.35;
  CGPathRef front_p = CGPathCreateWithRoundedRect(front, rad * 0.7, rad * 0.7, nullptr);
  CGContextAddPath(ctx, front_p);
  CGContextStrokePath(ctx);
  CGPathRelease(front_p);
  CGContextRestoreGState(ctx);
}

CGFloat draw_step2(CGContextRef ctx, const IdleStubCopy &copy, CTFontRef font, CGColorRef color, CGFloat x,
                   CGFloat top, CGFloat canvas_h, CGFloat icon_size) {
  CFAttributedStringRef a1 = make_attr(copy.step2_prefix, font, color);
  CFAttributedStringRef a2 = make_attr(copy.step2, font, color);
  CTLineRef l1 = CTLineCreateWithAttributedString(a1);
  CTLineRef l2 = CTLineCreateWithAttributedString(a2);
  CGFloat as1 = 0, ds1 = 0, lead = 0;
  CGFloat as2 = 0, ds2 = 0;
  CTLineGetTypographicBounds(l1, &as1, &ds1, &lead);
  CTLineGetTypographicBounds(l2, &as2, &ds2, &lead);
  CGFloat ascent = std::max(as1, as2);
  CGFloat descent = std::max(ds1, ds2);
  CGFloat w1 = (CGFloat)CTLineGetTypographicBounds(l1, nullptr, nullptr, nullptr);
  CGFloat baseline = canvas_h - top - ascent;
  CGContextSetTextPosition(ctx, x, baseline);
  CTLineDraw(l1, ctx);
  CGRect icon = CGRectMake(x + w1 + 2, baseline - icon_size * 0.12, icon_size, icon_size * 0.78);
  draw_airplay_icon(ctx, icon, color);
  CGContextSetTextPosition(ctx, CGRectGetMaxX(icon) + 8, baseline);
  CTLineDraw(l2, ctx);
  CFRelease(l1);
  CFRelease(l2);
  CFRelease(a1);
  CFRelease(a2);
  return ascent + descent + 2;
}

void draw_blob(CGContextRef ctx, CGPoint c, CGFloat r, CGFloat R, CGFloat G, CGFloat B, CGFloat A) {
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGFloat comps[] = {R, G, B, A, R, G, B, 0};
  CGFloat locs[] = {0, 1};
  CGGradientRef g = CGGradientCreateWithColorComponents(cs, comps, locs, 2);
  CGContextDrawRadialGradient(ctx, g, c, 0, c, r, kCGGradientDrawsBeforeStartLocation);
  CGGradientRelease(g);
  CGColorSpaceRelease(cs);
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

  CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
  CGContextFillRect(ctx, CGRectMake(0, 0, w, h));

  const CGFloat canvas_h = (CGFloat)h;
  draw_blob(ctx, CGPointMake(w * 0.18, canvas_h - h * 0.08), h * 0.42, 1.0, 0.62, 0.32, 0.28);
  draw_blob(ctx, CGPointMake(w * 0.48, canvas_h - h * 0.02), h * 0.38, 0.35, 0.62, 0.95, 0.26);
  draw_blob(ctx, CGPointMake(w * 0.72, canvas_h - h * 0.12), h * 0.28, 0.55, 0.85, 0.45, 0.12);

  const CGFloat s = (CGFloat)h / 1080.0f;
  const CGFloat title_sz = std::max<CGFloat>(18, 28 * s);
  const CGFloat body_sz = std::max<CGFloat>(17, 26 * s);
  const CGFloat hint_sz = std::max<CGFloat>(12, 16 * s);
  const CGFloat gap_major = 28 * s;
  const CGFloat gap_hint = 6 * s;
  const CGFloat block_w = std::min((CGFloat)w * 0.72f, 920 * s);
  const CGFloat x = ((CGFloat)w - block_w) * 0.5f;

  CTFontRef title_f = ui_font(title_sz, true);
  CTFontRef body_f = ui_font(body_sz, true);
  CTFontRef hint_f = ui_font(hint_sz, false);
  CGColorRef dark = CGColorCreateGenericRGB(0.12, 0.12, 0.12, 1);
  CGColorRef gray = CGColorCreateGenericRGB(0.42, 0.42, 0.42, 1);

  std::string step3 = copy.step3_prefix + "\"" + receiver_name + "\"";

  auto measure = [&](const std::string &t, CTFontRef f) -> CGFloat {
    if (t.empty())
      return 0;
    CFAttributedStringRef attr = make_attr(t, f, dark);
    CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(attr);
    CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr,
                                                             CGSizeMake(block_w, CGFLOAT_MAX), nullptr);
    CFRelease(fs);
    CFRelease(attr);
    return std::ceil(sz.height) + 1;
  };

  CGFloat total = 0;
  total += measure(copy.header, title_f) + gap_major;
  total += measure(copy.step1, body_f) + gap_major;
  total += body_sz + 8 + gap_hint;
  total += measure(copy.step2_hint_a, hint_f) + 2;
  total += measure(copy.step2_hint_b, hint_f) + gap_major;
  total += measure(step3, body_f) + gap_hint;
  total += measure(copy.step3_hint, hint_f);

  CGFloat y = std::max<CGFloat>(40 * s, ((CGFloat)h - total) * 0.5f);

  y += draw_wrapped(ctx, copy.header, title_f, dark, x, y, canvas_h, block_w) + gap_major;
  y += draw_wrapped(ctx, copy.step1, body_f, dark, x, y, canvas_h, block_w) + gap_major;
  y += draw_step2(ctx, copy, body_f, dark, x, y, canvas_h, body_sz * 0.95f) + gap_hint;
  y += draw_wrapped(ctx, copy.step2_hint_a, hint_f, gray, x, y, canvas_h, block_w) + 2;
  y += draw_wrapped(ctx, copy.step2_hint_b, hint_f, gray, x, y, canvas_h, block_w) + gap_major;
  y += draw_wrapped(ctx, step3, body_f, dark, x, y, canvas_h, block_w) + gap_hint;
  draw_wrapped(ctx, copy.step3_hint, hint_f, gray, x, y, canvas_h, block_w);

  CGColorRelease(dark);
  CGColorRelease(gray);
  CFRelease(title_f);
  CFRelease(body_f);
  CFRelease(hint_f);
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
