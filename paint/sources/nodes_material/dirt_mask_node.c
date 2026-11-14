
#include "../global.h"

extern char *str_tex_noise; // noise_texture_node.c

// Dirt Mask: the Dirt effect of the Imperfectionist add-on.
//   cavity  = (1 - AO) + fine cavities from a linked Curvature Texture (Convex Only off) x Cavity
//   breakup = grunge * 2b - b, centered on 0: grunge both adds dirt (bright) and removes it (dark)
//   mask    = smoothstep(low, low + Softness, cavity + breakup), low = (1 - Amount) - Softness / 2
// The AO is baked by the node itself on the cpu (make_dirt_ao_bake), on the mesh UVs whatever the projection.
// Spread is the AO distance relative to the model size, how far dirt reaches out of corners and cavities.
// Grunge empty: the internal noise (Noise Scale, Detail, Roughness, Distortion, Seed). Linked: that image or pattern.

char *dirt_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_t *kong = parser_material_kong;
	char          *id   = make_material_node_preview_id(node, NULL);
	char          *tex  = string_tmp("texbake_%s", id);
	node_shader_add_texture(kong, tex, string_tmp("_%s", tex));
	char *s       = parser_material_store_var_name(node);
	char *uv      = parser_material_mesh_uv ? "tex_coord_uv" : "tex_coord"; // Baked on the mesh UVs, whatever the projection
	char *curv    = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *amount  = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *soft    = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char *cav_w   = parser_material_parse_value_input(node->inputs->buffer[6], false);
	char *breakup = parser_material_parse_value_input(node->inputs->buffer[7], false);

	char *grunge;
	if (parser_material_get_input_link(node->inputs->buffer[0]) != NULL) {
		grunge = parser_material_parse_value_input(node->inputs->buffer[0], false);
	}
	else {
		node_shader_add_function(kong, str_tex_noise);
		char *co      = parser_material_default_coord();
		char *scale   = parser_material_parse_value_input(node->inputs->buffer[8], false);
		char *detail  = parser_material_parse_value_input(node->inputs->buffer[9], false);
		char *rough   = parser_material_parse_value_input(node->inputs->buffer[10], false);
		char *distort = parser_material_parse_value_input(node->inputs->buffer[11], false);
		char *seed    = parser_material_parse_value_input(node->inputs->buffer[12], false);
		parser_material_write(kong, string_tmp("float %s_gn = tex_noise(%s + float3(%s, %s, %s) * 0.137, %s, %s, %s, 2.0, %s).x;", s, co, seed, seed, seed, scale,
		                                       detail, rough, distort));
		grunge = string_tmp("%s_gn", s);
	}

	parser_material_write(kong, string_tmp("float %s_ao = sample(%s, sampler_linear, %s).r;", s, tex, uv));
	parser_material_write(kong, string_tmp("float %s_cav = (1.0 - %s_ao) + clamp((0.5 - %s) * 2.0, 0.0, 1.0) * %s;", s, s, curv, cav_w));
	parser_material_write(kong, string_tmp("float %s_src = %s_cav + %s * 2.0 * %s - %s;", s, s, grunge, breakup, breakup));
	parser_material_write(kong, string_tmp("float %s_w = max(%s, 0.001);", s, soft));
	parser_material_write(kong, string_tmp("float %s_lo = (1.0 - %s) - %s_w * 0.5;", s, amount, s));
	parser_material_write(kong, string_tmp("float %s_m = clamp((%s_src - %s_lo) / %s_w, 0.0, 1.0);", s, s, s, s));
	parser_material_write(kong, string_tmp("%s_m = %s_m * %s_m * (3.0 - 2.0 * %s_m);", s, s, s, s));
	return string_tmp("%s_m", s);
}

static ui_node_socket_t *dirt_mask_socket(char *name, f32 value, f32 min, f32 max, i32 precision) {
	return ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                     .node_id       = 0,
	                                     .name          = name,
	                                     .type          = "VALUE",
	                                     .color         = 0xffa1a1a1,
	                                     .default_value = f32_array_create_x(value),
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = precision,
	                                     .display       = 0});
}

void dirt_mask_node_init() {
	ui_node_t *dirt_mask_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                       .name    = _tr("Dirt Mask"),
	                                                       .type    = "DIRT_MASK", // extension
	                                                       .x       = 0,
	                                                       .y       = 0,
	                                                       .color   = 0xff4982a0,
	                                                       .inputs  = any_array_create_from_raw(
                                                              (void *[]){
                                                                  dirt_mask_socket(_tr("Grunge"), 0.5, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Curvature"), 0.5, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Amount"), 0.6, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Softness"), 0.35, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Spread"), 0.15, 0.01, 1.0, 100),
                                                                  dirt_mask_socket(_tr("AO Samples"), 32.0, 1.0, 256.0, 1),
                                                                  dirt_mask_socket(_tr("Cavity"), 0.3, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Breakup"), 0.5, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Noise Scale"), 6.0, 0.0, 50.0, 100),
                                                                  dirt_mask_socket(_tr("Noise Detail"), 6.0, 0.0, 15.0, 100),
                                                                  dirt_mask_socket(_tr("Noise Roughness"), 0.6, 0.0, 1.0, 100),
                                                                  dirt_mask_socket(_tr("Noise Distortion"), 0.3, 0.0, 5.0, 100),
                                                                  dirt_mask_socket(_tr("Seed"), 0.0, 0.0, 1000.0, 100),
                                                              },
                                                              13),
	                                                       .outputs = any_array_create_from_raw(
                                                              (void *[]){
                                                                  dirt_mask_socket(_tr("Mask"), 0.0, 0.0, 1.0, 100),
                                                              },
                                                              1),
	                                                       .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                       .width   = 0,
	                                                       .flags   = 0});

	any_array_push(nodes_material_texture, dirt_mask_node_def);
	any_map_set(parser_material_node_values, "DIRT_MASK", dirt_mask_node_value);
}
