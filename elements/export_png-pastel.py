"""
Xuất từng element trong elements.html ra file PNG riêng (nền trong suốt).

Cài đặt (chạy 1 lần):
    pip install playwright
    playwright install chromium

Chạy:
    python export_png.py

Kết quả: các file PNG sẽ được lưu trong thư mục ./output/
"""

from pathlib import Path
from playwright.sync_api import sync_playwright

HTML_FILE = Path(__file__).parent / "elements-pastel.html"
OUTPUT_DIR = Path(__file__).parent / "output"

ELEMENT_IDS = [
    # windows
    "window-sky", "window-heartlist", "window-musicplayer",
    "titlebar-pink", "titlebar-blue", "titlebar-yellow",
    # window control icons
    "wc-min", "wc-max", "wc-close", "wc-play-blue", "wc-pause-pink",
    "wc-stop-blue", "wc-pause-yellow",
    # media control row
    "btn-rewind-full", "btn-back", "btn-play", "btn-pause", "btn-stop",
    "btn-forward", "btn-forward-full",
    # sparkles/stars
    "sparkle-yellow", "sparkle-white-thin", "sparkle-small-1", "sparkle-small-2",
    "burst-black", "burst-pink", "burst-pink-outline",
    # hearts
    "heart-pink", "heart-blue",
    # file/mail icons
    "icon-envelope", "icon-folder-pink", "icon-folder-blue",
    "icon-document-1", "icon-document-2",
    # buttons/pills/bars
    "search-bar", "btn-yellow-rect", "pill-pink", "pill-blue-small",
    "pill-blue-long", "pill-dashed",
    # speech bubbles
    "bubble-pink", "bubble-yellow",
    # sliders/progress
    "slider-blue", "progress-pink",
    # vertical scrollbars
    "vscroll-outline", "vscroll-pink", "vscroll-blue", "vscroll-yellow",
    # nav/arrow buttons
    "nav-up-pink", "nav-up-yellow", "nav-left-pink", "nav-down-blue",
    "nav-right", "nav-left", "nav-left-blue", "nav-dots",
    # radio dots
    "dot-pink", "dot-blue", "dot-yellow", "dot-white",
    # patterns
    "pattern-x-row", "pattern-plus-row", "pattern-diagonal-stripes",
    "pattern-checker", "pattern-x-scatter",
    # background tiles
    "bg-grid-tile", "bg-grid-large", "bg-grid-decorated",
]


def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    file_url = HTML_FILE.resolve().as_uri()

    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1800, "height": 1400})
        page.goto(file_url)

        for element_id in ELEMENT_IDS:
            try:
                locator = page.locator(f"#{element_id}")
                # bỏ qua label debug màu xanh lá, chỉ chụp phần tử thật bên trong
                target = locator.locator("div, svg").nth(1)
                if target.count() == 0:
                    target = locator

                out_path = OUTPUT_DIR / f"{element_id}.png"
                target.screenshot(path=str(out_path), omit_background=True)
                print(f"✓ Exported {element_id} -> {out_path}")
            except Exception as e:
                print(f"✗ Failed {element_id}: {e}")

        browser.close()

    print("\nHoàn tất! Xem thư mục ./output/")


if __name__ == "__main__":
    main()