/*
 * This file is part of NetSurf, http://netsurf.sourceforge.net/
 * Licensed under the GNU General Public License,
 *                http://www.opensource.org/licenses/gpl-license
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2005 John M Bell <jmb202@ecs.soton.ac.uk>
 */

/** \file
 * Conversion of XML tree to box tree (implementation).
 */

#define _GNU_SOURCE  /* for strndup */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "libxml/HTMLparser.h"
#include "netsurf/utils/config.h"
#include "netsurf/content/content.h"
#include "netsurf/css/css.h"
#include "netsurf/desktop/options.h"
#include "netsurf/render/box.h"
#include "netsurf/render/form.h"
#include "netsurf/render/html.h"
#ifdef riscos
#include "netsurf/desktop/gui.h"
#endif
#define NDEBUG
#include "netsurf/utils/log.h"
#include "netsurf/utils/messages.h"
#include "netsurf/utils/pool.h"
#include "netsurf/utils/url.h"
#include "netsurf/utils/utils.h"


/** Status of box tree construction. */
struct box_status {
	struct content *content;
	char *href;
	char *title;
	struct form *current_form;
	char *id;
};

/** Return type for special case element functions. */
struct box_result {
	/** Box for element, if any, 0 otherwise. */
	struct box *box;
	/** Children of this element should be converted. */
	bool convert_children;
	/** Memory was exhausted when handling the element. */
	bool memory_error;
};

/** MultiLength, as defined by HTML 4.01. */
struct box_multi_length {
	enum { LENGTH_PX, LENGTH_PERCENT, LENGTH_RELATIVE } type;
	float value;
};


static const content_type image_types[] = {
#ifdef WITH_JPEG
	CONTENT_JPEG,
#endif
#ifdef WITH_GIF
	CONTENT_GIF,
#endif
#ifdef WITH_PNG
	CONTENT_PNG,
#endif
#ifdef WITH_MNG
	CONTENT_JNG,
	CONTENT_MNG,
#endif
#ifdef WITH_SPRITE
	CONTENT_SPRITE,
#endif
#ifdef WITH_DRAW
	CONTENT_DRAW,
#endif
	CONTENT_UNKNOWN };

#define MAX_SPAN (100)


static bool convert_xml_to_box(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status);
bool box_construct_element(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status);
bool box_construct_text(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status);
static struct css_style * box_get_style(struct content *c,
		struct css_style *parent_style,
		xmlNode *n);
static void box_solve_display(struct css_style *style, bool root);
static void box_text_transform(char *s, unsigned int len,
		css_text_transform tt);
static struct box_result box_a(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_body(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_br(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_image(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_form(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_textarea(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_select(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_input(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box *box_input_text(xmlNode *n, struct box_status *status,
		struct css_style *style, bool password);
static struct box_result box_button(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_frameset(xmlNode *n, struct box_status *status,
		struct css_style *style);
static bool box_select_add_option(struct form_control *control, xmlNode *n);
static struct box_result box_object(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_embed(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_applet(xmlNode *n, struct box_status *status,
		struct css_style *style);
static struct box_result box_iframe(xmlNode *n, struct box_status *status,
		struct css_style *style);
static bool plugin_decode(struct content* content, struct box* box);
static struct box_multi_length *box_parse_multi_lengths(const char *s,
		unsigned int *count);


/* element_table must be sorted by name */
struct element_entry {
	char name[10];   /* element type */
	struct box_result (*convert)(xmlNode *n, struct box_status *status,
			struct css_style *style);
};
static const struct element_entry element_table[] = {
	{"a", box_a},
/*	{"applet", box_applet},*/
	{"body", box_body},
	{"br", box_br},
	{"button", box_button},
	{"embed", box_embed},
	{"form", box_form},
	{"frameset", box_frameset},
	{"iframe", box_iframe},
	{"img", box_image},
	{"input", box_input},
	{"object", box_object},
	{"select", box_select},
	{"textarea", box_textarea}
};
#define ELEMENT_TABLE_COUNT (sizeof(element_table) / sizeof(element_table[0]))


/**
 * Construct a box tree from an xml tree and stylesheets.
 *
 * \param  n  xml tree
 * \param  c  content of type CONTENT_HTML to construct box tree in
 * \return  true on success, false on memory exhaustion
 */

bool xml_to_box(xmlNode *n, struct content *c)
{
	struct box root;
	struct box_status status = {c, 0, 0, 0, 0};
	struct box *inline_container = 0;

	assert(c->type == CONTENT_HTML);

	root.type = BOX_BLOCK;
	root.style = NULL;
	root.next = NULL;
	root.prev = NULL;
	root.children = NULL;
	root.last = NULL;
	root.parent = NULL;
	root.float_children = NULL;
	root.next_float = NULL;

	c->data.html.style = css_duplicate_style(&css_base_style);
	if (!c->data.html.style)
		return false;
	c->data.html.style->font_size.value.length.value =
			option_font_size * 0.1;

	c->data.html.object_count = 0;
	c->data.html.object = 0;

	if (!convert_xml_to_box(n, c, c->data.html.style, &root,
			&inline_container, status))
		return false;
	if (!box_normalise_block(&root, c->data.html.box_pool))
		return false;

	c->data.html.layout = root.children;
	c->data.html.layout->parent = NULL;

	return true;
}


/* mapping from CSS display to box type
 * this table must be in sync with css/css_enums */
static const box_type box_map[] = {
	0, /*CSS_DISPLAY_INHERIT,*/
	BOX_INLINE, /*CSS_DISPLAY_INLINE,*/
	BOX_BLOCK, /*CSS_DISPLAY_BLOCK,*/
	BOX_BLOCK, /*CSS_DISPLAY_LIST_ITEM,*/
	BOX_INLINE, /*CSS_DISPLAY_RUN_IN,*/
	BOX_INLINE_BLOCK, /*CSS_DISPLAY_INLINE_BLOCK,*/
	BOX_TABLE, /*CSS_DISPLAY_TABLE,*/
	BOX_TABLE, /*CSS_DISPLAY_INLINE_TABLE,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_ROW_GROUP,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_HEADER_GROUP,*/
	BOX_TABLE_ROW_GROUP, /*CSS_DISPLAY_TABLE_FOOTER_GROUP,*/
	BOX_TABLE_ROW, /*CSS_DISPLAY_TABLE_ROW,*/
	BOX_INLINE, /*CSS_DISPLAY_TABLE_COLUMN_GROUP,*/
	BOX_INLINE, /*CSS_DISPLAY_TABLE_COLUMN,*/
	BOX_TABLE_CELL, /*CSS_DISPLAY_TABLE_CELL,*/
	BOX_INLINE /*CSS_DISPLAY_TABLE_CAPTION,*/
};


/**
 * Recursively construct a box tree from an xml tree and stylesheets.
 *
 * \param  n             fragment of xml tree
 * \param  content       content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree
 * \param  parent        parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *                       new current inline container on exit
 * \param  status        status for forms etc.
 * \return  true on success, false on memory exhaustion
 */

bool convert_xml_to_box(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status)
{
	switch (n->type) {
	case XML_ELEMENT_NODE:
		return box_construct_element(n, content, parent_style, parent,
				inline_container, status);
	case XML_TEXT_NODE:
		return box_construct_text(n, content, parent_style, parent,
				inline_container, status);
	default:
		/* not an element or text node: ignore it (eg. comment) */
		return true;
	}
}


/**
 * Construct the box tree for an XML element.
 *
 * \param  n             XML node of type XML_ELEMENT_NODE
 * \param  content       content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree
 * \param  parent        parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *                       new current inline container on exit
 * \param  status        status for forms etc.
 * \return  true on success, false on memory exhaustion
 */

bool box_construct_element(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status)
{
	struct box *box = 0;
	struct box *inline_container_c;
	struct css_style *style = 0;
	xmlNode *c;
	char *s;
	xmlChar *title0, *id0;
	char *title = 0, *id = 0;
	bool convert_children = true;
	char *href_in = status.href;
	struct element_entry *element;

	assert(n);
	assert(n->type == XML_ELEMENT_NODE);
	assert(parent_style);
	assert(parent);
	assert(inline_container);

	gui_multitask();

	style = box_get_style(content, parent_style, n);
	if (!style)
		goto no_memory;
	if (style->display == CSS_DISPLAY_NONE) {
		css_free_style(style);
		goto end;
	}

	/* extract title attribute, if present */
	if ((title0 = xmlGetProp(n, (const xmlChar *) "title"))) {
		status.title = title = squash_whitespace(title0);
		xmlFree(title0);
		if (!title)
			goto no_memory;
	}

	/* extract id attribute, if present */
	if ((id0 = xmlGetProp(n, (const xmlChar *) "id"))) {
		status.id = id = squash_whitespace(id0);
		xmlFree(id0);
		if (!id)
			goto no_memory;
	}

	/* special elements */
	element = bsearch((const char *) n->name, element_table,
			ELEMENT_TABLE_COUNT, sizeof(element_table[0]),
			(int (*)(const void *, const void *)) strcmp);
	if (element) {
		/* a special convert function exists for this element */
		struct box_result res = element->convert(n, &status, style);
		box = res.box;
		convert_children = res.convert_children;
		if (res.memory_error)
			goto no_memory;
		if (!box) {
			/* no box for this element */
			assert(!convert_children);
			css_free_style(style);
			goto end;
		}
	} else {
		/* general element */
		box = box_create(style, status.href, title, id,
				content->data.html.box_pool);
		if (!box)
			goto no_memory;
	}
	/* set box type from style if it has not been set already */
	if (box->type == BOX_INLINE)
		box->type = box_map[style->display];

	content->size += sizeof(struct box) + sizeof(struct css_style);

	if (box->type == BOX_INLINE ||
			box->type == BOX_INLINE_BLOCK ||
			style->float_ == CSS_FLOAT_LEFT ||
			style->float_ == CSS_FLOAT_RIGHT ||
			box->type == BOX_BR) {
		/* this is an inline box */
		if (!*inline_container) {
			/* this is the first inline node: make a container */
			*inline_container = box_create(0, 0, 0, 0,
					content->data.html.box_pool);
			if (!*inline_container)
				goto no_memory;
			(*inline_container)->type = BOX_INLINE_CONTAINER;
			box_add_child(parent, *inline_container);
		}

		if (box->type == BOX_INLINE || box->type == BOX_BR) {
			/* inline box: add to tree and recurse */
			box_add_child(*inline_container, box);
			if (convert_children) {
				for (c = n->children; c != 0; c = c->next)
					if (!convert_xml_to_box(c, content,
							style, parent,
							inline_container,
							status))
						goto no_memory;
			}
			goto end;
		} else if (box->type == BOX_INLINE_BLOCK) {
			/* inline block box: add to tree and recurse */
			box_add_child(*inline_container, box);
			if (convert_children) {
				inline_container_c = 0;
				for (c = n->children; c != 0; c = c->next)
					if (!convert_xml_to_box(c, content,
							style, box,
							&inline_container_c,
							status))
						goto no_memory;
			}
			goto end;
		} else {
			/* float: insert a float box between the parent and
			 * current node */
			assert(style->float_ == CSS_FLOAT_LEFT ||
					style->float_ == CSS_FLOAT_RIGHT);
			parent = box_create(0, status.href, title, id,
					content->data.html.box_pool);
			if (!parent)
				goto no_memory;
			if (style->float_ == CSS_FLOAT_LEFT)
				parent->type = BOX_FLOAT_LEFT;
			else
				parent->type = BOX_FLOAT_RIGHT;
			box_add_child(*inline_container, parent);
			if (box->type == BOX_INLINE ||
					box->type == BOX_INLINE_BLOCK)
				box->type = BOX_BLOCK;
		}
	}

	/* non-inline box: add to tree and recurse */
	box_add_child(parent, box);
	if (convert_children) {
		inline_container_c = 0;
		for (c = n->children; c != 0; c = c->next)
			if (!convert_xml_to_box(c, content, style,
					box, &inline_container_c, status))
				goto no_memory;
	}
	if (style->float_ == CSS_FLOAT_NONE)
		/* new inline container unless this is a float */
		*inline_container = 0;

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "colspan")) != NULL) {
		box->columns = strtol(s, NULL, 10);
		if (MAX_SPAN < box->columns)
			box->columns = 1;
		xmlFree(s);
	}
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "rowspan")) != NULL) {
		box->rows = strtol(s, NULL, 10);
		if (MAX_SPAN < box->rows)
			box->rows = 1;
		xmlFree(s);
	}

end:
	free(title);
	free(id);
	if (!href_in)
		xmlFree(status.href);

	/* Now fetch any background image for this box */
	if (box && box->style && box->style->background_image.type ==
			CSS_BACKGROUND_IMAGE_URI) {
		char *url = strdup(box->style->background_image.uri);
		if (!url)
			return false;
		/* start fetch */
		if (!html_fetch_object(content, url, box, image_types,
				content->available_width, 1000, true))
			return false;
	}

	return true;

no_memory:
	free(title);
	free(id);
	if (!href_in)
		xmlFree(status.href);
	if (style && !box)
		css_free_style(style);

	return false;
}


/**
 * Construct the box tree for an XML text node.
 *
 * \param  n             XML node of type XML_TEXT_NODE
 * \param  content       content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree
 * \param  parent        parent in box tree
 * \param  inline_container  current inline container box, or 0, updated to
 *                       new current inline container on exit
 * \param  status        status for forms etc.
 * \return  true on success, false on memory exhaustion
 */

bool box_construct_text(xmlNode *n, struct content *content,
		struct css_style *parent_style,
		struct box *parent, struct box **inline_container,
		struct box_status status)
{
	struct box *box = 0;

	assert(n);
	assert(n->type == XML_TEXT_NODE);
	assert(parent_style);
	assert(parent);
	assert(inline_container);

	content->size += sizeof(struct box) + sizeof(struct css_style);

	if (parent_style->white_space == CSS_WHITE_SPACE_NORMAL ||
			 parent_style->white_space == CSS_WHITE_SPACE_NOWRAP) {
		char *text = squash_whitespace(n->content);
		if (!text)
			goto no_memory;

		/* if the text is just a space, combine it with the preceding
		 * text node, if any */
		if (text[0] == ' ' && text[1] == 0) {
			if (*inline_container) {
				assert((*inline_container)->last != 0);
				(*inline_container)->last->space = 1;
			}
			free(text);
			goto end;
		}

		if (!*inline_container) {
			/* this is the first inline node: make a container */
			*inline_container = box_create(0, 0, 0, 0,
					content->data.html.box_pool);
			if (!*inline_container)	{
				free(text);
				goto no_memory;
			}
			(*inline_container)->type = BOX_INLINE_CONTAINER;
			box_add_child(parent, *inline_container);
		}

		box = box_create(parent_style, status.href, 0, 0,
				content->data.html.box_pool);
		if (!box) {
			free(text);
			goto no_memory;
		}
		box->text = text;
		box->style_clone = 1;
		box->length = strlen(text);
		/* strip ending space char off */
		if (box->length > 1 && text[box->length - 1] == ' ') {
			box->space = 1;
			box->length--;
		}
		if (parent_style->text_transform != CSS_TEXT_TRANSFORM_NONE)
			box_text_transform(box->text, box->length,
					parent_style->text_transform);
		if (parent_style->white_space == CSS_WHITE_SPACE_NOWRAP) {
			unsigned int i;
			for (i = 0; i != box->length && text[i] != ' '; ++i)
				; /* no body */
			if (i != box->length) {
				/* there is a space in text block and we
				 * want all spaces to be converted to NBSP
				 */
				box->text = cnv_space2nbsp(text);
				if (!box->text) {
					free(text);
					goto no_memory;
				}
				box->length = strlen(box->text);
			}
		}

		box_add_child(*inline_container, box);
		if (box->text[0] == ' ') {
			box->length--;
			memmove(box->text, &box->text[1], box->length);
			if (box->prev != NULL)
				box->prev->space = 1;
		}
		goto end;

	} else {
		/* white-space: pre */
		char *text = cnv_space2nbsp(n->content);
		char *current;
		/* note: pre-wrap/pre-line are unimplemented */
		assert(parent_style->white_space == CSS_WHITE_SPACE_PRE ||
			parent_style->white_space ==
						CSS_WHITE_SPACE_PRE_LINE ||
			parent_style->white_space ==
						CSS_WHITE_SPACE_PRE_WRAP);
		if (!text)
			goto no_memory;
		if (parent_style->text_transform != CSS_TEXT_TRANSFORM_NONE)
			box_text_transform(text, strlen(text),
					parent_style->text_transform);
		current = text;
		do {
			size_t len = strcspn(current, "\r\n");
			char old = current[len];
			current[len] = 0;
			if (!*inline_container) {
				*inline_container = box_create(0, 0, 0, 0,
						content->data.html.box_pool);
				if (!*inline_container) {
					free(text);
					goto no_memory;
				}
				(*inline_container)->type =
						BOX_INLINE_CONTAINER;
				box_add_child(parent, *inline_container);
			}
			box = box_create(parent_style, status.href, 0,
					0, content->data.html.box_pool);
			if (!box) {
				free(text);
				goto no_memory;
			}
			box->type = BOX_INLINE;
			box->style_clone = 1;
			box->text = strdup(current);
			if (!box->text) {
				free(text);
				goto no_memory;
			}
			box->length = strlen(box->text);
			box_add_child(*inline_container, box);
			current[len] = old;
			current += len;
			if (current[0] == '\r' && current[1] == '\n') {
				current += 2;
				*inline_container = 0;
			} else if (current[0] != 0) {
				current++;
				*inline_container = 0;
			}
		} while (*current);
		free(text);
		goto end;
	}

end:
	return true;

no_memory:
	return false;
}


/**
 * Get the style for an element.
 *
 * \param  c             content of type CONTENT_HTML that is being processed
 * \param  parent_style  style at this point in xml tree
 * \param  n             node in xml tree
 * \return  the new style, or 0 on memory exhaustion
 *
 * The style is collected from three sources:
 *  1. any styles for this element in the document stylesheet(s)
 *  2. non-CSS HTML attributes
 *  3. the 'style' attribute
 */

struct css_style * box_get_style(struct content *c,
		struct css_style *parent_style,
		xmlNode *n)
{
	char *s;
	unsigned int i;
	unsigned int stylesheet_count = c->data.html.stylesheet_count;
	struct content **stylesheet = c->data.html.stylesheet_content;
	struct css_style *style;
	struct css_style *style_new;
	char *url;
	url_func_result res;

	style = css_duplicate_style(parent_style);
	if (!style)
		return 0;

	style_new = css_duplicate_style(&css_blank_style);
	if (!style_new) {
		css_free_style(style);
		return 0;
	}

	for (i = 0; i != stylesheet_count; i++) {
		if (stylesheet[i]) {
			assert(stylesheet[i]->type == CONTENT_CSS);
			css_get_style(stylesheet[i], n, style_new);
		}
	}
	css_cascade(style, style_new);

	/* style_new isn't needed past this point */
	css_free_style(style_new);

	/* This property only applies to the body element, if you believe
	 * the spec. Many browsers seem to allow it on other elements too,
	 * so let's be generic ;) */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "background"))) {
		res = url_join(s, c->data.html.base_url, &url);
		if (res == URL_FUNC_NOMEM) {
			css_free_style(style);
			return 0;
		} else if (res == URL_FUNC_OK) {
			/* if url is equivalent to the parent's url,
			 * we've got infinite inclusion: ignore */
			if (strcmp(url, c->data.html.base_url) == 0)
				free(url);
			else {
				style->background_image.type =
						CSS_BACKGROUND_IMAGE_URI;
				style->background_image.uri = url;
			}
		}
		xmlFree(s);
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "bgcolor")) != NULL) {
		unsigned int r, g, b;
		if (s[0] == '#' && sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) == 3)
			style->background_color = (b << 16) | (g << 8) | r;
		else if (s[0] != '#')
			style->background_color = named_colour(s);
		xmlFree(s);
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "color")) != NULL) {
		unsigned int r, g, b;
		if (s[0] == '#' && sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) == 3)
			style->color = (b << 16) | (g << 8) | r;
		else if (s[0] != '#')
			style->color = named_colour(s);
		xmlFree(s);
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "height")) != NULL) {
		float value = atof(s);
		if (value < 0 || strlen(s) == 0) {
			/* ignore negative values and height="" */
		} else if (strrchr(s, '%')) {
			/* the specification doesn't make clear what
			 * percentage heights mean, so ignore them */
		} else {
			style->height.height = CSS_HEIGHT_LENGTH;
			style->height.length.unit = CSS_UNIT_PX;
			style->height.length.value = value;
		}
		xmlFree(s);
	}

	if (strcmp((const char *) n->name, "input") == 0) {
		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "size")) != NULL) {
			int size = atoi(s);
			if (0 < size) {
				char *type = (char *) xmlGetProp(n, (const xmlChar *) "type");
				style->width.width = CSS_WIDTH_LENGTH;
				if (!type || strcasecmp(type, "text") == 0 ||
						strcasecmp(type, "password") == 0)
					/* in characters for text, password, file */
					style->width.value.length.unit = CSS_UNIT_EX;
				else if (strcasecmp(type, "file") != 0)
					/* in pixels otherwise */
					style->width.value.length.unit = CSS_UNIT_PX;
				style->width.value.length.value = size;
				if (type)
					xmlFree(type);
			}
			xmlFree(s);
		}
	}

	if (strcmp((const char *) n->name, "body") == 0) {
		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "text")) != NULL) {
			unsigned int r, g, b;
			if (s[0] == '#' && sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) == 3)
				style->color = (b << 16) | (g << 8) | r;
			else if (s[0] != '#')
				style->color = named_colour(s);
			xmlFree(s);
		}
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "width")) != NULL) {
		float value = atof(s);
		if (value < 0 || strlen(s) == 0) {
			/* ignore negative values and width="" */
		} else if (strrchr(s, '%')) {
			style->width.width = CSS_WIDTH_PERCENT;
			style->width.value.percent = value;
		} else {
			style->width.width = CSS_WIDTH_LENGTH;
			style->width.value.length.unit = CSS_UNIT_PX;
			style->width.value.length.value = value;
		}
		xmlFree(s);
	}

	if (strcmp((const char *) n->name, "textarea") == 0) {
		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "rows")) != NULL) {
			int value = atoi(s);
			if (0 < value) {
				style->height.height = CSS_HEIGHT_LENGTH;
				style->height.length.unit = CSS_UNIT_EM;
				style->height.length.value = value;
			}
			xmlFree(s);
		}
		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "cols")) != NULL) {
			int value = atoi(s);
			if (0 < value) {
				style->width.width = CSS_WIDTH_LENGTH;
				style->width.value.length.unit = CSS_UNIT_EX;
				style->width.value.length.value = value;
			}
			xmlFree(s);
		}
	}

	if (strcmp((const char *) n->name, "table") == 0) {
		if ((s = (char *) xmlGetProp(n,
				(const xmlChar *) "cellspacing"))) {
			if (!strrchr(s, '%')) {		/* % not implemented */
				int value = atoi(s);
				if (0 <= value) {
					style->border_spacing.border_spacing =
						CSS_BORDER_SPACING_LENGTH;
					style->border_spacing.horz.unit =
					style->border_spacing.vert.unit =
							CSS_UNIT_PX;
					style->border_spacing.horz.value =
					style->border_spacing.vert.value =
							value;
				}
			}
		}
		style->html_style.cellpadding.type = CSS_CELLPADDING_VALUE;
		if ((s = (char *) xmlGetProp(n,
				(const xmlChar *) "cellpadding"))) {
			if (!strrchr(s, '%')) {		/* % not implemented */
				int value = atoi(s);
				if (0 <= value) {
					style->html_style.cellpadding.value = value;
					/* todo: match <td> and <th> rules and don't set if they are */
					for (i = 0; i < 4; i++)
						style->padding[i].override_cellpadding = false;
				}
			}
		} else {
			style->html_style.cellpadding.value = 1;
		}
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "style")) != NULL) {
		struct css_style *astyle;
		astyle = css_duplicate_style(&css_empty_style);
		if (!astyle) {
			xmlFree(s);
			css_free_style(style);
			return 0;
		}
		css_parse_property_list(c, astyle, s);
		css_cascade(style, astyle);
		css_free_style(astyle);
		xmlFree(s);
	}

	box_solve_display(style, !n->parent);

	return style;
}


/**
 * Calculate 'display' based on 'display', 'position', and 'float', as given
 * by CSS 2.1 9.7.
 *
 * \param  style  style to update
 * \param  root   this is the root element
 */

void box_solve_display(struct css_style *style, bool root)
{
	if (style->display == CSS_DISPLAY_NONE)		/* 1. */
		return;
	else if (style->position == CSS_POSITION_ABSOLUTE ||
			style->position == CSS_POSITION_FIXED)	/* 2. */
		style->float_ = CSS_FLOAT_NONE;
	else if (style->float_ != CSS_FLOAT_NONE)		/* 3. */
		;
	else if (root)						/* 4. */
		;
	else							/* 5. */
		return;

	/* map specified value to computed value using table given in 9.7 */
	if (style->display == CSS_DISPLAY_INLINE_TABLE)
		style->display = CSS_DISPLAY_TABLE;
	else if (style->display == CSS_DISPLAY_LIST_ITEM ||
			style->display == CSS_DISPLAY_TABLE)
		; /* same as specified */
	else
		style->display = CSS_DISPLAY_BLOCK;
}


/**
 * Apply the CSS text-transform property to given text for its ASCII chars.
 *
 * \param  s    string to transform
 * \param  len  length of s
 * \param  tt   transform type
 */

void box_text_transform(char *s, unsigned int len,
		css_text_transform tt)
{
	unsigned int i;
	if (len == 0)
		return;
	switch (tt) {
		case CSS_TEXT_TRANSFORM_UPPERCASE:
			for (i = 0; i < len; ++i)
				if (s[i] < 0x80)
					s[i] = toupper(s[i]);
			break;
		case CSS_TEXT_TRANSFORM_LOWERCASE:
			for (i = 0; i < len; ++i)
				if (s[i] < 0x80)
					s[i] = tolower(s[i]);
			break;
		case CSS_TEXT_TRANSFORM_CAPITALIZE:
			if (s[0] < 0x80)
				s[0] = toupper(s[0]);
			for (i = 1; i < len; ++i)
				if (s[i] < 0x80 && isspace(s[i - 1]))
					s[i] = toupper(s[i]);
			break;
		default:
			break;
	}
}


/*
 * Special case elements
 *
 * These functions are called by convert_xml_to_box when an element is being
 * converted, according to the entries in element_table (top of file).
 *
 * The parameters are the xmlNode, a status structure for the conversion, and
 * the style found for the element.
 *
 * If a box is created, it is returned in the result structure. The
 * convert_children field should be 1 if convert_xml_to_box should convert the
 * node's children recursively, 0 if it should ignore them (presumably they
 * have been processed in some way by the function). If box is 0, no box will
 * be created for that element, and convert_children must be 0.
 */
struct box_result box_a(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	char *s, *s1;
	char *id = status->id;

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "href")) != NULL)
		status->href = s;

	/* name and id share the same namespace */
	if ((s1 = (char *) xmlGetProp(n, (const xmlChar *) "name")) != NULL) {
		if (status->id && strcmp(status->id, s1) == 0) {
			/* both specified and they match => ok */
			id = status->id;
		}
		else if (!status->id) {
			/* only name specified */
			id = squash_whitespace(s1);
			if (!id) {
				xmlFree(s1);
				return (struct box_result) {0, false, true};
			}
		} else
			/* both specified but no match */
			id = 0;

		xmlFree(s1);
	}

	box = box_create(style, status->href, status->title, id,
			status->content->data.html.box_pool);

	if (id && id != status->id)
		free(id);

	if (!box)
		return (struct box_result) {0, false, true};

	return (struct box_result) {box, true, false};
}

struct box_result box_body(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	status->content->data.html.background_colour = style->background_color;
	box = box_create(style, status->href, status->title, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};
	return (struct box_result) {box, true, false};
}

struct box_result box_br(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	box = box_create(style, status->href, status->title, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};
	box->type = BOX_BR;
	return (struct box_result) {box, false, false};
}

struct box_result box_image(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	char *s, *url, *s1, *map;
	xmlChar *s2;
	url_func_result res;

	box = box_create(style, status->href, status->title, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};

	/* handle alt text */
	if ((s2 = xmlGetProp(n, (const xmlChar *) "alt")) != NULL) {
		box->text = squash_whitespace(s2);
		xmlFree(s2);
		if (!box->text)
			return (struct box_result) {0, false, true};
		box->length = strlen(box->text);
	}

	/* imagemap associated with this image */
	if ((map = xmlGetProp(n, (const xmlChar *) "usemap")) != NULL) {
		if (map[0] == '#')
			box->usemap = strdup(map + 1);
		else
			box->usemap = strdup(map);
		xmlFree(map);
		if (!box->usemap) {
			free(box->text);
			return (struct box_result) {0, false, true};
		}
	}

	/* img without src is an error */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "src")) == NULL)
		return (struct box_result) {box, false, false};

	/* remove leading and trailing whitespace */
	s1 = strip(s);
	res = url_join(s1, status->content->data.html.base_url, &url);
	xmlFree(s);
	if (res == URL_FUNC_NOMEM) {
		free(box->text);
		return (struct box_result) {0, false, true};
	} else if (res == URL_FUNC_FAILED) {
		return (struct box_result) {box, false, false};
	}

	if (strcmp(url, status->content->data.html.base_url) == 0)
		/* if url is equivalent to the parent's url,
		 * we've got infinite inclusion: ignore */
		return (struct box_result) {box, false, false};

	/* start fetch */
	if (!html_fetch_object(status->content, url, box, image_types,
			status->content->available_width, 1000, false))
		return (struct box_result) {0, false, true};

	return (struct box_result) {box, false, false};
}

struct box_result box_form(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	char *action, *method, *enctype;
	form_method fmethod;
	struct box *box;
	struct form *form;

	box = box_create(style, status->href, status->title, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};

	if (!(action = (char *) xmlGetProp(n, (const xmlChar *) "action"))) {
		/* the action attribute is required */
		return (struct box_result) {box, true, false};
	}

	fmethod = method_GET;
	if ((method = (char *) xmlGetProp(n, (const xmlChar *) "method"))) {
		if (strcasecmp(method, "post") == 0) {
			fmethod = method_POST_URLENC;
			if ((enctype = (char *) xmlGetProp(n,
					(const xmlChar *) "enctype"))) {
				if (strcasecmp(enctype,
						"multipart/form-data") == 0)
					fmethod = method_POST_MULTIPART;
				xmlFree(enctype);
			}
		}
		xmlFree(method);
	}

	status->current_form = form = form_new(action, fmethod);
	if (!form) {
		xmlFree(action);
		return (struct box_result) {0, false, true};
	}

	return (struct box_result) {box, true, false};
}

struct box_result box_textarea(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	/* A textarea is an INLINE_BLOCK containing a single INLINE_CONTAINER,
	 * which contains the text as runs of INLINE separated by BR. There is
	 * at least one INLINE. The first and last boxes are INLINE.
	 * Consecutive BR may not be present. These constraints are satisfied
	 * by using a 0-length INLINE for blank lines. */

	xmlChar *content, *current;
	struct box *box, *inline_container, *inline_box, *br_box;
	char *s;
	size_t len;

	box = box_create(style, NULL, 0, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};
	box->type = BOX_INLINE_BLOCK;
	box->gadget = form_new_control(GADGET_TEXTAREA);
	if (!box->gadget)
		return (struct box_result) {0, false, true};
	box->gadget->box = box;

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "name")) != NULL) {
		box->gadget->name = strdup(s);
		xmlFree(s);
		if (!box->gadget->name)
			return (struct box_result) {0, false, true};
	}

	inline_container = box_create(0, 0, 0, 0,
			status->content->data.html.box_pool);
	if (!inline_container)
		return (struct box_result) {0, false, true};
	inline_container->type = BOX_INLINE_CONTAINER;
	box_add_child(box, inline_container);

	current = content = xmlNodeGetContent(n);
	while (1) {
		/* BOX_INLINE */
		len = strcspn(current, "\r\n");
		s = strndup(current, len);
		if (!s) {
			box_free(box);
			xmlFree(content);
			return (struct box_result) {NULL, false, false};
		}

		inline_box = box_create(style, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!inline_box)
			return (struct box_result) {0, false, true};
		inline_box->type = BOX_INLINE;
		inline_box->style_clone = 1;
		inline_box->text = s;
		inline_box->length = len;
		box_add_child(inline_container, inline_box);

		current += len;
		if (current[0] == 0)
			/* finished */
			break;

		/* BOX_BR */
		br_box = box_create(style, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!br_box)
			return (struct box_result) {0, false, true};
		br_box->type = BOX_BR;
		br_box->style_clone = 1;
		box_add_child(inline_container, br_box);

		if (current[0] == '\r' && current[1] == '\n')
			current += 2;
		else
			current++;
	}
	xmlFree(content);

	if (status->current_form)
		form_add_control(status->current_form, box->gadget);

	return (struct box_result) {box, false, false};
}

struct box_result box_select(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	struct box *inline_container;
	struct box *inline_box;
	struct form_control *gadget;
	char* s;
	xmlNode *c, *c2;

	gadget = form_new_control(GADGET_SELECT);
	if (!gadget)
		return (struct box_result) {0, false, true};

	gadget->data.select.multiple = false;
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "multiple")) != NULL) {
		gadget->data.select.multiple = true;
		xmlFree(s);
	}

	gadget->data.select.items = NULL;
	gadget->data.select.last_item = NULL;
	gadget->data.select.num_items = 0;
	gadget->data.select.num_selected = 0;

	for (c = n->children; c; c = c->next) {
		if (strcmp((const char *) c->name, "option") == 0) {
			if (!box_select_add_option(gadget, c))
				goto no_memory;
		} else if (strcmp((const char *) c->name, "optgroup") == 0) {
			for (c2 = c->children; c2; c2 = c2->next) {
				if (strcmp((const char *) c2->name,
						"option") == 0) {
					if (!box_select_add_option(gadget, c2))
						goto no_memory;
				}
			}
		}
	}

	if (gadget->data.select.num_items == 0) {
		/* no options: ignore entire select */
		form_free_control(gadget);
		return (struct box_result) {0, false, false};
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "name")) != NULL) {
		gadget->name = strdup(s);
		xmlFree(s);
		if (!gadget->name)
			goto no_memory;
	}

	box = box_create(style, NULL, 0, status->id,
			status->content->data.html.box_pool);
	if (!box)
		goto no_memory;
	box->type = BOX_INLINE_BLOCK;
	box->gadget = gadget;
	gadget->box = box;

	inline_container = box_create(0, 0, 0, 0,
			status->content->data.html.box_pool);
	if (!inline_container)
		goto no_memory;
	inline_container->type = BOX_INLINE_CONTAINER;
	inline_box = box_create(style, 0, 0, 0,
			status->content->data.html.box_pool);
	if (!inline_box)
		goto no_memory;
	inline_box->type = BOX_INLINE;
	inline_box->style_clone = 1;
	box_add_child(inline_container, inline_box);
	box_add_child(box, inline_container);

	if (!gadget->data.select.multiple &&
			gadget->data.select.num_selected == 0) {
		gadget->data.select.current = gadget->data.select.items;
		gadget->data.select.current->initial_selected =
			gadget->data.select.current->selected = true;
		gadget->data.select.num_selected = 1;
	}

	if (gadget->data.select.num_selected == 0)
		inline_box->text = strdup(messages_get("Form_None"));
	else if (gadget->data.select.num_selected == 1)
		inline_box->text = strdup(gadget->data.select.current->text);
	else
		inline_box->text = strdup(messages_get("Form_Many"));
	if (!inline_box->text)
		goto no_memory;

	inline_box->length = strlen(inline_box->text);

	if (status->current_form)
		form_add_control(status->current_form, gadget);

	return (struct box_result) {box, false, false};

no_memory:
	form_free_control(gadget);
	return (struct box_result) {0, false, true};
}


/**
 * Add an option to a form select control.
 *
 * \param  control  select containing the option
 * \param  n        xml element node for <option>
 * \return  true on success, false on memory exhaustion
 */

bool box_select_add_option(struct form_control *control, xmlNode *n)
{
	char *value = 0;
	char *text = 0;
	bool selected;
	xmlChar *content;
	xmlChar *s;

	content = xmlNodeGetContent(n);
	if (!content)
		goto no_memory;
	text = squash_whitespace(content);
	xmlFree(content);
	if (!text)
		goto no_memory;

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "value"))) {
		value = strdup(s);
		xmlFree(s);
	} else
		value = strdup(text);
	if (!value)
		goto no_memory;

	selected = xmlHasProp(n, (const xmlChar *) "selected");

	if (!form_add_option(control, value, text, selected))
		goto no_memory;

	return true;

no_memory:
	free(value);
	free(text);
	return false;
}


struct box_result box_input(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box* box = NULL;
	struct form_control *gadget = NULL;
	char *s, *type, *url;
	url_func_result res;

	type = (char *) xmlGetProp(n, (const xmlChar *) "type");

	if (type && strcasecmp(type, "password") == 0) {
		box = box_input_text(n, status, style, true);
		if (!box)
			goto no_memory;
		gadget = box->gadget;
		gadget->box = box;

	} else if (type && strcasecmp(type, "file") == 0) {
		box = box_create(style, NULL, 0, status->id,
				status->content->data.html.box_pool);
		if (!box)
			goto no_memory;
		box->type = BOX_INLINE_BLOCK;
		box->gadget = gadget = form_new_control(GADGET_FILE);
		if (!gadget)
			goto no_memory;
		gadget->box = box;

	} else if (type && strcasecmp(type, "hidden") == 0) {
		/* no box for hidden inputs */
		gadget = form_new_control(GADGET_HIDDEN);
		if (!gadget)
			goto no_memory;

		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "value"))) {
			gadget->value = strdup(s);
			xmlFree(s);
			if (!gadget->value)
				goto no_memory;
			gadget->length = strlen(gadget->value);
		}

	} else if (type && (strcasecmp(type, "checkbox") == 0 ||
			strcasecmp(type, "radio") == 0)) {
		box = box_create(style, NULL, 0, status->id,
				status->content->data.html.box_pool);
		if (!box)
			goto no_memory;
		box->gadget = gadget = form_new_control(type[0] == 'c' ||
				type[0] == 'C' ? GADGET_CHECKBOX :
				GADGET_RADIO);
		if (!gadget)
			goto no_memory;
		gadget->box = box;

		gadget->selected = xmlHasProp(n, (const xmlChar *) "checked");

		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "value")) != NULL) {
			gadget->value = strdup(s);
			xmlFree(s);
			if (!gadget->value)
				goto no_memory;
			gadget->length = strlen(gadget->value);
		}

	} else if (type && (strcasecmp(type, "submit") == 0 ||
			strcasecmp(type, "reset") == 0)) {
		struct box_result result = box_button(n, status, style);
		struct box *inline_container, *inline_box;
		if (result.memory_error)
			goto no_memory;
		box = result.box;
		inline_container = box_create(0, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!inline_container)
			goto no_memory;
		inline_container->type = BOX_INLINE_CONTAINER;
		inline_box = box_create(style, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!inline_box)
			goto no_memory;
		inline_box->type = BOX_INLINE;
		inline_box->style_clone = 1;
		if (box->gadget->value != NULL)
			inline_box->text = strdup(box->gadget->value);
		else if (box->gadget->type == GADGET_SUBMIT)
			inline_box->text = strdup(messages_get("Form_Submit"));
		else
			inline_box->text = strdup(messages_get("Form_Reset"));
		if (!inline_box->text)
			goto no_memory;
		inline_box->length = strlen(inline_box->text);
		box_add_child(inline_container, inline_box);
		box_add_child(box, inline_container);

	} else if (type && strcasecmp(type, "button") == 0) {
		struct box_result result = box_button(n, status, style);
		struct box *inline_container, *inline_box;
		if (result.memory_error)
			goto no_memory;
		box = result.box;
		inline_container = box_create(0, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!inline_container)
			goto no_memory;
		inline_container->type = BOX_INLINE_CONTAINER;
		inline_box = box_create(style, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!inline_box)
			goto no_memory;
		inline_box->type = BOX_INLINE;
		inline_box->style_clone = 1;
		if ((s = (char *) xmlGetProp(n, (const xmlChar *) "value")))
			inline_box->text = strdup(s);
		else
			inline_box->text = strdup("Button");
		if (!inline_box->text)
			goto no_memory;
		inline_box->length = strlen(inline_box->text);
		box_add_child(inline_container, inline_box);
		box_add_child(box, inline_container);

	} else if (type && strcasecmp(type, "image") == 0) {
		box = box_create(style, NULL, 0, status->id,
				status->content->data.html.box_pool);
		if (!box)
			goto no_memory;
		box->gadget = gadget = form_new_control(GADGET_IMAGE);
		if (!gadget)
			goto no_memory;
		gadget->box = box;
		gadget->type = GADGET_IMAGE;
		if ((s = (char *) xmlGetProp(n, (const xmlChar*) "src")) != NULL) {
			res = url_join(s, status->content->data.html.base_url, &url);
			/* if url is equivalent to the parent's url,
			 * we've got infinite inclusion. stop it here.
			 * also bail if url_join failed.
			 */
			if (res == URL_FUNC_OK &&
					strcasecmp(url, status->content->data.html.base_url) != 0)
				if (!html_fetch_object(status->content, url, box,
						image_types,
						status->content->available_width,
						1000, false))
					goto no_memory;
			xmlFree(s);
		}

	} else {
		/* the default type is "text" */
		box = box_input_text(n, status, style, false);
		if (!box)
			goto no_memory;
		gadget = box->gadget;
		gadget->box = box;
	}

	if (type != 0)
		xmlFree(type);

	if (gadget != 0) {
		if (status->current_form)
			form_add_control(status->current_form, gadget);
		else
			gadget->form = 0;
		s = (char *) xmlGetProp(n, (const xmlChar *) "name");
		if (s) {
			gadget->name = strdup(s);
			xmlFree(s);
			if (!gadget->name)
				goto no_memory;
		}
	}

	return (struct box_result) {box, false, false};

no_memory:
	if (type)
		xmlFree(type);
	if (gadget)
		form_free_control(gadget);

	return (struct box_result) {0, false, true};
}

struct box *box_input_text(xmlNode *n, struct box_status *status,
		struct css_style *style, bool password)
{
	char *s;
	struct box *box = box_create(style, 0, 0, status->id,
			status->content->data.html.box_pool);
	struct box *inline_container, *inline_box;

	if (!box)
		return 0;

	box->type = BOX_INLINE_BLOCK;

	box->gadget = form_new_control((password) ? GADGET_PASSWORD : GADGET_TEXTBOX);
	if (!box->gadget)
		return 0;
	box->gadget->box = box;

	box->gadget->maxlength = 100;
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "maxlength")) != NULL) {
		box->gadget->maxlength = atoi(s);
		xmlFree(s);
	}

	s = (char *) xmlGetProp(n, (const xmlChar *) "value");
	box->gadget->value = strdup((s != NULL) ? s : "");
	box->gadget->initial_value = strdup((box->gadget->value != NULL) ? box->gadget->value : "");
	if (s)
		xmlFree(s);
	if (box->gadget->value == NULL || box->gadget->initial_value == NULL) {
		box_free(box);
		return NULL;
	}
	box->gadget->length = strlen(box->gadget->value);

	inline_container = box_create(0, 0, 0, 0,
			status->content->data.html.box_pool);
	if (!inline_container)
		return 0;
	inline_container->type = BOX_INLINE_CONTAINER;
	inline_box = box_create(style, 0, 0, 0,
			status->content->data.html.box_pool);
	if (!inline_box)
		return 0;
	inline_box->type = BOX_INLINE;
	inline_box->style_clone = 1;
	if (password) {
		inline_box->length = strlen(box->gadget->value);
		inline_box->text = malloc(inline_box->length + 1);
		if (!inline_box->text)
			return 0;
		memset(inline_box->text, '*', inline_box->length);
		inline_box->text[inline_box->length] = '\0';
	} else {
		/* replace spaces/TABs with hard spaces to prevent line wrapping */
		inline_box->text = cnv_space2nbsp(box->gadget->value);
		if (!inline_box->text)
			return 0;
		inline_box->length = strlen(inline_box->text);
	}
	box_add_child(inline_container, inline_box);
	box_add_child(box, inline_container);

	return box;
}

struct box_result box_button(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	xmlChar *s;
	char *type = (char *) xmlGetProp(n, (const xmlChar *) "type");
	struct box *box = box_create(style, 0, 0, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};
	box->type = BOX_INLINE_BLOCK;

	if (!type || strcasecmp(type, "submit") == 0) {
		box->gadget = form_new_control(GADGET_SUBMIT);
	} else if (strcasecmp(type, "reset") == 0) {
		box->gadget = form_new_control(GADGET_RESET);
	} else {
		/* type="button" or unknown: just render the contents */
		xmlFree(type);
		return (struct box_result) {box, true, false};
	}

	if (type)
		xmlFree(type);

	if (!box->gadget) {
		box_free_box(box);
		return (struct box_result) {0, false, true};
	}

	if (status->current_form)
		form_add_control(status->current_form, box->gadget);
	else
		box->gadget->form = 0;
	box->gadget->box = box;
	if ((s = xmlGetProp(n, (const xmlChar *) "name")) != NULL) {
		box->gadget->name = strdup((char *) s);
		xmlFree(s);
		if (!box->gadget->name) {
			box_free_box(box);
			return (struct box_result) {0, false, true};
		}
	}
	if ((s = xmlGetProp(n, (const xmlChar *) "value")) != NULL) {
		box->gadget->value = strdup((char *) s);
		xmlFree(s);
		if (!box->gadget->value) {
			box_free_box(box);
			return (struct box_result) {0, false, true};
		}
	}

	return (struct box_result) {box, true, false};
}


/**
 * add an object to the box tree
 */
struct box_result box_object(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	struct object_params *po;
	struct plugin_params *pp = NULL;
	char *s, *map;
	xmlNode *c;

	po = calloc(1, sizeof(struct  object_params));
	if (!po)
		return (struct box_result) {0, false, true};

	box = box_create(style, status->href, 0, status->id,
			status->content->data.html.box_pool);
	if (!box) {
		free(po);
		return (struct box_result) {0, false, true};
	}

	/* object data */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "data")) != NULL) {
		po->data = strdup(s);
		xmlFree(s);
		if (!po->data)
			goto no_memory;
		LOG(("object '%s'", po->data));
	}

	/* imagemap associated with this object */
	if ((map = xmlGetProp(n, (const xmlChar *) "usemap")) != NULL) {
		box->usemap = (map[0] == '#') ? strdup(map+1) : strdup(map);
		xmlFree(map);
		if (!box->usemap)
			goto no_memory;
	}

	/* object type */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "type")) != NULL) {
		po->type = strdup(s);
		xmlFree(s);
		if (!po->type)
			goto no_memory;
		LOG(("type: %s", po->type));
	}

	/* object codetype */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "codetype")) != NULL) {
		po->codetype = strdup(s);
		xmlFree(s);
		if (!po->codetype)
			goto no_memory;
		LOG(("codetype: %s", po->codetype));
	}

	/* object codebase */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "codebase")) != NULL) {
		po->codebase = strdup(s);
		xmlFree(s);
		if (!po->codebase)
			goto no_memory;
		LOG(("codebase: %s", po->codebase));
	}

	/* object classid */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "classid")) != NULL) {
		po->classid = strdup(s);
		xmlFree(s);
		if (!po->classid)
			goto no_memory;
		LOG(("classid: %s", po->classid));
	}

	/* parameters
	 * parameter data is stored in a singly linked list.
	 * po->params points to the head of the list.
	 * new parameters are added to the head of the list.
	 */
	for (c = n->children; c != NULL; c = c->next) {
		if (c->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char *) c->name, "param") == 0) {
			pp = calloc(1, sizeof(struct plugin_params));
			if (!pp)
				goto no_memory;

			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "name")) != NULL) {
				pp->name = strdup(s);
				xmlFree(s);
				if (!pp->name)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "value")) != NULL) {
				pp->value = strdup(s);
				xmlFree(s);
				if (!pp->value)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "type")) != NULL) {
				pp->type = strdup(s);
				xmlFree(s);
				if (!pp->type)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "valuetype")) != NULL) {
				pp->valuetype = strdup(s);
				xmlFree(s);
				if (!pp->valuetype)
					goto no_memory;
			} else {
				pp->valuetype = strdup("data");
				if (!pp->valuetype)
					goto no_memory;
			}

			pp->next = po->params;
			po->params = pp;
		} else {
				/* The first non-param child is the start
				 * of the alt html. Therefore, we should
				 * break out of this loop.
				 */
				break;
		}
	}

	box->object_params = po;

	/* start fetch */
	if (plugin_decode(status->content, box))
		return (struct box_result) {box, false, false};

	return (struct box_result) {box, true, false};

no_memory:
	if (pp && pp != po->params) {
		/* ran out of memory creating parameter struct */
		free(pp->name);
		free(pp->value);
		free(pp->type);
		free(pp->valuetype);
		free(pp);
	}

	box_free_object_params(po);
	box_free_box(box);

	return (struct box_result) {0, false, true};
}

/**
 * add an embed to the box tree
 */
struct box_result box_embed(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	struct object_params *po;
	struct plugin_params *pp = NULL;
	char *s;
	xmlAttr *a;

	po = calloc(1, sizeof(struct object_params));
	if (!po)
		return (struct box_result) {0, false, true};

	box = box_create(style, status->href, 0, status->id,
			status->content->data.html.box_pool);
	if (!box) {
		free(po);
		return (struct box_result) {0, false, true};
	}

	/* embed src */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "src")) != NULL) {
		LOG(("embed '%s'", s));
		po->data = strdup(s);
		xmlFree(s);
		if (!po->data)
			goto no_memory;
	}

	/**
	 * we munge all other attributes into a plugin_parameter structure
	 */
	for (a=n->properties; a != NULL; a=a->next) {
		pp = calloc(1, sizeof(struct plugin_params));
		if (!pp)
			goto no_memory;

		if (strcasecmp((const char*)a->name, "src") != 0 &&
				a->children && a->children->content) {
			pp->name = strdup((const char*)a->name);
			pp->value = strdup((char*)a->children->content);
			pp->valuetype = strdup("data");
			if (!pp->name || !pp->value || !pp->valuetype)
				goto no_memory;

			pp->next = po->params;
			po->params = pp;
		}
	 }

	box->object_params = po;

	/* start fetch */
	/* embeds have no content, so we don't care if this returns false */
	plugin_decode(status->content, box);

	return (struct box_result) {box, false, false};

no_memory:
	if (pp && pp != po->params) {
		/* ran out of memory creating parameter struct */
		free(pp->name);
		free(pp->value);
		free(pp->type);
		free(pp->valuetype);
		free(pp);
	}

	box_free_object_params(po);
	box_free_box(box);

	return (struct box_result) {0, false, true};
}

/**
 * add an applet to the box tree
 *
 * \todo This needs reworking to be compliant to the spec
 * For now, we simply ignore all applet tags.
 */
struct box_result box_applet(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	struct object_params *po;
	struct plugin_params *pp = NULL;
	char *s;
	xmlNode *c;

	po = calloc(1, sizeof(struct object_params));
	if (!po)
		return (struct box_result) {0, false, true};

	box = box_create(style, status->href, 0, status->id,
			status->content->data.html.box_pool);
	if (!box) {
		free(po);
		return (struct box_result) {0, false, true};
	}

	/* archive */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "archive")) != NULL) {
		/** \todo tokenise this comma separated list */
		LOG(("archive '%s'", s));
		po->data = strdup(s);
		xmlFree(s);
		if (!po->data)
			goto no_memory;
	}
	/* code */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "code")) != NULL) {
		LOG(("applet '%s'", s));
		po->classid = strdup(s);
		xmlFree(s);
		if (!po->classid)
			goto no_memory;
	}

	/* object codebase */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "codebase")) != NULL) {
		po->codebase = strdup(s);
		LOG(("codebase: %s", s));
		xmlFree(s);
		if (!po->codebase)
			goto no_memory;
	}

	/* parameters
	 * parameter data is stored in a singly linked list.
	 * po->params points to the head of the list.
	 * new parameters are added to the head of the list.
	 */
	for (c = n->children; c != 0; c = c->next) {
		if (c->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp((const char *) c->name, "param") == 0) {
			pp = calloc(1, sizeof(struct plugin_params));
			if (!pp)
				goto no_memory;

			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "name")) != NULL) {
				pp->name = strdup(s);
				xmlFree(s);
				if (!pp->name)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "value")) != NULL) {
				pp->value = strdup(s);
				xmlFree(s);
				if (!pp->value)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "type")) != NULL) {
				pp->type = strdup(s);
				xmlFree(s);
				if (!pp->type)
					goto no_memory;
			}
			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "valuetype")) != NULL) {
				pp->valuetype = strdup(s);
				xmlFree(s);
				if (!pp->valuetype)
					goto no_memory;
			} else {
				pp->valuetype = strdup("data");
				if (!pp->valuetype)
					goto no_memory;
			}

			pp->next = po->params;
			po->params = pp;
		} else {
				 /* The first non-param child is the start
				  * of the alt html. Therefore, we should
				  * break out of this loop.
				  */
				break;
		}
	}

	box->object_params = po;

	/* start fetch */
	if (plugin_decode(status->content, box))
		return (struct box_result) {box, false, false};

	return (struct box_result) {box, true, false};

no_memory:
	if (pp && pp != po->params) {
		/* ran out of memory creating parameter struct */
		free(pp->name);
		free(pp->value);
		free(pp->type);
		free(pp->valuetype);
		free(pp);
	}

	box_free_object_params(po);
	box_free_box(box);

	return (struct box_result) {0, false, true};
}

/**
 * box_iframe
 * add an iframe to the box tree
 * TODO - implement GUI nested wimp stuff 'cos this looks naff atm. (16_5)
 */
struct box_result box_iframe(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	struct box *box;
	struct object_params *po;
	char *s;

	po = calloc(1, sizeof(struct object_params));
	if (!po)
		return (struct box_result) {0, false, true};

	box = box_create(style, status->href, 0, status->id,
			status->content->data.html.box_pool);
	if (!box) {
		free(po);
		return (struct box_result) {0, false, true};
	}

	/* iframe src */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "src")) != NULL) {
		LOG(("iframe '%s'", s));
		po->data = strdup(s);
		xmlFree(s);
		if (!po->data)
			goto no_memory;
	}

	box->object_params = po;

	/* start fetch */
	plugin_decode(status->content, box);

	return (struct box_result) {box, false, false};

no_memory:
	box_free_object_params(po);
	box_free_box(box);

	return (struct box_result) {0, false, true};
}

/**
 * plugin_decode
 * This function checks that the contents of the plugin_object struct
 * are valid. If they are, it initiates the fetch process. If they are
 * not, it exits, leaving the box structure as it was on entry. This is
 * necessary as there are multiple ways of declaring an object's attributes.
 *
 * Returns false if the object could not be handled.
 */
bool plugin_decode(struct content *content, struct box *box)
{
	char *codebase, *url = NULL;
	struct object_params *po;
	struct plugin_params *pp;
	url_func_result res;

	assert(content && box);

	po = box->object_params;

	/* Check if the codebase attribute is defined.
	 * If it is not, set it to the codebase of the current document.
	 */
	if (po->codebase == 0)
		res = url_join("./", content->data.html.base_url,
							&codebase);
	else
		res = url_join(po->codebase, content->data.html.base_url,
							&codebase);

	if (res != URL_FUNC_OK)
		return false;

	/* free pre-existing codebase */
	if (po->codebase)
		free(po->codebase);

	po->codebase = codebase;

	/* Set basehref */
	po->basehref = strdup(content->data.html.base_url);

	if (po->data == 0 && po->classid == 0)
		/* no data => ignore this object */
		return false;

	if (po->data == 0 && po->classid != 0) {
		/* just classid specified */
		if (strncasecmp(po->classid, "clsid:", 6) == 0) {
			if (strcasecmp(po->classid, "clsid:D27CDB6E-AE6D-11cf-96B8-444553540000") == 0) {
				/* Flash */
				for (pp = po->params;
					pp != 0 &&
					strcasecmp(pp->name, "movie") != 0;
					pp = pp->next)
					/* no body */;
				if (pp == 0)
						return false;
				res = url_join(pp->value, po->basehref, &url);
				if (res != URL_FUNC_OK)
						return false;
				/* munge the codebase */
				res = url_join("./",
						content->data.html.base_url,
						&codebase);
				if (res != URL_FUNC_OK) {
						free(url);
						return false;
				}
				if (po->codebase)
					free(po->codebase);
				po->codebase = codebase;
			}
			else {
				LOG(("ActiveX object"));
				return false;
			}
		} else {
			res = url_join(po->classid, po->codebase, &url);
			if (res != URL_FUNC_OK)
				return false;

#if 0
			/* jmb - I'm not convinced by this */
			/* The java plugin doesn't need the .class extension
			 * so we strip it.
			 */
			if (strcasecmp(&po->classid[strlen(po->classid)-6],
					".class") == 0)
				po->classid[strlen(po->classid)-6] = 0;
#endif
		}
	} else {
		/* just data (or both) specified - data takes precedence */
		res = url_join(po->data, po->codebase, &url);
		if (res != URL_FUNC_OK)
			return false;
	}

	/* Check if the declared mime type is understandable.
	 * Checks type and codetype attributes.
	 */
	if (po->type != 0 && content_lookup(po->type) == CONTENT_OTHER)
		goto no_handler;
	if (po->codetype != 0 &&
			content_lookup(po->codetype) == CONTENT_OTHER)
		goto no_handler;

	/* Ensure that the object to be included isn't this document */
	if (strcasecmp(url, content->data.html.base_url) == 0)
		goto no_handler;

	/* If we've got to here, the object declaration has provided us with
	 * enough data to enable us to have a go at downloading and
	 * displaying it.
	 *
	 * We may still find that the object has a MIME type that we can't
	 * handle when we fetch it (if the type was not specified or is
	 * different to that given in the attributes).
	 */
	if (!html_fetch_object(content, url, box, 0, 1000, 1000, false))
		goto no_handler;

	/* do _not_ free url here - html_fetch_object doesn't copy it */

	return true;

no_handler:
	free(url);
	return false;
}


struct box_result box_frameset(xmlNode *n, struct box_status *status,
		struct css_style *style)
{
	unsigned int row, col;
	unsigned int rows = 1, cols = 1;
	int object_width, object_height;
	char *s, *s1, *url;
	struct box *box;
	struct box *row_box;
	struct box *cell_box;
	struct box *object_box;
	struct css_style *row_style;
	struct css_style *cell_style;
	struct css_style *object_style;
	struct box_result r;
	struct box_multi_length *row_height = 0, *col_width = 0;
	xmlNode *c;
	url_func_result res;

	box = box_create(style, 0, status->title, status->id,
			status->content->data.html.box_pool);
	if (!box)
		return (struct box_result) {0, false, true};
	box->type = BOX_TABLE;

	/* parse rows and columns */
	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "rows")) != NULL) {
		row_height = box_parse_multi_lengths(s, &rows);
		xmlFree(s);
		if (!row_height) {
			box_free_box(box);
			return (struct box_result) {0, false, true};
		}
	}

	if ((s = (char *) xmlGetProp(n, (const xmlChar *) "cols")) != NULL) {
		col_width = box_parse_multi_lengths(s, &cols);
		xmlFree(s);
		if (!col_width) {
			free(row_height);
			box_free_box(box);
			return (struct box_result) {0, false, true};
		}
	}

	LOG(("rows %u, cols %u", rows, cols));

	box->min_width = 1;
	box->max_width = 10000;
	box->col = malloc(sizeof box->col[0] * cols);
	if (!box->col) {
		free(row_height);
		free(col_width);
		box_free_box(box);
		return (struct box_result) {0, false, true};
	}

	if (col_width) {
		for (col = 0; col != cols; col++) {
			if (col_width[col].type == LENGTH_PX) {
				box->col[col].type = COLUMN_WIDTH_FIXED;
				box->col[col].width = col_width[col].value;
			} else if (col_width[col].type == LENGTH_PERCENT) {
				box->col[col].type = COLUMN_WIDTH_PERCENT;
				box->col[col].width = col_width[col].value;
			} else {
				box->col[col].type = COLUMN_WIDTH_RELATIVE;
				box->col[col].width = col_width[col].value;
			}
			box->col[col].min = 1;
			box->col[col].max = 10000;
		}
	} else {
		box->col[0].type = COLUMN_WIDTH_RELATIVE;
		box->col[0].width = 1;
		box->col[0].min = 1;
		box->col[0].max = 10000;
	}

	/* create the frameset table */
	c = n->children;
	for (row = 0; c && row != rows; row++) {
		row_style = css_duplicate_style(style);
		if (!row_style) {
			box_free(box);
			free(row_height);
			free(col_width);
			return (struct box_result) {0, false, true};
		}
		object_height = 1000;  /** \todo  get available height */
	/*	if (row_height) {
			row_style->height.height = CSS_HEIGHT_LENGTH;
			row_style->height.length.unit = CSS_UNIT_PX;
			if (row_height[row].type == LENGTH_PERCENT)
				row_style->height.length.value = 1000 *
						row_height[row].value / 100;
			else if (row_height[row].type == LENGTH_RELATIVE)
				row_style->height.length.value = 100 *
						row_height[row].value;
			else
				row_style->height.length.value =
						row_height[row].value;
			object_height = row_style->height.length.value;
		}*/
		row_box = box_create(row_style, 0, 0, 0,
				status->content->data.html.box_pool);
		if (!row_box)
			return (struct box_result) {0, false, true};

		row_box->type = BOX_TABLE_ROW;
		box_add_child(box, row_box);

		for (col = 0; c && col != cols; col++) {
			while (c && !(c->type == XML_ELEMENT_NODE && (
				strcmp((const char *) c->name, "frame") == 0 ||
				strcmp((const char *) c->name, "frameset") == 0
					)))
				c = c->next;
			if (!c)
				break;

			/* estimate frame width */
			object_width = status->content->available_width;
			if (col_width && col_width[col].type == LENGTH_PX)
				object_width = col_width[col].value;

			cell_style = css_duplicate_style(style);
			if (!cell_style) {
				box_free(box);
				free(row_height);
				free(col_width);
				return (struct box_result) {0, false, true};
			}
			css_cascade(cell_style, &css_blank_style);
			cell_style->overflow = CSS_OVERFLOW_AUTO;

			cell_box = box_create(cell_style, 0, 0, 0,
					status->content->data.html.box_pool);
			if (!cell_box)
				return (struct box_result) {0, false, true};
			cell_box->type = BOX_TABLE_CELL;
			box_add_child(row_box, cell_box);

			if (strcmp((const char *) c->name, "frameset") == 0) {
				LOG(("frameset"));
				r = box_frameset(c, status, style);
				if (r.memory_error) {
					box_free(box);
					free(row_height);
					free(col_width);
					return (struct box_result) {0, false,
							true};
				}
				r.box->style_clone = 1;
				box_add_child(cell_box, r.box);

				c = c->next;
				continue;
			}

			object_style = css_duplicate_style(style);
			if (!object_style) {
				box_free(box);
				free(row_height);
				free(col_width);
				return (struct box_result) {0, false, true};
			}
			if (col_width && col_width[col].type == LENGTH_PX) {
				object_style->width.width = CSS_WIDTH_LENGTH;
				object_style->width.value.length.unit =
						CSS_UNIT_PX;
				object_style->width.value.length.value =
						object_width;
			}

			object_box = box_create(object_style, 0, 0, 0,
					status->content->data.html.box_pool);
			if (!object_box)
				return (struct box_result) {0, false, true};
			object_box->type = BOX_BLOCK;
			box_add_child(cell_box, object_box);

			if ((s = (char *) xmlGetProp(c, (const xmlChar *) "src")) == NULL) {
				c = c->next;
				continue;
			}

			s1 = strip(s);
			res = url_join(s1, status->content->data.html.base_url, &url);
			/* if url is equivalent to the parent's url,
			 * we've got infinite inclusion. stop it here.
			 * also bail if url_join failed.
			 */
			if (res != URL_FUNC_OK || strcasecmp(url, status->content->data.html.base_url) == 0) {
				xmlFree(s);
				c = c->next;
				continue;
			}

			LOG(("frame, url '%s'", url));

			if (!html_fetch_object(status->content, url,
					object_box, 0,
					object_width, object_height, false))
				return (struct box_result) {0, false, true};
			xmlFree(s);

			c = c->next;
		}
	}

	free(row_height);
	free(col_width);

	style->width.width = CSS_WIDTH_PERCENT;
	style->width.value.percent = 100;

	return (struct box_result) {box, false, false};
}


/**
 * Parse a multi-length-list, as defined by HTML 4.01.
 *
 * \param  s    string to parse
 * \param  count  updated to number of entries
 * \return  array of struct box_multi_length, or 0 on memory exhaustion
 */

struct box_multi_length *box_parse_multi_lengths(const char *s,
		unsigned int *count)
{
	char *end;
	unsigned int i, n;
	struct box_multi_length *length;

	for (i = 0, n = 1; s[i]; i++)
		if (s[i] == ',')
			n++;

	if ((length = malloc(sizeof *length * n)) == NULL)
		return NULL;

	for (i = 0; i != n; i++) {
		while (isspace(*s))
			s++;
		length[i].value = strtof(s, &end);
		if (length[i].value <= 0)
			length[i].value = 1;
		s = end;
		switch (*s) {
			case '%': length[i].type = LENGTH_PERCENT; break;
			case '*': length[i].type = LENGTH_RELATIVE; break;
			default:  length[i].type = LENGTH_PX; break;
		}
		while (*s && *s != ',')
			s++;
		if (*s == ',')
			s++;
	}

	*count = n;
	return length;
}
