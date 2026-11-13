
#include "../global.h"

// Properties of the selected layer, mask or folder.
// A fill layer or fill mask owns its material, its color, roughness, metallic and other inputs are edited here.

static void tab_properties_update_fill_layers(void *_) {
	layers_update_fill_layers();
}

static char *tab_properties_kind(slot_layer_t *l) {
	if (slot_layer_is_group(l)) {
		return tr("Folder");
	}
	if (slot_layer_is_mask(l)) {
		return l->fill_material != NULL ? tr("Fill mask") : tr("Paint mask");
	}
	if (slot_layer_is_filter(l)) {
		return tr("Filter");
	}
	return l->fill_material != NULL ? tr("Fill layer") : tr("Paint layer");
}

static void tab_properties_draw_opacity(slot_layer_t *l) {
	f32 mask_opacity = l->mask_opacity;
	ui_set_next_id((ui_id_t)&l->mask_opacity + 2);
	ui_slider(&mask_opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	mask_opacity = math_max(0.0, math_min(1.0, mask_opacity));
	if (ui_item_changed()) {
		if (g_ui->input_started) {
			history_layer_opacity();
		}
		l->mask_opacity = mask_opacity;
		if (l == g_project->_->layers->buffer[0] && mask_opacity < 1.0) {
			layers_enable_translucency();
		}
		make_material_parse_mesh_material();
		if (slot_layer_is_mask(l)) {
			util_layer_mask_slot_changed(l);
		}
	}
}

// Projection, scale and angle of a fill
static void tab_properties_draw_projection(slot_layer_t *l) {
	string_array_t *uv_type_items = any_array_create_from_raw(
	    (void *[]){
	        tr("UV Map"),
	        tr("Triplanar"),
	        tr("Project"),
	    },
	    3);
	i32 prev_uv_type = l->uv_type;
	ui_set_next_id((ui_id_t)&l->uv_type + 2);
	ui_inline_radio((int *)&l->uv_type, uv_type_items, UI_ALIGN_LEFT);
	if (ui_item_changed()) {
		i32 new_uv_type = l->uv_type;
		l->uv_type      = prev_uv_type;
		history_layer_uv_type();
		l->uv_type = new_uv_type;
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&tab_properties_update_fill_layers, NULL);
	}

	f32 prev_scale = l->scale;
	ui_set_next_id((ui_id_t)&l->scale + 2);
	ui_slider(&l->scale, tr("UV Scale"), 0.0, 5.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		if (g_ui->input_started) {
			f32 new_scale = l->scale;
			l->scale      = prev_scale;
			history_layer_scale();
			l->scale = new_scale;
		}
		sys_notify_on_next_frame(&tab_properties_update_fill_layers, NULL);
	}

	f32 prev_angle = l->angle;
	ui_set_next_id((ui_id_t)&l->angle + 2);
	ui_slider(&l->angle, tr("Angle"), 0.0, 360, true, 1, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		if (g_ui->input_started) {
			f32 new_angle = l->angle;
			l->angle      = prev_angle;
			history_layer_angle();
			l->angle = new_angle;
		}
		make_material_parse_paint_material(true);
		sys_notify_on_next_frame(&tab_properties_update_fill_layers, NULL);
	}
}

void tab_properties_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Properties"), false, -1, false)) {
		return;
	}
	if (g_ui->_window_w <= ui_sidebar_w_mini) {
		return;
	}
	slot_layer_t *l = g_context->layer;
	if (l == NULL || array_index_of(g_project->_->layers, l) < 0) {
		return;
	}

	// The fill of the selected layer is the material being edited
	if (l->fill_material != NULL && g_context->material != l->fill_material) {
		context_set_material(l->fill_material);
	}

	ui_text(string_tmp("%s - %s", tab_properties_kind(l), l->name), UI_ALIGN_LEFT, 0x00000000);

	tab_layers_combo_site = 2; // Ids apart from the same controls in the layer row and its context menu
	if (slot_layer_is_mask(l)) {
		tab_layers_combo_mask_blending(l, true);
	}
	else if (!slot_layer_is_group(l) && !slot_layer_is_filter(l) && l->texpaint_sculpt == NULL) {
		tab_layers_combo_blending(l, true);
	}
	tab_layers_combo_site = 0;

	tab_properties_draw_opacity(l);

	if (l->fill_material == NULL) {
		if (slot_layer_is_group(l) || slot_layer_is_filter(l)) {
			return;
		}
		// What the brush puts down on this layer: its own color, roughness, metallic and height, or an asset picked from the shelf
		slot_material_t *m = g_context->material;
		ui_text(slot_material_is_hidden(m) ? tr("Brush") : string_tmp("%s - %s", tr("Brush"), m->canvas->name), UI_ALIGN_LEFT, 0x00000000);
		ui_node_params_draw(CANVAS_TYPE_MATERIAL, &g_config->material_params);
		return;
	}

	tab_properties_draw_projection(l);

	if (ui_icon_button(tr("Open in Node Editor"), ICON_EDIT, UI_ALIGN_CENTER)) {
		ui_base_show_material_nodes();
	}

	// Color, opacity, occlusion, roughness, metallic, normal map, emission, height and subsurface of the fill, each with a texture slot
	ui_node_params_draw(CANVAS_TYPE_MATERIAL, &g_config->material_params);
}
