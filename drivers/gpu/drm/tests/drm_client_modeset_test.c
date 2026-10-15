// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Maxime Ripard <mripard@kernel.org>
 */

#include <kunit/test.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

struct drm_client_modeset_test_priv {
	struct drm_device *drm;
	struct device *dev;
	struct drm_connector connector;
	struct drm_encoder encoder;
};

static int drm_client_modeset_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int count;

	count = drm_add_modes_noedid(connector, 1920, 1200);

	mode = drm_mode_analog_ntsc_480i(connector->dev);
	if (!mode)
		return count;

	drm_mode_probed_add(connector, mode);
	count += 1;

	mode = drm_mode_analog_pal_576i(connector->dev);
	if (!mode)
		return count;

	drm_mode_probed_add(connector, mode);
	count += 1;

	return count;
}

static const struct drm_connector_helper_funcs drm_client_modeset_connector_helper_funcs = {
	.get_modes = drm_client_modeset_connector_get_modes,
};

static int drm_client_modeset_connector_get_modes_preferred(struct drm_connector *connector)
{
	int count = drm_add_modes_noedid(connector, 1920, 1200);

	drm_set_preferred_mode(connector, 1920, 1200);

	return count;
}

static const struct drm_connector_helper_funcs drm_client_modeset_preferred_helper_funcs = {
	.get_modes = drm_client_modeset_connector_get_modes_preferred,
};

static const struct drm_connector_funcs drm_client_modeset_connector_funcs = {
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.fill_modes             = drm_helper_probe_single_connector_modes,
};

static int drm_client_modeset_test_init(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	test->priv = priv;

	priv->dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	priv->drm = __drm_kunit_helper_alloc_drm_device(test, priv->dev,
							sizeof(*priv->drm), 0,
							DRIVER_MODESET);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->drm);

	ret = drmm_connector_init(priv->drm, &priv->connector,
				  &drm_client_modeset_connector_funcs,
				  DRM_MODE_CONNECTOR_Unknown,
				  NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(&priv->connector, &drm_client_modeset_connector_helper_funcs);

	priv->connector.interlace_allowed = true;
	priv->connector.doublescan_allowed = true;

	return 0;
}

static void drm_test_pick_cmdline_res_1920_1080_60(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline_mode = &connector->cmdline_mode;
	struct drm_display_mode *expected_mode;
	const struct drm_display_mode *mode;
	const char *cmdline = "1920x1080@60";
	int ret;

	expected_mode = drm_mode_find_dmt(priv->drm, 1920, 1080, 60, false);
	KUNIT_ASSERT_NOT_NULL(test, expected_mode);

	ret = drm_kunit_add_mode_destroy_action(test, expected_mode);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_ASSERT_TRUE(test,
			  drm_mode_parse_command_line_for_connector(cmdline,
								    connector,
								    cmdline_mode));

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1080);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_pick_cmdline_mode(connector, 1920, 1080);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	KUNIT_EXPECT_TRUE(test, drm_mode_equal(expected_mode, mode));
}

struct drm_connector_pick_cmdline_mode_test {
	const char *cmdline;
	struct drm_display_mode *(*func)(struct drm_device *drm);
};

#define TEST_CMDLINE(_cmdline, _fn)		\
	{					\
		.cmdline = _cmdline,		\
		.func = _fn,			\
	}

static void drm_test_pick_cmdline_named(struct kunit *test)
{
	const struct drm_connector_pick_cmdline_mode_test *params = test->param_value;
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline_mode = &connector->cmdline_mode;
	const struct drm_display_mode *mode;
	struct drm_display_mode *expected_mode;
	const char *cmdline = params->cmdline;
	int ret;

	KUNIT_ASSERT_TRUE(test,
			  drm_mode_parse_command_line_for_connector(cmdline,
								    connector,
								    cmdline_mode));

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1080);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_pick_cmdline_mode(connector, 1920, 1080);
	KUNIT_ASSERT_NOT_NULL(test, mode);

	expected_mode = params->func(drm);
	KUNIT_ASSERT_NOT_NULL(test, expected_mode);

	ret = drm_kunit_add_mode_destroy_action(test, expected_mode);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, drm_mode_equal(expected_mode, mode));
}

static const
struct drm_connector_pick_cmdline_mode_test drm_connector_pick_cmdline_mode_tests[] = {
	TEST_CMDLINE("NTSC", drm_mode_analog_ntsc_480i),
	TEST_CMDLINE("NTSC-J", drm_mode_analog_ntsc_480i),
	TEST_CMDLINE("PAL", drm_mode_analog_pal_576i),
	TEST_CMDLINE("PAL-M", drm_mode_analog_ntsc_480i),
};

static void
drm_connector_pick_cmdline_mode_desc(const struct drm_connector_pick_cmdline_mode_test *t,
				     char *desc)
{
	sprintf(desc, "%s", t->cmdline);
}

KUNIT_ARRAY_PARAM(drm_connector_pick_cmdline_mode,
		  drm_connector_pick_cmdline_mode_tests,
		  drm_connector_pick_cmdline_mode_desc);

static struct kunit_case drm_test_pick_cmdline_tests[] = {
	KUNIT_CASE(drm_test_pick_cmdline_res_1920_1080_60),
	KUNIT_CASE_PARAM(drm_test_pick_cmdline_named,
			 drm_connector_pick_cmdline_mode_gen_params),
	{}
};

static struct kunit_suite drm_test_pick_cmdline_test_suite = {
	.name = "drm_test_pick_cmdline",
	.init = drm_client_modeset_test_init,
	.test_cases = drm_test_pick_cmdline_tests
};

kunit_test_suite(drm_test_pick_cmdline_test_suite);

static void drm_test_pick_first_mode_within_limits(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	const struct drm_display_mode *mode;
	int ret;

	drm_connector_helper_add(connector, &drm_client_modeset_preferred_helper_funcs);

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1200);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_first_mode(connector, 1920, 1200);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	KUNIT_EXPECT_EQ(test, mode->hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 1200);

	KUNIT_ASSERT_NULL(test, drm_connector_preferred_mode(connector, 1024, 768));

	mode = drm_connector_first_mode(connector, 1024, 768);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	KUNIT_EXPECT_EQ(test, mode->hdisplay, 1024);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 768);
}

static void drm_test_pick_cmdline_mode_within_limits(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	struct drm_cmdline_mode *cmdline_mode = &connector->cmdline_mode;
	const struct drm_display_mode *mode;
	int ret;

	KUNIT_ASSERT_TRUE(test, drm_mode_parse_command_line_for_connector("1920x1200", connector,
									  cmdline_mode));

	mutex_lock(&drm->mode_config.mutex);
	ret = drm_helper_probe_single_connector_modes(connector, 1920, 1200);
	mutex_unlock(&drm->mode_config.mutex);
	KUNIT_ASSERT_GT(test, ret, 0);

	mode = drm_connector_pick_cmdline_mode(connector, 1920, 1200);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	KUNIT_EXPECT_EQ(test, mode->hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 1200);

	KUNIT_EXPECT_NULL(test, drm_connector_pick_cmdline_mode(connector, 1024, 768));
}

KUNIT_DEFINE_ACTION_WRAPPER(drm_client_modeset_free_wrapper, drm_client_modeset_free,
			    struct drm_client_dev *);

static void drm_test_probe_keeps_modes_above_limits(struct kunit *test)
{
	struct drm_client_modeset_test_priv *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct drm_connector *connector = &priv->connector;
	const struct drm_display_mode *mode;
	struct drm_client_dev *client;
	struct drm_encoder *encoder = &priv->encoder;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int ret;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	plane = drm_kunit_helper_create_primary_plane(test, drm, NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	crtc = drm_kunit_helper_create_crtc(test, drm, plane, NULL, NULL, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);

	ret = drmm_encoder_init(drm, encoder, NULL, DRM_MODE_ENCODER_NONE, NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_connector_attach_encoder(connector, encoder);
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_connector_helper_add(connector, &drm_client_modeset_preferred_helper_funcs);

	client = kunit_kzalloc(test, sizeof(*client), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, client);
	client->dev = drm;

	ret = drm_client_modeset_create(client);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test, drm_client_modeset_free_wrapper, client);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_client_modeset_probe(client, 1024, 768);
	KUNIT_ASSERT_EQ(test, ret, 0);

	mode = client->modesets[0].mode;
	KUNIT_ASSERT_NOT_NULL(test, mode);
	KUNIT_EXPECT_EQ(test, mode->hdisplay, 1024);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 768);

	mode = drm_connector_preferred_mode(connector, 1920, 1200);
	KUNIT_ASSERT_NOT_NULL(test, mode);
	KUNIT_EXPECT_EQ(test, mode->hdisplay, 1920);
	KUNIT_EXPECT_EQ(test, mode->vdisplay, 1200);
}

static struct kunit_case drm_test_pick_mode_limits_tests[] = {
	KUNIT_CASE(drm_test_pick_first_mode_within_limits),
	KUNIT_CASE(drm_test_pick_cmdline_mode_within_limits),
	KUNIT_CASE(drm_test_probe_keeps_modes_above_limits),
	{}
};

static struct kunit_suite drm_test_pick_mode_limits_test_suite = {
	.name = "drm_test_pick_mode_limits",
	.init = drm_client_modeset_test_init,
	.test_cases = drm_test_pick_mode_limits_tests
};

kunit_test_suite(drm_test_pick_mode_limits_test_suite);

/*
 * This file is included directly by drm_client_modeset.c so we can't
 * use any MODULE_* macro here.
 */
