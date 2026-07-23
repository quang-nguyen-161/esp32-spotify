"""
Xuất từng element trong elements-pastel.html ra file SVG riêng (vector, có thể
chỉnh sửa lại trong Figma/Illustrator/Inkscape).

Cài đặt (chạy 1 lần):
    pip install playwright
    playwright install chromium

Chạy:
    python export_svg.py

Kết quả: các file .svg sẽ được lưu trong thư mục ./output_svg/

LƯU Ý:
- Với các element đã là SVG thuần (heart, sparkle, pattern, bg-grid...),
  file xuất ra là SVG chuẩn, mở được ở mọi phần mềm.
- Với các element là HTML/CSS (window, button, pill...), script sẽ bọc chúng
  trong thẻ <foreignObject> kèm theo toàn bộ CSS của trang. Cách này hiển thị
  đúng khi mở bằng trình duyệt hoặc Inkscape, nhưng một số phần mềm cũ
  (Illustrator, một số máy in) có thể không hỗ trợ foreignObject đầy đủ.
  Nếu cần SVG "thuần vector" 100% cho các phần tử này, cách chắc ăn nhất là
  vẫn dùng PNG (export_png-pastel.py) hoặc nhờ vẽ lại bằng path SVG thủ công.
"""

from pathlib import Path
from playwright.sync_api import sync_playwright

HTML_FILE = Path(__file__).parent / "elements-pastel.html"
OUTPUT_DIR = Path(__file__).parent / "output_svg"

ELEMENT_IDS = [
    "window-sky", "window-heartlist", "window-musicplayer",
    "titlebar-pink", "titlebar-blue", "titlebar-yellow",
    "wc-min", "wc-max", "wc-close", "wc-play-blue", "wc-pause-pink",
    "wc-stop-blue", "wc-pause-yellow",
    "btn-rewind-full", "btn-back", "btn-play", "btn-pause", "btn-stop",
    "btn-forward", "btn-forward-full",
    "sparkle-yellow", "sparkle-white-thin", "sparkle-small-1", "sparkle-small-2",
    "burst-black", "burst-pink", "burst-pink-outline",
    "heart-pink", "heart-blue",
    "icon-envelope", "icon-folder-pink", "icon-folder-blue",
    "icon-document-1", "icon-document-2",
    "search-bar", "btn-yellow-rect", "pill-pink", "pill-blue-small",
    "pill-blue-long", "pill-dashed",
    "bubble-pink", "bubble-yellow",
    "slider-blue", "progress-pink",
    "vscroll-outline", "vscroll-pink", "vscroll-blue", "vscroll-yellow",
    "nav-up-pink", "nav-up-yellow", "nav-left-pink", "nav-down-blue",
    "nav-right", "nav-left", "nav-left-blue", "nav-dots",
    "dot-pink", "dot-blue", "dot-yellow", "dot-white",
    "pattern-x-row", "pattern-plus-row", "pattern-diagonal-stripes",
    "pattern-checker", "pattern-x-scatter",
    "bg-grid-tile", "bg-grid-large", "bg-grid-decorated",
]


def build_svg_wrapper(inner_html: str, css_text: str, width: float, height: float) -> str:
    """Bọc 1 phần tử HTML/CSS vào SVG dùng foreignObject."""
    return f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xhtml="http://www.w3.org/1999/xhtml"
     width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <foreignObject x="0" y="0" width="{width}" height="{height}">
    <xhtml:div xmlns="http://www.w3.org/1999/xhtml" style="all:initial;">
      <style>{css_text}</style>
      <div style="display:inline-block;">{inner_html}</div>
    </xhtml:div>
  </foreignObject>
</svg>'''


def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    file_url = HTML_FILE.resolve().as_uri()

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1800, "height": 1400})
        page.goto(file_url)

        # Lấy toàn bộ CSS trong <style> của trang để nhúng vào từng SVG
        css_text = page.eval_on_selector("style", "el => el.textContent") or ""

        for element_id in ELEMENT_IDS:
            try:
                locator = page.locator(f"#{element_id}")
                target = locator.locator("div, svg").nth(1)
                if target.count() == 0:
                    target = locator

                tag_name = target.evaluate("el => el.tagName.toLowerCase()")
                box = target.bounding_box()
                width = box["width"] if box else 100
                height = box["height"] if box else 100

                out_path = OUTPUT_DIR / f"{element_id}.svg"

                if tag_name == "svg":
                    # Element vốn đã là SVG -> lấy nguyên outerHTML, chỉ thêm xmlns nếu thiếu
                    svg_html = target.evaluate("el => el.outerHTML")
                    if "xmlns=" not in svg_html:
                        svg_html = svg_html.replace(
                            "<svg", '<svg xmlns="http://www.w3.org/2000/svg"', 1
                        )
                    out_path.write_text(svg_html, encoding="utf-8")
                else:
                    # Element là HTML/CSS -> bọc trong foreignObject
                    inner_html = target.evaluate("el => el.outerHTML")
                    svg_html = build_svg_wrapper(inner_html, css_text, width, height)
                    out_path.write_text(svg_html, encoding="utf-8")

                print(f"✓ Exported {element_id} -> {out_path}")
            except Exception as e:
                print(f"✗ Failed {element_id}: {e}")

        browser.close()

    print("\nHoàn tất! Xem thư mục ./output_svg/")


if __name__ == "__main__":
    main()