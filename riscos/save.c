/*
 * This file is part of NetSurf, http://netsurf.sourceforge.net/
 * Licensed under the GNU General Public License,
 *                http://www.opensource.org/licenses/gpl-license
 * Copyright 2004 James Bursa <bursa@users.sourceforge.net>
 */

/** \file
 * Save dialog and drag and drop saving (implementation).
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oslib/dragasprite.h"
#include "oslib/osfile.h"
#include "oslib/osspriteop.h"
#include "oslib/wimp.h"
#include "netsurf/desktop/save_text.h"
#include "netsurf/riscos/gui.h"
#include "netsurf/riscos/save_complete.h"
#include "netsurf/riscos/save_draw.h"
#include "netsurf/riscos/thumbnail.h"
#include "netsurf/utils/log.h"
#include "netsurf/utils/messages.h"
#include "netsurf/utils/utils.h"

gui_save_type gui_current_save_type;

extern struct content *save_content;
extern char *save_link;

static void ro_gui_save_complete(struct content *c, char *path);
static void ro_gui_save_object_native(struct content *c, char *path);
static void ro_gui_save_link(int format, char *path);


/**
 * Handle clicks in the save dialog.
 */

void ro_gui_save_click(wimp_pointer *pointer)
{
	switch (pointer->i) {
		case ICON_SAVE_ICON:
			if (pointer->buttons == wimp_DRAG_SELECT) {
				gui_current_drag_type = GUI_DRAG_SAVE;
				ro_gui_drag_icon(pointer);
			}
			break;
	}
}


/**
 * Start drag of icon under the pointer.
 */

void ro_gui_drag_icon(wimp_pointer *pointer)
{
	char *sprite;
	os_box box = { pointer->pos.x - 34, pointer->pos.y - 34,
			pointer->pos.x + 34, pointer->pos.y + 34 };
	os_error *error;

	if (pointer->i == -1)
		return;

	sprite = ro_gui_get_icon_string(pointer->w, pointer->i);

	error = xdragasprite_start(dragasprite_HPOS_CENTRE |
			dragasprite_VPOS_CENTRE |
			dragasprite_BOUND_POINTER |
			dragasprite_DROP_SHADOW,
			(osspriteop_area *) 1, sprite, &box, 0);
	if (error) {
		LOG(("xdragasprite_start: 0x%x: %s",
				error->errnum, error->errmess));
		warn_user(error->errmess);
	}
}


/**
 * Handle User_Drag_Box event for a drag from the save dialog.
 */

void ro_gui_save_drag_end(wimp_dragged *drag)
{
	char *name;
	char *dot;
	wimp_pointer pointer;
	wimp_message message;

	wimp_get_pointer_info(&pointer);

	name = ro_gui_get_icon_string(dialog_saveas, ICON_SAVE_PATH);
	dot = strrchr(name, '.');
	if (dot)
		name = dot + 1;

	message.your_ref = 0;
	message.action = message_DATA_SAVE;
	message.data.data_xfer.w = pointer.w;
	message.data.data_xfer.i = pointer.i;
	message.data.data_xfer.pos.x = pointer.pos.x;
	message.data.data_xfer.pos.y = pointer.pos.y;
	message.data.data_xfer.est_size = 1000;
	message.data.data_xfer.file_type = 0xfaf;
	if (gui_current_save_type == GUI_SAVE_DRAW)
		message.data.data_xfer.file_type = 0xaff;
	if (gui_current_save_type == GUI_SAVE_COMPLETE) {
		message.data.data_xfer.file_type = 0x2000;
		if (name[0] != '!') {
			message.data.data_xfer.file_name[0] = '!';
			strncpy(message.data.data_xfer.file_name + 1, name,
					211);
		} else {
			strncpy(message.data.data_xfer.file_name, name, 212);
		}
	} else
		strncpy(message.data.data_xfer.file_name, name, 212);
	message.data.data_xfer.file_name[211] = 0;
	message.size = 44 + ((strlen(message.data.data_xfer.file_name) + 4) &
			(~3u));

	wimp_send_message_to_window(wimp_USER_MESSAGE, &message,
			pointer.w, pointer.i);
}


/**
 * Handle Message_DataSaveAck for a drag from the save dialog.
 */

void ro_gui_save_datasave_ack(wimp_message *message)
{
	char *path = message->data.data_xfer.file_name;
        struct content *c = save_content;
	os_error *error;

	ro_gui_set_icon_string(dialog_saveas, ICON_SAVE_PATH, path);

	switch (gui_current_save_type) {
		case GUI_SAVE_SOURCE:
		        if (!c)
		                return;
	                error = xosfile_save_stamped(path,
	                		ro_content_filetype(c),
					c->source_data,
					c->source_data + c->source_size);
			if (error) {
				LOG(("xosfile_save_stamped: 0x%x: %s",
						error->errnum, error->errmess));
				warn_user(error->errmess);
			}
			break;

		case GUI_SAVE_COMPLETE:
			if (!c)
				return;
			ro_gui_save_complete(c, path);
			break;

		case GUI_SAVE_DRAW:
			if (!c)
				return;
			save_as_draw(c, path);
			break;

		case GUI_SAVE_TEXT:
			if (!c)
				return;
			save_as_text(c, path);
			xosfile_set_type(path, 0xfff);
			break;

		case GUI_SAVE_OBJECT_ORIG:
		        if (!c)
		                return;
		        error = xosfile_save_stamped(path,
	                		ro_content_filetype(c),
					c->source_data,
					c->source_data + c->source_size);
			if (error) {
				LOG(("xosfile_save_stamped: 0x%x: %s",
						error->errnum, error->errmess));
				warn_user(error->errmess);
			}
			break;

		case GUI_SAVE_OBJECT_NATIVE:
		        if (!c)
		                return;
		        ro_gui_save_object_native(c, path);
		        break;

		case GUI_SAVE_LINK_URI:
		        if (!save_link)
		                return;
		        ro_gui_save_link(1, path);
		        break;

		case GUI_SAVE_LINK_URL:
		        if (!save_link)
		                return;
		        ro_gui_save_link(2, path);
		        break;

		case GUI_SAVE_LINK_TEXT:
		        if (!save_link)
		                return;
		        ro_gui_save_link(3, path);
		        break;
	}

        if (save_link) xfree(save_link);
        save_content = NULL;
	wimp_create_menu(wimp_CLOSE_MENU, 0, 0);
}


/**
 * Prepare an application directory and save_complete() to it.
 */

#define WIDTH 64
#define HEIGHT 64
#define SPRITE_SIZE (16 + 44 + ((WIDTH / 2 + 3) & ~3) * HEIGHT / 2)

void ro_gui_save_complete(struct content *c, char *path)
{
	char buf[256];
	FILE *fp;
	os_error *error;
	osspriteop_area *area;
	osspriteop_header *sprite_header;
	char *appname;
	unsigned int index;

        /* Create dir */
	error = xosfile_create_dir(path, 0);
	if (error) {
		LOG(("xosfile_create_dir: 0x%x: %s",
				error->errnum, error->errmess));
		warn_user(error->errmess);
		return;
	}

        /* Save !Run file */
	snprintf(buf, sizeof buf, "%s.!Run", path);
	fp = fopen(buf, "w");
	if (!fp) {
		LOG(("fopen(): errno = %i", errno));
		warn_user(strerror(errno));
		return;
	}
	fprintf(fp, "Filer_Run <Obey$Dir>.index\n");
	fclose(fp);
	error = xosfile_set_type(buf, 0xfeb);
	if (error) {
		LOG(("xosfile_set_type: 0x%x: %s",
				error->errnum, error->errmess));
		warn_user(error->errmess);
		return;
	}

        /* Create !Sprites */
	snprintf(buf, sizeof buf, "%s.!Sprites", path);
	appname = strrchr(path, '.');
	if (!appname) {
	        LOG(("Couldn't get appname"));
	        warn_user("Failed to acquire dirname");
	        return;
	}

  	area = thumbnail_initialise(34, 34, os_MODE8BPP90X90);
  	if (!area) {
		LOG(("Iconsprite initialisation failed."));
		return;
	}
	sprite_header = (osspriteop_header *)(area + 1);
	strncpy(sprite_header->name, appname + 1, 12);

	/*	!Paint gets confused with uppercase characters
	*/
	for (int index = 0; index < 12; index++) {
		sprite_header->name[index] = tolower(sprite_header->name[index]);
	}
	thumbnail_create(c, area,
			(osspriteop_header *) ((char *) area + 16),
			34, 34);
	error = xosspriteop_save_sprite_file(osspriteop_NAME, area, buf);
	if (error) {
	        LOG(("Failed to save iconsprite"));
	        warn_user("Failed to save iconsprite");
	        free(area);
	        return;
	}

	free(area);

        /* Create !Boot file */
	snprintf(buf, sizeof buf, "%s.!Boot", path);
	fp = fopen(buf, "w");
	if (!fp) {
		LOG(("fopen(): errno = %i", errno));
		warn_user(strerror(errno));
		return;
	}
	fprintf(fp, "IconSprites <Obey$Dir>.!Sprites\n");
	fclose(fp);
	error = xosfile_set_type(buf, 0xfeb);
	if (error) {
		LOG(("xosfile_set_type: 0x%x: %s",
				error->errnum, error->errmess));
		warn_user(error->errmess);
		return;
	}

	save_complete(c, path);
}

void ro_gui_save_object_native(struct content *c, char *path)
{
        os_error *error;
        osspriteop_area *temp;

        switch (c->type) {
                case CONTENT_JPEG:
                        error = xosspriteop_save_sprite_file(osspriteop_USER_AREA, c->data.jpeg.sprite_area, path);
                        break;
                case CONTENT_PNG:
                        error = xosspriteop_save_sprite_file(osspriteop_USER_AREA, c->data.png.sprite_area, path);
                        break;
                case CONTENT_GIF:
                        /* create sprite area */
                        temp = calloc(c->data.gif.gif->frame_image->size+16,
                                      sizeof(char));
                        temp->size = c->data.gif.gif->frame_image->size+16;
                        temp->sprite_count = 1;
                        temp->first = 16;
                        temp->used = c->data.gif.gif->frame_image->size+16;
                        memcpy((char*)temp+16,
                               (char*)c->data.gif.gif->frame_image,
                               c->data.gif.gif->frame_image->size);
                        /* ensure extra words for name are null */
                        memset((char*)temp+24, 0, 8);
                        error = xosspriteop_save_sprite_file(osspriteop_USER_AREA, temp, path);
                        free(temp);
                        break;
                default:
                        break;
        }
}

void ro_gui_save_link(int format, char *path)
{
        FILE *fp = fopen(path, "w");

        if (!fp) return;

        switch (format) {
               case 1: /* URI */
                       fprintf(fp, "%s\t%s\n", "URI", "100");
                       fprintf(fp, "\t# NetSurf %s\n\n", netsurf_version);
                       fprintf(fp, "\t%s\n", save_link);
                       fprintf(fp, "\t*\n");
                       break;
               case 2: /* URL */
               case 3: /* Text */
                       fprintf(fp, "%s\n", save_link);
                       break;
        }

        fclose(fp);

        switch (format) {
               case 1: /* URI */
                       xosfile_set_type(path, 0xf91);
                       break;
               case 2: /* URL */
                       xosfile_set_type(path, 0xb28);
                       break;
               case 3: /* Text */
                       xosfile_set_type(path, 0xfff);
                       break;
        }
}
