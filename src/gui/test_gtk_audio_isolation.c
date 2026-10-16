#include "gui_window.h"
#include "converter_platform.h"
#include "mux_policy.h"
#include "hq_catalog.h"

#include <string.h>

static void drain_updates(void)
{
    while (g_main_context_iteration(NULL, FALSE))
        ;
}

static const char *selected_text(GtkWidget *widget)
{
    GtkStringObject *item = GTK_STRING_OBJECT(
        gtk_drop_down_get_selected_item(GTK_DROP_DOWN(widget)));
    g_assert_nonnull(item);
    return gtk_string_object_get_string(item);
}

static void select_text(GtkWidget *widget, const char *text)
{
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
    guint count = g_list_model_get_n_items(model);
    for (guint i = 0; i < count; i++) {
        GtkStringObject *item = g_list_model_get_item(model, i);
        gboolean match = g_strcmp0(gtk_string_object_get_string(item), text) == 0;
        g_object_unref(item);
        if (match) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), i);
            drain_updates();
            return;
        }
    }
    g_error("Missing dropdown entry: %s", text);
}

static gboolean dropdown_has_text(GtkWidget *widget, const char *text)
{
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
    for (guint i = 0; i < g_list_model_get_n_items(model); i++) {
        GtkStringObject *item = g_list_model_get_item(model, i);
        gboolean match = g_strcmp0(gtk_string_object_get_string(item), text) == 0;
        g_object_unref(item);
        if (match)
            return TRUE;
    }
    return FALSE;
}

static ConvertOptions collect_options(AppWidgets *w)
{
    ConvertOptions opts;
    char **files;
    int count;
    collect_options_from_gui(w, &opts, &files, &count);
    for (int i = 0; i < count; i++)
        g_free(files[i]);
    g_free(files);
    return opts;
}

static void assert_preset_model(AppWidgets *w, const char *group, const char *encoder)
{
    const char **presets = NULL;
    int count = selection_catalog_list_presets(w->selection_catalog,
                                               group, encoder, &presets);
    g_assert_cmpint(count, >, 0);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(w->preset_list)), ==, count);
    for (int i = 0; i < count; i++)
        g_assert_cmpstr(gtk_string_list_get_string(w->preset_list, i), ==, presets[i]);
    selection_catalog_free_list(presets);
}

static void assert_video_unchanged(AppWidgets *w, const ConvertOptions *before,
                                   const char *video_args, guint codec,
                                   guint encoder, guint preset, guint vulkan,
                                   guint vaapi, guint filter, guint deblock)
{
    ConvertOptions after = collect_options(w);
    g_assert_cmpstr(after.codec, ==, before->codec);
    g_assert_cmpstr(after.preset, ==, before->preset);
    g_assert_cmpstr(after.hw_device, ==, before->hw_device);
    g_assert_cmpint(after.vulkan_device, ==, before->vulkan_device);
    g_assert_cmpint(after.deblock, ==, before->deblock);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->codec_combo)), ==, codec);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->encoder_combo)), ==, encoder);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->profile_combo)), ==, preset);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vulkan_device_combo)), ==, vulkan);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vaapi_device_combo)), ==, vaapi);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->filter_combo)), ==, filter);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->deblock_combo)), ==, deblock);
    g_assert_cmpstr(platform_get_video_codec_flags(after.codec, NULL, &after), ==, video_args);
}

static void test_audio_changes(AppWidgets *w, const char *group,
                               const char *encoder, const char *preset)
{
    static const char *audio_modes[] = {"pcm", "fdk_aac_320", "fdk_aac_320_ac3_640"};
    static const char *normalizations[] = {
        "none", "peak_norm", "peak_norm_2pass", "loudness_norm", "loudness_norm_2pass"
    };
    select_text(w->codec_combo, group);
    select_text(w->encoder_combo, encoder);
    g_assert_false(dropdown_has_text(w->audio_output_combo, "copy"));
    assert_preset_model(w, group, encoder);
    select_text(w->filter_combo, "strong");
    select_text(w->deblock_combo, "strong");
    select_text(w->profile_combo, preset);
    if (g_list_model_get_n_items(G_LIST_MODEL(w->vulkan_device_list)) > 1)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->vulkan_device_combo), 1);
    if (strcmp(group, "vaapi") == 0) {
        gchar *node = g_strdup("/dev/dri/test-render-node");
        gtk_string_list_append(w->vaapi_device_list, "Test VAAPI selection");
        g_array_append_val(w->vaapi_device_nodes, node);
    }
    if (g_list_model_get_n_items(G_LIST_MODEL(w->vaapi_device_list)) > 1)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->vaapi_device_combo), 1);
    drain_updates();
    ConvertOptions before = collect_options(w);
    g_assert_cmpstr(before.preset, ==, preset);
    char *video_args = g_strdup(platform_get_video_codec_flags(before.codec, NULL, &before));
    if (strcmp(group, "software") != 0) {
        g_assert_nonnull(video_args);
        g_assert_true(video_args[0] != '\0');
    }
    guint codec_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->codec_combo));
    guint encoder_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->encoder_combo));
    guint preset_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->profile_combo));
    guint vulkan_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vulkan_device_combo));
    guint vaapi_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->vaapi_device_combo));
    guint filter_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->filter_combo));
    guint deblock_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->deblock_combo));
    for (guint a = 0; a < G_N_ELEMENTS(audio_modes); a++) {
        select_text(w->audio_output_combo, audio_modes[a]);
        for (guint n = 0; n < G_N_ELEMENTS(normalizations); n++) {
            select_text(w->audio_norm_combo, normalizations[n]);
            ConvertOptions after = collect_options(w);
            g_assert_cmpstr(after.audio_output_mode, ==, audio_modes[a]);
            g_assert_cmpstr(after.audio_norm, ==, normalizations[n]);
            g_assert_cmpint(gtk_widget_get_sensitive(w->genre_combo), ==,
                           strcmp(normalizations[n], "loudness_norm_2pass") == 0);
            assert_video_unchanged(w, &before, video_args, codec_index,
                                   encoder_index, preset_index, vulkan_index,
                                   vaapi_index, filter_index, deblock_index);
        }
    }
    set_running_ui_state(w, TRUE);
    set_running_ui_state(w, FALSE);
    drain_updates();
    assert_video_unchanged(w, &before, video_args, codec_index, encoder_index,
                           preset_index, vulkan_index, vaapi_index,
                           filter_index, deblock_index);
    g_free(video_args);
}

static void test_hq_audio(AppWidgets *w)
{
    /* Load only the data catalog; no HQ library or hardware probe is needed. */
    g_assert_cmpint(hq_catalog_load(FFMPEG_CONVERTER_HQ_TEST_ROOT), ==, 0);
    if (!dropdown_has_text(w->codec_combo, "hq_converter"))
        gtk_string_list_append(w->codec_list, "hq_converter");
    select_text(w->codec_combo, "hq_converter");
    for (size_t i = 0; i < hq_catalog_codec_count(); i++) {
        const char *encoder = hq_catalog_codec_name(i);
        select_text(w->encoder_combo, encoder);
        MuxBuildProfile profile;
        g_assert_true(mux_build_profile_default(&profile));
        AudioModeAvailability available = audio_mode_availability(
            &profile, AUDIO_WORKFLOW_HQ, MUX_MODE_INVALID, NULL, 0,
            mux_video_codec_parse(encoder));
        GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(w->audio_output_combo));
        g_assert_cmpuint(g_list_model_get_n_items(model), ==, available.count);
        g_assert_cmpuint(available.count, >, 0);
        if (hq_catalog_is_prores_codec(encoder)) {
            g_assert_cmpuint(available.count, ==, 1);
            g_assert_cmpstr(selected_text(w->audio_output_combo), ==, "pcm");
        }
        guint presets = g_list_model_get_n_items(G_LIST_MODEL(w->preset_list));
        g_assert_cmpuint(presets, >, 0);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->profile_combo), presets - 1);
        drain_updates();
        char *preset = g_strdup(selected_text(w->profile_combo));
        for (size_t a = 0; a < available.count; a++) {
            const char *mode = mux_audio_mode_name(available.modes[a]);
            select_text(w->audio_output_combo, mode);
            select_text(w->audio_norm_combo, "none");
            g_assert_cmpstr(selected_text(w->audio_output_combo), ==, mode);
            g_assert_cmpstr(selected_text(w->profile_combo), ==, preset);
            ConvertOptions opts = collect_options(w);
            g_assert_cmpstr(opts.hw_device, ==, encoder);
            g_assert_cmpstr(opts.preset, ==, preset);
        }
        g_free(preset);
    }
}

static void test_mux_default(AppWidgets *w)
{
    MuxBuildProfile profile;
    MuxPolicy expected;
    char error[256];
    g_test_message("MUX-T016: GTK resolves mux defaults through shared Policy");
    g_assert_true(mux_build_profile_default(&profile));
    g_assert_true(mux_policy_resolve(&profile, MUX_MODE_M4V, NULL,
                                    MUX_VIDEO_UNKNOWN, MUX_VIDEO_UNKNOWN,
                                    &expected, error, sizeof(error)));
    select_text(w->audio_output_combo, "pcm");
    select_text(w->codec_combo, "mux");
    select_text(w->encoder_combo, "m4v");
    g_assert_cmpstr(selected_text(w->audio_output_combo), ==,
                    mux_audio_mode_name(expected.audio_mode));
    ConvertOptions opts = collect_options(w);
    g_assert_cmpstr(opts.codec, ==, "mux");
    g_assert_cmpstr(opts.preset, ==, "m4v");
    g_assert_cmpstr(opts.audio_output_mode, ==,
                    mux_audio_mode_name(expected.audio_mode));
    g_assert_cmpstr(selected_text(w->profile_combo), ==, "default");
    g_assert_false(dropdown_has_text(w->audio_output_combo, "pcm"));
    g_assert_false(dropdown_has_text(w->audio_output_combo, "copy"));
    select_text(w->audio_output_combo, "fdk_aac_320_ac3_640");
    g_assert_cmpstr(selected_text(w->audio_output_combo), ==, "fdk_aac_320_ac3_640");
    select_text(w->codec_combo, "software");
    g_assert_cmpstr(selected_text(w->audio_output_combo), ==, "fdk_aac_320_ac3_640");
    g_assert_false(dropdown_has_text(w->audio_output_combo, "copy"));
    select_text(w->encoder_combo, "prores");
    assert_preset_model(w, "software", "prores");
    select_text(w->profile_combo, "hq");
    select_text(w->encoder_combo, "prores_ks");
    assert_preset_model(w, "software", "prores_ks");
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->profile_combo)), ==, 0);
    select_text(w->codec_combo, "vaapi");
    select_text(w->encoder_combo, "h264");
    assert_preset_model(w, "vaapi", "h264");
    select_text(w->profile_combo, "quality");
    select_text(w->encoder_combo, "av1");
    assert_preset_model(w, "vaapi", "av1");
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(w->profile_combo)), ==, 0);
}

int main(void)
{
    g_setenv("PRESETS_PATH", FFMPEG_CONVERTER_PRESETS_DIR, TRUE);
    if (!gtk_init_check()) {
        g_print("No GTK display available; skipping widget tests.\n");
        return 77;
    }
    GtkApplication *app = gtk_application_new(
        "io.github.possible947.ffmpeg_converter.tests", G_APPLICATION_NON_UNIQUE);
    GError *error = NULL;
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
    AppWidgets w = {0};
    g_mutex_init(&w.thread_lock);
    /* Model capabilities only: never run a real hardware encode/probe. */
    w.linux_codec_support.has_av1_vaapi = 1;
    w.linux_codec_support.has_h264_vaapi = 1;
    w.linux_codec_support.has_hevc_vulkan = 1;
    w.linux_codec_support.vulkan_hw_device_index = 2;
    w.linux_codec_support.vulkan_hw_working_mask = 1 << 2;
    w.linux_codec_support.has_hevc_nvenc = 1;
    w.linux_codec_support.has_h264_qsv = 1;
    w.window = create_main_window(app, &w);
    drain_updates();
    test_audio_changes(&w, "software", "prores_ks", "hq");
    test_audio_changes(&w, "vaapi", "av1", "quality");
    test_audio_changes(&w, "vulkan", "hevc", "quality");
    test_audio_changes(&w, "nvenc", "hevc", "quality");
    test_audio_changes(&w, "qsv", "h264", "speed");
    test_mux_default(&w);
    test_hq_audio(&w);
    w.shutting_down = TRUE;
    if (w.dependent_update_source_id)
        g_source_remove(w.dependent_update_source_id);
    gtk_window_destroy(GTK_WINDOW(w.window));
    selection_catalog_free(w.selection_catalog);
    g_ptr_array_unref(w.file_paths);
    g_array_unref(w.vulkan_device_ids);
    for (guint i = 0; i < w.vaapi_device_nodes->len; i++)
        g_free(g_array_index(w.vaapi_device_nodes, gchar *, i));
    g_array_unref(w.vaapi_device_nodes);
    g_free(w.output_dir_path);
    g_free(w.video_track_path);
    g_mutex_clear(&w.thread_lock);
    g_object_unref(app);
    platform_cleanup();
    g_print("GTK audio/video isolation tests passed.\n");
    return 0;
}
