
#include "../global.h"

buffer_t *slot_material_default_canvas = NULL;

static bool slot_material_is_unique_name(ui_node_canvas_t *self, char *name) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		ui_node_canvas_t *c = g_project->_->materials->buffer[i]->canvas;
		if (c != self && string_equals(c->name, name)) {
			return false;
		}
	}
	return true;
}

char *slot_material_unique_name(ui_node_canvas_t *self, char *name) {
	if (slot_material_is_unique_name(self, name)) {
		return name;
	}
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!slot_material_is_unique_name(self, res)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

ui_node_canvas_t *slot_material_default_canvas_clone() {
	if (slot_material_default_canvas == NULL) { // Synchronous
		slot_material_default_canvas = data_get_blob("default_material.arm");
	}
	ui_node_canvas_t *decoded = armpack_decode(slot_material_default_canvas);
	ui_node_canvas_t *canvas  = util_clone_canvas(decoded);
	free(decoded);
	return canvas;
}

slot_material_t *slot_material_create(shader_data_t *m, ui_node_canvas_t *c) {
	slot_material_t *raw = ALLOC_INIT(slot_material_t, {0});
	raw->nodes           = ui_nodes_create();
	raw->preview_ready   = false;
	raw->id              = 0;
	raw->paint_base      = true;
	raw->paint_opac      = true;
	raw->paint_occ       = true;
	raw->paint_rough     = true;
	raw->paint_met       = true;
	raw->paint_nor       = true;
	raw->paint_height    = true;
	raw->paint_emis      = true;
	raw->paint_subs      = true;

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *mat = g_project->_->materials->buffer[i];
		if (mat->id >= raw->id) {
			raw->id = mat->id + 1;
		}
	}
	raw->data = m;

	i32 w           = util_render_material_preview_size;
	i32 w_icon      = ui_base_asset_icon_size();
	raw->image      = gpu_create_render_target(w, w, GPU_TEXTURE_FORMAT_RGBA64);
	raw->image_icon = gpu_create_render_target(w_icon, w_icon, GPU_TEXTURE_FORMAT_RGBA64);

	if (c == NULL) {
		if (slot_material_default_canvas == NULL) { // Synchronous
			buffer_t *b                  = data_get_blob("default_material.arm");
			slot_material_default_canvas = b;
		}
		ui_node_canvas_t *decoded = armpack_decode(slot_material_default_canvas);
		raw->canvas               = util_clone_canvas(decoded);
		free(decoded);
		i32 id            = (raw->id + 1);
		raw->canvas->name = string("Material %d", id);
	}
	else {
		raw->canvas = util_clone_canvas(c);
	}
	raw->canvas->name = string_copy(slot_material_unique_name(raw->canvas, raw->canvas->name));

	if (g_config->node_previews) {
		for (i32 i = 0; i < raw->canvas->nodes->length; ++i) {
			ui_node_t *n = raw->canvas->nodes->buffer[i];
			n->flags |= UI_NODE_FLAG_PREVIEW;
		}
	}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	raw->nodes->pan_x -= 50; // Center initial position
#else
	raw->nodes->pan_x += 110;
#endif
	raw->nodes->pan_y += 120;

	return raw;
}

// Materials that belong to a layer ("layer:" + id) or to the brush ("brush:paint") are edited in the properties, not listed as assets
bool slot_material_is_hidden(slot_material_t *m) {
	return m != NULL && m->canvas != NULL && (starts_with(m->canvas->name, "layer:") || starts_with(m->canvas->name, "brush:"));
}

// The material the brush paints with: its own color, roughness, metallic and height.
// Paint layers and paint masks each have one, a mask is painted in white until the color is changed.
// Created on first use and saved with the project.
slot_material_t *slot_material_brush(bool mask) {
	char *name = mask ? "brush:mask" : "brush:paint";
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (string_equals(m->canvas->name, name)) {
			return m;
		}
	}
	if (g_project->_->materials->length == 0) {
		return NULL;
	}
	slot_material_t *m = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	m->canvas->name    = name;
	if (mask) {
		for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
			ui_node_t *n = m->canvas->nodes->buffer[i];
			if (string_equals(n->type, "RGB") && n->outputs->length > 0 && n->outputs->buffer[0]->default_value->length >= 3) {
				n->outputs->buffer[0]->default_value->buffer[0] = 1.0;
				n->outputs->buffer[0]->default_value->buffer[1] = 1.0;
				n->outputs->buffer[0]->default_value->buffer[2] = 1.0;
			}
		}
	}
	any_array_push(g_project->_->materials, m);
	return m;
}

void slot_material_unload(slot_material_t *raw) {
	gpu_delete_texture(raw->image);
	gpu_delete_texture(raw->image_icon);
}

void slot_material_delete(slot_material_t *raw) {
	slot_material_unload(raw);
	make_material_delete_node_previews(raw, NULL);
	i32 mpos = array_index_of(g_project->_->materials, raw);
	array_remove(g_project->_->materials, raw);
	if (g_project->_->materials->length > 0) {
		context_set_material(g_project->_->materials->buffer[mpos > 0 ? mpos - 1 : 0]);
	}
}
