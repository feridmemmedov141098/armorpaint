
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

static ui_node_t *slot_material_output_node(slot_material_t *m) {
	for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			return n;
		}
	}
	return NULL;
}

// White base color of the default material, the starting point of a black and white fill
void slot_material_set_base_white(slot_material_t *m) {
	// The default material holds its color in the Base Color input of the output node
	ui_node_t *out = slot_material_output_node(m);
	if (out != NULL && out->inputs->length > 0 && out->inputs->buffer[0]->default_value->length >= 3) {
		out->inputs->buffer[0]->default_value->buffer[0] = 1.0;
		out->inputs->buffer[0]->default_value->buffer[1] = 1.0;
		out->inputs->buffer[0]->default_value->buffer[2] = 1.0;
	}
	// A color node driving it, when there is one
	for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if (string_equals(n->type, "RGB") && n->outputs->length > 0 && n->outputs->buffer[0]->default_value->length >= 3) {
			n->outputs->buffer[0]->default_value->buffer[0] = 1.0;
			n->outputs->buffer[0]->default_value->buffer[1] = 1.0;
			n->outputs->buffer[0]->default_value->buffer[2] = 1.0;
		}
	}
}

// Drives the base color with a project texture, asset_index is its index in the project assets
void slot_material_set_base_texture(slot_material_t *m, i32 asset_index) {
	ui_node_canvas_t *canvas = m->canvas;
	ui_node_t        *out    = slot_material_output_node(m);
	if (out == NULL || asset_index < 0 || asset_index >= g_project->_->assets->length) {
		return;
	}

	// Drop what drives the base color now
	ui_node_link_t *link = NULL;
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->to_id == out->id && l->to_socket == 0) {
			link = l;
			break;
		}
	}
	if (link != NULL) {
		ui_node_t *from = ui_get_node(canvas->nodes, link->from_id);
		array_remove(canvas->links, link);
		bool used = false;
		for (i32 i = 0; from != NULL && i < canvas->links->length; ++i) {
			if (canvas->links->buffer[i]->from_id == from->id) {
				used = true;
				break;
			}
		}
		if (from != NULL && !used) {
			ui_remove_node(from, canvas);
		}
	}

	nodes_material_init();
	ui_node_t *tex = ui_nodes_make_node(nodes_material_get_node_t("TEX_IMAGE"), m->nodes, canvas);
	tex->x         = out->x - 220;
	tex->y         = out->y;
	any_array_push(canvas->nodes, tex);
	tex->buttons->buffer[0]->default_value->buffer[0] = asset_index;
	any_array_push(canvas->links, project_create_node_link(canvas->links, tex->id, 0, out->id, 0));
}

// A smart mask is an asset meant for the mask slot of a layer.
// Recognised by an opacity driven by a pattern (edge wear and similar overlays) or by "mask" in its name.
bool slot_material_is_smart_mask(slot_material_t *m) {
	if (m == NULL || m->canvas == NULL || slot_material_is_hidden(m)) {
		return false;
	}
	if (string_index_of(to_lower_case(m->canvas->name), "mask") >= 0) {
		return true;
	}
	ui_node_t *out = slot_material_output_node(m);
	for (i32 i = 0; out != NULL && i < m->canvas->links->length; ++i) {
		ui_node_link_t *l = m->canvas->links->buffer[i];
		if (l->to_id == out->id && l->to_socket == 1) { // Opacity
			return true;
		}
	}
	return false;
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
		slot_material_set_base_white(m);
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
