# Prosperous Alchemist localization resources

Locale files are loaded at startup from the directory beside `alchemist.dll`:

```text
SKSE/Plugins/
├── alchemist.dll
└── locales/
	└── alchemist.zh-cn.json
```

Use a BCP 47 language tag in the filename. Tags are case-insensitive; examples include `alchemist.en.json`, `alchemist.fr.json`, `alchemist.zh-cn.json`, `alchemist.zh-tw.json`, `alchemist.ja.json`, and `alchemist.ko.json`. The plugin also accepts an explicit `Language` value in `[Localization]` in `alchemist.ini`; an empty value or `auto` follows the Windows user locale.

The loader tries the English resource first, then the base language, then the exact selected tag. Missing keys, invalid values, malformed JSON, or unavailable fonts fall back without preventing the overlay from opening. A resource may therefore translate only the keys it needs.

The supported schema is UTF-8 JSON:

```json
{
  "locale": "fr",
  "languageName": "Français",
  "fonts": ["NotoSans-Regular.ttf"],
  "strings": {
	"ui.settings": "Paramètres"
  }
}
```

`strings` values are UTF-8 display strings. Format strings use named placeholders such as `{count}`, `{page}`, `{pages}`, `{phase}`, `{current}`, `{total}`, `{percent}`, and `{reason}`. Keep those placeholders when translating the corresponding message.

The built-in English fallback is authoritative for keys not present in a custom resource. User translations can be added or replaced without rebuilding the plugin; restart Skyrim after changing a resource.
