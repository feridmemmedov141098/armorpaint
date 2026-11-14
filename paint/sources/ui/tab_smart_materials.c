
#include "../global.h"

// Smart materials: a layer folder (group) saved to the library on disk and applied to any project.
// The file is JSON (decoded through armpack into the structs below), its textures are copied next to it.
// Fill layers and fill masks keep their material, painted layers and masks come back empty (black).

typedef struct smart_layer {
	char *name;
	i32   kind;      // 0 group, 1 layer, 2 mask
	i32   parent;    // Index in layers, -1 for the folder itself
	i32   fill;      // Index in canvases, -1 when painted
	i32   blending;
	i32   uv_type;
	i32   uv_map;
	i32   mat_flags; // Channels of the fill material, bits in the order of paint_base..paint_subs
	f32   opacity;
	f32   scale;
	f32   angle;
	bool  visible;
	bool  paint_base;
	bool  paint_opac;
	bool  paint_occ;
	bool  paint_rough;
	bool  paint_met;
	bool  paint_nor;
	bool  paint_nor_blend;
	bool  paint_height;
	bool  paint_height_blend;
	bool  paint_emis;
	bool  paint_subs;
} smart_layer_t;

typedef struct smart_layer_array {
	smart_layer_t **buffer;
	i32             length;
	i32             capacity;
} smart_layer_t_array_t;

typedef struct smart_material_file {
	char                  *name;
	smart_layer_t_array_t *layers;
	string_array_t        *canvases; // Node canvas JSON of each fill material
	string_array_t        *textures; // Files in the <name>_files folder, TEX_IMAGE nodes point into this list
} smart_material_file_t;

typedef struct smart_material_entry {
	char          *name;
	char          *file;
	gpu_texture_t *image;
} smart_material_entry_t;

static any_array_t *smart_materials = NULL;

static char *smart_materials_dir() {
	return string("%ssmart_materials", data_path());
}

static char *smart_material_files_dir(char *file) {
	return string("%.*s_files", string_length(file) - (i32)strlen(".smartmat"), file);
}

static char *smart_material_thumb_path(char *file) {
	return string("%.*s.png", string_length(file) - (i32)strlen(".smartmat"), file);
}

static char *smart_material_base_name(char *path) {
	i32 a = string_last_index_of(path, "\\");
	i32 b = string_last_index_of(path, "/");
	i32 i = a > b ? a : b;
	return substring(path, i + 1, string_length(path));
}

static void smart_materials_load() {
	if (smart_materials != NULL) {
		return;
	}
	smart_materials = any_array_create(0);
	char *dir       = smart_materials_dir();
	if (!iron_file_exists(dir)) {
		return;
	}
	any_array_t *files = file_read_directory(dir);
	for (i32 i = 0; i < files->length; ++i) {
		char *f = files->buffer[i];
		if (!ends_with(f, ".smartmat")) {
			continue;
		}
		smart_material_entry_t *e = ALLOC_INIT(smart_material_entry_t, {0});
		e->name                   = string_copy(substring(f, 0, string_length(f) - (i32)strlen(".smartmat")));
		e->file                   = string("%s%s%s", dir, PATH_SEP, f);
		char *thumb               = smart_material_thumb_path(e->file);
		e->image                  = iron_file_exists(thumb) ? iron_load_texture(thumb) : NULL;
		any_array_push(smart_materials, e);
	}
}

// Renders the fill layers and fill masks of list into their textures, on the current paint object
static void smart_material_fill(slot_layer_t_array_t *list) {
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	tool_type_t      _tool     = g_context->tool;
	i32              _fill     = g_context->fill_type;
	g_context->pdirty          = 1;
	g_context->tool            = TOOL_TYPE_FILL;
	g_context->fill_type       = FILL_TYPE_OBJECT;
	for (i32 i = 0; i < list->length; ++i) {
		slot_layer_t *l = list->buffer[i];
		if (slot_layer_is_group(l) || l->fill_material == NULL) {
			continue;
		}
		g_context->layer    = l;
		g_context->material = l->fill_material;
		make_material_parse_paint_material(false);
		layers_set_object_mask();
		slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
	}
	g_context->pdirty               = 0;
	g_context->ddirty               = 2;
	g_context->rtdirty              = 1;
	g_context->layers_preview_dirty = true;
	g_context->layer                = _layer;
	g_context->material             = _material;
	layers_set_object_mask();
	g_context->tool      = _tool;
	g_context->fill_type = _fill;
	make_material_parse_paint_material(false);
}

// Sphere preview of the folder's base material: its bottom visible fill layer, already rendered as the layer icon
static gpu_texture_t *smart_material_render_thumb(slot_layer_t_array_t *sub) {
	for (i32 i = 0; i < sub->length; ++i) {
		slot_layer_t *l = sub->buffer[i];
		if (slot_layer_is_layer(l) && l->fill_material != NULL && l->visible) {
			return l->fill_material->image;
		}
	}
	return NULL;
}
static gpu_texture_t *_smart_material_thumb_rgba32;
static char          *_smart_material_thumb_file;

static void smart_material_write_thumb(void *_) {
	buffer_t *pixels = gpu_get_texture_pixels(_smart_material_thumb_rgba32);
	iron_write_png(_smart_material_thumb_file, pixels, _smart_material_thumb_rgba32->width, _smart_material_thumb_rgba32->height, 0);
}

static slot_layer_t *_smart_material_save_group;

static void smart_material_save_on_next_frame(void *_) {
	slot_layer_t *group = _smart_material_save_group;
	if (group == NULL || array_index_of(g_project->_->layers, group) < 0) {
		return;
	}
	smart_materials_load();

	// The folder and everything in it, in project order (children come before their folder)
	slot_layer_t_array_t *sub = any_array_create(0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l == group || slot_layer_is_descendant(l, group)) {
			any_array_push(sub, l);
		}
	}

	// Unique file name from the folder name
	char *name = string_copy(group->name);
	for (char *c = name; *c != '\0'; ++c) {
		if (strchr("\\/:*?\"<>|", *c) != NULL) {
			*c = '_';
		}
	}
	char *dir = smart_materials_dir();
	iron_create_directory(dir);
	char *base = name;
	char *file = string("%s%s%s.smartmat", dir, PATH_SEP, base);
	for (i32 n = 2; iron_file_exists(file); ++n) {
		base = string("%s %d", name, n);
		file = string("%s%s%s.smartmat", dir, PATH_SEP, base);
	}
	char *files_dir = smart_material_files_dir(file);

	// Fill materials as node canvas JSON, their textures copied next to the file
	string_array_t *canvases  = any_array_create(0);
	string_array_t *textures  = any_array_create(0);
	asset_t_array_t *assets   = any_array_create(0);
	i32_array_t     *fill_ids = i32_array_create(sub->length);
	for (i32 i = 0; i < sub->length; ++i) {
		slot_layer_t *l      = sub->buffer[i];
		fill_ids->buffer[i]  = -1;
		if (slot_layer_is_group(l) || l->fill_material == NULL) {
			continue;
		}
		ui_node_canvas_t *c = util_clone_canvas(l->fill_material->canvas);
		for (i32 j = 0; j < c->nodes->length; ++j) {
			ui_node_t *n = c->nodes->buffer[j];
			if (!string_equals(n->type, "TEX_IMAGE")) {
				continue;
			}
			f32 *v     = &n->buttons->buffer[0]->default_value->buffer[0];
			i32  index = (i32)*v;
			if (index < 0 || index >= g_project->_->assets->length) {
				*v = -1;
				continue;
			}
			asset_t *a = g_project->_->assets->buffer[index];
			i32      t = array_index_of(assets, a);
			if (t < 0) {
				t = assets->length;
				any_array_push(assets, a);
				char *dst = string("%d_%s", t, smart_material_base_name(a->file));
				if (iron_file_exists(a->file)) {
					iron_create_directory(files_dir);
					file_copy(a->file, string("%s%s%s", files_dir, PATH_SEP, dst));
				}
				else {
					console_error(string("%s: %s", tr("Texture file not found"), a->file));
				}
				string_array_push(textures, dst);
			}
			*v = t;
		}
		fill_ids->buffer[i] = canvases->length;
		string_array_push(canvases, string_copy(ui_node_canvas_to_json(c)));
	}

	json_encode_begin();
	json_encode_string("name", base);
	json_encode_begin_array("layers");
	for (i32 i = 0; i < sub->length; ++i) {
		slot_layer_t    *l = sub->buffer[i];
		slot_material_t *m = l->fill_material;
		i32 flags = m == NULL ? 0
		                      : (m->paint_base << 0) | (m->paint_opac << 1) | (m->paint_occ << 2) | (m->paint_rough << 3) | (m->paint_met << 4) |
		                            (m->paint_nor << 5) | (m->paint_height << 6) | (m->paint_emis << 7) | (m->paint_subs << 8);
		json_encode_begin_object();
		json_encode_string("name", l->name);
		json_encode_i32("kind", slot_layer_is_group(l) ? 0 : slot_layer_is_mask(l) ? 2 : 1);
		json_encode_i32("parent", l == group ? -1 : array_index_of(sub, l->parent));
		json_encode_i32("fill", fill_ids->buffer[i]);
		json_encode_i32("blending", l->blending);
		json_encode_i32("uv_type", l->uv_type);
		json_encode_i32("uv_map", l->uv_map);
		json_encode_i32("mat_flags", flags);
		json_encode_f32("opacity", l->mask_opacity);
		json_encode_f32("scale", l->scale);
		json_encode_f32("angle", l->angle);
		json_encode_bool("visible", l->visible);
		json_encode_bool("paint_base", l->paint_base);
		json_encode_bool("paint_opac", l->paint_opac);
		json_encode_bool("paint_occ", l->paint_occ);
		json_encode_bool("paint_rough", l->paint_rough);
		json_encode_bool("paint_met", l->paint_met);
		json_encode_bool("paint_nor", l->paint_nor);
		json_encode_bool("paint_nor_blend", l->paint_nor_blend);
		json_encode_bool("paint_height", l->paint_height);
		json_encode_bool("paint_height_blend", l->paint_height_blend);
		json_encode_bool("paint_emis", l->paint_emis);
		json_encode_bool("paint_subs", l->paint_subs);
		json_encode_end_object();
	}
	json_encode_end_array();
	json_encode_string_array("canvases", canvases);
	json_encode_string_array("textures", textures);
	char     *json = json_encode_end();
	buffer_t *b    = sys_string_to_buffer(json);
	iron_file_save_bytes(file, b, 0);

	// Thumbnail, written as png on the next frame
	gpu_texture_t *image = smart_material_render_thumb(sub);
	gpu_texture_t *thumb = NULL;
	if (image != NULL) { // Own copy, the layer can be deleted
		thumb = gpu_create_render_target(image->width, image->height, GPU_TEXTURE_FORMAT_RGBA32);
		draw_begin(thumb, true, 0x00000000);
		draw_set_pipeline(pipes_copy);
		draw_scaled_image(image, 0, 0, image->width, image->height);
		draw_set_pipeline(NULL);
		draw_end();
		_smart_material_thumb_rgba32 = thumb;
		_smart_material_thumb_file   = smart_material_thumb_path(file);
		sys_notify_on_next_frame(&smart_material_write_thumb, NULL);
	}

	smart_material_entry_t *e = ALLOC_INIT(smart_material_entry_t, {0});
	e->name                   = string_copy(base);
	e->file                   = string_copy(file);
	e->image                  = thumb;
	any_array_push(smart_materials, e);
	console_info(string("%s: %s", tr("Smart material saved"), base));
	ui_dock_redraw(-1);
}

void smart_material_save(slot_layer_t *group) {
	if (!slot_layer_is_group(group)) {
		console_info(tr("Put the layers in a folder to save them as a smart material"));
		return;
	}
	_smart_material_save_group = group;
	sys_notify_on_next_frame(&smart_material_save_on_next_frame, NULL);
}

static char *_smart_material_apply_file;

static void smart_material_redraw(void *_) {
	ui_dock_redraw(-1);
}

static void smart_material_apply_on_next_frame(void *_) {
	char     *file = _smart_material_apply_file;
	buffer_t *b    = iron_load_blob(file);
	if (b == NULL) {
		console_error(string("%s: %s", tr("Unable to open"), file));
		return;
	}
	smart_material_file_t *d = armpack_decode(json_encode_to_armpack(sys_buffer_to_string(b)));
	if (d == NULL || d->layers == NULL || d->layers->length == 0) {
		console_error(string("%s: %s", tr("Unable to open"), file));
		return;
	}

	// Textures of the smart material become project textures
	char        *files_dir  = smart_material_files_dir(file);
	i32          tex_count  = d->textures != NULL ? d->textures->length : 0;
	i32_array_t *tex_assets = i32_array_create(tex_count > 0 ? tex_count : 1);
	for (i32 i = 0; i < tex_count; ++i) {
		char *path             = string("%s%s%s", files_dir, PATH_SEP, d->textures->buffer[i]);
		tex_assets->buffer[i]  = 9999; // Pink when missing
		if (!iron_file_exists(path)) {
			continue;
		}
		import_texture_run(path, false);
		for (i32 j = 0; j < g_project->_->assets->length; ++j) {
			if (string_equals(g_project->_->assets->buffer[j]->file, path)) {
				tex_assets->buffer[i] = j;
				break;
			}
		}
	}

	// Placed above the folder or layer selected at the top level
	slot_layer_t_array_t *layers = g_project->_->layers;
	i32                   pos    = layers->length;
	slot_layer_t         *top    = g_context->layer;
	while (top != NULL && top->parent != NULL) {
		top = top->parent;
	}
	if (top != NULL && array_index_of(layers, top) >= 0) {
		pos = array_index_of(layers, top) + 1;
	}

	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	i32                   n        = d->layers->length;
	slot_layer_t_array_t *made     = any_array_create(0);
	for (i32 i = 0; i < n; ++i) {
		smart_layer_t *s    = d->layers->buffer[i];
		layer_slot_type_t t = s->kind == 0 ? LAYER_SLOT_TYPE_GROUP : s->kind == 2 ? LAYER_SLOT_TYPE_MASK : LAYER_SLOT_TYPE_LAYER;
		slot_layer_t  *l    = slot_layer_create("", t, NULL);
		array_insert(layers, pos + i, l); // In the list before the next one is created, layer ids stay unique
		any_array_push(made, l);
		tab_stages_add_layer(l->name);
	}
	for (i32 i = 0; i < n; ++i) {
		smart_layer_t *s     = d->layers->buffer[i];
		slot_layer_t  *l     = made->buffer[i];
		l->parent            = s->parent >= 0 && s->parent < n ? made->buffer[s->parent] : NULL;
		char *name           = string_copy(slot_layer_unique_name(l, s->name));
		tab_stages_rename_layer(l->name, name); // The layer list shows the layers of the stage by name
		l->name = name;
		l->visible           = s->visible;
		l->blending          = s->blending;
		l->mask_opacity      = s->opacity;
		l->uv_type           = s->uv_type;
		l->uv_map            = s->uv_map;
		l->scale             = s->scale;
		l->angle             = s->angle;
		l->paint_base        = s->paint_base;
		l->paint_opac        = s->paint_opac;
		l->paint_occ         = s->paint_occ;
		l->paint_rough       = s->paint_rough;
		l->paint_met         = s->paint_met;
		l->paint_nor         = s->paint_nor;
		l->paint_nor_blend   = s->paint_nor_blend;
		l->paint_height      = s->paint_height;
		l->paint_height_blend = s->paint_height_blend;
		l->paint_emis        = s->paint_emis;
		l->paint_subs        = s->paint_subs;
		if (slot_layer_is_group(l)) {
			continue;
		}
		if (s->fill < 0 || d->canvases == NULL || s->fill >= d->canvases->length) {
			slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0); // Painted: empty, a mask black
			continue;
		}
		ui_node_canvas_t *c = json_parse(d->canvases->buffer[s->fill]);
		for (i32 j = 0; j < c->nodes->length; ++j) {
			ui_node_t *node = c->nodes->buffer[j];
			for (i32 k = 0; k < node->buttons->length; ++k) {
				ui_node_button_t *but = node->buttons->buffer[k];
				if (but->data != NULL) { // String to u8 array, as when pasting nodes
					char *str = (char *)but->data;
					but->data = u8_array_create_from_raw((uint8_t *)str, strlen(str) + 1);
				}
			}
			if (string_equals(node->type, "TEX_IMAGE")) {
				f32 *v = &node->buttons->buffer[0]->default_value->buffer[0];
				i32  t = (i32)*v;
				*v     = t >= 0 && t < tex_count ? tex_assets->buffer[t] : 9999;
			}
		}
		slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, c);
		any_array_push(g_project->_->materials, m);
		m->canvas->name = string("layer:%d", l->id);
		i32 f           = s->mat_flags;
		m->paint_base   = (f >> 0) & 1;
		m->paint_opac   = (f >> 1) & 1;
		m->paint_occ    = (f >> 2) & 1;
		m->paint_rough  = (f >> 3) & 1;
		m->paint_met    = (f >> 4) & 1;
		m->paint_nor    = (f >> 5) & 1;
		m->paint_height = (f >> 6) & 1;
		m->paint_emis   = (f >> 7) & 1;
		m->paint_subs   = (f >> 8) & 1;
		l->fill_material = m;
	}
	tab_layers_remap_all_layer_pointers(pointers);

	smart_material_fill(made);

	// Layer icons are the previews of the fill materials
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < n; ++i) {
		slot_layer_t *l = made->buffer[i];
		if (l->fill_material != NULL) {
			g_context->material = l->fill_material;
			util_render_make_material_preview();
		}
	}
	g_context->material = _material;

	context_set_layer(made->buffer[n - 1]); // The folder
	make_material_parse_mesh_material();
	make_material_parse_paint_material(true);
	g_context->layers_preview_dirty = true;
	g_context->ddirty               = 2;
	ui_dock_redraw(-1);
	sys_notify_on_next_frame(&smart_material_redraw, NULL);
	console_info(string("%s: %s", tr("Smart material applied"), d->name));
}

// Called by import_asset_run for .smartmat files: dropped from the shelf or from the file explorer
void smart_material_apply(char *file) {
	_smart_material_apply_file = string_copy(file);
	sys_notify_on_next_frame(&smart_material_apply_on_next_frame, NULL);
}

static smart_material_entry_t *_smart_material_menu_entry;

static void smart_material_delete(smart_material_entry_t *e) {
	iron_delete_file(e->file);
	char *thumb = smart_material_thumb_path(e->file);
	if (iron_file_exists(thumb)) {
		iron_delete_file(thumb);
	}
	array_remove(smart_materials, e);
	ui_dock_redraw(TAB_AREA_STATUS);
}

static void tab_smart_materials_menu() {
	if (ui_menu_button(tr("Apply"), "", ICON_LAYER)) {
		smart_material_apply(_smart_material_menu_entry->file);
	}
	if (ui_menu_button(tr("Open Containing Directory..."), "", ICON_FOLDER_OPEN)) {
		file_start(smart_materials_dir());
	}
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		smart_material_delete(_smart_material_menu_entry);
	}
}

void tab_smart_materials_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Smart Materials"), false, -1, false)) {
		return;
	}
	smart_materials_load();
	if (smart_materials->length == 0) {
		ui_text(tr("Drag a folder from Layers here to save it as a smart material"), UI_ALIGN_LEFT, 0);
		return;
	}

	f32 a     = ui_base_asset_scale();
	i32 slotw = math_floor((50 * a + 1) * UI_SCALE() + g_config->window_scale * 2);
	i32 num   = math_floor(g_ui->_window_w / (float)slotw);
	if (num == 0) {
		return;
	}
	i32 count = smart_materials->length;
	for (i32 row = 0; row < math_floor(math_ceil(count / (float)num)); ++row) {
		i32          mult = g_config->show_asset_names ? 2 : 1;
		f32_array_t *ar   = f32_array_create_from_raw_tmp(NULL, num * mult);
		for (i32 i = 0; i < num * mult; ++i) {
			ar->buffer[i] = 1 / (float)num;
		}
		ui_row(ar);
		g_ui->_x += 2 * UI_SCALE();
		f32 off = g_config->show_asset_names ? UI_ELEMENT_OFFSET() * 10.0 + (a - 1) * 50 * UI_SCALE() : 6;
		if (row > 0) {
			g_ui->_y += off;
		}
		for (i32 j = 0; j < num; ++j) {
			i32 imgw = math_floor(50 * a * UI_SCALE());
			i32 i    = j + row * num;
			if (i >= count) {
				ui_end_element_of_size(imgw);
				if (g_config->show_asset_names) {
					ui_end_element_of_size(0);
				}
				continue;
			}
			smart_material_entry_t *e   = smart_materials->buffer[i];
			f32                     uix = g_ui->_x;
			f32                     uiy = g_ui->_y;
			i32                     tile = UI_SCALE() > 1 ? 100 : 50;
			g_ui->image_scroll_align    = false;
			ui_state_t state            = e->image != NULL ? ui_image(e->image, 0xffffffff, imgw)
			                                               : ui_sub_image(resource_get("icons.k"), 0xffffffff, imgw, tile, tile, tile, tile);
			g_ui->image_scroll_align    = true;

			// Drag onto the layers or the 3D view to apply
			if (state == UI_STATE_STARTED && g_ui->input_y > g_ui->_window_y) {
				base_drag_off_x     = -(mouse_x - uix - g_ui->_window_x - 3);
				base_drag_off_y     = -(mouse_y - uiy - g_ui->_window_y + 1);
				base_drag_file      = e->file;
				base_drag_file_icon = e->image;
			}
			if (g_ui->is_hovered) {
				if (e->image != NULL) {
					ui_tooltip_image(e->image, 0);
				}
				ui_tooltip(string_tmp("%s\n%s", e->name, tr("Drag onto Layers or the 3D view to apply")));
			}
			if (g_ui->is_hovered && g_ui->input_released_r) {
				_smart_material_menu_entry = e;
				ui_menu_draw(&tab_smart_materials_menu, -1, -1);
			}
			if (g_config->show_asset_names) {
				g_ui->_y += slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				ui_base_asset_name(e->name, uix, slotw);
				g_ui->_y -= slotw * 0.9 + (a - 1) * 5 * UI_SCALE();
				if (i == count - 1) {
					g_ui->_y += j == num - 1 ? imgw : imgw + UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
				}
			}
		}
		g_ui->_y += 6;
	}
}
