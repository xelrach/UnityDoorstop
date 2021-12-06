#include "bootstrap.h"
#include "config.h"
#include "crt.h"
#include "il2cpp.h"
#include "logging.h"
#include "mono.h"
#include "paths.h"
#include "util.h"

void doorstop_bootstrap(void *mono_domain) {
    if (!getenv(TEXT("DOORSTOP_INITIALIZED"))) {
        LOG("DOORSTOP_INITIALIZED is set! Skipping!");
        cleanup_config();
        return;
    }
    setenv(TEXT("DOORSTOP_INITIALIZED"), TEXT("TRUE"), TRUE);

    mono.thread_set_main(mono.thread_current());

    if (mono.domain_set_config) {
#define CONFIG_EXT TEXT(".config")
        char_t *app_path = NULL;
        program_path(&app_path);
        char_t *config_path =
            calloc(strlen(app_path) + 1 + STR_LEN(CONFIG_EXT), sizeof(char_t));
        strcpy(config_path, app_path);

        strcat(config_path, CONFIG_EXT);
        char_t *folder_path = get_folder_name(app_path);

        char *config_path_n = narrow(config_path);
        char *folder_path_n = narrow(folder_path);

        LOG("Setting config paths: base dir: %s; config path: %s\n",
            folder_path_n, config_path_n);

        mono.domain_set_config(mono_domain, folder_path_n, config_path_n);

        free(app_path);
        free(folder_path);
        free(config_path);
        free(config_path_n);
        free(folder_path_n);
#undef CONFIG_EXT
    }

    char *assembly_dir = mono.assembly_getrootdir();
    LOG("Assembly dir: %s\n", assembly_dir);

    char_t *norm_assembly_dir = widen(assembly_dir);
    setenv(TEXT("DOORSTOP_MANAGED_FOLDER_DIR"), norm_assembly_dir, TRUE);
    free(norm_assembly_dir);

    char *dll_path = narrow(config.target_assembly);

    char_t *app_path = NULL;
    get_module_path(NULL, &app_path, NULL, 0);
    setenv(TEXT("DOORSTOP_PROCESS_PATH"), app_path, TRUE);

    LOG("Loading assembly: %s\n", dll_path);

    void *file = fopen(config.target_assembly, "r");
    if (!file) {
        LOG("Failed to open assembly: %s\n", config.target_assembly);
        return;
    }

    size_t size = get_file_size(file);
    void *data = malloc(size);
    fread(data, size, 1, file);
    fclose(file);

    MonoImageOpenStatus s = MONO_IMAGE_OK;
    void *image = mono.image_open_from_data_with_name(data, size, TRUE, &s,
                                                      FALSE, dll_path);
    free(data);
    if (s != MONO_IMAGE_OK) {
        LOG("Failed to load assembly image: %s. Got result: %d\n",
            config.target_assembly, s);
        return;
    }

    free(dll_path);

    s = MONO_IMAGE_OK;
    void *assembly = mono.assembly_load_from_full(image, dll_path, &s, FALSE);

    if (s != MONO_IMAGE_OK) {
        LOG("Failed to load assembly: %s. Got result: %d\n",
            config.target_assembly, s);
        return;
    }

    void *desc = mono.method_desc_new("*:Main", FALSE);
    void *method = mono.method_desc_search_in_image(desc, image);
    mono.method_desc_free(desc);
    ASSERT_SOFT(method != NULL);

    void *signature = mono.method_signature(method);

    unsigned int params = mono.signature_get_param_count(signature);

    void **args = NULL;
    if (params == 1) {
        void *args_array =
            mono.array_new(mono_domain, mono.get_string_class(), 0);
        args = malloc(sizeof(void *) * 1);
        args[0] = args_array;
    }

    LOG("Invoking method %p\n", method);
    void *exc = NULL;
    mono.runtime_invoke(method, NULL, args, &exc);
    if (exc != NULL) {
        LOG("Error invoking code!\n");
        if (mono.object_to_string) {
            void *str = mono.object_to_string(exc, NULL);
            char *exc_str = mono.string_to_utf8(str);
            LOG("Error message: %s\n", exc_str);
        }
    }
    LOG("Done\n");

    if (args != NULL) {
        free(app_path);
        free(args);
    }

    cleanup_config();
}

void *init_mono(const char *root_domain_name, const char *runtime_version) {
    LOG("Starting mono domain \"%s\"", root_domain_name);
    char_t *root_dir = widen(mono.assembly_getrootdir());
    LOG("Current root: %s\n", root_dir);
    if (config.mono_dll_search_path_override) {
        LOG("Overriding mono DLL search path\n");

        char_t *override_dir_full =
            get_full_path(config.mono_dll_search_path_override);
        LOG("Override root path: %s\n", override_dir_full);

        char_t *mono_search_path = calloc(
            strlen(root_dir) + strlen(override_dir_full) + 2, sizeof(char_t));
        strcat(mono_search_path, override_dir_full);
        strcat(mono_search_path, TEXT(";"));
        strcat(mono_search_path, root_dir);

        LOG("Mono search path: %s\n", mono_search_path);
        char *mono_search_path_n = narrow(mono_search_path);
        mono.set_assemblies_path(mono_search_path_n);
        setenv(TEXT("DOORSTOP_DLL_SEARCH_DIRS"), mono_search_path, TRUE);
        free(mono_search_path);
        free(override_dir_full);
    } else {
        setenv(TEXT("DOORSTOP_DLL_SEARCH_DIRS"), root_dir, TRUE);
    }
    void *domain = mono.jit_init_version(root_domain_name, runtime_version);
    doorstop_bootstrap(domain);
    return domain;
}

int init_il2cpp(const char *domain_name) {
    LOG("Starting IL2CPP domain \"%s\"\n", domain_name);
    const int orig_result = il2cpp.init(domain_name);

    char_t *mono_lib_dir = get_full_path(config.mono_lib_dir);
    char_t *mono_corlib_dir = get_full_path(config.mono_corlib_dir);
    char_t *mono_config_dir = get_full_path(config.mono_config_dir);

    LOG("Mono lib: %s\n", mono_lib_dir);
    LOG("Mono mscorlib dir: %s\n", mono_corlib_dir);
    LOG("Mono confgi dir: %s\n", mono_config_dir);

    if (!file_exists(mono_lib_dir) || !folder_exists(mono_corlib_dir) ||
        !folder_exists(mono_config_dir)) {
        LOG("Mono startup dirs are not set up, skipping invoking Doorstop\n");
        return orig_result;
    }

    void *mono_module = dlopen(mono_lib_dir, RTLD_LAZY);
    LOG("Loaded mono.dll: %p\n", mono_module);
    if (!mono_module) {
        LOG("Failed to load mono.dll! Skipping!");
        return orig_result;
    }

    load_mono_funcs(mono_module);
    LOG("Loaded mono.dll functions\n");

    char *mono_corlib_dir_narrow = narrow(mono_corlib_dir);
    char *mono_config_dir_narrow = narrow(mono_config_dir);
    mono.set_dirs(mono_corlib_dir_narrow, mono_config_dir_narrow);
    mono.config_parse(NULL);

    void *domain = mono.jit_init_version("Doorstop Root Domain", NULL);
    LOG("Created domain: %p\n", domain);

    doorstop_bootstrap(domain);

    return orig_result;
}

void *hook_mono_image_open_from_data_with_name(void *data,
                                               unsigned long data_len,
                                               int need_copy,
                                               MonoImageOpenStatus *status,
                                               int refonly, const char *name) {
    void *result = NULL;
    if (config.mono_dll_search_path_override) {
        char_t *name_wide = widen(name);
        char_t *name_file = get_file_name(name_wide, TRUE);
        free(name_wide);

        size_t name_file_len = strlen(name_file);
        size_t bcl_root_len = strlen(config.mono_dll_search_path_override);

        char_t *new_full_path =
            calloc(name_file_len + bcl_root_len + 2, sizeof(char_t));
        strcat(new_full_path, config.mono_dll_search_path_override);
        strcat(new_full_path, TEXT("/"));
        strcat(new_full_path, name_file);

        if (file_exists(new_full_path)) {
            void *file = fopen(new_full_path, "r");
            size_t size = get_file_size(file);
            void *buf = malloc(size);
            fread(buf, 1, size, file);
            fclose(file);
            result = mono.image_open_from_data_with_name(buf, size, need_copy,
                                                         status, refonly, name);
            if (need_copy)
                free(buf);
        }
        free(new_full_path);
    }

    if (!result) {
        result = mono.image_open_from_data_with_name(data, data_len, need_copy,
                                                     status, refonly, name);
    }
    return result;
}