# Optional multilingual fonts

Dear ImGui needs glyphs in its font atlas. Prosperous Alchemist discovers Windows fonts for the selected locale and always keeps a default-font fallback. For scripts not installed in Windows, place legally redistributable font files in this directory and list them in the selected locale JSON:

```text
SKSE/Plugins/
├── alchemist.dll
├── fonts/
│   ├── NotoSans-Regular.ttf
│   └── NotoSansCJKsc-Regular.otf
└── locales/
	└── alchemist.zh-cn.json
```

The shipped Chinese, Japanese, Korean, Arabic, and Hindi examples name common Noto font filenames but do not include font binaries. Obtain fonts from their official distribution and comply with their license before copying them here. The plugin loads only paths below this directory, merges every available font over the selected multilingual glyph ranges, and skips missing or unsupported files.

Useful font families include:

- `NotoSans-Regular.ttf` for Latin, Greek, and Cyrillic coverage.
- `NotoSansCJKsc-Regular.otf`, `NotoSansCJKtc-Regular.otf`, and `NotoSansCJKjp-Regular.otf` for Chinese and Japanese glyphs.
- `NotoSansCJKkr-Regular.otf` for Korean glyphs.
- `NotoSansArabic-Regular.ttf` for Arabic.
- `NotoSansDevanagari-Regular.ttf` for Hindi and other Devanagari text.

Fonts are optional. If none load, the plugin still opens with Dear ImGui's default font and English fallback text instead of failing the render hook.
