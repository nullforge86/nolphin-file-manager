/* nolphin-markdown-view.c
 *
 * Eigenständiger Markdown-Renderer auf Basis von GtkTextBuffer/GtkTextTag
 * (siehe Kopfdatei). Blockstruktur wird zeilenweise
 * erkannt, Inline-Auszeichnungen rekursiv über die Absatztexte.
 */

#include "nolphin-markdown-view.h"

#include <glib/gstdio.h>
#include <string.h>

#define HREF_KEY "nolphin-md-href"
#define HANDLER_KEY "nolphin-md-handler"
#define TABLE_KEY "nolphin-md-table"
#define CHECK_KEY "nolphin-md-check"
#define DECOR_KEY "nolphin-md-decor"
#define KIND_KEY "nolphin-md-kind"
#define IMG_W_KEY "nolphin-md-image-w"
#define IMG_H_KEY "nolphin-md-image-h"
#define TOGGLE_KEY "nolphin-md-toggle"
#define IMG_PATH_KEY "nolphin-md-image-path"
#define IMG_ALT_KEY "nolphin-md-image-alt"
#define KIDS_KEY "nolphin-md-children"
/* Linker Rand der Ansicht (gtk_text_view_set_left_margin): Ein Tag-Rand
 * ersetzt ihn, statt ihn zu ergänzen, deshalb rechnen alle Tags ihn mit. */
#define VIEW_MARGIN 8

/* Größere Bilddateien werden nicht geladen (Alternativtext statt Bild). */
#define IMAGE_MAX_BYTES (16 * 1024 * 1024)

typedef struct {
	GtkTextBuffer *buffer;
	const gchar   *base_dir;   /* Ordner für relative Bildpfade oder NULL */
} Ctx;

typedef struct {
	NolphinMarkdownLinkFunc func;
	gpointer                data;
} LinkHandler;

/* Zeichen-Dekorationen hinter/über dem Text (GtkTextView kann weder Rahmen
 * noch Hintergründe über mehrere Zeilen): Code-Kasten, Zitat-Balken,
 * Linie unter H1/H2 und horizontale Linie. Offsets sind Zeichenpositionen
 * im (danach unveränderten) Puffer; @end ist das letzte Zeichen des Blocks. */
typedef enum { DECOR_BOX, DECOR_BAR, DECOR_RULE, DECOR_HR } DecorType;

typedef struct {
	DecorType type;
	gint      start;
	gint      end;
	gint      level;
} Decor;

/* Aufklappbarer Abschnitt (<details>): Inhalt und die beiden Pfeile. */
typedef struct {
	GtkTextTag *content;
	GtkTextTag *closed;   /* Pfeil "zu" */
	GtkTextTag *open;     /* Pfeil "auf" */
} DetInfo;

/* Tabelle (Spalten, Ausrichtung 0 links / 1 Mitte / 2 rechts, Zeilen als
 * NULL-terminierte Listen von Pango-Markup, Zeile 0 ist die Kopfzeile). */
typedef struct {
	guint      ncols;
	gint      *aligns;
	GPtrArray *rows;
} TableData;

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

static void
setup_tags (GtkTextBuffer *buffer)
{
	/* Größenverhältnisse und Abstände wie bei GitHub (16px Basis): H1 2.0,
	 * H2 1.5, H3 1.25, H4 1.0, H5 0.875, H6 0.85; oben 24px, unten 16px;
	 * bei H1/H2 kommt die Linie dazu (siehe DECOR_RULE). */
	static const struct { const gchar *name; gdouble scale; gint above; gint below; } headings[] = {
		{ "h1", 2.0,   24, 27 }, { "h2", 1.5,   24, 24 }, { "h3", 1.25,  24, 16 },
		{ "h4", 1.0,   24, 16 }, { "h5", 0.875, 24, 16 }, { "h6", 0.85,  24, 16 },
	};
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table (buffer);
	guint i;

	if (gtk_text_tag_table_lookup (table, "bold") != NULL)
		return;

	for (i = 0; i < G_N_ELEMENTS (headings); i++) {
		gtk_text_buffer_create_tag (buffer, headings[i].name,
					    "scale", headings[i].scale,
					    "weight", PANGO_WEIGHT_SEMIBOLD,
					    "pixels-above-lines", headings[i].above,
					    "pixels-below-lines", headings[i].below,
					    "wrap-mode", GTK_WRAP_WORD_CHAR,
					    NULL);
	}
	gtk_text_buffer_create_tag (buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
	gtk_text_buffer_create_tag (buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
	gtk_text_buffer_create_tag (buffer, "strike", "strikethrough", TRUE, NULL);
	gtk_text_buffer_create_tag (buffer, "code", "family", "monospace", "scale", 0.85, NULL);
	/* Code-Kasten: Innenabstand 16px; der Kasten selbst wird gezeichnet. */
	gtk_text_buffer_create_tag (buffer, "codeblock",
				    "family", "monospace",
				    "scale", 0.85,
				    "left-margin", VIEW_MARGIN + 16,
				    "right-margin", VIEW_MARGIN + 16,
				    "wrap-mode", GTK_WRAP_CHAR,
				    NULL);
	gtk_text_buffer_create_tag (buffer, "code-first", "pixels-above-lines", 16, NULL);
	gtk_text_buffer_create_tag (buffer, "code-last", "pixels-below-lines", 32, NULL);
	gtk_text_buffer_create_tag (buffer, "link", "underline", PANGO_UNDERLINE_SINGLE, NULL);
	/* HTML-Teilmenge: <kbd>, <sub>, <sup>, <u> und Ausrichtung */
	gtk_text_buffer_create_tag (buffer, "kbd", "family", "monospace", "scale", 0.8, NULL);
	gtk_text_buffer_create_tag (buffer, "sub", "rise", -3 * PANGO_SCALE, "scale", 0.8, NULL);
	gtk_text_buffer_create_tag (buffer, "sup", "rise", 5 * PANGO_SCALE, "scale", 0.8, NULL);
	gtk_text_buffer_create_tag (buffer, "u-html", "underline", PANGO_UNDERLINE_SINGLE, NULL);
	gtk_text_buffer_create_tag (buffer, "jc", "justification", GTK_JUSTIFY_CENTER, NULL);
	gtk_text_buffer_create_tag (buffer, "jr", "justification", GTK_JUSTIFY_RIGHT, NULL);
	gtk_text_buffer_create_tag (buffer, "quote", NULL);
	/* Zeile der horizontalen Linie: winzige Schrift, Rand 24px; die Linie wird gezeichnet. */
	gtk_text_buffer_create_tag (buffer, "hr",
				    "size", 1 * PANGO_SCALE,
				    "pixels-above-lines", 23,
				    "pixels-below-lines", 23,
				    NULL);
	gtk_text_buffer_create_tag (buffer, "dim", NULL);
}

static GtkTextTag *
T (Ctx *c, const gchar *name)
{
	return gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (c->buffer), name);
}

/* Absatz-Tag mit linkem Rand (ab Textrand), Erstzeileneinzug und Abstand
 * nach unten; wird einmal angelegt und danach wiederverwendet. */
static GtkTextTag *
para_tag (Ctx *c, gint left, gint indent, gint below)
{
	gchar *name = g_strdup_printf ("p-%d-%d-%d", left, indent, below);
	GtkTextTag *tag = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (c->buffer), name);

	if (tag == NULL) {
		tag = gtk_text_buffer_create_tag (c->buffer, name,
						  "left-margin", VIEW_MARGIN + left,
						  "indent", indent,
						  "pixels-below-lines", below,
						  "wrap-mode", GTK_WRAP_WORD_CHAR,
						  NULL);
	}
	g_free (name);
	return tag;
}

static GtkTextTag *
make_link_tag (Ctx *c, const gchar *href, gsize len)
{
	GtkTextTag *tag = gtk_text_buffer_create_tag (c->buffer, NULL, NULL);

	g_object_set_data_full (G_OBJECT (tag), HREF_KEY, g_strndup (href, len), g_free);
	return tag;
}

const gchar *
nolphin_markdown_tag_get_href (GtkTextTag *tag)
{
	return tag != NULL ? g_object_get_data (G_OBJECT (tag), HREF_KEY) : NULL;
}

/* ------------------------------------------------------------------ */
/* Inline                                                              */
/* ------------------------------------------------------------------ */

static gint
buffer_length (Ctx *c)
{
	return gtk_text_buffer_get_char_count (c->buffer);
}

static void
insert_run (Ctx *c, const gchar *s, const gchar *e, GPtrArray *tags)
{
	GtkTextIter it, a;
	gint start;
	guint i;

	if (e <= s)
		return;
	gtk_text_buffer_get_end_iter (c->buffer, &it);
	start = gtk_text_iter_get_offset (&it);
	gtk_text_buffer_insert (c->buffer, &it, s, (gint) (e - s));
	gtk_text_buffer_get_iter_at_offset (c->buffer, &a, start);
	gtk_text_buffer_get_end_iter (c->buffer, &it);
	for (i = 0; i < tags->len; i++)
		gtk_text_buffer_apply_tag (c->buffer, g_ptr_array_index (tags, i), &a, &it);
}

static void
insert_literal (Ctx *c, const gchar *text, GPtrArray *tags)
{
	insert_run (c, text, text + strlen (text), tags);
}

/* Ende eines Code-Spans, der an @p (nach den Backticks) beginnt, oder NULL. */
static const gchar *
find_code_close (const gchar *p, const gchar *end, gint n)
{
	const gchar *q = p;

	while (q < end) {
		if (*q == '`') {
			gint run = 0;
			while (q + run < end && q[run] == '`')
				run++;
			if (run == n)
				return q;
			q += run;
		} else {
			q++;
		}
	}
	return NULL;
}

static const gchar *
find_emph_close (const gchar *p, const gchar *end, gchar ch, gint n)
{
	const gchar *q = p;

	while (q < end) {
		if (*q == '\\' && q + 1 < end) {
			q += 2;
			continue;
		}
		if (*q == '`') {
			gint run = 0;
			const gchar *close;
			while (q + run < end && q[run] == '`')
				run++;
			close = find_code_close (q + run, end, run);
			q = close != NULL ? close + run : q + run;
			continue;
		}
		if (*q == ch) {
			gint run = 0;
			while (q + run < end && q[run] == ch)
				run++;
			if (run == n && q > p && !g_ascii_isspace (q[-1])) {
				if (ch == '_' && q + n < end && g_ascii_isalnum (q[n])) {
					q += run;
					continue;
				}
				return q;
			}
			q += run;
			continue;
		}
		q++;
	}
	return NULL;
}

/* Passende schließende Klammer zu @open an @p (zeigt auf die öffnende). */
static const gchar *
find_matching (const gchar *p, const gchar *end, gchar open, gchar close)
{
	gint depth = 0;
	const gchar *q;

	for (q = p; q < end; q++) {
		if (*q == '\\' && q + 1 < end) {
			q++;
		} else if (*q == open) {
			depth++;
		} else if (*q == close) {
			if (--depth == 0)
				return q;
		}
	}
	return NULL;
}

/* Pfad eines lokalen Bildes innerhalb von base_dir oder NULL (entfernte
 * Adressen, Pfade außerhalb des Ordners, fehlende Dateien). */
static gchar *
resolve_local_image (Ctx *c, const gchar *src, gsize len)
{
	gchar *raw, *unescaped, *full, *canon, *base_canon, *result = NULL;
	gchar *scheme, *cut;
	const gchar *rel;

	if (c->base_dir == NULL || len == 0)
		return NULL;
	raw = g_strndup (src, len);
	scheme = g_uri_parse_scheme (raw);
	if (scheme != NULL) {
		g_free (scheme);
		g_free (raw);
		return NULL;
	}
	cut = strpbrk (raw, "#?");
	if (cut != NULL)
		*cut = '\0';
	unescaped = g_uri_unescape_string (raw, NULL);
	g_free (raw);
	if (unescaped == NULL || *unescaped == '\0') {
		g_free (unescaped);
		return NULL;
	}
	rel = unescaped;
	while (*rel == '/')
		rel++;
	full = g_build_filename (c->base_dir, rel, NULL);
	canon = g_canonicalize_filename (full, NULL);
	base_canon = g_canonicalize_filename (c->base_dir, NULL);
	if (g_str_has_prefix (canon, base_canon) &&
	    (canon[strlen (base_canon)] == G_DIR_SEPARATOR || base_canon[strlen (base_canon) - 1] == G_DIR_SEPARATOR) &&
	    g_file_test (canon, G_FILE_TEST_IS_REGULAR))
		result = g_strdup (canon);
	g_free (unescaped);
	g_free (full);
	g_free (canon);
	g_free (base_canon);
	return result;
}

/* Setzt einen Anker für ein lokales Bild; die Ansicht hängt das Widget ein. */
static void
insert_image_anchor (Ctx *c, const gchar *path, const gchar *alt, gsize alt_len, GPtrArray *tags,
		     const gchar *width_attr, const gchar *height_attr)
{
	GtkTextIter it, a;
	GtkTextChildAnchor *anchor;
	gint start;
	guint i;

	gtk_text_buffer_get_end_iter (c->buffer, &it);
	start = gtk_text_iter_get_offset (&it);
	anchor = gtk_text_buffer_create_child_anchor (c->buffer, &it);
	g_object_set_data_full (G_OBJECT (anchor), IMG_PATH_KEY, g_strdup (path), g_free);
	g_object_set_data_full (G_OBJECT (anchor), IMG_ALT_KEY, g_strndup (alt, alt_len), g_free);
	if (width_attr != NULL && *width_attr != '\0')
		g_object_set_data_full (G_OBJECT (anchor), IMG_W_KEY, g_strdup (width_attr), g_free);
	if (height_attr != NULL && *height_attr != '\0')
		g_object_set_data_full (G_OBJECT (anchor), IMG_H_KEY, g_strdup (height_attr), g_free);
	gtk_text_buffer_get_iter_at_offset (c->buffer, &a, start);
	gtk_text_buffer_get_end_iter (c->buffer, &it);
	for (i = 0; i < tags->len; i++)
		gtk_text_buffer_apply_tag (c->buffer, g_ptr_array_index (tags, i), &a, &it);
}

/* ------------------------------------------------------------------ */
/* HTML-Teilmenge und Entities                                         */
/* ------------------------------------------------------------------ */

typedef struct {
	const gchar *name;      /* Tag-Name (nicht nullterminiert) */
	gsize        name_len;
	gboolean     closing;   /* </name> */
	const gchar *attrs;     /* Attribute: [attrs, attrs_end) */
	const gchar *attrs_end;
	const gchar *tag_end;   /* erstes Zeichen nach '>' */
	gboolean     self_closing;
} HtmlTag;

static gboolean
parse_html_tag (const gchar *p, const gchar *end, HtmlTag *t)
{
	const gchar *q = p + 1;
	gchar quote = 0;

	if (p >= end || *p != '<' || q >= end)
		return FALSE;
	t->closing = FALSE;
	if (*q == '/') {
		t->closing = TRUE;
		q++;
	}
	if (q >= end || !g_ascii_isalpha (*q))
		return FALSE;
	t->name = q;
	while (q < end && (g_ascii_isalnum (*q) || *q == '-'))
		q++;
	t->name_len = (gsize) (q - t->name);
	t->attrs = q;
	for (; q < end; q++) {
		if (quote != 0) {
			if (*q == quote)
				quote = 0;
		} else if (*q == '"' || *q == '\'') {
			quote = *q;
		} else if (*q == '>') {
			break;
		}
	}
	if (q >= end)
		return FALSE;
	t->attrs_end = q;
	t->tag_end = q + 1;
	t->self_closing = (q > t->attrs && q[-1] == '/');
	return TRUE;
}

static gboolean
html_tag_is (const HtmlTag *t, const gchar *name)
{
	return strlen (name) == t->name_len && g_ascii_strncasecmp (t->name, name, t->name_len) == 0;
}

/* Wert eines Attributs (neu angelegt) oder NULL; Attribute ohne Wert liefern "". */
static gchar *
html_attr (const HtmlTag *t, const gchar *attr)
{
	const gchar *p = t->attrs, *e = t->attrs_end;
	gsize al = strlen (attr);

	while (p < e) {
		const gchar *ns;
		gsize nl;
		gchar *val = NULL;

		while (p < e && (g_ascii_isspace (*p) || *p == '/'))
			p++;
		ns = p;
		while (p < e && !g_ascii_isspace (*p) && *p != '=' && *p != '/')
			p++;
		nl = (gsize) (p - ns);
		while (p < e && g_ascii_isspace (*p))
			p++;
		if (p < e && *p == '=') {
			const gchar *vs;

			p++;
			while (p < e && g_ascii_isspace (*p))
				p++;
			if (p < e && (*p == '"' || *p == '\'')) {
				gchar q = *p++;

				vs = p;
				while (p < e && *p != q)
					p++;
				val = g_strndup (vs, (gsize) (p - vs));
				if (p < e)
					p++;
			} else {
				vs = p;
				while (p < e && !g_ascii_isspace (*p))
					p++;
				val = g_strndup (vs, (gsize) (p - vs));
			}
		}
		if (nl > 0 && nl == al && g_ascii_strncasecmp (ns, attr, nl) == 0)
			return val != NULL ? val : g_strdup ("");
		g_free (val);
	}
	return NULL;
}

/* Schließendes Tag zu @open ab @from (Verschachtelung gleichnamiger Tags
 * wird gezählt); liefert die Stelle des '<' oder NULL, @close_out bekommt
 * das schließende Tag. */
static const gchar *
find_close_tag (const gchar *from, const gchar *end, const HtmlTag *open, HtmlTag *close_out)
{
	const gchar *q = from;
	gint depth = 1;

	while (q < end) {
		HtmlTag t;
		const gchar *lt = memchr (q, '<', (gsize) (end - q));

		if (lt == NULL)
			return NULL;
		if (parse_html_tag (lt, end, &t) && t.name_len == open->name_len &&
		    g_ascii_strncasecmp (t.name, open->name, t.name_len) == 0) {
			if (t.closing) {
				if (--depth == 0) {
					*close_out = t;
					return lt;
				}
			} else if (!t.self_closing) {
				depth++;
			}
			q = t.tag_end;
		} else {
			q = lt + 1;
		}
	}
	return NULL;
}

/* "&name;", "&#N;", "&#xH;" → UTF-8 (neu angelegt) und Stelle dahinter, sonst NULL. */
static gchar *
decode_entity (const gchar *p, const gchar *end, const gchar **next)
{
	static const struct { const gchar *name; const gchar *text; } named[] = {
		{ "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" },
		{ "nbsp", " " }, { "copy", "©" }, { "reg", "®" }, { "trade", "™" },
		{ "hellip", "…" }, { "mdash", "—" }, { "ndash", "–" },
		{ "laquo", "«" }, { "raquo", "»" }, { "middot", "·" }, { "bull", "•" },
		{ "rarr", "→" }, { "larr", "←" }, { "uarr", "↑" }, { "darr", "↓" },
		{ "times", "×" }, { "deg", "°" }, { "check", "✓" },
	};
	const gchar *semi = memchr (p, ';', MIN ((gsize) (end - p), (gsize) 12));
	gsize len;
	guint i;

	if (semi == NULL || semi < p + 2)
		return NULL;
	len = (gsize) (semi - p - 1);
	if (p[1] == '#') {
		gunichar ch = 0;
		gchar *tail = NULL;
		gchar *num = g_strndup (p + 2, len - 1);

		ch = (num[0] == 'x' || num[0] == 'X') ? (gunichar) g_ascii_strtoull (num + 1, &tail, 16)
						      : (gunichar) g_ascii_strtoull (num, &tail, 10);
		if (tail == NULL || *tail != '\0' || !g_unichar_validate (ch) || ch == 0) {
			g_free (num);
			return NULL;
		}
		g_free (num);
		*next = semi + 1;
		{
			gchar buf[8];
			gint n = g_unichar_to_utf8 (ch, buf);

			return g_strndup (buf, (gsize) n);
		}
	}
	for (i = 0; i < G_N_ELEMENTS (named); i++) {
		if (strlen (named[i].name) == len && strncmp (p + 1, named[i].name, len) == 0) {
			*next = semi + 1;
			return g_strdup (named[i].text);
		}
	}
	return NULL;
}

static void render_inline (Ctx *c, const gchar *s, const gchar *end, GPtrArray *tags);

/* Ein HTML-Element im Fließtext. Liefert die Stelle hinter dem verarbeiteten
 * Teil oder NULL, wenn das Tag nicht unterstützt wird (der Aufrufer entfernt
 * es dann). Der Text vor @p ist bereits eingefügt. */
static const gchar *
html_inline (Ctx *c, const gchar *p, const gchar *end, GPtrArray *tags)
{
	HtmlTag t, close;
	const gchar *close_at;
	const gchar *fmt = NULL;
	gboolean plain = FALSE, is_link = FALSE;
	guint pushed = 0;
	gchar *href = NULL;

	if (!parse_html_tag (p, end, &t))
		return NULL;
	if (t.closing)
		return t.tag_end;   /* verwaistes schließendes Tag: weglassen */

	if (html_tag_is (&t, "br")) {
		insert_literal (c, "\n", tags);
		return t.tag_end;
	}
	if (html_tag_is (&t, "wbr") || html_tag_is (&t, "hr"))
		return t.tag_end;
	if (html_tag_is (&t, "li")) {
		insert_literal (c, "\n• ", tags);
		return t.tag_end;
	}
	if (html_tag_is (&t, "img")) {
		gchar *src = html_attr (&t, "src"), *alt = html_attr (&t, "alt");
		gchar *w = html_attr (&t, "width"), *h = html_attr (&t, "height");
		gchar *img = src != NULL ? resolve_local_image (c, src, strlen (src)) : NULL;

		if (img != NULL) {
			insert_image_anchor (c, img, alt != NULL ? alt : "", alt != NULL ? strlen (alt) : 0, tags, w, h);
		} else if (alt != NULL && *alt != '\0') {
			g_ptr_array_add (tags, T (c, "italic"));
			insert_literal (c, alt, tags);
			g_ptr_array_remove_index (tags, tags->len - 1);
		}
		g_free (img);
		g_free (src);
		g_free (alt);
		g_free (w);
		g_free (h);
		return t.tag_end;
	}

	if (html_tag_is (&t, "b") || html_tag_is (&t, "strong"))
		fmt = "bold";
	else if (html_tag_is (&t, "i") || html_tag_is (&t, "em") || html_tag_is (&t, "cite"))
		fmt = "italic";
	else if (html_tag_is (&t, "s") || html_tag_is (&t, "del") || html_tag_is (&t, "strike"))
		fmt = "strike";
	else if (html_tag_is (&t, "u") || html_tag_is (&t, "ins"))
		fmt = "u-html";
	else if (html_tag_is (&t, "code") || html_tag_is (&t, "tt"))
		fmt = "code";
	else if (html_tag_is (&t, "kbd"))
		fmt = "kbd";
	else if (html_tag_is (&t, "sub"))
		fmt = "sub";
	else if (html_tag_is (&t, "sup"))
		fmt = "sup";
	else if (html_tag_is (&t, "a"))
		is_link = TRUE;
	else if (html_tag_is (&t, "span") || html_tag_is (&t, "mark") || html_tag_is (&t, "small") || html_tag_is (&t, "big") ||
		 html_tag_is (&t, "font") || html_tag_is (&t, "abbr") || html_tag_is (&t, "q"))
		plain = TRUE;
	else
		return NULL;

	close_at = find_close_tag (t.tag_end, end, &t, &close);
	if (close_at == NULL)
		return t.tag_end;   /* nicht geschlossen: nur das Tag weglassen */

	if (is_link) {
		href = html_attr (&t, "href");
		if (href != NULL && *href != '\0') {
			g_ptr_array_add (tags, T (c, "link"));
			g_ptr_array_add (tags, make_link_tag (c, href, strlen (href)));
			pushed = 2;
		}
		g_free (href);
	} else if (fmt != NULL) {
		g_ptr_array_add (tags, T (c, fmt));
		pushed = 1;
	}
	(void) plain;
	render_inline (c, t.tag_end, close_at, tags);
	g_ptr_array_set_size (tags, tags->len - pushed);
	return close.tag_end;
}

static void
render_inline (Ctx *c, const gchar *s, const gchar *end, GPtrArray *tags)
{
	const gchar *p = s, *run = s;

	while (p < end) {
		gchar ch = *p;

		if (ch == '\\' && p + 1 < end && p[1] == '\n') {
			insert_run (c, run, p, tags);
			insert_literal (c, "\n", tags);
			p += 2;
			run = p;
			continue;
		}
		if (ch == '\\' && p + 1 < end && strchr ("\\`*_{}[]()#+-.!~|<>", p[1]) != NULL) {
			insert_run (c, run, p, tags);
			run = p + 1;
			p += 2;
			continue;
		}
		if (ch == '\n') {
			gboolean hard = (p - run >= 2 && p[-1] == ' ' && p[-2] == ' ');

			insert_run (c, run, hard ? p - 2 : p, tags);
			insert_literal (c, hard ? "\n" : " ", tags);
			p++;
			run = p;
			continue;
		}
		if (ch == '`') {
			gint n = 0;
			const gchar *close;

			while (p + n < end && p[n] == '`')
				n++;
			close = find_code_close (p + n, end, n);
			if (close != NULL) {
				const gchar *cs = p + n, *ce = close;

				insert_run (c, run, p, tags);
				if (ce - cs >= 2 && *cs == ' ' && ce[-1] == ' ') {
					cs++;
					ce--;
				}
				g_ptr_array_add (tags, T (c, "code"));
				insert_run (c, cs, ce, tags);
				g_ptr_array_remove_index (tags, tags->len - 1);
				p = close + n;
				run = p;
			} else {
				p += n;
			}
			continue;
		}
		if (ch == '*' || ch == '_' || ch == '~') {
			gint n = 0, use;
			const gchar *close;
			gboolean open_ok;

			while (p + n < end && p[n] == ch)
				n++;
			if (ch == '~')
				use = (n >= 2) ? 2 : 0;
			else
				use = MIN (n, 3);
			open_ok = use > 0 && p + use < end && !g_ascii_isspace (p[use]);
			if (open_ok && ch == '_' && p > s && g_ascii_isalnum (p[-1]))
				open_ok = FALSE;
			close = open_ok ? find_emph_close (p + use, end, ch, use) : NULL;
			if (close != NULL && close > p + use) {
				insert_run (c, run, p, tags);
				if (ch == '~') {
					g_ptr_array_add (tags, T (c, "strike"));
				} else {
					if (use >= 2)
						g_ptr_array_add (tags, T (c, "bold"));
					if (use == 1 || use == 3)
						g_ptr_array_add (tags, T (c, "italic"));
				}
				render_inline (c, p + use, close, tags);
				g_ptr_array_set_size (tags, tags->len - (use == 3 ? 2 : 1));
				p = close + use;
				run = p;
			} else {
				p += n;
			}
			continue;
		}
		if (ch == '[' || (ch == '!' && p + 1 < end && p[1] == '[')) {
			gboolean image = (ch == '!');
			const gchar *open = image ? p + 1 : p;
			const gchar *close = find_matching (open, end, '[', ']');

			if (close != NULL && close + 1 < end && close[1] == '(') {
				const gchar *dest_close = find_matching (close + 1, end, '(', ')');

				if (dest_close != NULL) {
					const gchar *ds = close + 2, *de = dest_close;

					while (ds < de && g_ascii_isspace (*ds))
						ds++;
					if (ds < de && *ds == '<') {
						const gchar *gt = memchr (ds, '>', de - ds);
						ds++;
						de = gt != NULL ? gt : de;
					} else {
						const gchar *sp = ds;
						while (sp < de && !g_ascii_isspace (*sp))
							sp++;
						de = sp;
					}
					insert_run (c, run, p, tags);
					if (image) {
						gchar *img = resolve_local_image (c, ds, de - ds);

						if (img != NULL) {
							insert_image_anchor (c, img, open + 1, close - open - 1, tags, NULL, NULL);
							g_free (img);
						} else {
							/* Entfernte oder nicht auffindbare Bilder: Alternativtext, kursiv. */
							g_ptr_array_add (tags, T (c, "italic"));
							render_inline (c, open + 1, close, tags);
							g_ptr_array_remove_index (tags, tags->len - 1);
						}
					} else {
						g_ptr_array_add (tags, T (c, "link"));
						g_ptr_array_add (tags, make_link_tag (c, ds, de - ds));
						render_inline (c, open + 1, close, tags);
						g_ptr_array_set_size (tags, tags->len - 2);
					}
					p = dest_close + 1;
					run = p;
					continue;
				}
			}
			p++;
			continue;
		}
		if (ch == '<') {
			const gchar *gt = memchr (p, '>', end - p);

			if (gt != NULL && gt > p + 1) {
				gsize len = gt - p - 1;
				const gchar *in = p + 1;

				if ((len > 7 && strncmp (in, "http://", 7) == 0) ||
				    (len > 8 && strncmp (in, "https://", 8) == 0) ||
				    (len > 7 && strncmp (in, "mailto:", 7) == 0)) {
					insert_run (c, run, p, tags);
					g_ptr_array_add (tags, T (c, "link"));
					g_ptr_array_add (tags, make_link_tag (c, in, len));
					insert_run (c, in, gt, tags);
					g_ptr_array_set_size (tags, tags->len - 2);
					p = gt + 1;
					run = p;
					continue;
				}
				if (g_ascii_isalpha (*in) || *in == '/' || *in == '!') {
					const gchar *next;

					insert_run (c, run, p, tags);
					run = p;
					next = html_inline (c, p, end, tags);
					if (next == NULL)
						next = gt + 1;   /* unbekanntes Tag: weglassen, Text bleibt */
					p = next;
					run = p;
					continue;
				}
			}
			p++;
			continue;
		}
		if (ch == '&') {
			const gchar *next = NULL;
			gchar *decoded = decode_entity (p, end, &next);

			if (decoded != NULL) {
				insert_run (c, run, p, tags);
				insert_literal (c, decoded, tags);
				g_free (decoded);
				p = next;
				run = p;
				continue;
			}
		}
		if (ch == 'h' && (p == s || !g_ascii_isalnum (p[-1])) &&
		    (strncmp (p, "http://", MIN (7, end - p)) == 0 || strncmp (p, "https://", MIN (8, end - p)) == 0) &&
		    end - p > 8) {
			const gchar *ue = p;

			while (ue < end && !g_ascii_isspace (*ue) && *ue != '<')
				ue++;
			while (ue > p && strchr (".,;:!?)", ue[-1]) != NULL)
				ue--;
			if (ue - p > 8 && (strncmp (p, "http://", 7) == 0 || strncmp (p, "https://", 8) == 0)) {
				insert_run (c, run, p, tags);
				g_ptr_array_add (tags, T (c, "link"));
				g_ptr_array_add (tags, make_link_tag (c, p, ue - p));
				insert_run (c, p, ue, tags);
				g_ptr_array_set_size (tags, tags->len - 2);
				p = ue;
				run = p;
				continue;
			}
		}
		p++;
	}
	insert_run (c, run, end, tags);
}

/* ------------------------------------------------------------------ */
/* Blöcke                                                              */
/* ------------------------------------------------------------------ */

static const gchar *
skip_spaces (const gchar *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static gboolean
is_blank (const gchar *line)
{
	return *skip_spaces (line) == '\0';
}

static gboolean
is_fence (const gchar *line, gchar *ch, gint *len)
{
	const gchar *s = skip_spaces (line);
	gint n = 0;

	if (*s != '`' && *s != '~')
		return FALSE;
	*ch = *s;
	while (s[n] == *ch)
		n++;
	if (n < 3)
		return FALSE;
	/* Info-String nach ``` darf keine weiteren Backticks enthalten. */
	if (*ch == '`' && strchr (s + n, '`') != NULL)
		return FALSE;
	*len = n;
	return TRUE;
}

static gint
heading_level (const gchar *line, const gchar **text)
{
	const gchar *s = line;
	gint n = 0;

	while (n < 3 && *s == ' ') {
		s++;
		n++;
	}
	n = 0;
	while (*s == '#') {
		s++;
		n++;
	}
	if (n < 1 || n > 6 || (*s != ' ' && *s != '\t' && *s != '\0'))
		return 0;
	*text = skip_spaces (s);
	return n;
}

static gboolean
is_hr (const gchar *line)
{
	const gchar *s = skip_spaces (line);
	gchar ch = *s;
	gint n = 0;

	/* HTML: <hr>, <hr/>, <hr /> allein in der Zeile */
	if (g_ascii_strncasecmp (s, "<hr", 3) == 0) {
		const gchar *q = s + 3;

		while (*q == ' ')
			q++;
		if (*q == '/')
			q++;
		if (*q == '>')
			return is_blank (q + 1);
		return FALSE;
	}

	if (ch != '-' && ch != '*' && ch != '_')
		return FALSE;
	for (; *s != '\0'; s++) {
		if (*s == ch)
			n++;
		else if (*s != ' ' && *s != '\t')
			return FALSE;
	}
	return n >= 3;
}

/* "===" bzw. "---" unter einem Absatz (Setext-Überschrift): Ebene oder 0. */
static gint
setext_level (const gchar *line)
{
	const gchar *s = skip_spaces (line);
	gchar ch = *s;
	gint n = 0;

	if (ch != '=' && ch != '-')
		return 0;
	while (*s == ch) {
		s++;
		n++;
	}
	if (!is_blank (s) || n < 1)
		return 0;
	return ch == '=' ? 1 : 2;
}

static gboolean
is_list_item (const gchar *line, gint *indent, gchar *marker_out, const gchar **number, gint *number_len, const gchar **content)
{
	const gchar *s = line;
	gint ind = 0;

	while (*s == ' ' || *s == '\t') {
		ind += (*s == '\t') ? 4 : 1;
		s++;
	}
	if ((*s == '-' || *s == '*' || *s == '+') && (s[1] == ' ' || s[1] == '\t')) {
		if (is_hr (line))
			return FALSE;
		*marker_out = *s;
		*number = NULL;
		*number_len = 0;
		*content = skip_spaces (s + 1);
		*indent = ind;
		return TRUE;
	}
	if (g_ascii_isdigit (*s)) {
		const gchar *d = s;

		while (g_ascii_isdigit (*d) && d - s < 9)
			d++;
		if ((*d == '.' || *d == ')') && (d[1] == ' ' || d[1] == '\t')) {
			*marker_out = *d;
			*number = s;
			*number_len = (gint) (d - s);
			*content = skip_spaces (d + 1);
			*indent = ind;
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
is_table_sep (const gchar *line)
{
	const gchar *s = line;
	gboolean dash = FALSE;

	if (strchr (line, '|') == NULL && strchr (line, '-') == NULL)
		return FALSE;
	for (; *s != '\0'; s++) {
		if (*s == '-')
			dash = TRUE;
		else if (*s != '|' && *s != ':' && *s != ' ' && *s != '\t')
			return FALSE;
	}
	return dash && strchr (line, '|') != NULL;
}

/* Zerlegt eine Tabellenzeile in Zellen ("\|" bleibt ein Zeichen "|"). */
static gchar **
split_cells (const gchar *line)
{
	gchar *trimmed = g_strstrip (g_strdup (line));
	GPtrArray *cells = g_ptr_array_new ();
	GString *cur = g_string_new (NULL);
	const gchar *p = trimmed;
	gboolean ended_with_pipe = FALSE;

	if (*p == '|')
		p++;
	for (; *p != '\0'; p++) {
		ended_with_pipe = FALSE;
		if (*p == '\\' && p[1] == '|') {
			g_string_append_c (cur, '|');
			p++;
		} else if (*p == '|') {
			g_ptr_array_add (cells, g_strstrip (g_string_free (cur, FALSE)));
			cur = g_string_new (NULL);
			ended_with_pipe = TRUE;
		} else {
			g_string_append_c (cur, *p);
		}
	}
	if (!ended_with_pipe)
		g_ptr_array_add (cells, g_strstrip (g_string_free (cur, FALSE)));
	else
		g_string_free (cur, TRUE);
	g_ptr_array_add (cells, NULL);
	g_free (trimmed);
	return (gchar **) g_ptr_array_free (cells, FALSE);
}

/* Kopfzeile plus Trennzeile mit gleich vielen Spalten (GFM). */
static gboolean
is_table_start (gchar **lines, gint i, gint n)
{
	gchar **head, **sep;
	gboolean ok;

	if (strchr (lines[i], '|') == NULL || i + 1 >= n || !is_table_sep (lines[i + 1]))
		return FALSE;
	head = split_cells (lines[i]);
	sep = split_cells (lines[i + 1]);
	ok = g_strv_length (head) == g_strv_length (sep) && g_strv_length (head) > 0;
	g_strfreev (head);
	g_strfreev (sep);
	return ok;
}

/* Beginn eines unterstützten HTML-Blocks: p, div, center, details, h1 bis h6. */
static gboolean
html_block_open (const gchar *line, HtmlTag *t)
{
	const gchar *ls = skip_spaces (line);

	if (*ls != '<' || !parse_html_tag (ls, ls + strlen (ls), t) || t->closing)
		return FALSE;
	if (html_tag_is (t, "p") || html_tag_is (t, "div") || html_tag_is (t, "center") ||
	    html_tag_is (t, "details"))
		return TRUE;
	return t->name_len == 2 && (t->name[0] == 'h' || t->name[0] == 'H') && t->name[1] >= '1' && t->name[1] <= '6';
}

static gboolean
starts_block (gchar **lines, gint i, gint n)
{
	const gchar *line = lines[i], *t;
	HtmlTag html;
	gchar ch;
	gint len, ind, nl;
	const gchar *num, *content;

	if (is_fence (line, &ch, &len) || heading_level (line, &t) > 0 || is_hr (line))
		return TRUE;
	if (*skip_spaces (line) == '>')
		return TRUE;
	if (is_list_item (line, &ind, &ch, &num, &nl, &content))
		return TRUE;
	if (is_table_start (lines, i, n))
		return TRUE;
	if (g_str_has_prefix (skip_spaces (line), "<!--"))
		return TRUE;
	if (html_block_open (line, &html))
		return TRUE;
	return FALSE;
}

static void
decor_array_free (gpointer array)
{
	g_array_unref (array);
}

static void
decor_add (Ctx *c, DecorType type, gint start, gint end, gint level)
{
	GArray *list = g_object_get_data (G_OBJECT (c->buffer), DECOR_KEY);
	Decor d = { type, start, end, level };

	if (list != NULL && end >= start)
		g_array_append_val (list, d);
}

static void
table_free (gpointer data)
{
	TableData *t = data;

	g_free (t->aligns);
	g_ptr_array_unref (t->rows);
	g_free (t);
}

static gboolean
tag_is (GtkTextTag *tag, const gchar *name)
{
	gchar *tag_name = NULL;
	gboolean same;

	g_object_get (tag, "name", &tag_name, NULL);
	same = g_strcmp0 (tag_name, name) == 0;
	g_free (tag_name);
	return same;
}

/* Inline-Markdown einer Tabellenzelle als Pango-Markup: der Text wird mit
 * dem normalen Inline-Renderer in einen Hilfspuffer gesetzt und danach nach
 * Tag-Abschnitten in Markup übersetzt (fett, kursiv, durchgestrichen,
 * Code, Links). */
static gchar *
cell_to_markup (const gchar *text)
{
	GtkTextBuffer *tmp = gtk_text_buffer_new (NULL);
	Ctx tc = { tmp, NULL };
	GPtrArray *tags = g_ptr_array_new ();
	GString *out = g_string_new (NULL);
	GtkTextIter it, end;

	setup_tags (tmp);
	render_inline (&tc, text, text + strlen (text), tags);
	gtk_text_buffer_get_start_iter (tmp, &it);
	gtk_text_buffer_get_end_iter (tmp, &end);
	while (!gtk_text_iter_is_end (&it)) {
		GtkTextIter next = it;
		GSList *tl, *l;
		gboolean b = FALSE, i = FALSE, st = FALSE, code = FALSE;
		const gchar *href = NULL;
		gchar *seg, *esc;

		if (!gtk_text_iter_forward_to_tag_toggle (&next, NULL))
			next = end;
		seg = gtk_text_iter_get_text (&it, &next);
		tl = gtk_text_iter_get_tags (&it);
		for (l = tl; l != NULL; l = l->next) {
			GtkTextTag *tag = l->data;

			if (nolphin_markdown_tag_get_href (tag) != NULL)
				href = nolphin_markdown_tag_get_href (tag);
			else if (tag_is (tag, "bold"))
				b = TRUE;
			else if (tag_is (tag, "italic"))
				i = TRUE;
			else if (tag_is (tag, "strike"))
				st = TRUE;
			else if (tag_is (tag, "code"))
				code = TRUE;
		}
		g_slist_free (tl);
		esc = g_markup_escape_text (seg, -1);
		if (href != NULL) {
			gchar *href_esc = g_markup_escape_text (href, -1);

			g_string_append_printf (out, "<a href=\"%s\">", href_esc);
			g_free (href_esc);
		}
		g_string_append (out, b ? "<b>" : "");
		g_string_append (out, i ? "<i>" : "");
		g_string_append (out, st ? "<s>" : "");
		g_string_append (out, code ? "<tt>" : "");
		g_string_append (out, esc);
		g_string_append (out, code ? "</tt>" : "");
		g_string_append (out, st ? "</s>" : "");
		g_string_append (out, i ? "</i>" : "");
		g_string_append (out, b ? "</b>" : "");
		g_string_append (out, href != NULL ? "</a>" : "");
		g_free (esc);
		g_free (seg);
		it = next;
	}
	g_ptr_array_free (tags, TRUE);
	g_object_unref (tmp);
	return g_string_free (out, FALSE);
}

/* Setzt einen Anker mit den Tags @tags an das Pufferende und liefert ihn. */
static GtkTextChildAnchor *
insert_anchor (Ctx *c, GPtrArray *tags)
{
	GtkTextIter it, a;
	GtkTextChildAnchor *anchor;
	gint start;
	guint k;

	gtk_text_buffer_get_end_iter (c->buffer, &it);
	start = gtk_text_iter_get_offset (&it);
	anchor = gtk_text_buffer_create_child_anchor (c->buffer, &it);
	gtk_text_buffer_get_iter_at_offset (c->buffer, &a, start);
	gtk_text_buffer_get_end_iter (c->buffer, &it);
	for (k = 0; k < tags->len; k++)
		gtk_text_buffer_apply_tag (c->buffer, g_ptr_array_index (tags, k), &a, &it);
	return anchor;
}

static void
emit_table (Ctx *c, gchar **header, gchar **sep, gchar ***body, guint nbody, gint quote)
{
	TableData *t = g_new0 (TableData, 1);
	GPtrArray *tags = g_ptr_array_new ();
	GtkTextChildAnchor *anchor;
	guint r, k;
	gint start = buffer_length (c);

	t->ncols = g_strv_length (sep);
	t->aligns = g_new0 (gint, t->ncols);
	t->rows = g_ptr_array_new_with_free_func ((GDestroyNotify) g_strfreev);
	for (k = 0; k < t->ncols; k++) {
		const gchar *cell = sep[k];
		gsize len = strlen (cell);
		gboolean left = len > 0 && cell[0] == ':';
		gboolean right = len > 0 && cell[len - 1] == ':';

		t->aligns[k] = (left && right) ? 1 : right ? 2 : 0;
	}
	for (r = 0; r <= nbody; r++) {
		gchar **src = (r == 0) ? header : body[r - 1];
		gchar **row = g_new0 (gchar *, t->ncols + 1);

		for (k = 0; k < t->ncols; k++)
			row[k] = cell_to_markup (k < g_strv_length (src) ? src[k] : "");
		g_ptr_array_add (t->rows, row);
	}
	g_ptr_array_add (tags, para_tag (c, quote * 16, 0, 16));
	anchor = insert_anchor (c, tags);
	g_object_set_data_full (G_OBJECT (anchor), TABLE_KEY, t, table_free);
	insert_literal (c, "\n", tags);
	g_ptr_array_free (tags, TRUE);
	(void) start;
}

/* Kästchen einer Aufgabenliste (nur Anzeige): 1 = angehakt, 2 = leer. */
static void
insert_check_anchor (Ctx *c, gboolean checked, GPtrArray *tags)
{
	GtkTextChildAnchor *anchor = insert_anchor (c, tags);

	g_object_set_data (G_OBJECT (anchor), CHECK_KEY, GINT_TO_POINTER (checked ? 1 : 2));
}

static void
end_paragraph (Ctx *c, GPtrArray *tags, gint start_len)
{
	if (buffer_length (c) > start_len)
		insert_literal (c, "\n", tags);
}

static void
emit_text_block (Ctx *c, const gchar *text, GtkTextTag *block_tag, gint quote)
{
	GPtrArray *tags = g_ptr_array_new ();
	gint start = buffer_length (c);

	g_ptr_array_add (tags, block_tag);
	if (quote > 0)
		g_ptr_array_add (tags, T (c, "quote"));
	render_inline (c, text, text + strlen (text), tags);
	end_paragraph (c, tags, start);
	g_ptr_array_free (tags, TRUE);
}

static void
flush_paragraph (Ctx *c, GString *para, gint quote)
{
	gchar *text;

	if (para->len == 0)
		return;
	text = g_strchomp (g_strdup (para->str));
	emit_text_block (c, text, para_tag (c, quote * 16, 0, 16), quote);
	g_free (text);
	g_string_truncate (para, 0);
}

static void
emit_verbatim (Ctx *c, const gchar *text, gint quote)
{
	gchar **lines = g_strsplit (text, "\n", -1);
	guint n = g_strv_length (lines), k;
	gint start = buffer_length (c);

	for (k = 0; k < n; k++) {
		GPtrArray *tags = g_ptr_array_new ();

		g_ptr_array_add (tags, T (c, "codeblock"));
		if (k == 0)
			g_ptr_array_add (tags, T (c, "code-first"));
		if (k == n - 1)
			g_ptr_array_add (tags, T (c, "code-last"));
		if (quote > 0)
			g_ptr_array_add (tags, para_tag (c, 16 + quote * 16, 0, 0));
		insert_literal (c, lines[k], tags);
		insert_literal (c, "\n", tags);
		g_ptr_array_free (tags, TRUE);
	}
	decor_add (c, DECOR_BOX, start, buffer_length (c) - 1, quote);
	g_strfreev (lines);
}

/* Sprungmarke für die Überschrift ab @start: Name wie bei GitHub (klein,
 * Leerzeichen zu "-", Satzzeichen entfallen, Doppelte mit -1, -2 ...). */
#define ANCHOR_PREFIX "md-anchor:"
#define SLUGS_KEY "nolphin-md-slugs"

static gchar *
heading_slug (const gchar *text)
{
	GString *out = g_string_new (NULL);
	gchar *low = g_utf8_strdown (text, -1);
	const gchar *p;

	for (p = low; *p != '\0'; p = g_utf8_next_char (p)) {
		gunichar ch = g_utf8_get_char (p);

		if (g_unichar_isalnum (ch) || ch == '-' || ch == '_')
			g_string_append_unichar (out, ch);
		else if (g_unichar_isspace (ch))
			g_string_append_c (out, '-');
	}
	g_free (low);
	return g_string_free (out, FALSE);
}

static void
register_heading_anchor (Ctx *c, gint start)
{
	GtkTextIter a, b;
	GHashTable *slugs = g_object_get_data (G_OBJECT (c->buffer), SLUGS_KEY);
	gchar *text, *slug, *name;
	gint count;

	if (slugs == NULL) {
		slugs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		g_object_set_data_full (G_OBJECT (c->buffer), SLUGS_KEY, slugs, (GDestroyNotify) g_hash_table_unref);
	}
	gtk_text_buffer_get_iter_at_offset (c->buffer, &a, start);
	gtk_text_buffer_get_end_iter (c->buffer, &b);
	text = gtk_text_buffer_get_text (c->buffer, &a, &b, FALSE);
	g_strstrip (text);
	slug = heading_slug (text);
	g_free (text);

	count = GPOINTER_TO_INT (g_hash_table_lookup (slugs, slug));
	g_hash_table_insert (slugs, g_strdup (slug), GINT_TO_POINTER (count + 1));
	name = count == 0 ? g_strconcat (ANCHOR_PREFIX, slug, NULL)
			  : g_strdup_printf (ANCHOR_PREFIX "%s-%d", slug, count);
	if (gtk_text_buffer_get_mark (c->buffer, name) == NULL)
		gtk_text_buffer_create_mark (c->buffer, name, &a, TRUE);
	g_free (name);
	g_free (slug);
}

/* Überschrift; H1/H2 bekommen zusätzlich die Linie darunter. */
static void
emit_heading (Ctx *c, const gchar *text, gint level)
{
	gchar *hname = g_strdup_printf ("h%d", level);
	gint start = buffer_length (c);

	emit_text_block (c, text, T (c, hname), 0);
	register_heading_anchor (c, start);
	if (level <= 2)
		decor_add (c, DECOR_RULE, start, buffer_length (c) - 1, level);
	g_free (hname);
}

static void render_blocks (Ctx *c, gchar **lines, gint n, gint quote);

/* Ausrichtung eines HTML-Blocks: "jc" (Mitte), "jr" (rechts) oder NULL. */
static const gchar *
html_justify_tag (const HtmlTag *t)
{
	gchar *align = html_attr (t, "align"), *style = html_attr (t, "style");
	const gchar *result = NULL;

	if (html_tag_is (t, "center"))
		result = "jc";
	if (align != NULL) {
		if (g_ascii_strcasecmp (align, "center") == 0)
			result = "jc";
		else if (g_ascii_strcasecmp (align, "right") == 0)
			result = "jr";
	}
	if (style != NULL) {
		gchar *flat = g_strdup (style), *w = flat, *r;

		for (r = flat; *r != '\0'; r++)
			if (!g_ascii_isspace (*r))
				*w++ = g_ascii_tolower (*r);
		*w = '\0';
		if (strstr (flat, "text-align:center") != NULL)
			result = "jc";
		else if (strstr (flat, "text-align:right") != NULL)
			result = "jr";
		g_free (flat);
	}
	g_free (align);
	g_free (style);
	return result;
}

static void
apply_tag_range (Ctx *c, GtkTextTag *tag, gint start, gint end)
{
	GtkTextIter a, b;

	if (tag == NULL || end <= start)
		return;
	gtk_text_buffer_get_iter_at_offset (c->buffer, &a, start);
	gtk_text_buffer_get_iter_at_offset (c->buffer, &b, end);
	gtk_text_buffer_apply_tag (c->buffer, tag, &a, &b);
}

static gchar *
one_line (const gchar *text)
{
	gchar *copy = g_strdup (text), *r;

	for (r = copy; *r != '\0'; r++)
		if (*r == '\n' || *r == '\r')
			*r = ' ';
	return g_strstrip (copy);
}

/* <details>: Pfeil und Titel (anklickbar) plus Inhalt, standardmäßig
 * zugeklappt wie bei GitHub (Attribut "open" klappt auf). Der Inhalt trägt
 * ein "invisible"-Tag, das der Klick umschaltet. */
static void
render_details (Ctx *c, const HtmlTag *open, const gchar *inner, gint quote)
{
	gchar *open_attr = html_attr (open, "open");
	gboolean is_open = open_attr != NULL;
	const gchar *end = inner + strlen (inner), *p = inner, *content = inner;
	gchar *summary = NULL, *name;
	HtmlTag st, sc;
	gint idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (c->buffer), "nolphin-md-det-count"));
	DetInfo *info = g_new0 (DetInfo, 1);
	GtkTextTag *stag;
	GPtrArray *tags = g_ptr_array_new ();
	gint start, cstart;
	gchar **lines;

	g_free (open_attr);
	while (p < end && g_ascii_isspace (*p))
		p++;
	if (p < end && *p == '<' && parse_html_tag (p, end, &st) && !st.closing && html_tag_is (&st, "summary")) {
		const gchar *sclose = find_close_tag (st.tag_end, end, &st, &sc);

		if (sclose != NULL) {
			gchar *raw = g_strndup (st.tag_end, (gsize) (sclose - st.tag_end));

			summary = one_line (raw);
			g_free (raw);
			content = sc.tag_end;
		}
	}
	if (summary == NULL || *summary == '\0') {
		g_free (summary);
		summary = g_strdup ("Details");
	}
	g_object_set_data (G_OBJECT (c->buffer), "nolphin-md-det-count", GINT_TO_POINTER (idx + 1));

	name = g_strdup_printf ("det-%d", idx);
	info->content = gtk_text_buffer_create_tag (c->buffer, name, "invisible", !is_open, NULL);
	g_free (name);
	name = g_strdup_printf ("det-%d-c", idx);
	info->closed = gtk_text_buffer_create_tag (c->buffer, name, "invisible", is_open, NULL);
	g_free (name);
	name = g_strdup_printf ("det-%d-o", idx);
	info->open = gtk_text_buffer_create_tag (c->buffer, name, "invisible", !is_open, NULL);
	g_free (name);
	name = g_strdup_printf ("det-%d-s", idx);
	stag = gtk_text_buffer_create_tag (c->buffer, name, NULL);
	g_free (name);
	g_object_set_data_full (G_OBJECT (stag), TOGGLE_KEY, info, g_free);

	/* Titelzeile */
	start = buffer_length (c);
	g_ptr_array_add (tags, para_tag (c, quote * 16, 0, 16));
	g_ptr_array_add (tags, stag);
	g_ptr_array_add (tags, info->closed);
	insert_literal (c, "\u25B8 ", tags);
	g_ptr_array_set_size (tags, tags->len - 1);
	g_ptr_array_add (tags, info->open);
	insert_literal (c, "\u25BE ", tags);
	g_ptr_array_set_size (tags, tags->len - 1);
	render_inline (c, summary, summary + strlen (summary), tags);
	end_paragraph (c, tags, start);
	g_ptr_array_free (tags, TRUE);

	/* Inhalt */
	cstart = buffer_length (c);
	lines = g_strsplit (content, "\n", -1);
	render_blocks (c, lines, (gint) g_strv_length (lines), quote);
	g_strfreev (lines);
	apply_tag_range (c, info->content, cstart, buffer_length (c));
	g_free (summary);
}

/* Unterstützte HTML-Blöcke: p, div, center (mit Ausrichtung), h1 bis h6,
 * details. Liefert die Anzahl verbrauchter Zeilen oder -1, wenn der Block
 * nicht geschlossen ist (dann gilt die Zeile als normaler Text). */
static gint
render_html_block (Ctx *c, gchar **lines, gint n, gint i, gint quote, GString *para)
{
	HtmlTag open, close;
	const gchar *ls = skip_spaces (lines[i]);
	GString *joined;
	const gchar *all, *close_at, *nl;
	gchar *inner, *remainder;
	gint consumed = 1, k, start, level = 0;
	const gchar *jtag;

	if (!html_block_open (lines[i], &open))
		return -1;
	joined = g_string_new (NULL);
	for (k = i; k < n; k++) {
		if (k > i)
			g_string_append_c (joined, '\n');
		g_string_append (joined, lines[k]);
	}
	all = joined->str;
	if (!parse_html_tag (all + (ls - lines[i]), all + joined->len, &open) ||
	    (close_at = find_close_tag (open.tag_end, all + joined->len, &open, &close)) == NULL) {
		g_string_free (joined, TRUE);
		return -1;
	}
	inner = g_strndup (open.tag_end, (gsize) (close_at - open.tag_end));
	nl = strchr (close.tag_end, '\n');
	remainder = g_strndup (close.tag_end, nl != NULL ? (gsize) (nl - close.tag_end) : strlen (close.tag_end));
	for (nl = all; nl < close.tag_end; nl++)
		if (*nl == '\n')
			consumed++;

	flush_paragraph (c, para, quote);
	jtag = html_justify_tag (&open);
	start = buffer_length (c);
	if (open.name_len == 2 && (open.name[0] == 'h' || open.name[0] == 'H'))
		level = open.name[1] - '0';

	if (level > 0) {
		gchar *text = one_line (inner);

		emit_heading (c, text, level);
		g_free (text);
	} else if (html_tag_is (&open, "details")) {
		render_details (c, &open, inner, quote);
	} else {
		gchar **inner_lines = g_strsplit (inner, "\n", -1);

		render_blocks (c, inner_lines, (gint) g_strv_length (inner_lines), quote);
		g_strfreev (inner_lines);
	}
	if (jtag != NULL)
		apply_tag_range (c, T (c, jtag), start, buffer_length (c));
	if (!is_blank (remainder)) {
		gchar *rem[2] = { remainder, NULL };

		render_blocks (c, rem, 1, quote);
	}
	g_free (inner);
	g_free (remainder);
	g_string_free (joined, TRUE);
	return consumed;
}

static void
render_blocks (Ctx *c, gchar **lines, gint n, gint quote)
{
	GString *para = g_string_new (NULL);
	gint i = 0;
	gint list_ind[16];
	gint list_depth = 0;

	while (i < n) {
		const gchar *line = lines[i], *htext;
		gchar ch;
		gint flen, level, ind, nl;
		const gchar *num, *content;

		if (is_blank (line)) {
			flush_paragraph (c, para, quote);
			i++;
			continue;
		}
		/* Jede andere Zeile als ein Listenpunkt beendet die Liste. */
		if (!is_list_item (line, &ind, &ch, &num, &nl, &content))
			list_depth = 0;

		/* HTML-Kommentar */
		if (g_str_has_prefix (skip_spaces (line), "<!--")) {
			flush_paragraph (c, para, quote);
			while (i < n && strstr (lines[i], "-->") == NULL)
				i++;
			i++;
			continue;
		}

		/* HTML-Block: p, div, center, h1 bis h6, details */
		{
			HtmlTag probe;

			if (html_block_open (line, &probe)) {
				gint used = render_html_block (c, lines, n, i, quote, para);

				if (used > 0) {
					i += used;
					continue;
				}
			}
		}

		/* Eingezäunter Codeblock */
		if (is_fence (line, &ch, &flen)) {
			GString *code = g_string_new (NULL);
			gchar cch;
			gint clen;

			flush_paragraph (c, para, quote);
			i++;
			while (i < n) {
				if (is_fence (lines[i], &cch, &clen) && cch == ch && clen >= flen &&
				    is_blank (skip_spaces (lines[i]) + clen))
					break;
				if (code->len > 0)
					g_string_append_c (code, '\n');
				g_string_append (code, lines[i]);
				i++;
			}
			i++; /* schließenden Zaun überspringen */
			if (code->len > 0)
				emit_verbatim (c, code->str, quote);
			g_string_free (code, TRUE);
			continue;
		}

		/* Setext-Überschrift: Absatz plus === bzw. --- */
		if (para->len > 0 && (level = setext_level (line)) > 0) {
			gchar *text = g_strstrip (g_strdup (para->str));

			emit_heading (c, text, level);
			g_free (text);
			g_string_truncate (para, 0);
			i++;
			continue;
		}

		if ((level = heading_level (line, &htext)) > 0) {
			gchar *text = g_strdup (htext);
			gsize len = strlen (text);

			flush_paragraph (c, para, quote);
			while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
				text[--len] = '\0';
			/* Schließende Rauten nur, wenn ein Leerzeichen davor steht ("# C#" bleibt). */
			{
				gsize k = len;

				while (k > 0 && text[k - 1] == '#')
					k--;
				if (k < len && (k == 0 || text[k - 1] == ' ' || text[k - 1] == '\t')) {
					text[k] = '\0';
					g_strchomp (text);
				}
			}
			emit_heading (c, text, level);
			g_free (text);
			i++;
			continue;
		}

		if (is_hr (line)) {
			GPtrArray *tags = g_ptr_array_new ();
			gint at;

			flush_paragraph (c, para, quote);
			/* Eine winzige Leerzeile; die Linie wird darüber gezeichnet. */
			at = buffer_length (c);
			g_ptr_array_add (tags, T (c, "hr"));
			insert_literal (c, "\n", tags);
			decor_add (c, DECOR_HR, at, at, 0);
			g_ptr_array_free (tags, TRUE);
			i++;
			continue;
		}

		/* Zitat */
		if (*skip_spaces (line) == '>') {
			GPtrArray *inner = g_ptr_array_new_with_free_func (g_free);
			gchar **iv;
			guint k;

			flush_paragraph (c, para, quote);
			while (i < n && *skip_spaces (lines[i]) == '>') {
				const gchar *q = skip_spaces (lines[i]) + 1;

				if (*q == ' ')
					q++;
				g_ptr_array_add (inner, g_strdup (q));
				i++;
			}
			iv = g_new0 (gchar *, inner->len + 1);
			for (k = 0; k < inner->len; k++)
				iv[k] = g_ptr_array_index (inner, k);
			{
				gint qstart = buffer_length (c);

				render_blocks (c, iv, (gint) inner->len, quote + 1);
				decor_add (c, DECOR_BAR, qstart, buffer_length (c) - 1, quote + 1);
			}
			g_free (iv);
			g_ptr_array_free (inner, TRUE);
			continue;
		}

		/* Liste */
		if (is_list_item (line, &ind, &ch, &num, &nl, &content)) {
			gint item_indent = ind;
			GString *item = g_string_new (content);
			gchar *prefix = NULL;
			gint lvl, left, below, hang = -16, j;
			gboolean task = FALSE, checked = FALSE, blank_between = FALSE;
			GPtrArray *tags;
			gint start;
			gchar *text;

			flush_paragraph (c, para, quote);
			/* Ebene aus der Einrückung: tiefer eingerückt = eine Ebene tiefer,
			 * gleichgültig ob 2 oder 4 Leerzeichen. */
			while (list_depth > 0 && item_indent < list_ind[list_depth - 1])
				list_depth--;
			if (list_depth == 0 || item_indent > list_ind[list_depth - 1]) {
				if (list_depth < 16)
					list_ind[list_depth++] = item_indent;
			}
			lvl = list_depth - 1;
			left = quote * 16 + 32 * (MIN (lvl, 6) + 1);

			i++;
			/* Fortsetzungszeilen: nicht leer, kein neuer Block. */
			while (i < n && !is_blank (lines[i]) && !starts_block (lines, i, n)) {
				g_string_append_c (item, '\n');
				g_string_append (item, skip_spaces (lines[i]));
				i++;
			}
			/* Abstand nach dem Punkt: 4px zum nächsten Punkt der Liste,
			 * sonst 16px (Listenende oder lockere Liste). */
			j = i;
			while (j < n && is_blank (lines[j])) {
				blank_between = TRUE;
				j++;
			}
			{
				gint ind2, nl2;
				gchar ch2;
				const gchar *num2, *content2;
				gboolean next_item = j < n && is_list_item (lines[j], &ind2, &ch2, &num2, &nl2, &content2);

				below = (next_item && !blank_between) ? 4 : 16;
			}
			if (g_str_has_prefix (item->str, "[ ] ") || g_str_has_prefix (item->str, "[x] ") ||
			    g_str_has_prefix (item->str, "[X] ")) {
				task = TRUE;
				checked = item->str[1] != ' ';
				g_string_erase (item, 0, 4);
				hang = -24;
			} else if (num != NULL) {
				gchar *digits = g_strndup (num, nl);

				prefix = g_strdup_printf ("%s%c ", digits, ch);
				g_free (digits);
			} else {
				static const gchar *bullets[] = { "• ", "◦ ", "▪ " };

				prefix = g_strdup (bullets[MIN (lvl, 2)]);
			}
			tags = g_ptr_array_new ();
			g_ptr_array_add (tags, para_tag (c, left, hang, below));
			if (quote > 0)
				g_ptr_array_add (tags, T (c, "quote"));
			start = buffer_length (c);
			if (task) {
				insert_check_anchor (c, checked, tags);
				insert_literal (c, " ", tags);
			} else {
				insert_literal (c, prefix, tags);
			}
			text = g_strchomp (g_string_free (item, FALSE));
			render_inline (c, text, text + strlen (text), tags);
			end_paragraph (c, tags, start);
			g_free (text);
			g_free (prefix);
			g_ptr_array_free (tags, TRUE);
			continue;
		}

		/* Tabelle (GFM): echte Tabelle als Widget */
		if (is_table_start (lines, i, n)) {
			gchar **header = split_cells (lines[i]);
			gchar **sep = split_cells (lines[i + 1]);
			GPtrArray *body = g_ptr_array_new ();
			guint k;

			flush_paragraph (c, para, quote);
			i += 2;
			while (i < n && !is_blank (lines[i]) && !starts_block (lines, i, n)) {
				g_ptr_array_add (body, split_cells (lines[i]));
				i++;
			}
			g_ptr_array_add (body, NULL);
			emit_table (c, header, sep, (gchar ***) body->pdata, body->len - 1, quote);
			for (k = 0; k + 1 < body->len; k++)
				g_strfreev (g_ptr_array_index (body, k));
			g_ptr_array_free (body, TRUE);
			g_strfreev (header);
			g_strfreev (sep);
			continue;
		}

		/* Absatzzeile */
		if (para->len > 0)
			g_string_append_c (para, '\n');
		g_string_append (para, skip_spaces (line));
		i++;
	}
	flush_paragraph (c, para, quote);
	g_string_free (para, TRUE);
}

/* Beim erneuten Rendern in denselben Puffer bleiben die Tags der früheren
 * <details>-Abschnitte in der Tag-Tabelle; sie werden entfernt, damit die
 * Namen wieder frei sind. */
static void
collect_details_tag (GtkTextTag *tag, gpointer data)
{
	gchar *name = NULL;

	g_object_get (tag, "name", &name, NULL);
	if (name != NULL && g_str_has_prefix (name, "det-"))
		*(GSList **) data = g_slist_prepend (*(GSList **) data, g_object_ref (tag));
	g_free (name);
}

static void
forget_details_tags (GtkTextBuffer *buffer)
{
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table (buffer);
	GSList *old = NULL, *l;

	gtk_text_tag_table_foreach (table, collect_details_tag, &old);
	for (l = old; l != NULL; l = l->next) {
		gtk_text_tag_table_remove (table, l->data);
		g_object_unref (l->data);
	}
	g_slist_free (old);
	g_object_set_data (G_OBJECT (buffer), "nolphin-md-det-count", GINT_TO_POINTER (0));
}

void
nolphin_markdown_render_to_buffer (GtkTextBuffer *buffer, const gchar *markdown)
{
	nolphin_markdown_render_to_buffer_with_base (buffer, markdown, NULL);
}

void
nolphin_markdown_render_to_buffer_with_base (GtkTextBuffer *buffer, const gchar *markdown, const gchar *base_dir)
{
	Ctx c = { buffer, base_dir };
	gchar *clean, **lines;
	gint n;

	g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));

	gtk_text_buffer_set_text (buffer, "", -1);
	setup_tags (buffer);
	forget_details_tags (buffer);
	g_object_set_data_full (G_OBJECT (buffer), DECOR_KEY, g_array_new (FALSE, TRUE, sizeof (Decor)),
				decor_array_free);
	if (markdown == NULL || *markdown == '\0')
		return;

	clean = g_utf8_make_valid (markdown, -1);
	/* CRLF vereinheitlichen */
	{
		gchar *w = clean, *r = clean;
		for (; *r != '\0'; r++) {
			if (*r == '\r' && r[1] == '\n')
				continue;
			*w++ = *r;
		}
		*w = '\0';
	}
	lines = g_strsplit (clean, "\n", -1);
	n = (gint) g_strv_length (lines);
	render_blocks (&c, lines, n, 0);
	g_strfreev (lines);
	g_free (clean);
}

/* ------------------------------------------------------------------ */
/* Widget                                                              */
/* ------------------------------------------------------------------ */

static void
apply_theme (GtkWidget *view)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table (buffer);
	GtkStyleContext *style = gtk_widget_get_style_context (view);
	GdkRGBA fg, dim, shade, link;
	GtkTextTag *tag;

	if (gtk_text_tag_table_lookup (table, "bold") == NULL)
		return;

	gtk_style_context_get_color (style, gtk_style_context_get_state (style), &fg);
	dim = fg;
	dim.alpha = fg.alpha * 0.65;
	shade = fg;
	shade.alpha = fg.alpha * 0.15;

	if ((tag = gtk_text_tag_table_lookup (table, "code")) != NULL)
		g_object_set (tag, "background-rgba", &shade, NULL);
	if ((tag = gtk_text_tag_table_lookup (table, "quote")) != NULL)
		g_object_set (tag, "foreground-rgba", &dim, NULL);
	if ((tag = gtk_text_tag_table_lookup (table, "dim")) != NULL)
		g_object_set (tag, "foreground-rgba", &dim, NULL);

	/* Linkfarbe aus dem aktiven GTK-Theme (Zustand "link"); definiert das
	 * Theme keine, bleibt es bei Textfarbe mit Unterstreichung. */
	gtk_style_context_save (style);
	gtk_style_context_set_state (style, GTK_STATE_FLAG_LINK);
	gtk_style_context_get_color (style, GTK_STATE_FLAG_LINK, &link);
	gtk_style_context_restore (style);
	if ((tag = gtk_text_tag_table_lookup (table, "link")) != NULL)
		g_object_set (tag, "foreground-rgba", &link, NULL);
}

static const gchar *
href_at_iter (const GtkTextIter *iter)
{
	GSList *tags = gtk_text_iter_get_tags (iter), *l;
	const gchar *href = NULL;

	for (l = tags; l != NULL && href == NULL; l = l->next)
		href = nolphin_markdown_tag_get_href (l->data);
	g_slist_free (tags);
	return href;
}

static gboolean
iter_for_event (GtkWidget *view, gdouble x, gdouble y, GtkTextIter *iter);

static void update_children_visibility (GtkWidget *view);

static void
set_image_width (GtkWidget *box, gint width)
{
	GdkPixbuf *orig = g_object_get_data (G_OBJECT (box), "nolphin-md-pixbuf");
	GtkWidget *image;
	gint ow, oh, target;

	if (orig == NULL)
		return;
	ow = gdk_pixbuf_get_width (orig);
	oh = gdk_pixbuf_get_height (orig);
	{
		/* Gewünschte Breite aus dem HTML (px; negativ = Prozent der Textbreite). */
		gint want = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "nolphin-md-want-w"));

		if (want < 0)
			target = MAX (1, width * (-want) / 100);
		else
			target = MAX (1, MIN (want > 0 ? want : ow, width));
	}
	if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "nolphin-md-cur-width")) == target)
		return;
	g_object_set_data (G_OBJECT (box), "nolphin-md-cur-width", GINT_TO_POINTER (target));
	image = gtk_bin_get_child (GTK_BIN (box));
	if (target == ow) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (image), orig);
	} else {
		GdkPixbuf *scaled = gdk_pixbuf_scale_simple (orig, target, MAX (1, (gint) ((gint64) oh * target / ow)),
							     GDK_INTERP_BILINEAR);

		gtk_image_set_from_pixbuf (GTK_IMAGE (image), scaled);
		g_object_unref (scaled);
	}
}

static gboolean
on_image_release (GtkWidget *box, GdkEventButton *event, gpointer view)
{
	LinkHandler *handler = g_object_get_data (G_OBJECT (view), HANDLER_KEY);
	const gchar *href = g_object_get_data (G_OBJECT (box), "nolphin-md-img-href");

	if (event->button == GDK_BUTTON_PRIMARY && href != NULL && handler != NULL && handler->func != NULL)
		handler->func (view, href, handler->data);
	return GDK_EVENT_PROPAGATE;
}

/* Widget für ein lokales Bild; kann es nicht geladen werden, erscheint der
 * Alternativtext. */
static GtkWidget *
make_image_widget (GtkWidget *view, const gchar *path, const gchar *alt, const gchar *href,
		   const gchar *width_attr, const gchar *height_attr)
{
	GStatBuf st;
	GdkPixbuf *pixbuf = NULL;
	GtkWidget *box;

	if (g_stat (path, &st) == 0 && st.st_size <= IMAGE_MAX_BYTES)
		pixbuf = gdk_pixbuf_new_from_file (path, NULL);
	if (pixbuf == NULL) {
		gchar *base = g_path_get_basename (path);
		GtkWidget *label = gtk_label_new (alt != NULL && *alt != '\0' ? alt : base);

		gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
		g_free (base);
		return label;
	}
	box = gtk_event_box_new ();
	gtk_event_box_set_visible_window (GTK_EVENT_BOX (box), FALSE);
	gtk_container_add (GTK_CONTAINER (box), gtk_image_new_from_pixbuf (pixbuf));
	gtk_widget_show_all (box);
	if (alt != NULL && *alt != '\0')
		gtk_widget_set_tooltip_text (box, alt);
	g_object_set_data_full (G_OBJECT (box), "nolphin-md-pixbuf", pixbuf, g_object_unref);
	{
		gint want = 0;

		if (width_attr != NULL && *width_attr != '\0') {
			gdouble v = g_ascii_strtod (width_attr, NULL);

			want = strchr (width_attr, '%') != NULL ? -(gint) v : (gint) v;
		} else if (height_attr != NULL && *height_attr != '\0' && strchr (height_attr, '%') == NULL) {
			gdouble v = g_ascii_strtod (height_attr, NULL);

			if (v > 0 && gdk_pixbuf_get_height (pixbuf) > 0)
				want = (gint) (v * gdk_pixbuf_get_width (pixbuf) / gdk_pixbuf_get_height (pixbuf));
		}
		g_object_set_data (G_OBJECT (box), "nolphin-md-want-w", GINT_TO_POINTER (want));
	}
	if (href != NULL) {
		g_object_set_data_full (G_OBJECT (box), "nolphin-md-img-href", g_strdup (href), g_free);
		g_signal_connect (box, "button-release-event", G_CALLBACK (on_image_release), view);
	}
	return box;
}

/* Einbettungs-Container mit fest vorgegebener Größe. GtkTextView fragt die
 * Größe eines eingebetteten Widgets ohne Breitenbezug ab; bei umbrechenden
 * Zellen wäre die Höhe dann die bei kleinster Breite (viel zu hoch). Dieser
 * Container meldet stattdessen die vorab richtig gemessene Größe und gibt
 * sie unverändert an sein Kind weiter. */
typedef struct {
	GtkBin     parent;
	gint       width;
	gint       height;      /* feste Höhe, wenn @measure NULL ist */
	GtkWidget *measure;     /* Widget, dessen Höhe bei @measure_width live gemessen wird */
	gint       measure_width;
	gint       extra;       /* zusätzliche Höhe (Bildlaufleiste) */
} MdFixedBox;

typedef struct {
	GtkBinClass parent_class;
} MdFixedBoxClass;

G_DEFINE_TYPE (MdFixedBox, md_fixed_box, GTK_TYPE_BIN)

static void
md_fixed_box_get_preferred_width (GtkWidget *widget, gint *min, gint *nat)
{
	*min = *nat = ((MdFixedBox *) widget)->width;
}

static void
md_fixed_box_get_preferred_height (GtkWidget *widget, gint *min, gint *nat)
{
	MdFixedBox *box = (MdFixedBox *) widget;

	if (box->measure != NULL) {
		/* Live messen: Schrift und Theme können sich nach dem Aufbau noch
		 * ändern, eine einmal gemessene Höhe wäre dann zu klein oder zu groß. */
		gint h = 0;

		gtk_widget_get_preferred_height_for_width (box->measure, box->measure_width, &h, NULL);
		*min = *nat = h + box->extra;
		return;
	}
	*min = *nat = box->height;
}

static GtkSizeRequestMode
md_fixed_box_get_request_mode (GtkWidget *widget)
{
	return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
md_fixed_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

	gtk_widget_set_allocation (widget, allocation);
	if (child != NULL && gtk_widget_get_visible (child))
		gtk_widget_size_allocate (child, allocation);
}

static void
md_fixed_box_class_init (MdFixedBoxClass *klass)
{
	GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);

	wc->get_preferred_width = md_fixed_box_get_preferred_width;
	wc->get_preferred_height = md_fixed_box_get_preferred_height;
	wc->get_request_mode = md_fixed_box_get_request_mode;
	wc->size_allocate = md_fixed_box_size_allocate;
}

static void
md_fixed_box_init (MdFixedBox *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
}

/* Stile der Tabellen (Rahmen, Kopfzeile, Streifen); Farben aus dem
 * Theme, damit helle und dunkle Themes passen. Einmal je Prozess. */
static void
ensure_table_css (void)
{
	static gboolean done = FALSE;
	GtkCssProvider *provider;

	if (done || gdk_screen_get_default () == NULL)
		return;
	done = TRUE;
	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (provider,
		".nolphin-md-table { border-top: 1px solid alpha(@theme_fg_color, 0.25);"
		"                    border-left: 1px solid alpha(@theme_fg_color, 0.25); }"
		".nolphin-md-cell { padding: 6px 13px;"
		"                   border-right: 1px solid alpha(@theme_fg_color, 0.25);"
		"                   border-bottom: 1px solid alpha(@theme_fg_color, 0.25); }"
		".nolphin-md-cell-odd { background-color: alpha(@theme_fg_color, 0.06); }"
		".nolphin-md-cell-head { font-weight: 600; }",
		-1, NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
}

static gboolean
on_cell_link (GtkLabel *label, const gchar *uri, gpointer view)
{
	LinkHandler *handler = g_object_get_data (G_OBJECT (view), HANDLER_KEY);

	if (handler != NULL && handler->func != NULL)
		handler->func (view, uri, handler->data);
	return TRUE;
}

static GtkWidget *
make_table_widget (GtkWidget *view, const TableData *t)
{
	GtkWidget *grid = gtk_grid_new ();
	GtkWidget *box, *scroller;
	guint r, c;

	ensure_table_css ();
	gtk_style_context_add_class (gtk_widget_get_style_context (grid), "nolphin-md-table");
	gtk_widget_set_halign (grid, GTK_ALIGN_START);
	for (r = 0; r < t->rows->len; r++) {
		gchar **row = g_ptr_array_index (t->rows, r);

		for (c = 0; c < t->ncols; c++) {
			GtkWidget *label = gtk_label_new (NULL);
			GtkStyleContext *ctx = gtk_widget_get_style_context (label);
			gint align = t->aligns[c];

			gtk_label_set_markup (GTK_LABEL (label), row[c]);
			gtk_label_set_selectable (GTK_LABEL (label), TRUE);
			gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
			gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD);
			gtk_label_set_xalign (GTK_LABEL (label), align == 1 ? 0.5f : align == 2 ? 1.0f : 0.0f);
			gtk_label_set_justify (GTK_LABEL (label),
					       align == 1 ? GTK_JUSTIFY_CENTER : align == 2 ? GTK_JUSTIFY_RIGHT : GTK_JUSTIFY_LEFT);
			gtk_widget_set_halign (label, GTK_ALIGN_FILL);
			gtk_widget_set_valign (label, GTK_ALIGN_FILL);
			gtk_widget_set_hexpand (label, TRUE);
			gtk_widget_set_vexpand (label, TRUE);
			gtk_style_context_add_class (ctx, "nolphin-md-cell");
			if (r == 0)
				gtk_style_context_add_class (ctx, "nolphin-md-cell-head");
			else if (r % 2 == 0)
				gtk_style_context_add_class (ctx, "nolphin-md-cell-odd");
			g_signal_connect (label, "activate-link", G_CALLBACK (on_cell_link), view);
			gtk_grid_attach (GTK_GRID (grid), label, (gint) c, (gint) r, 1, 1);
		}
	}
	g_object_set_data (G_OBJECT (grid), "nolphin-md-cols", GUINT_TO_POINTER (t->ncols));
	g_object_set_data (G_OBJECT (grid), "nolphin-md-rows", GUINT_TO_POINTER (t->rows->len));
	gtk_widget_show_all (grid);
	/* Zu breite Tabellen scrollen waagerecht (wie GitHub); senkrechtes
	 * Scrollen reicht das innere Fenster an die Seite weiter. */
	scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_EXTERNAL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_min_content_width (GTK_SCROLLED_WINDOW (scroller), 1);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroller), 1);
	gtk_container_add (GTK_CONTAINER (scroller), grid);
	gtk_widget_show (scroller);
	box = g_object_new (md_fixed_box_get_type (), NULL);
	((MdFixedBox *) box)->width = 1;
	((MdFixedBox *) box)->height = 1;
	g_object_set_data (G_OBJECT (box), "nolphin-md-grid", grid);
	gtk_container_add (GTK_CONTAINER (box), scroller);
	gtk_widget_show (box);
	return box;
}

/* Spaltenbreiten einer Tabelle: so breit wie der Inhalt, höchstens die
 * Textbreite; ist der Platz knapp, werden die Spalten zwischen ihrer
 * kleinsten und ihrer natürlichen Breite verteilt. Die Breite wird jeder
 * Zelle direkt vorgegeben, damit GtkTextView die Höhe der Tabelle (die
 * er ohne Breitenbezug misst) bei genau diesen Breiten erhält. */
static void
set_table_width (GtkWidget *box, gint avail)
{
	GtkWidget *grid = g_object_get_data (G_OBJECT (box), "nolphin-md-grid");
	gint total = 0, height = 0;
	guint ncols = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (grid), "nolphin-md-cols"));
	guint nrows = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (grid), "nolphin-md-rows"));
	gint *minw = g_new0 (gint, ncols), *natw = g_new0 (gint, ncols), *width = g_new0 (gint, ncols);
	gint64 sum_min = 0, sum_nat = 0, sum_extra = 0;
	guint r, c;

	for (c = 0; c < ncols; c++) {
		for (r = 0; r < nrows; r++) {
			GtkWidget *cell = gtk_grid_get_child_at (GTK_GRID (grid), (gint) c, (gint) r);
			gint mn = 0, nt = 0;

			gtk_widget_set_size_request (cell, -1, -1);
			gtk_widget_get_preferred_width (cell, &mn, &nt);
			minw[c] = MAX (minw[c], mn);
			natw[c] = MAX (natw[c], nt);
		}
		sum_min += minw[c];
		sum_nat += natw[c];
		sum_extra += natw[c] - minw[c];
	}
	for (c = 0; c < ncols; c++) {
		if (sum_nat <= avail)
			width[c] = natw[c];
		else if (sum_min >= avail || sum_extra == 0)
			width[c] = minw[c];
		else
			width[c] = minw[c] + (gint) ((avail - sum_min) * (natw[c] - minw[c]) / sum_extra);
	}
	for (c = 0; c < ncols; c++) {
		total += width[c];
		for (r = 0; r < nrows; r++)
			gtk_widget_set_size_request (gtk_grid_get_child_at (GTK_GRID (grid), (gint) c, (gint) r),
						     width[c], -1);
	}
	/* Tatsächliche Breite des Gitters: die Summe der Spalten plus der eigene
	 * Rand (CSS) des Gitters. Wäre der sichtbare Kasten auch nur 1 px
	 * schmaler, erschiene die Bildlaufleiste und überdeckte die letzte Zeile. */
	{
		gint grid_min = 0;

		gtk_widget_get_preferred_width (grid, &grid_min, NULL);
		total = MAX (total, grid_min);
	}
	/* Höhe bei genau dieser Breite, dem Container fest vorgeben. */
	gtk_widget_get_preferred_height_for_width (grid, total, &height, NULL);
	/* sichtbar höchstens die Textbreite; darüber hinaus scrollt die Tabelle */
	((MdFixedBox *) box)->width = MAX (1, MIN (total, avail));
	((MdFixedBox *) box)->height = height + (total > avail ? 14 : 0);
	((MdFixedBox *) box)->measure = grid;
	((MdFixedBox *) box)->measure_width = total;
	((MdFixedBox *) box)->extra = (total > avail ? 14 : 0);
	gtk_widget_queue_resize (box);
	g_free (minw);
	g_free (natw);
	g_free (width);
}

static gboolean
swallow_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	return GDK_EVENT_STOP;
}

/* Hängt für jeden Anker ein Widget ein: das Bild bzw. seinen
 * Alternativtext, eine Tabelle oder ein Aufgaben-Kästchen. */
static void
attach_children (GtkWidget *view)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	GPtrArray *kids = g_ptr_array_new ();
	GtkTextIter it;

	gtk_text_buffer_get_start_iter (buffer, &it);
	do {
		GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor (&it);
		GtkWidget *child = NULL;
		const gchar *kind = NULL;
		const gchar *img_path;
		const TableData *table;
		gint check;

		if (anchor == NULL)
			continue;
		img_path = g_object_get_data (G_OBJECT (anchor), IMG_PATH_KEY);
		table = g_object_get_data (G_OBJECT (anchor), TABLE_KEY);
		check = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (anchor), CHECK_KEY));
		if (table != NULL) {
			child = make_table_widget (view, table);
			kind = "table";
		} else if (check != 0) {
			child = gtk_check_button_new ();
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (child), check == 1);
			/* nur Anzeige: Klicks werden abgefangen, das Kästchen bleibt aber
			 * in normaler Darstellung (deaktiviert wären leere Kästchen in
			 * dunklen Themes kaum zu sehen). */
			gtk_widget_set_can_focus (child, FALSE);
			g_signal_connect (child, "button-press-event", G_CALLBACK (swallow_event), NULL);
			g_signal_connect (child, "button-release-event", G_CALLBACK (swallow_event), NULL);
			gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
			kind = "check";
		} else if (img_path != NULL) {
			child = make_image_widget (view, img_path, g_object_get_data (G_OBJECT (anchor), IMG_ALT_KEY),
						   href_at_iter (&it),
						   g_object_get_data (G_OBJECT (anchor), IMG_W_KEY),
						   g_object_get_data (G_OBJECT (anchor), IMG_H_KEY));
			kind = "image";
		}
		if (child != NULL) {
			g_object_set_data (G_OBJECT (child), KIND_KEY, (gpointer) kind);
			g_object_set_data (G_OBJECT (child), "nolphin-md-anchor", anchor);
			gtk_text_view_add_child_at_anchor (GTK_TEXT_VIEW (view), child, anchor);
			gtk_widget_show (child);
			g_ptr_array_add (kids, child);
		}
	} while (gtk_text_iter_forward_char (&it));
	g_object_set_data_full (G_OBJECT (view), KIDS_KEY, kids, (GDestroyNotify) g_ptr_array_unref);
	g_object_set_data (G_OBJECT (view), "nolphin-md-sep-width", GINT_TO_POINTER (0));
	update_children_visibility (view);
}

/* GtkTextView misst ein eingebettetes Widget nur beim Layouten der Zeile.
 * Ändert sich seine Größe danach, stimmen die zwischengespeicherten
 * Zeilenhöhen nicht mehr: Zeilen, Widgets und Dekorationen verrutschen
 * gegeneinander. Deshalb wird nach dem eigenen Layout der Ansicht (im
 * Leerlauf) das gesamte Layout ungültig gemacht, indem ein Anzeigewert
 * kurz umgeschaltet wird; die Zeilen werden dann mit den neuen
 * Widget-Größen neu vermessen. */
static gboolean
revalidate_idle (gpointer data)
{
	GtkWidget *view = GTK_WIDGET (data);
	GtkTextView *tv = GTK_TEXT_VIEW (view);
	gint v = gtk_text_view_get_pixels_inside_wrap (tv);

	g_object_set_data (G_OBJECT (view), "nolphin-md-revalidate", NULL);
	update_children_visibility (view);
	gtk_text_view_set_pixels_inside_wrap (tv, v + 1);
	gtk_text_view_set_pixels_inside_wrap (tv, v);
	gtk_widget_queue_resize (view);
	return G_SOURCE_REMOVE;
}

static void
schedule_revalidate (GtkWidget *view)
{
	if (g_object_get_data (G_OBJECT (view), "nolphin-md-revalidate") != NULL)
		return;
	g_object_set_data (G_OBJECT (view), "nolphin-md-revalidate", GINT_TO_POINTER (1));
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, revalidate_idle, g_object_ref (view), g_object_unref);
}

/* Eingebettete Widgets in eingeklapptem Text (<details>) ausblenden. */
static gboolean
iter_is_invisible (const GtkTextIter *iter)
{
	GSList *tags = gtk_text_iter_get_tags (iter), *l;
	gboolean hidden = FALSE;

	for (l = tags; l != NULL && !hidden; l = l->next) {
		gboolean inv = FALSE;

		g_object_get (l->data, "invisible", &inv, NULL);
		hidden = inv;
	}
	g_slist_free (tags);
	return hidden;
}

static void
update_children_visibility (GtkWidget *view)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	GPtrArray *kids = g_object_get_data (G_OBJECT (view), KIDS_KEY);
	guint i;

	for (i = 0; kids != NULL && i < kids->len; i++) {
		GtkWidget *kid = g_ptr_array_index (kids, i);
		GtkTextChildAnchor *anchor = g_object_get_data (G_OBJECT (kid), "nolphin-md-anchor");
		GtkTextIter it;

		if (anchor == NULL || gtk_text_child_anchor_get_deleted (anchor))
			continue;
		gtk_text_buffer_get_iter_at_child_anchor (buffer, &it, anchor);
		gtk_widget_set_visible (kid, !iter_is_invisible (&it));
	}
}

/* Tabellen und Bilder passen sich der Textbreite der Ansicht an. */
static void
on_size_allocate (GtkWidget *view, GdkRectangle *allocation, gpointer user_data)
{
	GPtrArray *kids = g_object_get_data (G_OBJECT (view), KIDS_KEY);
	gint width = allocation->width - gtk_text_view_get_left_margin (GTK_TEXT_VIEW (view)) -
		     gtk_text_view_get_right_margin (GTK_TEXT_VIEW (view));
	guint i;

	width = MAX (width, 1);
	if (kids == NULL || kids->len == 0 ||
	    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view), "nolphin-md-sep-width")) == width)
		return;
	g_object_set_data (G_OBJECT (view), "nolphin-md-sep-width", GINT_TO_POINTER (width));
	for (i = 0; i < kids->len; i++) {
		GtkWidget *kid = g_ptr_array_index (kids, i);
		const gchar *kind = g_object_get_data (G_OBJECT (kid), KIND_KEY);


		if (g_strcmp0 (kind, "table") == 0)
			set_table_width (kid, width);
		else if (g_strcmp0 (kind, "image") == 0) {
			gint cap = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view), "nolphin-md-max-image-width"));

			set_image_width (kid, cap > 0 ? MIN (width, cap) : width);
		}
	}
	/* Größen haben sich geändert: Zeilen im Leerlauf neu vermessen lassen. */
	schedule_revalidate (view);
}

/* Zeichnet die Dekorationen (Code-Kasten, Zitat-Balken, Linien) über den
 * Text; alle Flächen sind halbtransparent, der Text bleibt lesbar. */
static gboolean
on_draw_after (GtkWidget *view, cairo_t *cr, gpointer user_data)
{
	GtkTextView *tv = GTK_TEXT_VIEW (view);
	GtkTextBuffer *buf = gtk_text_view_get_buffer (tv);
	GArray *list = g_object_get_data (G_OBJECT (buf), DECOR_KEY);
	GdkWindow *win = gtk_text_view_get_window (tv, GTK_TEXT_WINDOW_TEXT);
	GtkStyleContext *style = gtk_widget_get_style_context (view);
	GdkRGBA fg;
	gdouble cx1, cy1, cx2, cy2;
	gint left, right;
	guint i;

	if (list == NULL || list->len == 0 || win == NULL || !gtk_cairo_should_draw_window (cr, win))
		return GDK_EVENT_PROPAGATE;

	gtk_style_context_get_color (style, gtk_style_context_get_state (style), &fg);
	cairo_save (cr);
	gtk_cairo_transform_to_window (cr, view, win);
	cairo_clip_extents (cr, &cx1, &cy1, &cx2, &cy2);
	left = gtk_text_view_get_left_margin (tv);
	right = gdk_window_get_width (win) - gtk_text_view_get_right_margin (tv);

	for (i = 0; i < list->len; i++) {
		const Decor *d = &g_array_index (list, Decor, i);
		GtkTextIter a, b;
		gint y1, h1, y2, h2, wy1, wy2;
		gdouble top, bottom;

		gtk_text_buffer_get_iter_at_offset (buf, &a, d->start);
		gtk_text_buffer_get_iter_at_offset (buf, &b, d->end);
		gtk_text_view_get_line_yrange (tv, &a, &y1, &h1);
		gtk_text_view_get_line_yrange (tv, &b, &y2, &h2);
		gtk_text_view_buffer_to_window_coords (tv, GTK_TEXT_WINDOW_TEXT, 0, y1, NULL, &wy1);
		gtk_text_view_buffer_to_window_coords (tv, GTK_TEXT_WINDOW_TEXT, 0, y2, NULL, &wy2);
		top = wy1;
		bottom = wy2 + h2;
		if (bottom < cy1 || top > cy2 || bottom - top < 2)
			continue;   /* außerhalb des Bildes oder eingeklappt */

		switch (d->type) {
		case DECOR_BOX: {
			gdouble x = left + d->level * 16, w = right - x, r = 6, y = top, h = bottom - 16 - top;

			cairo_new_sub_path (cr);
			cairo_arc (cr, x + w - r, y + r, r, -G_PI / 2, 0);
			cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI / 2);
			cairo_arc (cr, x + r, y + h - r, r, G_PI / 2, G_PI);
			cairo_arc (cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
			cairo_close_path (cr);
			cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha * 0.07);
			cairo_fill (cr);
			break;
		}
		case DECOR_BAR:
			cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha * 0.30);
			cairo_rectangle (cr, left + (d->level - 1) * 16, top, 3.3, bottom - 16 - top);
			cairo_fill (cr);
			break;
		case DECOR_RULE:
			cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha * 0.25);
			cairo_rectangle (cr, left, bottom - 16 - 1, right - left, 1);
			cairo_fill (cr);
			break;
		case DECOR_HR:
			cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha * 0.25);
			cairo_rectangle (cr, left, top + 23, right - left, 2);
			cairo_fill (cr);
			break;
		}
	}
	cairo_restore (cr);
	return GDK_EVENT_PROPAGATE;
}

static gboolean
iter_for_event (GtkWidget *view, gdouble x, gdouble y, GtkTextIter *iter)
{
	gint bx, by;

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_WIDGET,
					       (gint) x, (gint) y, &bx, &by);
	return gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view), iter, bx, by);
}

/* Titelzeile eines <details>-Abschnitts an dieser Stelle, sonst NULL. */
static DetInfo *
toggle_at_iter (const GtkTextIter *iter)
{
	GSList *tags = gtk_text_iter_get_tags (iter), *l;
	DetInfo *info = NULL;

	for (l = tags; l != NULL && info == NULL; l = l->next)
		info = g_object_get_data (G_OBJECT (l->data), TOGGLE_KEY);
	g_slist_free (tags);
	return info;
}

static void schedule_revalidate (GtkWidget *view);

static gboolean
on_button_release (GtkWidget *view, GdkEventButton *event, gpointer user_data)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	LinkHandler *handler = g_object_get_data (G_OBJECT (view), HANDLER_KEY);
	GtkTextIter iter;
	const gchar *href;
	DetInfo *det;

	if (event->button != GDK_BUTTON_PRIMARY)
		return GDK_EVENT_PROPAGATE;
	if (gtk_text_buffer_get_has_selection (buffer))
		return GDK_EVENT_PROPAGATE;
	if (!iter_for_event (view, event->x, event->y, &iter))
		return GDK_EVENT_PROPAGATE;
	det = toggle_at_iter (&iter);
	if (det != NULL) {
		gboolean collapsed;

		g_object_get (det->content, "invisible", &collapsed, NULL);
		collapsed = !collapsed;
		g_object_set (det->content, "invisible", collapsed, NULL);
		g_object_set (det->closed, "invisible", !collapsed, NULL);
		g_object_set (det->open, "invisible", collapsed, NULL);
		schedule_revalidate (view);
		return GDK_EVENT_PROPAGATE;
	}
	href = href_at_iter (&iter);
	if (href != NULL && handler != NULL && handler->func != NULL)
		handler->func (view, href, handler->data);
	return GDK_EVENT_PROPAGATE;
}

static gboolean
on_motion (GtkWidget *view, GdkEventMotion *event, gpointer user_data)
{
	GtkTextIter iter;
	GdkWindow *win = gtk_text_view_get_window (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_TEXT);
	gboolean over = iter_for_event (view, event->x, event->y, &iter) &&
			(href_at_iter (&iter) != NULL || toggle_at_iter (&iter) != NULL);
	gboolean was = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view), "nolphin-md-over"));

	if (win != NULL && over != was) {
		GdkCursor *cursor = over ? gdk_cursor_new_from_name (gtk_widget_get_display (view), "pointer") : NULL;

		gdk_window_set_cursor (win, cursor);
		if (cursor != NULL)
			g_object_unref (cursor);
		g_object_set_data (G_OBJECT (view), "nolphin-md-over", GINT_TO_POINTER (over));
	}
	return GDK_EVENT_PROPAGATE;
}

GtkWidget *
nolphin_markdown_view_new (void)
{
	GtkWidget *view = gtk_text_view_new ();

	gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), VIEW_MARGIN);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), VIEW_MARGIN);

	g_signal_connect (view, "button-release-event", G_CALLBACK (on_button_release), NULL);
	g_signal_connect (view, "motion-notify-event", G_CALLBACK (on_motion), NULL);
	g_signal_connect (view, "style-updated", G_CALLBACK (apply_theme), NULL);
	g_signal_connect (view, "size-allocate", G_CALLBACK (on_size_allocate), NULL);
	g_signal_connect_after (view, "draw", G_CALLBACK (on_draw_after), NULL);

	nolphin_markdown_view_set_text (view, NULL);
	return view;
}

void
nolphin_markdown_view_set_text (GtkWidget *view, const gchar *markdown)
{
	nolphin_markdown_view_set_text_with_base (view, markdown, NULL);
}

void
nolphin_markdown_view_set_text_with_base (GtkWidget *view, const gchar *markdown, const gchar *base_dir)
{
	GtkTextBuffer *buffer;

	g_return_if_fail (GTK_IS_TEXT_VIEW (view));

	/* Neuer Puffer je Inhalt, damit die Link-Tags des alten Inhalts
	 * nicht in der Tag-Tabelle liegen bleiben. */
	buffer = gtk_text_buffer_new (NULL);
	nolphin_markdown_render_to_buffer_with_base (buffer, markdown, base_dir);
	g_object_set_data (G_OBJECT (view), KIDS_KEY, NULL);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), buffer);
	g_object_unref (buffer);
	attach_children (view);
	apply_theme (view);
	gtk_widget_queue_resize (view);
}

void
nolphin_markdown_view_set_link_handler (GtkWidget *view, NolphinMarkdownLinkFunc func, gpointer user_data)
{
	LinkHandler *handler = g_new0 (LinkHandler, 1);

	handler->func = func;
	handler->data = user_data;
	g_object_set_data_full (G_OBJECT (view), HANDLER_KEY, handler, g_free);
}

gchar *
nolphin_markdown_anchor_describe (GtkTextChildAnchor *anchor)
{
	const TableData *t;
	const gchar *img;
	gint check;

	g_return_val_if_fail (GTK_IS_TEXT_CHILD_ANCHOR (anchor), NULL);

	t = g_object_get_data (G_OBJECT (anchor), TABLE_KEY);
	img = g_object_get_data (G_OBJECT (anchor), IMG_PATH_KEY);
	check = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (anchor), CHECK_KEY));
	if (t != NULL) {
		GString *out = g_string_new (NULL);
		guint r, c;

		g_string_append_printf (out, "table:%ux%u:", t->rows->len, t->ncols);
		for (c = 0; c < t->ncols; c++)
			g_string_append_printf (out, "%s%c", c ? "," : "", t->aligns[c] == 1 ? 'c' : t->aligns[c] == 2 ? 'r' : 'l');
		for (r = 0; r < t->rows->len; r++) {
			gchar **row = g_ptr_array_index (t->rows, r);

			g_string_append (out, r ? "/" : ":");
			for (c = 0; c < t->ncols; c++)
				g_string_append_printf (out, "%s%s", c ? "|" : "", row[c]);
		}
		return g_string_free (out, FALSE);
	}
	if (check != 0)
		return g_strdup_printf ("check:%d", check == 1 ? 1 : 0);
	if (img != NULL) {
		const gchar *w = g_object_get_data (G_OBJECT (anchor), IMG_W_KEY);
		const gchar *h = g_object_get_data (G_OBJECT (anchor), IMG_H_KEY);

		if (w != NULL || h != NULL)
			return g_strdup_printf ("image:%s|w=%s|h=%s", img, w != NULL ? w : "", h != NULL ? h : "");
		return g_strdup_printf ("image:%s", img);
	}
	return NULL;
}

gboolean
nolphin_markdown_view_scroll_to_anchor (GtkWidget *view, const gchar *anchor)
{
	GtkTextBuffer *buffer;
	GtkTextMark *mark;
	gchar *slug, *name;

	g_return_val_if_fail (GTK_IS_TEXT_VIEW (view), FALSE);
	g_return_val_if_fail (anchor != NULL, FALSE);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	slug = g_utf8_strdown (anchor, -1);
	name = g_strconcat (ANCHOR_PREFIX, slug, NULL);
	mark = gtk_text_buffer_get_mark (buffer, name);
	g_free (slug);
	g_free (name);
	if (mark == NULL)
		return FALSE;
	gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (view), mark, 0.0, TRUE, 0.0, 0.0);
	return TRUE;
}

void
nolphin_markdown_view_set_max_image_width (GtkWidget *view, gint max_width)
{
	g_return_if_fail (GTK_IS_TEXT_VIEW (view));

	g_object_set_data (G_OBJECT (view), "nolphin-md-max-image-width", GINT_TO_POINTER (MAX (max_width, 0)));
	/* Breiten neu berechnen lassen */
	g_object_set_data (G_OBJECT (view), "nolphin-md-sep-width", GINT_TO_POINTER (0));
	gtk_widget_queue_resize (view);
}
