
#include "../global.h"

extern char *str_tex_noise; // noise_texture_node.c

// Edge Mask: curvature edges whose reach is moved per point by a grunge value, the method of
// Substance's mask generators and of the Imperfectionist add-on (grunge sets the edge width, it does not paint the wear).
//   edge = curvature + (grunge - 0.5) * 2 * Grunge Amount
//   mask = smoothstep(1 - Wear Level -+ Softness / 2, edge)
//   mask = clamp(mask + peel spots on flat faces) * breakup patches
// Link a Curvature Texture with a large Radius (soft falloff from the edges) and an image or noise as Grunge.

char *edge_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	node_shader_t *kong = parser_material_kong;
	node_shader_add_function(kong, str_tex_noise);
	char *s        = parser_material_store_var_name(node);
	char *curv     = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *grunge   = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *level    = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *amount   = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char *soft     = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char *peel     = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char *pscale   = parser_material_parse_value_input(node->inputs->buffer[6], false);
	char *breakup  = parser_material_parse_value_input(node->inputs->buffer[7], false);
	char *bscale   = parser_material_parse_value_input(node->inputs->buffer[8], false);
	char *contrast = parser_material_parse_value_input(node->inputs->buffer[9], false);
	char *seed     = parser_material_parse_value_input(node->inputs->buffer[10], false);
	char *co       = parser_material_default_coord();

	parser_material_write(kong, string_tmp("float %s_edge = clamp(%s, 0.0, 1.0) + (%s - 0.5) * 2.0 * %s;", s, curv, grunge, amount));
	parser_material_write(kong, string_tmp("float %s_w = max(%s * 0.5, 0.001);", s, soft));
	parser_material_write(kong, string_tmp("float %s_m = clamp((%s_edge - (1.0 - %s - %s_w)) / (%s_w * 2.0), 0.0, 1.0);", s, s, level, s, s));
	parser_material_write(kong, string_tmp("%s_m = %s_m * %s_m * (3.0 - 2.0 * %s_m);", s, s, s, s));
	// Peel: small spots on flat faces (the peel ramp of edge_wear.py: 0.118 -> 0.327, 0.355 -> 0)
	parser_material_write(kong, string_tmp("float %s_pn = tex_noise_fbm(%s * %s + float3(%s + 7.7, %s + 7.7, %s + 7.7), 4.4, 0.4658, 2.0);", s, co, pscale, seed,
	                                       seed, seed));
	parser_material_write(kong, string_tmp("float %s_pr = clamp((%s_pn - 0.35455) / (0.11818 - 0.35455), 0.0, 1.0) * 0.327122;", s, s));
	parser_material_write(kong, string_tmp("%s_m = clamp(%s_m + %s_pr * %s * 15.0, 0.0, 1.0);", s, s, s, peel));
	// Breakup: removes the mask in noise patches, the cut moves from 0.35 (little removed) to 0.65 (most removed)
	parser_material_write(kong, string_tmp("float %s_bn = tex_noise_fbm(%s * %s + float3(%s + 41.3, %s + 41.3, %s + 41.3), 4.0, 0.55, 2.0);", s, co, bscale, seed,
	                                       seed, seed));
	parser_material_write(kong, string_tmp("float %s_cut = 0.35 + 0.3 * %s;", s, breakup));
	parser_material_write(kong, string_tmp("float %s_half = 0.003 + 0.247 * (1.0 - %s) * (1.0 - %s);", s, contrast, contrast));
	parser_material_write(kong, string_tmp("float %s_bk = clamp((%s_bn - (%s_cut - %s_half)) / (%s_half * 2.0), 0.0, 1.0);", s, s, s, s, s));
	parser_material_write(kong, string_tmp("if (%s > 0.0001) { %s_m = %s_m * %s_bk; }", breakup, s, s, s));
	return string_tmp("%s_m", s);
}

static ui_node_socket_t *edge_mask_socket(char *name, f32 value, f32 min, f32 max) {
	return ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                     .node_id       = 0,
	                                     .name          = name,
	                                     .type          = "VALUE",
	                                     .color         = 0xffa1a1a1,
	                                     .default_value = f32_array_create_x(value),
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = 100,
	                                     .display       = 0});
}

void edge_mask_node_init() {
	ui_node_t *edge_mask_node_def = ALLOC_INIT(ui_node_t, {.id      = 0,
	                                                       .name    = _tr("Edge Mask"),
	                                                       .type    = "EDGE_MASK", // extension
	                                                       .x       = 0,
	                                                       .y       = 0,
	                                                       .color   = 0xff4982a0,
	                                                       .inputs  = any_array_create_from_raw(
                                                              (void *[]){
                                                                  edge_mask_socket(_tr("Curvature"), 0.5, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Grunge"), 0.5, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Wear Level"), 0.5, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Grunge Amount"), 0.5, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Softness"), 0.1, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Peel"), 0.0, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Peel Scale"), 25.0, 0.0, 100.0),
                                                                  edge_mask_socket(_tr("Breakup"), 0.0, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Breakup Scale"), 2.0, 0.0, 20.0),
                                                                  edge_mask_socket(_tr("Breakup Contrast"), 0.5, 0.0, 1.0),
                                                                  edge_mask_socket(_tr("Seed"), 0.0, 0.0, 1000.0),
                                                              },
                                                              11),
	                                                       .outputs = any_array_create_from_raw(
                                                              (void *[]){
                                                                  edge_mask_socket(_tr("Mask"), 0.0, 0.0, 1.0),
                                                              },
                                                              1),
	                                                       .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                       .width   = 0,
	                                                       .flags   = 0});

	any_array_push(nodes_material_texture, edge_mask_node_def);
	any_map_set(parser_material_node_values, "EDGE_MASK", edge_mask_node_value);
}
