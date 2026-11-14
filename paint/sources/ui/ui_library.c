
#include "../global.h"

// Asset library shipped with the app in data/library, shown in the shelf tabs:
//   library/alphas/{Alphas,Masks,Shapes}/name.jpg + name_icon.jpg
//   library/brushes/name.arm + name_icon.png
// Lists are read once, icons load a few per frame while the tab is drawn.

static any_map_t *ui_library_lists = NULL; // Folder -> string array of absolute file paths
static any_map_t *ui_library_icons = NULL; // File -> icon texture

char *ui_library_dir(char *sub) {
	char rel[1024];
	snprintf(rel, sizeof(rel), "%slibrary%s%s", data_path(), PATH_SEP, sub);
#ifdef IRON_WINDOWS
	char abs[1024];
	if (_fullpath(abs, rel, sizeof(abs)) != NULL) {
		return string_copy(abs);
	}
#endif
	return string_copy(rel);
}

// Files of a library folder with the extension, icons left out
string_array_t *ui_library_list(char *sub, char *ext) {
	if (ui_library_lists == NULL) {
		ui_library_lists = any_map_create();
	}
	string_array_t *list = any_map_get(ui_library_lists, sub);
	if (list != NULL) {
		return list;
	}
	list      = any_array_create(0);
	char *dir = ui_library_dir(sub);
	if (iron_is_directory(dir)) {
		any_array_t *files = file_read_directory(dir);
		for (i32 i = 0; i < files->length; ++i) {
			char *f = files->buffer[i];
			if (ends_with(to_lower_case(f), ext) && string_index_of(f, "_icon.") < 0) {
				string_array_push(list, string("%s%s%s", dir, PATH_SEP, f));
			}
		}
	}
	any_map_set(ui_library_lists, string_copy(sub), list);
	return list;
}

static char *ui_library_name(char *file) {
	i32 a = string_last_index_of(file, "\\");
	i32 b = string_last_index_of(file, "/");
	i32 s = (a > b ? a : b) + 1;
	i32 e = string_last_index_of(file, ".");
	return substring(file, s, e > s ? e : string_length(file));
}

static gpu_texture_t *ui_library_icon(char *file, i32 *loads) {
	if (ui_library_icons == NULL) {
		ui_library_icons = any_map_create();
	}
	gpu_texture_t *icon = any_map_get(ui_library_icons, file);
	if (icon != NULL || *loads <= 0) {
		return icon;
	}
	(*loads)--;
	i32   e     = string_last_index_of(file, ".");
	char *base  = substring(file, 0, e);
	char *png   = string("%s_icon.png", base);
	char *jpg   = string("%s_icon.jpg", base);
	char *path  = iron_file_exists(png) ? png : iron_file_exists(jpg) ? jpg : file;
	icon        = iron_load_texture(path);
	if (icon != NULL) {
		any_map_set(ui_library_icons, string_copy(file), icon);
	}
	return icon;
}

// Grid of library items. Returns the clicked file or NULL. selected is highlighted.
char *ui_library_draw_grid(string_array_t *files, char *selected) {
	char *picked = NULL;
	f32   a      = ui_base_asset_scale();
	i32   slotw  = math_floor((50 * a + 2) * UI_SCALE());
	i32   num    = math_floor(g_ui->_window_w / (float)slotw);
	if (num == 0 || files->length == 0) {
		return NULL;
	}
	i32  loads   = 8; // Icons loaded per frame
	bool pending = false;
	i32  tile    = UI_SCALE() > 1 ? 100 : 50;
	for (i32 row = 0; row < math_floor(math_ceil(files->length / (float)num)); ++row) {
		f32_array_t *ar = f32_array_create_from_raw_tmp(NULL, num);
		for (i32 i = 0; i < num; ++i) {
			ar->buffer[i] = 1 / (float)num;
		}
		ui_row(ar);
		g_ui->_x += 2;
		if (row > 0) {
			g_ui->_y += 6;
		}
		for (i32 j = 0; j < num; ++j) {
			i32 imgw = math_floor(50 * a * UI_SCALE());
			i32 i    = j + row * num;
			if (i >= files->length) {
				ui_end_element_of_size(imgw);
				continue;
			}
			char *file = files->buffer[i];
			// Load only what can be seen, rows further down wait until scrolled to
			bool           visible = g_ui->_y + g_ui->current_window->scroll_offset < g_ui->_window_h + imgw;
			gpu_texture_t *icon    = visible ? ui_library_icon(file, &loads) : NULL;
			if (icon == NULL && visible) {
				pending = true;
			}
			f32 uix = g_ui->_x;
			f32 uiy = g_ui->_y;
			if (selected != NULL && string_equals(selected, file)) {
				ui_rect_round(1, 1, 50 * a + 2, 50 * a + 2, g_theme->HIGHLIGHT_COL, 2 * UI_SCALE());
			}
			g_ui->_x                 = uix;
			g_ui->_y                 = uiy;
			g_ui->image_scroll_align = false;
			ui_state_t state         = icon != NULL ? ui_sub_image(icon, 0xffffffff, imgw, 0, 0, icon->width, icon->height)
			                                        : ui_sub_image(resource_get("icons.k"), 0xffffffff, imgw, tile, tile, tile, tile);
			g_ui->image_scroll_align = true;
			if (state == UI_STATE_STARTED && g_ui->input_y > g_ui->_window_y) {
				picked = file;
			}
			if (g_ui->is_hovered) {
				if (icon != NULL) {
					ui_tooltip_image(icon, 256);
				}
				ui_tooltip(ui_library_name(file));
			}
		}
	}
	if (pending) {
		ui_dock_redraw(TAB_AREA_STATUS); // Keep loading icons on the next frames
	}
	return picked;
}
