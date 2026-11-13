
#include "global.h"

bool _project_save_and_quit;
bool _project_import_mesh_replace_existing;
void (*_project_import_mesh_done)(void);
bool     _project_import_asset_hdr_as_envmap;
bool     _project_import_swatches_replace_existing;
asset_t *_project_reimport_texture_asset;

void project_open_on_file_picked(char *path) {
	if (!path_has_ext(path, "arm")) {
		console_error(strings_arm_file_expected());
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	import_arm_run_project(path);
	if (in_use)
		draw_begin(current, false, 0);
}

void project_open() {
	ui_files_show("arm", false, false, &project_open_on_file_picked);
}

void project_save_on_next_frame(void *_) {
	export_arm_run_project(g_project->_->filepath);
	if (_project_save_and_quit) {
		iron_stop();
	}
}

void project_save(bool save_and_quit) {
	if (agent_running) {
		return;
	}

#ifdef IRON_WASM
	if (!box_projects_is_cloud_path(g_project->_->filepath) && !starts_with(g_project->_->filepath, "/files/")) {
		box_projects_cloud_save_show(save_and_quit);
		return;
	}
#endif

	if (string_equals(g_project->_->filepath, "")) {
#ifdef IRON_IOS
		char *document_directory = iron_save_dialog("", "");
		document_directory       = string_copy(substring(document_directory, 0, string_length(document_directory) - 8)); // Strip /"untitled"
		g_project->_->filepath   = string("%s/%s.arm", document_directory, sys_title());
#elif defined(IRON_ANDROID)
		g_project->_->filepath = string("%s/%s.arm", iron_internal_save_path(), sys_title());
#else
		project_save_as(save_and_quit);
		return;
#endif
	}

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS) || defined(IRON_WASM)
	char *filename = substring(g_project->_->filepath, string_last_index_of(g_project->_->filepath, PATH_SEP) + 1, string_length(g_project->_->filepath) - 4);
	sys_title_set(string("%s - %s", filename, manifest_title));
#endif

	_project_save_and_quit = save_and_quit;

	sys_notify_on_next_frame(&project_save_on_next_frame, NULL);
}

void project_save_as_on_file_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	g_project->_->filepath = string("%s%s%s", path, PATH_SEP, f);
	if (!ends_with(g_project->_->filepath, ".arm")) {
		g_project->_->filepath = string("%s.arm", g_project->_->filepath);
	}
	project_save(_project_save_and_quit);
}

void project_save_as(bool save_and_quit) {
	_project_save_and_quit = save_and_quit;
	ui_files_show("arm", true, false, &project_save_as_on_file_picked);
}

void project_cleanup() {
	render_path_raytrace_bake_lightmap_cancel();
	util_mesh_remove_merged();
	nodes_mesh_reset();
	util_path_tool_reset();
	sculpt_reset_objects();

	if (g_project->_->paint_objects != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			object_set_parent(p->base, NULL);
		}
		util_mesh_remove_objects(g_project->_->paint_objects, g_context->paint_object);
	}

	if (g_context->paint_object != NULL) {
		util_mesh_delete_data_uncache(g_context->paint_object->data);
	}

	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		data_delete_texture(a->file);
	}

	ui_view2d_stop_sound();
	for (i32 i = 0; i < g_project->_->sounds->length; ++i) {
		data_delete_sound(g_project->_->sounds->buffer[i]->file);
	}

	util_physics_clear();
}

void project_new_on_next_frame(void *_) {
	// Once layers and meshes are populated on project open
	util_render_make_material_preview();
	g_context->ddirty = 4;
}

void project_new_reset_layers(void *_) {
	layers_init();
}

void project_new_resize_layers(void *_) {
	layers_resize();
}

void project_new(bool reset_layers) {
	if (g_context->paint_object != NULL) {
		project_cleanup();
		g_project->_->filepath = "";
	}
	g_project->stages          = NULL;
	g_project->_->translucency = false;
	project_scripts_trusted    = true;

	if (g_project->_->layers->length == 0) {
		any_array_push(g_project->_->layers, slot_layer_create("", LAYER_SLOT_TYPE_LAYER, NULL));
		g_context->layer = g_project->_->layers->buffer[0];
	}

	g_context->layer_preview_dirty = true;
	g_context->layer_filter        = 0;
	g_context->texture             = NULL;
	g_project->mesh_assets         = any_array_create_from_raw((void *[]){}, 0);

	bool  no_default = project_default_mesh_list == NULL || g_context->project_type == -1;
	char *mesh_name  = no_default ? "cube_bevel" : project_default_mesh_list->buffer[g_context->project_type];

	mesh_data_t *md = util_shape_make_data(mesh_name);
	if (md != NULL) {
		md->name = "Tessellated";
	}
	else {
		buffer_t *b         = data_get_blob(string("meshes/%s.arm", mesh_name));
		scene_t  *scene_raw = armpack_decode(b);
		md                  = mesh_data_create(scene_raw->mesh_datas->buffer[0]);
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	shader_data_t *m = data_get_shader("Scene", "Material");
	if (g_context->paint_object == NULL) {
		g_context->paint_object             = scene_add_mesh_object(md, m, NULL);
		g_context->paint_object->base->name = "paint_object";
	}
	else {
		mesh_object_set_data(g_context->paint_object, md);
	}

	g_project->_->paint_objects = any_array_create_from_raw(
	    (void *[]){
	        g_context->paint_object,
	    },
	    1);
	g_context->paint_object = context_main_object();
	context_select_paint_object(context_main_object());

	g_context->paint_object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	transform_build_matrix(g_context->paint_object->base->transform);
	g_context->paint_object->base->name    = "Tessellated";
	g_context->paint_object->base->visible = true;

	while (g_project->_->materials->length > 0) {
		slot_material_unload(array_pop(g_project->_->materials));
	}
	any_array_push(g_project->_->materials, slot_material_create(m, NULL));
	tab_meshes_reset_overrides();

	g_context->picker_material_mask        = false;
	g_context->picker_color_mask           = false;
	g_context->picker_hide_other_materials = false;
	g_context->material                    = g_project->_->materials->buffer[0];
	ui_nodes_hwnd->redraws                 = 2;
	ui_nodes_group_stack                   = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->material_groups          = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->mesh_groups              = any_array_create(0);
	g_project->_->brushes                  = any_array_create_from_raw((void *[]){}, 0);
	any_array_push(g_project->_->brushes, slot_brush_create(NULL));
	g_context->brush    = g_project->_->brushes->buffer[0];
	g_project->_->fonts = any_array_create_from_raw(
	    (void *[]){
	        slot_font_create("default.ttf", g_font, ""),
	    },
	    1);
	g_context->font      = g_project->_->fonts->buffer[0];
	g_project->_->sounds = any_array_create_from_raw((void *[]){}, 0);
	g_context->sound     = NULL;
	project_set_default_swatches();
	g_context->swatch                = g_project->swatches->buffer[0];
	g_context->picked_color          = project_make_swatch(0xffffffff);
	g_context->color_picker_callback = NULL;
	history_reset();
	tab_timeline_reset();

	make_material_parse_paint_material(true);
	make_material_parse_brush();

	g_project->_->assets        = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->next_asset_id = 0;
	g_project->packed_assets    = any_array_create_from_raw((void *[]){}, 0);
	g_context->ddirty           = 4;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);

	if (reset_layers) {
		bool res_changed = g_project->_->layers->buffer[0]->texpaint->width != config_get_texture_res_x() ||
		                   g_project->_->layers->buffer[0]->texpaint->height != config_get_texture_res_y();
		while (g_project->_->layers->length > 0) {
			slot_layer_unload(array_pop(g_project->_->layers));
		}
		slot_layer_t *layer = slot_layer_create("", LAYER_SLOT_TYPE_LAYER, NULL);
		any_array_push(g_project->_->layers, layer);
		context_set_layer(layer);
		if (res_changed) {
			sys_notify_on_next_frame(&project_new_resize_layers, NULL);
		}
		sys_notify_on_next_frame(&project_new_reset_layers, NULL);
	}

	if (in_use)
		draw_begin(current, false, 0);

	tab_stages_init();
	tab_meshes_reset_preview_map();
	base_update_tools();
	project_set_default_envmap();
	context_select_tool(TOOL_TYPE_BRUSH);
	scene_camera->data->near_plane      = 0.01;
	scene_camera->data->far_plane       = 100.0;
	scene_camera->data->frustum_culling = false;
	viewport_reset();
	viewport_scale_to_bounds(1.8);
	render_path_raytrace_ready = false;
	tab_scripts_minimap_dirty  = true;

	sys_notify_on_next_frame(&project_new_on_next_frame, NULL);
}

void project_set_default_envmap() {
	g_context->saved_envmap          = NULL;
	g_context->envmap_loaded         = false;
	scene_world->_->envmap           = g_context->empty_envmap;
	scene_world->envmap              = util_render_default_envmap_name;
	g_context->show_envmap           = false;
	scene_world->_->radiance         = g_context->default_radiance;
	scene_world->_->radiance_mipmaps = g_context->default_radiance_mipmaps;
	scene_world->_->irradiance       = g_context->default_irradiance;
	scene_world->strength            = 1.0;
	g_context->envmap_angle          = 0.0;
	g_context->show_envmap_blur      = false;
	g_project->envmap                = NULL;
}

void project_import_material_on_file_picked(char *path) {
	if (path_has_ext(path, "json")) {
		import_json_material_run(path);
	}
	else {
		import_arm_run_material(path);
	}
}

void project_import_material() {
	ui_files_show("arm,json", false, true, &project_import_material_on_file_picked);
}

ui_node_link_t *project_create_node_link(ui_node_link_t_array_t *links, i32 from_id, i32 from_socket, i32 to_id, i32 to_socket) {
	ui_node_link_t *link =
	    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(links), .from_id = from_id, .from_socket = from_socket, .to_id = to_id, .to_socket = to_socket});
	return link;
}

void project_import_brush_make_brush_preview(void *_) {
	util_render_make_brush_preview();
}

void project_import_brush_on_file_picked(char *path) {
	// Create brush from texture
	if (path_is_texture(path)) {
		// Import texture
		import_asset_run(path, -1.0, -1.0, true, true, NULL);
		i32 asset_index = 0;
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			if (string_equals(g_project->_->assets->buffer[i]->file, path)) {
				asset_index = i;
				break;
			}
		}

		// Create a new brush
		g_context->brush = slot_brush_create(NULL);
		any_array_push(g_project->_->brushes, g_context->brush);

		// Create and link image node
		ui_node_t *n                         = nodes_brush_create_node("TEX_IMAGE");
		n->x                                 = 83;
		n->y                                 = 340;
		n->buttons->buffer[0]->default_value = f32_array_create_x(asset_index);
		ui_node_link_t_array_t *links        = g_context->brush->canvas->links;
		ui_node_link_t         *link         = project_create_node_link(links, n->id, 0, 0, 4);
		any_array_push(links, link);

		// Parse brush
		make_material_parse_brush();
		ui_nodes_hwnd->redraws = 2;
		sys_notify_on_next_frame(&project_import_brush_make_brush_preview, NULL);
	}
	// Import from project file
	else {
		import_arm_run_brush(path);
	}
}

void project_import_brush() {
	char *formats = string_array_join(path_texture_formats(), ",");
	ui_files_show(string("arm,%s", formats), false, true, &project_import_brush_on_file_picked);
}

void project_import_mesh_on_file_picked(char *path) {
	project_import_mesh_box(path, _project_import_mesh_replace_existing, true, false, _project_import_mesh_done);
}

void project_import_mesh(bool replace_existing, void (*done)(void)) {
	_project_import_mesh_replace_existing = replace_existing;
	_project_import_mesh_done             = done;
	char *formats                         = string_array_join(path_mesh_formats(), ",");
	ui_files_show(formats, false, false, &project_import_mesh_on_file_picked);
}

// UV reprojection on reimport: layers are textures laid out by the old UVs. After the mesh is reimported, triangles of the
// new mesh are matched to triangles of the old mesh by their corner positions, and every layer texture is redrawn so each
// new texel takes the old texel at the same point of the surface. Changed geometry keeps no paint.

static f32_array_t *_project_old_tris = NULL; // Per triangle: 3 positions (9 floats) then 3 uvs (6 floats)

static f32_array_t *project_collect_triangles() {
	f32_array_t *out = f32_array_create(0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_data_t *md  = g_project->_->paint_objects->buffer[i]->data;
		i16_array_t *pos = NULL;
		i16_array_t *tex = NULL;
		for (i32 j = 0; j < md->vertex_arrays->length; ++j) {
			vertex_array_t *va = md->vertex_arrays->buffer[j];
			if (string_equals(va->attrib, "pos")) {
				pos = va->values;
			}
			else if (string_equals(va->attrib, "tex")) {
				tex = va->values;
			}
		}
		if (pos == NULL || tex == NULL) {
			continue;
		}
		u32_array_t *ind = md->index_array;
		for (i32 t = 0; t + 2 < (i32)ind->length; t += 3) {
			for (i32 c = 0; c < 3; ++c) {
				u32 v = ind->buffer[t + c];
				f32_array_push(out, pos->buffer[v * 4] / 32767.0 * md->scale_pos);
				f32_array_push(out, pos->buffer[v * 4 + 1] / 32767.0 * md->scale_pos);
				f32_array_push(out, pos->buffer[v * 4 + 2] / 32767.0 * md->scale_pos);
			}
			for (i32 c = 0; c < 3; ++c) {
				u32 v = ind->buffer[t + c];
				f32_array_push(out, tex->buffer[v * 2] / 32767.0 * md->scale_tex);
				f32_array_push(out, tex->buffer[v * 2 + 1] / 32767.0 * md->scale_tex);
			}
		}
	}
	return out;
}

static u64 project_corner_hash(f32 *p) {
	i64 x = (i64)llround(p[0] * 10000.0);
	i64 y = (i64)llround(p[1] * 10000.0);
	i64 z = (i64)llround(p[2] * 10000.0);
	u64 h = (u64)x * 0x9E3779B97F4A7C15ull ^ (u64)y * 0xC2B2AE3D27D4EB4Full ^ (u64)z * 0x165667B19E3779F9ull;
	return h ^ (h >> 29);
}

static bool project_same_point(f32 *a, f32 *b) {
	return fabsf(a[0] - b[0]) < 0.0002 && fabsf(a[1] - b[1]) < 0.0002 && fabsf(a[2] - b[2]) < 0.0002;
}

// For every texel of a w x h layer, the old texel holding the same surface point, -1 where nothing matches
static i32 *project_reprojection_map(f32_array_t *old_tris, f32_array_t *new_tris, i32 w, i32 h) {
	i32  old_count = old_tris->length / 15;
	i32  cap       = 1;
	while (cap < old_count * 2) {
		cap <<= 1;
	}
	i32 *table = malloc(sizeof(i32) * cap);
	for (i32 i = 0; i < cap; ++i) {
		table[i] = -1;
	}
	for (i32 t = 0; t < old_count; ++t) {
		f32 *p   = old_tris->buffer + t * 15;
		u64  key = project_corner_hash(p) + project_corner_hash(p + 3) + project_corner_hash(p + 6); // Corner order does not matter
		u32  s   = (u32)(key & (cap - 1));
		while (table[s] != -1) {
			s = (s + 1) & (cap - 1);
		}
		table[s] = t;
	}

	i32 *map = malloc(sizeof(i32) * w * h);
	for (i32 i = 0; i < w * h; ++i) {
		map[i] = -1;
	}
	i32 new_count = new_tris->length / 15;
	for (i32 t = 0; t < new_count; ++t) {
		f32 *np  = new_tris->buffer + t * 15;
		u64  key = project_corner_hash(np) + project_corner_hash(np + 3) + project_corner_hash(np + 6);
		u32  s   = (u32)(key & (cap - 1));
		f32  old_uv[6];
		bool found = false;
		while (!found && table[s] != -1) {
			f32 *op = old_tris->buffer + table[s] * 15;
			i32  hit = 0;
			for (i32 c = 0; c < 3; ++c) {
				for (i32 d = 0; d < 3; ++d) {
					if (project_same_point(np + c * 3, op + d * 3)) {
						old_uv[c * 2]     = op[9 + d * 2];
						old_uv[c * 2 + 1] = op[9 + d * 2 + 1];
						hit++;
						break;
					}
				}
			}
			found = hit == 3;
			s     = (s + 1) & (cap - 1);
		}
		if (!found) {
			continue;
		}

		// Rasterize the new triangle in texel space, interpolate the old uv at each texel center
		f32 *uv = np + 9;
		f32  x0 = uv[0] * w, y0 = uv[1] * h, x1 = uv[2] * w, y1 = uv[3] * h, x2 = uv[4] * w, y2 = uv[5] * h;
		f32  area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (fabsf(area) < 1e-12) {
			continue;
		}
		i32 minx = (i32)math_floor(math_min(x0, math_min(x1, x2)));
		i32 maxx = (i32)math_ceil(math_max(x0, math_max(x1, x2)));
		i32 miny = (i32)math_floor(math_min(y0, math_min(y1, y2)));
		i32 maxy = (i32)math_ceil(math_max(y0, math_max(y1, y2)));
		minx     = minx < 0 ? 0 : minx;
		miny     = miny < 0 ? 0 : miny;
		maxx     = maxx > w - 1 ? w - 1 : maxx;
		maxy     = maxy > h - 1 ? h - 1 : maxy;
		for (i32 y = miny; y <= maxy; ++y) {
			for (i32 x = minx; x <= maxx; ++x) {
				f32 px = x + 0.5;
				f32 py = y + 0.5;
				f32 b0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
				f32 b1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
				f32 b2 = 1.0 - b0 - b1;
				if (b0 < -0.001 || b1 < -0.001 || b2 < -0.001) {
					continue;
				}
				f32 u  = b0 * old_uv[0] + b1 * old_uv[2] + b2 * old_uv[4];
				f32 v  = b0 * old_uv[1] + b1 * old_uv[3] + b2 * old_uv[5];
				i32 ox = (i32)(u * w);
				i32 oy = (i32)(v * h);
				ox     = ox < 0 ? 0 : ox > w - 1 ? w - 1 : ox;
				oy     = oy < 0 ? 0 : oy > h - 1 ? h - 1 : oy;
				map[y * w + x] = oy * w + ox;
			}
		}
	}
	free(table);

	// Grow covered texels a few texels out so seams do not bleed the background in
	i32 *next = malloc(sizeof(i32) * w * h);
	for (i32 pass = 0; pass < 4; ++pass) {
		memcpy(next, map, sizeof(i32) * w * h);
		for (i32 y = 0; y < h; ++y) {
			for (i32 x = 0; x < w; ++x) {
				i32 i = y * w + x;
				if (map[i] != -1) {
					continue;
				}
				if (x > 0 && map[i - 1] != -1) {
					next[i] = map[i - 1];
				}
				else if (x < w - 1 && map[i + 1] != -1) {
					next[i] = map[i + 1];
				}
				else if (y > 0 && map[i - w] != -1) {
					next[i] = map[i - w];
				}
				else if (y < h - 1 && map[i + w] != -1) {
					next[i] = map[i + w];
				}
			}
		}
		memcpy(map, next, sizeof(i32) * w * h);
	}
	free(next);
	return map;
}

static void project_reproject_texture(gpu_texture_t *target, i32 *map) {
	if (target == NULL) {
		return;
	}
	i32       w   = target->width;
	i32       h   = target->height;
	buffer_t *old = gpu_get_texture_pixels(target);
	i32       bpp = old->length / (w * h);
	buffer_t *out = buffer_create(old->length);
	memset(out->buffer, 0, out->length);
	for (i32 i = 0; i < w * h; ++i) {
		if (map[i] != -1) {
			memcpy(out->buffer + (size_t)i * bpp, old->buffer + (size_t)map[i] * bpp, bpp);
		}
	}
	gpu_texture_format_t format = bpp == 16 ? GPU_TEXTURE_FORMAT_RGBA128 : bpp == 8 ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA32;
	gpu_pipeline_t      *pipe   = bpp == 16 ? pipes_copy128 : bpp == 8 ? pipes_copy64 : pipes_copy;
	gpu_texture_t       *tmp    = gpu_create_texture_from_bytes(out, w, h, format);
	draw_begin(target, false, 0);
	draw_set_pipeline(pipe);
	draw_image(tmp, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(tmp);
}

static void project_reproject_layers(void *_) {
	if (_project_old_tris == NULL || g_project->_->layers->length == 0) {
		return;
	}
	f32_array_t *new_tris = project_collect_triangles();
	gpu_texture_t *first  = g_project->_->layers->buffer[0]->texpaint;
	i32           *map    = project_reprojection_map(_project_old_tris, new_tris, first->width, first->height);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_group(l) || l->texpaint == NULL || l->texpaint->width != first->width || l->texpaint->height != first->height) {
			continue;
		}
		project_reproject_texture(l->texpaint, map);
		if (slot_layer_is_layer(l)) {
			project_reproject_texture(l->texpaint_nor, map);
			project_reproject_texture(l->texpaint_pack, map);
		}
	}
	free(map);
	array_free(new_tris);
	free(new_tris);
	array_free(_project_old_tris);
	free(_project_old_tris);
	_project_old_tris = NULL;
	layers_update_fill_layers(); // Fills are regenerated on the new uvs
	g_context->layers_preview_dirty = true;
	g_context->ddirty               = 2;
	console_info(tr("Layers reprojected to the new UVs"));
}

static void project_reimport_mesh_done() {
	sys_notify_on_next_frame(&project_reproject_layers, NULL);
}

void project_reimport_mesh() {
	if (g_project->mesh_assets != NULL && g_project->mesh_assets->length > 0 && iron_file_exists(g_project->mesh_assets->buffer[0])) {
		_project_old_tris = project_collect_triangles(); // Old surface, for reprojecting the layers once the new mesh is in
		project_import_mesh_box(g_project->mesh_assets->buffer[0], true, false, true, &project_reimport_mesh_done);
	}
	else {
		project_import_asset(NULL, true);
	}
}

// culled is from the last drawn frame, path tracing and split view see beyond it
static bool project_skin_in_view(mesh_object_t *o) {
	return !scene_camera->data->frustum_culling || !o->culled || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE || base_is_split();
}

bool project_reskin_object(mesh_object_t *o, i32 frame) {
	mesh_data_t *md = o->data;
	if (md->_->skin_blob == NULL) {
		return false;
	}

	// Each mesh loops over its own animation length
	i32 frames     = util_skin_frame_count(md->_->skin_blob);
	i32 mesh_frame = frames > 0 ? frame % frames : frame;
	if (mesh_frame == md->_->skin_frame) {
		md->_->skin_pending = false;
		return false;
	}

	// Skip skinning culled meshes, project_reskin_pending() poses them once back in view
	if (!project_skin_in_view(o)) {
		md->_->skin_pending       = true;
		md->_->skin_pending_frame = mesh_frame;
		return false;
	}
	md->_->skin_pending = false;

	vertex_array_t *pos = mesh_data_get_vertex_array(md, "pos");
	vertex_array_t *nor = mesh_data_get_vertex_array(md, "nor");
	if (!util_skin_apply(md->_->skin_blob, mesh_frame, pos->values, nor->values, &md->scale_pos)) {
		return false;
	}

	md->_->skin_frame = mesh_frame;
	mesh_data_build_vertices(md->_->vertex_buffer, md->vertex_arrays);
	return true;
}

// Poses the skinned meshes at a frame
bool project_reskin_mesh(int frame) {
	bool any = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->base->visible && project_reskin_object(o, frame)) {
			any = true;
		}
	}
	if (!any) {
		return false;
	}
	project_reskin_finish();
	return true;
}

// Poses the meshes whose skinning was skipped while culled
void project_reskin_pending() {
	bool any = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->data->_->skin_pending && o->base->visible && project_reskin_object(o, o->data->_->skin_pending_frame)) {
			any = true;
		}
	}
	if (any) {
		project_reskin_finish();
	}
}

void project_reskin_finish() {
	if (g_context->merged_object != NULL && base_player) {
		util_mesh_merged_stale = true;
	}
	else if (g_context->merged_object != NULL) {
		if (!util_mesh_merge_refresh()) {
			util_mesh_merge(NULL);
		}
		if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
			sculpt_bake_to_mesh();
		}
	}
	g_context->ddirty          = 4;
	render_path_raytrace_ready = false;
}

void project_unwrap_mesh_box_draw() {
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("Unwrap"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		ui_box_hide();

#if defined(IRON_ANDROID) || defined(IRON_IOS)
		console_toast(tr("Unwrapping mesh"));
#endif

		util_mesh_uv_unwrap(NULL);
	}
}

void project_unwrap_mesh_box() {
	ui_box_show_custom(&project_unwrap_mesh_box_draw, 400, 200, NULL, true, tr("Unwrap Mesh"));
}

void project_import_asset_on_file_picked(char *path) {
	import_asset_run(path, -1.0, -1.0, true, _project_import_asset_hdr_as_envmap, NULL);
}

void project_import_asset(char *filters, bool hdr_as_envmap) {
	if (filters == NULL) {
		filters = string("%s,%s", string_array_join(path_texture_formats(), ","), string_array_join(path_mesh_formats(), ","));
	}

	_project_import_asset_hdr_as_envmap = hdr_as_envmap;

	ui_files_show(filters, false, true, &project_import_asset_on_file_picked);
}

void project_import_swatches_on_file_picked(char *path) {
	import_arm_run_swatches(path, _project_import_swatches_replace_existing);
}

void project_import_swatches(bool replace_existing) {
	_project_import_swatches_replace_existing = replace_existing;
	ui_files_show("arm", false, false, &project_import_swatches_on_file_picked);
}

void project_reimport_textures() {
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *asset = g_project->_->assets->buffer[i];
		project_reimport_texture(asset);
	}
}

void project_reimport_texture_load_on_next_frame(void *_) {
	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
}

void project_reimport_texture_load(char *path, asset_t *asset) {
	asset->file = string_copy(path);
	i32 i       = array_index_of(g_project->_->assets, asset);
	data_delete_texture(asset->file);
	asset_t *old_asset = g_project->_->assets->buffer[i];
	array_splice(g_project->_->assets, i, 1);
	import_texture_run(asset->file, true);
	array_insert(g_project->_->assets, i, array_pop(g_project->_->assets));

	if (g_context->texture == old_asset) {
		g_context->texture = g_project->_->assets->buffer[i];
	}

	sys_notify_on_next_frame(&project_reimport_texture_load_on_next_frame, NULL);
}

void project_reimport_texture_on_file_picked(char *path) {
	project_reimport_texture_load(path, _project_reimport_texture_asset);
}

void project_reimport_texture(asset_t *asset) {
	if (!iron_file_exists(asset->file)) {
		char *filters                   = string_array_join(path_texture_formats(), ",");
		_project_reimport_texture_asset = asset;
		ui_files_show(filters, false, false, &project_reimport_texture_on_file_picked);
	}
	else {
		project_reimport_texture_load(asset->file, asset);
	}
}

gpu_texture_t *project_get_image(asset_t *asset) {
	return asset != NULL ? asset->image : NULL;
}

asset_t_array_t *project_get_assets() {
	return g_project->_->assets;
}

string_array_t *project_get_used_atlases() {
	if (g_project->atlas_objects == NULL) {
		return NULL;
	}

	static i32_array_t    used = {0};
	static string_array_t res  = {0};
	used.length                = 0;
	for (i32 i = 0; i < g_project->atlas_objects->length; ++i) {
		i32 ao = g_project->atlas_objects->buffer[i];
		if (i32_array_index_of(&used, ao) == -1) {
			i32_array_push(&used, ao);
		}
	}
	if (used.length > 1) {
		res.length = 0;
		for (i32 i = 0; i < used.length; ++i) {
			string_array_push(&res, g_project->atlas_names->buffer[used.buffer[i]]);
		}
		return &res;
	}
	return NULL;
}

bool project_is_atlas_object(mesh_object_t *p) {
	if (g_context->layer_filter <= g_project->_->paint_objects->length) {
		return false;
	}
	char *atlas_name = project_get_used_atlases()->buffer[g_context->layer_filter - g_project->_->paint_objects->length - 1];
	i32   atlas_i    = string_array_index_of(g_project->atlas_names, atlas_name);
	return atlas_i == g_project->atlas_objects->buffer[array_index_of(g_project->_->paint_objects, p)];
}

mesh_object_t_array_t *project_get_atlas_objects(i32 object_mask) {
	string_array_t *atlases = project_get_used_atlases();
	i32             i       = object_mask - g_project->_->paint_objects->length - 1;
	if (atlases == NULL || i >= atlases->length) {
		return g_project->_->paint_objects;
	}
	char                  *atlas_name = atlases->buffer[i];
	i32                    atlas_i    = string_array_index_of(g_project->atlas_names, atlas_name);
	mesh_object_t_array_t *visibles   = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->atlas_objects->buffer[i] == atlas_i) {
			any_array_push(visibles, g_project->_->paint_objects->buffer[i]);
		}
	}
	return visibles;
}

bool project_packed_asset_exists(packed_asset_t_array_t *packed_assets, char *name) {
	for (i32 i = 0; i < packed_assets->length; ++i) {
		packed_asset_t *pa = packed_assets->buffer[i];
		if (string_equals(pa->name, name)) {
			return true;
		}
	}
	return false;
}

void project_export_swatches_on_file_picked(char *path) {
	char *f = ui_files_filename;
	if (string_equals(f, "")) {
		f = string_copy(tr("untitled"));
	}
	export_arm_run_swatches(string("%s%s%s", path, PATH_SEP, f));
}

void project_export_swatches() {
	ui_files_show("arm", true, false, &project_export_swatches_on_file_picked);
}

swatch_color_t *project_make_swatch(i32 base) {
	swatch_color_t *s = ALLOC_INIT(swatch_color_t, {.base       = base,
	                                                .opacity    = 1.0,
	                                                .occlusion  = 1.0,
	                                                .roughness  = 0.8,
	                                                .metallic   = 0.0,
	                                                .normal     = 0xff8080ff,
	                                                .emission   = 0.0,
	                                                .height     = 0.0,
	                                                .subsurface = 0.0});
	return s;
}

swatch_color_t *project_clone_swatch(swatch_color_t *swatch) {
	swatch_color_t *s = ALLOC_INIT(swatch_color_t, {.base       = swatch->base,
	                                                .opacity    = swatch->opacity,
	                                                .occlusion  = swatch->occlusion,
	                                                .roughness  = swatch->roughness,
	                                                .metallic   = swatch->metallic,
	                                                .normal     = swatch->normal,
	                                                .emission   = swatch->emission,
	                                                .height     = swatch->height,
	                                                .subsurface = swatch->subsurface});
	return s;
}

void project_set_default_swatches() {
	// 32-Color Palette by Andrew Kensler
	// http://eastfarthing.com/blog/2016-05-06-palette/
	g_project->swatches = any_array_create_from_raw((void *[]){}, 0);
	i32_array_t *colors = i32_array_create_from_raw(
	    (i32[]){
	        0xffffffff, 0xff000000, 0xffd6a090, 0xffa12c32, 0xfffa2f7a, 0xfffb9fda, 0xffe61cf7, 0xff992f7c, 0xff47011f, 0xff051155, 0xff4f02ec,
	        0xff2d69cb, 0xff00a6ee, 0xff6febff, 0xff08a29a, 0xff2a666a, 0xff063619, 0xff4a4957, 0xff8e7ba4, 0xffb7c0ff, 0xffacbe9c, 0xff827c70,
	        0xff5a3b1c, 0xffae6507, 0xfff7aa30, 0xfff4ea5c, 0xff9b9500, 0xff566204, 0xff11963b, 0xff51e113, 0xff08fdcc,
	    },
	    31);
	for (i32 i = 0; i < colors->length; ++i) {
		i32 c = colors->buffer[i];
		any_array_push(g_project->swatches, project_make_swatch(c));
	}
}

node_group_t *project_get_material_group_by_name(char *group_name) {
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *g = g_project->_->material_groups->buffer[i];
		if (string_equals(g->canvas->name, group_name)) {
			return g;
		}
	}
	return NULL;
}

bool project_is_material_group_in_use(node_group_t *group) {
	ui_node_canvas_t_array_t *canvases = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		any_array_push(canvases, m->canvas);
	}
	for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
		node_group_t *m = g_project->_->material_groups->buffer[i];
		any_array_push(canvases, m->canvas);
	}
	for (i32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *canvas = canvases->buffer[i];
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n     = canvas->nodes->buffer[i];
			char      *cname = group->canvas->name;
			if (string_equals(n->type, "GROUP") && string_equals(n->name, cname)) {
				return true;
			}
		}
	}
	return false;
}
