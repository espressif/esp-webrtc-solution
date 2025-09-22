/* OpenAI realtime communication Demo code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "common.h"
#include <cJSON.h>

#define TAG "OPENAI_APP"

#define ELEMS(a) (sizeof(a) / sizeof(a[0]))

typedef struct attribute_t attribute_t;
typedef struct class_t     class_t;
typedef enum {
    ATTRIBUTE_TYPE_NONE,
    ATTRIBUTE_TYPE_BOOL,
    ATTRIBUTE_TYPE_INT,
    ATTRIBUTE_TYPE_PARENT,
} attribute_type_t;

struct attribute_t {
    char            *name;
    char            *desc;
    attribute_type_t type;
    union {
        bool         b_state;
        int          i_value;
        attribute_t *attr_list;
    };
    int  attr_num;
    bool required;
    int (*control)(attribute_t *attr);
};

struct class_t {
    char        *name;
    char        *desc;
    attribute_t *attr_list;
    int          attr_num;
    class_t     *next;
};

static esp_webrtc_handle_t webrtc  = NULL;
static class_t            *classes = NULL;

static int set_light_on_off(attribute_t *attr)
{
    printf("Light set to %s\n", attr->b_state ? "ON" : "OFF");
    return 0;
}

static int set_light_color_red(attribute_t *attr)
{
    printf("Red set to %d\n", attr->i_value);
    return 0;
}

static int set_light_color_blue(attribute_t *attr)
{
    printf("Blue set to %d\n", attr->i_value);
    return 0;
}

static int set_light_color_green(attribute_t *attr)
{
    printf("Green set to %d\n", attr->i_value);
    return 0;
}

static int set_speaker_volume(attribute_t *attr)
{
    printf("Volume set to %d\n", attr->i_value);
    return 0;
}

static int set_door_state(attribute_t *attr)
{
    printf("Door is %s\n", attr->b_state ? "Opened" : "Closed");
    return 0;
}

static class_t *build_volume_class(void)
{
    class_t *vol = (class_t *)calloc(1, sizeof(class_t));
    if (vol == NULL) {
        return NULL;
    }
    static attribute_t vol_attrs[] = {
        {
            .name = "volume",
            .desc = "Speaker volume range 0-100",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_speaker_volume,
            .required = true,
        },
    };
    vol->name = "SetVolume";
    vol->desc = "Changes speaker volume";
    vol->attr_list = vol_attrs;
    vol->attr_num = ELEMS(vol_attrs);
    return vol;
}

static class_t *build_door_class(void)
{
    class_t *vol = (class_t *)calloc(1, sizeof(class_t));
    if (vol == NULL) {
        return NULL;
    }
    static attribute_t vol_attrs[] = {
        {
            .name = "open",
            .desc = "Open or close the door",
            .type = ATTRIBUTE_TYPE_BOOL,
            .control = set_door_state,
            .required = true,
        },
    };
    vol->name = "OpenDoor";
    vol->desc = "Toggle the door state to open or close";
    vol->attr_list = vol_attrs;
    vol->attr_num = ELEMS(vol_attrs);
    return vol;
}

static class_t *build_light_class(void)
{
    class_t *light = (class_t *)calloc(1, sizeof(class_t));
    if (light == NULL) {
        return NULL;
    }
    static attribute_t light_color[] = {
        {
            .name = "red",
            .desc = "Red value in the range of 0-255",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_light_color_red,
            .required = true,
        },
        {
            .name = "green",
            .desc = "Green value in the range of 0-255",
            .type = ATTRIBUTE_TYPE_INT,
            .control = set_light_color_green,
            .required = true,
        },
        {
            .name = "blue",
            .desc = "Blue value in the range of 0-255",
            .control = set_light_color_blue,
            .type = ATTRIBUTE_TYPE_INT,
            .required = true,
        },
    };
    static attribute_t light_attrs[] = {
        {
            .name = "LightState",
            .desc = "New light state (true or false is expected)",
            .type = ATTRIBUTE_TYPE_BOOL,
            .control = set_light_on_off,
            .required = true,
        },
        {
            .name = "LightColor",
            .desc = "Set light color of red, green and blue",
            .type = ATTRIBUTE_TYPE_PARENT,
            .attr_list = light_color,
            .attr_num = ELEMS(light_color),
        },
    };
    light->name = "SetLightState";
    light->desc = "Changes the state of the light";
    light->attr_list = light_attrs;
    light->attr_num = ELEMS(light_attrs);
    return light;
}

static void add_class(class_t *cls)
{
    if (classes == NULL) {
        classes = cls;
    } else {
        cls->next = classes;
        classes = cls;
    }
}

static int build_classes(void)
{
    static bool build_once = false;
    if (build_once) {
        return 0;
    }
    add_class(build_light_class());
    add_class(build_volume_class());
    add_class(build_door_class());
    build_once = true;
    return 0;
}

static char *get_attr_type(attribute_type_t type)
{
    if (type == ATTRIBUTE_TYPE_BOOL) {
        return "boolean";
    }
    if (type == ATTRIBUTE_TYPE_INT) {
        return "integer";
    }
    if (type == ATTRIBUTE_TYPE_PARENT) {
        return "object";
    }
    return "";
}

static int add_parent_attribute(cJSON *parent, attribute_t *attr)
{
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddItemToObject(parent, "properties", properties);
    int require_num = 0;
    for (int i = 0; i < attr->attr_num; i++) {
        attribute_t *sub_attr = &attr->attr_list[i];
        cJSON *prop = cJSON_CreateObject();
        cJSON_AddItemToObject(properties, sub_attr->name, prop);
        cJSON_AddStringToObject(prop, "type", get_attr_type(sub_attr->type));
        cJSON_AddStringToObject(prop, "description", sub_attr->desc);
        if (sub_attr->type == ATTRIBUTE_TYPE_PARENT) {
            add_parent_attribute(prop, sub_attr);
        }
        if (sub_attr->required) {
            require_num++;
        }
    }
    if (require_num) {
        cJSON *requires = cJSON_CreateArray();
        for (int i = 0; i < attr->attr_num; i++) {
            attribute_t *sub_attr = &attr->attr_list[i];
            if (sub_attr->required) {
                cJSON_AddItemToArray(requires, cJSON_CreateString(sub_attr->name));
            }
        }
        cJSON_AddItemToObject(parent, "required", requires);
    }
    return 0;
}

static int send_function_desc(void)
{
    if (classes == NULL || webrtc == NULL) {
        return 0;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "session.update");
    cJSON *session = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "session", session);

    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));

    cJSON_AddItemToObject(session, "modalities", modalities);
    cJSON_AddNullToObject(session, "input_audio_transcription");
    cJSON *tools = cJSON_CreateArray();
    cJSON_AddItemToObject(session, "tools", tools);

    class_t *iter = classes;
    while (iter) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddItemToArray(tools, tool);
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON_AddStringToObject(tool, "name", iter->name);
        cJSON_AddStringToObject(tool, "description", iter->desc);
        cJSON *parameters = cJSON_CreateObject();
        cJSON_AddItemToObject(tool, "parameters", parameters);
        cJSON_AddStringToObject(parameters, "type", "object");
        cJSON *properties = cJSON_CreateObject();
        cJSON_AddItemToObject(parameters, "properties", properties);
        int require_num = 0;
        for (int i = 0; i < iter->attr_num; i++) {
            attribute_t *attr = &iter->attr_list[i];
            cJSON *prop = cJSON_CreateObject();
            cJSON_AddItemToObject(properties, attr->name, prop);
            cJSON_AddStringToObject(prop, "type", get_attr_type(attr->type));
            cJSON_AddStringToObject(prop, "description", attr->desc);
            if (attr->type == ATTRIBUTE_TYPE_PARENT) {
                add_parent_attribute(prop, attr);
            }
            if (attr->required) {
                require_num++;
            }
        }
        if (require_num) {
            cJSON *requires = cJSON_CreateArray();
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                if (attr->required) {
                    cJSON_AddItemToArray(requires, cJSON_CreateString(attr->name));
                }
            }
            cJSON_AddItemToObject(parameters, "required", requires);
        }
        iter = iter->next;
    }
    char *json_string = cJSON_Print(root);
    if (json_string) {
        esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, (uint8_t *)json_string, strlen(json_string));
        free(json_string);
    }
    cJSON_Delete(root);
    return 0;
}

static int match_and_execute(cJSON *cur, attribute_t *attr)
{
    cJSON *attr_value = cJSON_GetObjectItemCaseSensitive(cur, attr->name);
    if (!attr_value) {
        if (attr->required) {
            printf("Missing required attribute: %s\n", attr->name);
        }
        return 0;
    }

    // Process based on attribute type
    if (attr->type == ATTRIBUTE_TYPE_BOOL && cJSON_IsBool(attr_value)) {
        attr->b_state = cJSON_IsTrue(attr_value);
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_INT && cJSON_IsNumber(attr_value)) {
        attr->i_value = attr_value->valueint;
        if (attr->control) {
            attr->control(attr);
        }
    } else if (attr->type == ATTRIBUTE_TYPE_PARENT && cJSON_IsObject(attr_value)) {
        // Process nested attributes
        for (int j = 0; j < attr->attr_num; j++) {
            attribute_t *sub_attr = &attr->attr_list[j];
            match_and_execute(attr_value, sub_attr);
        }
    } else {
        printf("Unhandled attribute type or invalid value for: %s\n", attr->name);
    }

    return 1; // Success
}

static int process_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        printf("Error parsing JSON data\n");
        return -1; // Parsing error
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "response.function_call_arguments.done") != 0) {
        cJSON_Delete(root);
        return 0;
    }
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        printf("Function Call %s\n", payload);
        free(payload);
    }

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(root, "arguments");
    if (!cJSON_IsString(name) || !name->valuestring || !cJSON_IsString(arguments) || !arguments->valuestring) {
        printf("Invalid JSON format\n");
        cJSON_Delete(root);
        return -1; // Invalid format
    }

    cJSON *args_root = cJSON_Parse(arguments->valuestring);
    if (!args_root) {
        printf("Error parsing arguments JSON\n");
        cJSON_Delete(root);
        return -1; // Parsing error
    }

    // Find the corresponding class and attributes
    class_t *iter = classes;
    while (iter) {
        if (strcmp(iter->name, name->valuestring) == 0) {
            for (int i = 0; i < iter->attr_num; i++) {
                attribute_t *attr = &iter->attr_list[i];
                match_and_execute(args_root, attr);
            }
        }
        iter = iter->next;
    }

    cJSON_Delete(args_root);
    cJSON_Delete(root);
    return 0;
}

static int webrtc_data_handler(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    process_json((const char *)data);

    cJSON *root = cJSON_Parse((const char *)data);
    if (!root) {
        return -1;
    }
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        char *text = strstr(payload, "transcript\":");
        if (text) {
            text += strlen("transcript\":");
            char *start = strchr(text, '"');
            char *end = start ? strchr(start + 1, '"') : NULL;
            if (end) {
                start++;
                printf("Transcript: %.*s\n", (int)(end - start), start);
            }
        }
        free(payload);
    }
    cJSON_Delete(root);
    return 0;
}

static int send_response(char *text)
{
    if (webrtc == NULL) {
        ESP_LOGE(TAG, "WebRTC not started yet");
        return -1;
    }
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "type", "response.create");
        cJSON *response = cJSON_CreateObject();
        cJSON *modalities = cJSON_CreateArray();
        cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
        cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
        cJSON_AddItemToObject(response, "modalities", modalities);
        cJSON_AddStringToObject(response, "instructions", text);
        cJSON_AddItemToObject(root, "response", response);
    }
    // Print the initial JSON structure
    char *send_text = cJSON_Print(root);
    if (send_text) {
        printf("Begin to send json:%s\n", send_text);
        esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, (uint8_t *)send_text, strlen(send_text));
        // Clean up
        free(send_text);
    }
    cJSON_Delete(root); // Free the cJSON object
    return 0;
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    printf("====================Event %d======================\n", event->type);
    if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_CONNECTED) {
        // As ESP32 act as SCTP server, it does not create data channel automatically
        // Here manually create one data channel
        esp_peer_data_channel_cfg_t cfg = {
            .label = "esp_channel",
        };
        esp_peer_handle_t peer_handle = NULL;
        esp_webrtc_get_peer_connection(webrtc, &peer_handle);
        esp_peer_create_data_channel(peer_handle, &cfg);
    }
    if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_OPENED) {
        send_response("You are helpful and have some tools installed. In the tools you have the ability to control a light bulb and change speaker volume. Say 'How can I help?");
        send_function_desc();
    }
    return 0;
}

int openai_send_text(char *text)
{
    if (webrtc == NULL) {
        ESP_LOGE(TAG, "WebRTC not started yet");
        return -1;
    }
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "type", "conversation.item.create");
        cJSON_AddNullToObject(root, "previous_item_id");
        cJSON *item = cJSON_CreateObject();
        if (item) {
            cJSON_AddStringToObject(item, "type", "message");
            cJSON_AddStringToObject(item, "role", "user");
        }
        cJSON *contentArray = cJSON_CreateArray();
        cJSON *contentItem = cJSON_CreateObject();
        cJSON_AddStringToObject(contentItem, "type", "input_text");
        cJSON_AddStringToObject(contentItem, "text", text);
        cJSON_AddItemToArray(contentArray, contentItem);
        cJSON_AddItemToObject(item, "content", contentArray);
        // Add the item to the root object
        cJSON_AddItemToObject(root, "item", item);
    }
    // Print the initial JSON structure
    char *send_text = cJSON_Print(root);
    if (send_text) {
        printf("Begin to send json:%s\n", send_text);
        esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL, (uint8_t *)send_text, strlen(send_text));
        // Clean up
        free(send_text);
    }
    cJSON_Delete(root); // Free the cJSON object
    return 0;
}

int start_webrtc(void)
{
    build_classes();
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }
    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };
    openai_signaling_cfg_t openai_cfg = {
        .token = OPENAI_API_KEY,
    };
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel = 1,
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
#endif
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .enable_data_channel = DATA_CHANNEL_ENABLED,
            .on_custom_data = webrtc_data_handler,
            .manual_ch_create = true, // Disable esp_peer create data channel automatically
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg.extra_cfg = &openai_cfg,
        .signaling_cfg.extra_size = sizeof(openai_cfg),
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_openai_signaling(),
    };
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open webrtc");
        return ret;
    }
    // Set media provider
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

    // Set event handler
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    // Start webrtc
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
    }
    return ret;
}

void query_webrtc(void)
{
    if (webrtc) {
        esp_webrtc_query(webrtc);
    }
}

int stop_webrtc(void)
{
    if (webrtc) {
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        esp_webrtc_close(handle);
    }
    return 0;
}
