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
                                CTTextAlignment align = kCTTextAlignmentNatural, CGFloat kern = 0) {
  CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, utf8.c_str(), kCFStringEncodingUTF8);
  const bool owned = s != nullptr;
  if (!s)
    s = CFSTR("");
  CTParagraphStyleSetting settings[] = {
      {kCTParagraphStyleSpecifierAlignment, sizeof(align), &align}};
  CTParagraphStyleRef style = CTParagraphStyleCreate(settings, 1);
  CFNumberRef kern_n = nullptr;
  const void *keys[4];
  const void *vals[4];
  int n = 0;
  keys[n] = kCTFontAttributeName;
  vals[n++] = font;
  keys[n] = kCTForegroundColorAttributeName;
  vals[n++] = color;
  keys[n] = kCTParagraphStyleAttributeName;
  vals[n++] = style;
  if (kern != 0) {
    kern_n = CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &kern);
    keys[n] = kCTKernAttributeName;
    vals[n++] = kern_n;
  }
  CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, n, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef attr = CFAttributedStringCreate(kCFAllocatorDefault, s, dict);
  CFRelease(dict);
  CFRelease(style);
  if (kern_n)
    CFRelease(kern_n);
  if (owned)
    CFRelease(s);
  return attr;
}

CGFloat measure_text(const std::string &utf8, CTFontRef font, CGFloat max_w,
                     CTTextAlignment align = kCTTextAlignmentNatural, CGFloat kern = 0) {
  if (utf8.empty())
    return 0;
  CFAttributedStringRef attr = make_attr(utf8, font, CGColorGetConstantColor(kCGColorWhite), align, kern);
  CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(attr);
  CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr,
                                                           CGSizeMake(max_w, CGFLOAT_MAX), nullptr);
  CFRelease(fs);
  CFRelease(attr);
  return std::ceil(sz.height) + 1;
}

CGFloat draw_wrapped(CGContextRef ctx, const std::string &utf8, CTFontRef font, CGColorRef color, CGFloat x,
                     CGFloat top, CGFloat canvas_h, CGFloat max_w,
                     CTTextAlignment align = kCTTextAlignmentNatural, CGFloat kern = 0) {
  if (utf8.empty())
    return 0;
  CFAttributedStringRef attr = make_attr(utf8, font, color, align, kern);
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

void begin_svg(CGContextRef ctx, CGRect r) {
  CGContextSaveGState(ctx);
  CGContextTranslateCTM(ctx, r.origin.x, r.origin.y + r.size.height);
  CGContextScaleCTM(ctx, r.size.width / 100.0, -r.size.height / 100.0);
}

void draw_airplay_video_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  begin_svg(ctx, r);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetFillColorWithColor(ctx, color);
  CGContextSetLineWidth(ctx, 7);
  CGContextSetLineCap(ctx, kCGLineCapRound);
  CGContextSetLineJoin(ctx, kCGLineJoinRound);
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, 30, 70);
  CGContextAddLineToPoint(ctx, 15, 70);
  CGContextAddArc(ctx, 15, 60, 10, (CGFloat)M_PI * 0.5, (CGFloat)M_PI, 0);
  CGContextAddLineToPoint(ctx, 5, 18);
  CGContextAddArc(ctx, 15, 18, 10, (CGFloat)M_PI, (CGFloat)M_PI * 1.5, 0);
  CGContextAddLineToPoint(ctx, 85, 8);
  CGContextAddArc(ctx, 85, 18, 10, (CGFloat)M_PI * 1.5, 0, 0);
  CGContextAddLineToPoint(ctx, 95, 60);
  CGContextAddArc(ctx, 85, 60, 10, 0, (CGFloat)M_PI * 0.5, 0);
  CGContextAddLineToPoint(ctx, 70, 70);
  CGContextStrokePath(ctx);
  CGContextBeginPath(ctx);
  CGContextMoveToPoint(ctx, 50, 64);
  CGContextAddLineToPoint(ctx, 78, 96);
  CGContextAddLineToPoint(ctx, 22, 96);
  CGContextClosePath(ctx);
  CGContextFillPath(ctx);
  CGContextRestoreGState(ctx);
}

void draw_airplay_audio_icon(CGContextRef ctx, CGRect r, CGColorRef color) {
  begin_svg(ctx, r);
  CGContextSetStrokeColorWithColor(ctx, color);
  CGContextSetFillColorWithColor(ctx, color);
  CGContextSetLineWidth(ctx, 7);
  CGContextSetLineCap(ctx, kCGLineCapRound);
  CGContextFillEllipseInRect(ctx, CGRectMake(40, 56, 20, 20));
  CGContextBeginPath(ctx);
  CGContextAddArc(ctx, 50, 66, 24, std::atan2(50.0 - 66.0, 31.0 - 50.0),
                  std::atan2(50.0 - 66.0, 69.0 - 50.0), 0);
  CGContextStrokePath(ctx);
  CGContextBeginPath(ctx);
  CGContextAddArc(ctx, 50, 66, 42, std::atan2(36.0 - 66.0, 17.0 - 50.0),
                  std::atan2(36.0 - 66.0, 83.0 - 50.0), 0);
  CGContextStrokePath(ctx);
  CGContextRestoreGState(ctx);
}

void paint_idle_bg(CGContextRef ctx, CGFloat w, CGFloat h, CGFloat ox, CGFloat oy, CGFloat lw, CGFloat lh) {
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGFloat linear[] = {0.086, 0.086, 0.106, 1, 0.031, 0.031, 0.039, 1};
  CGGradientRef g = CGGradientCreateWithColorComponents(cs, linear, nullptr, 2);
  CGContextDrawLinearGradient(ctx, g, CGPointMake(0, h), CGPointMake(0, 0), 0);
  CGGradientRelease(g);

  CGFloat glow[] = {0.494, 0.549, 0.667, 0.16, 0.494, 0.549, 0.667, 0};
  CGGradientRef gg = CGGradientCreateWithColorComponents(cs, glow, nullptr, 2);
  CGPoint gc = CGPointMake(ox + lw * 0.34, h - (oy + lh * 0.22));
  CGContextDrawRadialGradient(ctx, gg, gc, 0, gc, lw * 0.72, kCGGradientDrawsAfterEndLocation);
  CGGradientRelease(gg);

  CGFloat vig[] = {0, 0, 0, 0, 0, 0, 0, 0.55};
  CGGradientRef vg = CGGradientCreateWithColorComponents(cs, vig, nullptr, 2);
  CGPoint vc = CGPointMake(ox + lw * 0.50, h - (oy + lh * 0.45));
  CGContextDrawRadialGradient(ctx, vg, vc, lw * 0.28, vc, std::max(lw, lh) * 0.85,
                              kCGGradientDrawsAfterEndLocation);
  CGGradientRelease(vg);
  CGColorSpaceRelease(cs);
}

struct Type {
  CTFontRef title;
  CTFontRef sub;
  CTFontRef value;
  CTFontRef hint;
  CTFontRef label;
};

CGFloat step_block_h(const std::string &title, const std::string &hint, CTFontRef vf, CTFontRef hf,
                     CGFloat text_w, CGFloat gap) {
  return measure_text(title, vf, text_w) + gap + measure_text(hint, hf, text_w);
}

void draw_step(CGContextRef ctx, const char *num, const std::string &title, const std::string &hint,
               const Type &t, CGColorRef white, CGColorRef muted, CGColorRef dim, CGFloat x, CGFloat y,
               CGFloat canvas_h, CGFloat num_w, CGFloat gap, CGFloat text_w, CGFloat hint_gap) {
  draw_wrapped(ctx, num, t.value, dim, x, y, canvas_h, num_w);
  CGFloat tx = x + num_w + gap;
  y += draw_wrapped(ctx, title, t.value, white, tx, y, canvas_h, text_w);
  y += hint_gap;
  draw_wrapped(ctx, hint, t.hint, muted, tx, y, canvas_h, text_w);
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

  const CGFloat canvas_w = (CGFloat)w;
  const CGFloat canvas_h = (CGFloat)h;
  const bool tall = canvas_w < canvas_h * 1.25f;
  const CGFloat s = tall ? std::min(canvas_w / 720.0f, canvas_h / 1280.0f)
                         : std::min(canvas_w / 1920.0f, canvas_h / 1080.0f);
  const CGFloat layout_w = (tall ? 720.0f : 1920.0f) * s;
  const CGFloat layout_h = (tall ? 1280.0f : 1080.0f) * s;
  const CGFloat ox = (canvas_w - layout_w) * 0.5f;
  const CGFloat oy = (canvas_h - layout_h) * 0.5f;
  const CGFloat ts = 1.35f;

  paint_idle_bg(ctx, canvas_w, canvas_h, ox, oy, layout_w, layout_h);

  Type t{ui_font(84 * ts * s, true), ui_font(22 * ts * s, true), ui_font(22 * ts * s, true),
         ui_font(14 * ts * s, false), ui_font(13 * ts * s, true)};
  CGColorRef white = CGColorCreateGenericRGB(1, 1, 1, 1);
  CGColorRef fg70 = CGColorCreateGenericRGB(1, 1, 1, 0.70);
  CGColorRef fg55 = CGColorCreateGenericRGB(1, 1, 1, 0.55);
  CGColorRef dim = CGColorCreateGenericRGB(1, 1, 1, 0.40);
  CGColorRef card_fill = CGColorCreateGenericRGB(0.235, 0.235, 0.263, 0.55);
  CGColorRef card_hi = CGColorCreateGenericRGB(1, 1, 1, 0.06);
  CGColorRef rule = CGColorCreateGenericRGB(1, 1, 1, 0.22);

  const std::string name = receiver_name.empty() ? "AirPlay Receiver" : receiver_name;
  const std::string step3 = copy.step3 + " «" + name + "»";
  const CGFloat kern = -0.025f * 84 * ts * s;
  const CTTextAlignment center = kCTTextAlignmentCenter;
  const CGFloat icon_s = (tall ? 96.0f : 88.0f) * s;
  const CGFloat icon_gap = 64 * s;
  const CGFloat brand_gap = 24 * s;
  const CGFloat pad = 40 * s;
  const CGFloat rad = 22 * s;
  const CGFloat step_gap = 32 * s;
  const CGFloat hint_gap = 8 * s;
  const CGFloat num_gap = 20 * s;
  const CGFloat num_w = measure_text("3", t.value, 80 * s) + 4 * s;

  auto brand_h = [&](CGFloat col_w) -> CGFloat {
    return measure_text("AirPlay", t.title, col_w, center, kern) + brand_gap +
           measure_text(copy.sub1, t.sub, col_w, center) + 2 * s +
           measure_text(copy.sub2, t.sub, col_w, center) + brand_gap + 8 * s + icon_s;
  };
  auto draw_brand = [&](CGFloat x, CGFloat y, CGFloat col_w) {
    y += draw_wrapped(ctx, "AirPlay", t.title, white, x, y, canvas_h, col_w, center, kern) + brand_gap;
    y += draw_wrapped(ctx, copy.sub1, t.sub, fg70, x, y, canvas_h, col_w, center) + 2 * s;
    y += draw_wrapped(ctx, copy.sub2, t.sub, fg70, x, y, canvas_h, col_w, center) + brand_gap + 8 * s;
    CGFloat pair = icon_s * 2 + icon_gap;
    CGFloat ix = x + (col_w - pair) * 0.5f;
    draw_airplay_video_icon(ctx, CGRectMake(ix, canvas_h - y - icon_s, icon_s, icon_s), white);
    draw_airplay_audio_icon(ctx, CGRectMake(ix + icon_s + icon_gap, canvas_h - y - icon_s, icon_s, icon_s),
                            white);
  };

  auto card_inner_h = [&](CGFloat inner_w, bool title, bool rules) -> CGFloat {
    CGFloat text_w = std::max<CGFloat>(1, inner_w - num_w - num_gap);
    CGFloat hh = 0;
    if (title)
      hh += measure_text(copy.card_title, t.label, inner_w) + step_gap;
    hh += step_block_h(copy.step1, copy.step1_hint, t.value, t.hint, text_w, hint_gap);
    hh += step_gap;
    if (rules)
      hh += 1 + step_gap;
    hh += step_block_h(copy.step2, copy.step2_hint, t.value, t.hint, text_w, hint_gap);
    hh += step_gap;
    if (rules)
      hh += 1 + step_gap;
    hh += step_block_h(step3, copy.step3_hint, t.value, t.hint, text_w, hint_gap);
    return hh;
  };
  auto paint_steps = [&](CGFloat x, CGFloat y, CGFloat inner_w, bool title, bool rules) {
    const CGFloat text_w = std::max<CGFloat>(1, inner_w - num_w - num_gap);
    if (title)
      y += draw_wrapped(ctx, copy.card_title, t.label, fg70, x, y, canvas_h, inner_w) + step_gap;
    const CGFloat h1 = step_block_h(copy.step1, copy.step1_hint, t.value, t.hint, text_w, hint_gap);
    draw_step(ctx, "1", copy.step1, copy.step1_hint, t, white, fg55, dim, x, y, canvas_h, num_w, num_gap,
              text_w, hint_gap);
    y += h1 + step_gap;
    if (rules) {
      draw_divider(ctx, x, y, canvas_h, inner_w, rule);
      y += 1 + step_gap;
    }
    const CGFloat h2 = step_block_h(copy.step2, copy.step2_hint, t.value, t.hint, text_w, hint_gap);
    draw_step(ctx, "2", copy.step2, copy.step2_hint, t, white, fg55, dim, x, y, canvas_h, num_w, num_gap,
              text_w, hint_gap);
    y += h2 + step_gap;
    if (rules) {
      draw_divider(ctx, x, y, canvas_h, inner_w, rule);
      y += 1 + step_gap;
    }
    draw_step(ctx, "3", step3, copy.step3_hint, t, white, fg55, dim, x, y, canvas_h, num_w, num_gap, text_w,
              hint_gap);
  };

  if (tall) {
    const CGFloat margin = 80 * s;
    const CGFloat col_w = layout_w - margin * 2;
    const CGFloat inner_w = col_w - pad * 2;
    const CGFloat bh = brand_h(col_w);
    const CGFloat ch = pad * 2 + card_inner_h(inner_w, true, true);
    const CGFloat gap = 56 * s;
    CGFloat y = oy + (layout_h - (bh + gap + ch)) * 0.5f;
    const CGFloat x = ox + margin;
    draw_brand(x, y, col_w);
    y += bh + gap;
    fill_round_rect(ctx, CGRectMake(x, canvas_h - y - ch, col_w, ch), rad, card_fill, nullptr, 0);
    fill_round_rect(ctx, CGRectMake(x, canvas_h - y - 1, col_w, 1), 0, card_hi, nullptr, 0);
    paint_steps(x + pad, y + pad, inner_w, true, true);
  } else {
    const CGFloat margin = 90 * s;
    const CGFloat gap = 96 * s;
    const CGFloat inner = layout_w - margin * 2;
    const CGFloat left_w = inner * 0.42f;
    const CGFloat card_w = inner - left_w - gap;
    const CGFloat left_x = ox + margin;
    const CGFloat card_x = left_x + left_w + gap;
    const CGFloat inner_w = card_w - pad * 2;
    const CGFloat bh = brand_h(left_w);
    const CGFloat ch = pad * 2 + card_inner_h(inner_w, false, false);
    draw_brand(left_x, oy + (layout_h - bh) * 0.5f, left_w);
    const CGFloat card_y = oy + (layout_h - ch) * 0.5f;
    fill_round_rect(ctx, CGRectMake(card_x, canvas_h - card_y - ch, card_w, ch), rad, card_fill, nullptr, 0);
    fill_round_rect(ctx, CGRectMake(card_x, canvas_h - card_y - 1, card_w, 1), 0, card_hi, nullptr, 0);
    paint_steps(card_x + pad, card_y + pad, inner_w, false, false);
  }

  CGColorRelease(white);
  CGColorRelease(fg70);
  CGColorRelease(fg55);
  CGColorRelease(dim);
  CGColorRelease(card_fill);
  CGColorRelease(card_hi);
  CGColorRelease(rule);
  CFRelease(t.title);
  CFRelease(t.sub);
  CFRelease(t.value);
  CFRelease(t.hint);
  CFRelease(t.label);
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

  auto measure = [&](const std::string &txt, CTFontRef f) -> CGFloat {
    if (txt.empty())
      return 0;
    CFAttributedStringRef attr = make_attr(txt, f, white, center);
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
