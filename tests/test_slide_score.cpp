#include "doctest.h"

#define SHEREDOM_JSON_IMPLEMENTATION
#include "json.h"

#include "slide_score.h"
#include "remote.h"
#include "viewer.h"

extern "C" {
i32 global_next_resource_id = 1000;

http_response_t* open_remote_uri_with_extra_headers(const char* uri, const char* api_token, const char* cookie_header) {
	(void)uri;
	(void)api_token;
	(void)cookie_header;
	return NULL;
}

void http_response_destroy(http_response_t* response) {
	(void)response;
}

void unload_all_images(app_state_t* app_state) {
	(void)app_state;
}

void add_image(app_state_t* app_state, image_t* image, bool need_zoom_reset, bool need_image_registration) {
	(void)app_state;
	(void)image;
	(void)need_zoom_reset;
	(void)need_image_registration;
}

void image_destroy(image_t* image) {
	(void)image;
}

bool init_image_from_slide_score(image_t* image, slide_score_remote_image_t* remote, bool is_overlay) {
	(void)image;
	(void)remote;
	(void)is_overlay;
	return false;
}
}

TEST_CASE("Slide Score metadata parser handles nested metadata and arrays") {
	const char* json =
		"{\"success\":true,\"metadata\":{"
		"\"level0TileWidth\":512,"
		"\"level0TileHeight\":512,"
		"\"osdTileSize\":512,"
		"\"mppX\":0.25,"
		"\"mppY\":0.26,"
		"\"objectivePower\":20,"
		"\"backgroundColor\":null,"
		"\"levelCount\":7,"
		"\"zLayerCount\":0,"
		"\"level0Width\":61187,"
		"\"level0Height\":20227,"
		"\"downsamples\":[1,2,4,8,16,32,64],"
		"\"fileName\":\"21_R81855\""
		"}}";

	slide_score_api_result_t result = debug_slide_score_api_handle_response(json, strlen(json), SLIDE_SCORE_API_GET_IMAGE_METADATA);

	CHECK(result.success);
	CHECK(result.get_image_metadata.tile_width == 512);
	CHECK(result.get_image_metadata.tile_height == 512);
	CHECK(result.get_image_metadata.mpp_x == doctest::Approx(0.25f));
	CHECK(result.get_image_metadata.mpp_y == doctest::Approx(0.26f));
	CHECK(result.get_image_metadata.level_count == 7);
	CHECK(result.get_image_metadata.level_0_width == 61187);
	CHECK(result.get_image_metadata.level_0_height == 20227);
	CHECK(result.get_image_metadata.downsample_count == 7);
	CHECK(result.get_image_metadata.downsamples[4] == doctest::Approx(16.0));
	CHECK(strcmp(result.get_image_metadata.filename, "21_R81855") == 0);
}

TEST_CASE("Slide Score tile-server parser and tile URL mapping") {
	const char* json =
		"{\"cookiePart\":\"cookie-token\","
		"\"urlPart\":\"url-token\","
		"\"expiresOn\":\"2027-05-25T09:02:03Z\"}";

	slide_score_api_result_t result = debug_slide_score_api_handle_response(json, strlen(json), SLIDE_SCORE_API_GET_TILE_SERVER);
	CHECK(strcmp(result.get_tile_server.cookie_part, "cookie-token") == 0);
	CHECK(strcmp(result.get_tile_server.url_part, "url-token") == 0);

	slide_score_remote_image_t remote = {};
	slide_score_client_init(&remote.client, "https://clidipa.slidescore.com", "secret");
	remote.image_id = 23;
	remote.max_deepzoom_level = 16;
	remote.tile_server = result.get_tile_server;
	remote.metadata.downsample_count = 7;
	remote.metadata.downsamples[0] = 1;
	remote.metadata.downsamples[1] = 2;
	remote.metadata.downsamples[2] = 4;
	remote.metadata.downsamples[3] = 8;
	remote.metadata.downsamples[4] = 16;
	remote.metadata.downsamples[5] = 32;
	remote.metadata.downsamples[6] = 64;

	char url[1024];
	slide_score_build_tile_url(url, sizeof(url), &remote, 4, 3, 5);
	CHECK(strcmp(url, "https://clidipa.slidescore.com/i/23/url-token/i_files/12/3_5.jpeg") == 0);
}
